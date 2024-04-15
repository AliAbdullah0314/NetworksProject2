#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <assert.h>

#include "packet.h"
#include "common.h"

#define STDIN_FD 0
#define RETRY 120 // millisecond

int next_seqno = 0;
int send_base = 0;
#define WINDOW_SIZE 10

int sockfd, serverlen;
struct sockaddr_in serveraddr;
struct itimerval timer;
tcp_packet *sndpkt;
tcp_packet *recvpkt;
tcp_packet *window[WINDOW_SIZE]; // Sliding window where window[0] serves as 'base' for oldest unacked packet
sigset_t sigmask;

int timer_running = 0; // Flag to indicate whether the timer is running or not
int eof = 0;           // Flag to indicate whether the end is reached or not

void resend_packets(int sig)
{
    if (sig == SIGALRM)
    {
        // Resend all packets range between
        // sendBase and nextSeqNum
        VLOG(INFO, "Timout happend");
        if (sendto(sockfd, window[0], TCP_HDR_SIZE + get_data_size(window[0]), 0, // replace sendpkt with window[0]
                   (const struct sockaddr *)&serveraddr, serverlen) < 0)
        {
            error("sendto");
        }
        timer_running = 0;
    }
}

void start_timer()
{
    if (!timer_running) // Start timer only if it isn't already running
    {
        sigprocmask(SIG_UNBLOCK, &sigmask, NULL);
        setitimer(ITIMER_REAL, &timer, NULL);
        timer_running = 1;
    }
}

void stop_timer()
{
    sigprocmask(SIG_BLOCK, &sigmask, NULL);
    timer_running = 0;
}

/*
 * init_timer: Initialize timer
 * delay: delay in milliseconds
 * sig_handler: signal handler function for re-sending unACKed packets
 */
void init_timer(int delay, void (*sig_handler)(int))
{
    signal(SIGALRM, sig_handler);
    timer.it_interval.tv_sec = delay / 1000; // sets an interval of the timer
    timer.it_interval.tv_usec = (delay % 1000) * 1000;
    timer.it_value.tv_sec = delay / 1000; // sets an initial value
    timer.it_value.tv_usec = (delay % 1000) * 1000;

    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGALRM);
}

int main(int argc, char **argv)
{
    int portno, len;
    uint32_t next_seqno; // 0 to 4,294,967,295
    char *hostname;
    char buffer[DATA_SIZE];
    FILE *fp;

    /* check command line arguments */
    if (argc != 4)
    {
        fprintf(stderr, "usage: %s <hostname> <port> <FILE>\n", argv[0]);
        exit(0);
    }
    hostname = argv[1];
    portno = atoi(argv[2]);
    fp = fopen(argv[3], "r");
    if (fp == NULL)
    {
        error(argv[3]);
    }

    /* socket: create the socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");

    /* initialize server server details */
    bzero((char *)&serveraddr, sizeof(serveraddr));
    serverlen = sizeof(serveraddr);

    /* covert host into network byte order */
    if (inet_aton(hostname, &serveraddr.sin_addr) == 0)
    {
        fprintf(stderr, "ERROR, invalid host %s\n", hostname);
        exit(0);
    }

    /* build the server's Internet address */
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(portno);

    assert(MSS_SIZE - TCP_HDR_SIZE > 0);

    // Stop and wait protocol

    init_timer(RETRY, resend_packets);
    // srand(time(NULL));

    // // Generate a random sequence number between 0 and 4,294,967,295
    // next_seqno = rand();
    next_seqno = 0;

    // first load ten packets into window and send them
    for (int i = 0; i < WINDOW_SIZE; i++)
    {
        len = fread(buffer, 1, DATA_SIZE, fp);
        if (len <= 0)
        {
            VLOG(INFO, "End Of File has been reached first");
            sndpkt = make_packet(0);
            sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0,
                   (const struct sockaddr *)&serveraddr, serverlen);
            eof = 1;
            break;
        }
        send_base = next_seqno;
        next_seqno = send_base + len; // will wrap around 0-4294967295, is the seqno that will be sent after snpkt is sent
        sndpkt = make_packet(len);
        memcpy(sndpkt->data, buffer, len);
        sndpkt->hdr.seqno = send_base;

        // Send the packet
        if (sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0,
                   (const struct sockaddr *)&serveraddr, serverlen) < 0)
        {
            error("sendto");
        }
        VLOG(DEBUG, "Sending packet %d to %s",
                 send_base, inet_ntoa(serveraddr.sin_addr));

        window[i] = sndpkt;
    }

    if (eof)
    {
        free(sndpkt);
        return 0;
    }

    int dupack = 0;
    while (1)
    {

        while (window[WINDOW_SIZE - 1] != NULL)
        {
            // Wait until the first packet in the window is acknowledged
            if (recvfrom(sockfd, buffer, MSS_SIZE, 0,
                         (struct sockaddr *)&serveraddr, (socklen_t *)&serverlen) < 0)
            {
                error("recvfrom");
            }

            recvpkt = (tcp_packet *)buffer;
            printf("%d \n", get_data_size(recvpkt));
            printf("ack from receiver: %d \n", recvpkt->hdr.ackno);
            if (recvpkt->hdr.ackno > window[0]->hdr.seqno)
            {
                stop_timer();
                // slide window
                while (recvpkt->hdr.ackno > window[0]->hdr.seqno) // essentially slide window until first element in window is greater or equal to the ack received
                {
                    free(window[0]);
                    for (int i = 0; i < WINDOW_SIZE - 1; i++)
                    {
                        window[i] = window[i + 1];
                    }
                    window[WINDOW_SIZE - 1] = NULL;
                }

                // send_base = recvpkt->hdr.ackno + 1;
            }
            else if (recvpkt->hdr.ackno == window[0]->hdr.seqno)
            {
                dupack++;
                if (dupack == 3) // packet is lost and must do a retransmit
                {
                    // stop_timer();
                    if (sendto(sockfd, window[0], TCP_HDR_SIZE + get_data_size(sndpkt), 0,
                               (const struct sockaddr *)&serveraddr, serverlen) < 0)
                    {
                        error("sendto");
                    }
                    // start_timer();

                    dupack = 0;
                }
            }
        }

        while (window[WINDOW_SIZE - 1] == NULL) // read and send data until window is full
        {
            len = fread(buffer, 1, DATA_SIZE, fp); // length of one packet
            if (len <= 0)
            {
                VLOG(INFO, "End Of File has been reached within");
                sndpkt = make_packet(0);
                sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0,
                       (const struct sockaddr *)&serveraddr, serverlen);
                eof = 1;
                break;
            }
            send_base = next_seqno;
            next_seqno = send_base + len; // will wrap around 0-4294967295
            sndpkt = make_packet(len);
            memcpy(sndpkt->data, buffer, len);
            sndpkt->hdr.seqno = send_base;
            for (int i = 0; i < WINDOW_SIZE; i++)
            {
                if (window[i] == NULL)
                {
                    window[i] = sndpkt; // puts the new packet in the first free spot
                    break;
                }
            }

            VLOG(DEBUG, "Sending packet %d to %s",
                 send_base, inet_ntoa(serveraddr.sin_addr));
            /*
             * If the sendto is called for the first time, the system will
             * will assign a random port number so that server can send its
             * response to the src port.
             */
            if (sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0,
                       (const struct sockaddr *)&serveraddr, serverlen) < 0)
            {
                error("sendto");
            }
            start_timer(); // should start timer only if timer isn't already running
        }

        if (eof)
        {
            free(sndpkt);
            return 0;
        }

        // // Wait for ACK
        // do
        // {

        //     // ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
        //     // struct sockaddr *src_addr, socklen_t *addrlen);

        //     do
        //     {
        //         if (recvfrom(sockfd, buffer, MSS_SIZE, 0,
        //                      (struct sockaddr *)&serveraddr, (socklen_t *)&serverlen) < 0)
        //         {
        //             error("recvfrom");
        //         }

        //         recvpkt = (tcp_packet *)buffer;
        //         printf("%d \n", get_data_size(recvpkt));
        //         assert(get_data_size(recvpkt) <= DATA_SIZE);
        //     } while (recvpkt->hdr.ackno < next_seqno); // ignore duplicate ACKs
        //     stop_timer();
        //     /*resend pack if don't recv ACK */
        // } while (recvpkt->hdr.ackno != next_seqno);

        free(sndpkt);
    }

    return 0;
}
