// #include <stdio.h>
// #include <unistd.h>
// #include <stdlib.h>
// #include <string.h>
// #include <sys/types.h>
// #include <sys/socket.h>
// #include <netinet/in.h>
// #include <arpa/inet.h>
// #include <sys/time.h>
// #include <assert.h>

// #include "common.h"
// #include "packet.h"

// /*
//  * You are required to change the implementation to support
//  * window size greater than one.
//  * In the current implementation the window size is one, hence we have
//  * only one send and receive packet
//  */
// tcp_packet *recvpkt;
// tcp_packet *sndpkt;
// int next_seqno = 0;

// int main(int argc, char **argv) {
//     int sockfd; /* socket */
//     int portno; /* port to listen on */
//     int clientlen; /* byte size of client's address */
//     struct sockaddr_in serveraddr; /* server's addr */
//     struct sockaddr_in clientaddr; /* client addr */
//     int optval; /* flag value for setsockopt */
//     FILE *fp;
//     char buffer[MSS_SIZE];
//     struct timeval tp;

//     /*
//      * check command line arguments
//      */
//     if (argc != 3) {
//         fprintf(stderr, "usage: %s <port> FILE_RECVD\n", argv[0]);
//         exit(1);
//     }
//     portno = atoi(argv[1]);

//     fp  = fopen(argv[2], "w");
//     if (fp == NULL) {
//         error(argv[2]);
//     }

//     /*
//      * socket: create the parent socket
//      */
//     sockfd = socket(AF_INET, SOCK_DGRAM, 0);
//     if (sockfd < 0)
//         error("ERROR opening socket");

//     /* setsockopt: Handy debugging trick that lets
//      * us rerun the server immediately after we kill it;
//      * otherwise we have to wait about 20 secs.
//      * Eliminates "ERROR on binding: Address already in use" error.
//      */
//     optval = 1;
//     setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
//             (const void *)&optval , sizeof(int));

//     /*
//      * build the server's Internet address
//      */
//     bzero((char *) &serveraddr, sizeof(serveraddr));
//     serveraddr.sin_family = AF_INET;
//     serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
//     serveraddr.sin_port = htons((unsigned short)portno);

//     /*
//      * bind: associate the parent socket with a port
//      */
//     if (bind(sockfd, (struct sockaddr *) &serveraddr,
//                 sizeof(serveraddr)) < 0)
//         error("ERROR on binding");

//     /*
//      * main loop: wait for a datagram, then echo it
//      */
//     VLOG(DEBUG, "epoch time, bytes received, sequence number");

//     clientlen = sizeof(clientaddr);
//     while (1) {
//         /*
//          * recvfrom: receive a UDP datagram from a client
//          */
//         //VLOG(DEBUG, "waiting from server \n");
//         if (recvfrom(sockfd, buffer, MSS_SIZE, 0,
//                 (struct sockaddr *) &clientaddr, (socklen_t *)&clientlen) < 0) {
//             error("ERROR in recvfrom");
//         }
//         recvpkt = (tcp_packet *) buffer;
//         assert(get_data_size(recvpkt) <= DATA_SIZE);
//         if ( recvpkt->hdr.data_size == 0) {
//             VLOG(INFO, "End Of File has been reached");
//             fclose(fp);
//             sndpkt = make_packet(0);
//             sndpkt->hdr.ackno = next_seqno;
//             sndpkt->hdr.ctr_flags = ACK;
//             for (int i = 0; i < 100; i++) {
//                 if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0,
//                 (struct sockaddr *) &clientaddr, clientlen) < 0) {
//                     printf("PLAAAYYYAAA");
//                     error("ERROR in sendto");
//                 }
//             }
//             break;
//         }
//         else {
//             if (recvpkt->hdr.seqno == next_seqno) {
//                 next_seqno += recvpkt->hdr.data_size;
//                 gettimeofday(&tp, NULL);
//                 VLOG(DEBUG, "%lu, %d, %d", tp.tv_sec, recvpkt->hdr.data_size, recvpkt->hdr.seqno);

//                 //fseek(fp, recvpkt->hdr.seqno, SEEK_SET);
//                 fwrite(recvpkt->data, 1, recvpkt->hdr.data_size, fp);
//                 sndpkt = make_packet(0);
//                 sndpkt->hdr.ackno = next_seqno;
//                 sndpkt->hdr.ctr_flags = ACK;
//                 if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0,
//                 (struct sockaddr *) &clientaddr, clientlen) < 0) {
//                     printf("NOOOOOOOO");
//                     error("ERROR in sendto");
//                 }
//             }
//             else {
//                 if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0,
//                 (struct sockaddr *) &clientaddr, clientlen) < 0) {
//                     printf("HELLOOOOO");
//                     error("ERROR in sendto");
//                 }
//             }
//         }
//     }

//     return 0;
// }

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <assert.h>
#include <time.h>

#include "common.h"
#include "packet.h"

#define WINDOW_SIZE 10 // Define the size of the sliding window for packet buffering

typedef struct
{
    tcp_packet *pkt; // Pointer to a packet structure
    int valid;       // Flag to check if the buffer slot is occupied
} PacketBuffer;

PacketBuffer window[WINDOW_SIZE]; // Sliding window buffer
int next_seqno = 0;               // Expected sequence number of the next packet
int base_seqno = 0;               // Sequence number of the first packet in the sliding window

void buffer_packet(tcp_packet *pkt)
{
    int index = (pkt->hdr.seqno - base_seqno) / DATA_SIZE; // Calculate buffer index based on sequence number
    if (index < 0 || index >= WINDOW_SIZE)
        return; // Ignore packets that fall outside the window

    // If the packet slot is not valid, allocate memory and copy the packet into the buffer
    if (!window[index].valid)
    {
        printf("buffering packet %d\n", pkt->hdr.seqno);
        window[index].pkt = malloc(sizeof(tcp_packet) + pkt->hdr.data_size);
        memcpy(window[index].pkt, pkt, sizeof(tcp_packet) + pkt->hdr.data_size);
        window[index].valid = 1;
    }
}

void write_buffered_packets(FILE *fp)
{
    while (window[0].valid)
    { // Write all valid packets from the buffer to the file
        printf("writing buffered packet %d\n",window[0].pkt->hdr.seqno);
        fwrite(window[0].pkt->data, 1, window[0].pkt->hdr.data_size, fp);
        next_seqno += window[0].pkt->hdr.data_size; // Update next expected sequence number
        free(window[0].pkt);                        // Free the packet memory
        window[0].valid = 0;                        // Mark buffer slot as invalid

        for (int i = 0; i < WINDOW_SIZE - 1; i++)
        {
            window[i] = window[i + 1];
        }

        window[WINDOW_SIZE - 1].valid = 0;

        // memmove(&window[0], &window[1], (WINDOW_SIZE - 1) * sizeof(PacketBuffer)); // Shift buffer contents
        // window[WINDOW_SIZE - 1].valid = 0;                                         // Clear the last slot
    }
}

int main(int argc, char **argv)
{
    int sockfd;                                // Socket file descriptor
    int portno;                                // Port number
    socklen_t clientlen;                       // Byte size of client's address
    struct sockaddr_in serveraddr, clientaddr; // Server and client address structures
    int optval;                                // Option value for setsockopt
    FILE *fp;                                  // File pointer for the received file
    char buffer[MSS_SIZE];                     // Buffer to receive data

    // Check for correct command-line arguments
    if (argc != 3)
    {
        fprintf(stderr, "usage: %s <port> <FILE_RECVD>\n", argv[0]);
        exit(1);
    }
    portno = atoi(argv[1]);    // Convert the port number from string to integer
    fp = fopen(argv[2], "wb"); // Open the output file in write-binary mode
    if (fp == NULL)
    {
        error("Error opening file");
    }

    sockfd = socket(AF_INET, SOCK_DGRAM, 0); // Create a UDP socket
    if (sockfd < 0)
        error("ERROR opening socket");

    // Allow reuse of local addresses, for rapid restarts of the server
    optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, sizeof(int));

    // Initialize the server address structure
    bzero((char *)&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)portno);

    // Bind the socket to the port
    if (bind(sockfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0)
        error("ERROR on binding");

    VLOG(DEBUG, "epoch time, bytes received, sequence number");

    // Initialize the sliding window
    memset(window, 0, sizeof(window));

    // Main loop: wait for a datagram, then process it
    while (1)
    {
        // Receive data from the client
        if (recvfrom(sockfd, buffer, MSS_SIZE, 0, (struct sockaddr *)&clientaddr, &clientlen) < 0)
        {
            error("ERROR in recvfrom");
        }

        // Process the received packet
        tcp_packet *recvpkt = (tcp_packet *)buffer;
        if (recvpkt->hdr.data_size == 0)
        { // Check for end-of-file packet
            VLOG(INFO, "End Of File has been reached");
            fclose(fp);
            tcp_packet *sndpkt = make_packet(0);
            sndpkt->hdr.ackno = next_seqno;
            sndpkt->hdr.ctr_flags = ACK;
            for (int i = 0; i < 100; i++)
            { // Send an ACK for the EOF packet multiple times
                if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0,
                           (struct sockaddr *)&clientaddr, clientlen) < 0)
                {
                    error("ERROR in sendto");
                }
            }
            break; // Exit the main loop
        }

        // Write packet directly to file if it is the next expected packet
        if (recvpkt->hdr.seqno == next_seqno)
        {
            time_t now = time(NULL); // Get the current epoch time
            printf("%ld, %d, %d\n", now, recvpkt->hdr.data_size, recvpkt->hdr.seqno);
            fwrite(recvpkt->data, 1, recvpkt->hdr.data_size, fp);
            //printf("wrote packet:\n %s\n\n\n\n\n", recvpkt->data);
            next_seqno += recvpkt->hdr.data_size;
            write_buffered_packets(fp);
        }
        else if (recvpkt->hdr.seqno > next_seqno) 
        {
            buffer_packet(recvpkt);
        }

        // Always send an ACK for the highest consecutive packet received
        tcp_packet *sndpkt = make_packet(0);
        sndpkt->hdr.ackno = next_seqno;
        sndpkt->hdr.ctr_flags = ACK;
        sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, (struct sockaddr *)&clientaddr, clientlen);
    }

    close(sockfd); // Close the socket
    return 0;
}