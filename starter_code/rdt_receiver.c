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

#include "common.h"
#include "packet.h"

#define WINDOW_SIZE 10

typedef struct {
    tcp_packet *pkt;
    int valid;
} PacketBuffer;

PacketBuffer window[WINDOW_SIZE];
int next_seqno = 0;
int base_seqno = 0;  // This is the sequence number of the first packet in the window

void buffer_packet(tcp_packet *pkt) {
    int index = (pkt->hdr.seqno - base_seqno) / DATA_SIZE;
    if (index < 0 || index >= WINDOW_SIZE) return;  // Packet outside of window

    if (!window[index].valid) {
        window[index].pkt = malloc(sizeof(tcp_packet) + pkt->hdr.data_size);
        memcpy(window[index].pkt, pkt, sizeof(tcp_packet) + pkt->hdr.data_size);
        window[index].valid = 1;
    }
}

void write_buffered_packets(FILE *fp) {
    while (window[0].valid) {
        fwrite(window[0].pkt->data, 1, window[0].pkt->hdr.data_size, fp);
        next_seqno += window[0].pkt->hdr.data_size;
        free(window[0].pkt);
        window[0].valid = 0;
        // Shift all packets down
        memmove(&window[0], &window[1], (WINDOW_SIZE - 1) * sizeof(PacketBuffer));
        window[WINDOW_SIZE - 1].valid = 0;
    }
}

int main(int argc, char **argv) {
    int sockfd;
    int portno;
    socklen_t clientlen;
    struct sockaddr_in serveraddr, clientaddr;
    int optval;
    FILE *fp;
    char buffer[MSS_SIZE];
    // struct timeval tp;

    /* Check command line arguments */
    if (argc != 3) {
        fprintf(stderr, "usage: %s <port> <FILE_RECVD>\n", argv[0]);
        exit(1);
    }
    portno = atoi(argv[1]);
    fp = fopen(argv[2], "wb");
    if (fp == NULL) {
        error("Error opening file");
    }

    /* Create the socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");

    /* Set socket options */
    optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, sizeof(int));

    /* Build the server's Internet address */
    bzero((char *)&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)portno);

    /* Bind the socket */
    if (bind(sockfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0) 
        error("ERROR on binding");

    VLOG(DEBUG, "epoch time, bytes received, sequence number");

    clientlen = sizeof(clientaddr);
    memset(window, 0, sizeof(window));

    /* Main loop: wait for a datagram, then process it */
    while (1) {
        if (recvfrom(sockfd, buffer, MSS_SIZE, 0, (struct sockaddr *)&clientaddr, &clientlen) < 0) {
            error("ERROR in recvfrom");
        }

        tcp_packet *recvpkt = (tcp_packet *) buffer;
        if (recvpkt->hdr.data_size == 0) {
             VLOG(INFO, "End Of File has been reached");
            fclose(fp);
            tcp_packet *sndpkt = make_packet(0);            sndpkt->hdr.ackno = next_seqno;
            sndpkt->hdr.ctr_flags = ACK;
            for (int i = 0; i < 100; i++) {
                if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, 
                (struct sockaddr *) &clientaddr, clientlen) < 0) {
                    error("ERROR in sendto");
                }
            }
            break;
        }

        if (recvpkt->hdr.seqno == next_seqno) {
            printf("PACKET NUM: %d, DATA SIZE: %d\n", recvpkt->hdr.seqno, recvpkt->hdr.data_size);
            fwrite(recvpkt->data, 1, recvpkt->hdr.data_size, fp);
            next_seqno += recvpkt->hdr.data_size;
            write_buffered_packets(fp);
        } else {
            buffer_packet(recvpkt);
        }

        tcp_packet *sndpkt = make_packet(0);
        sndpkt->hdr.ackno = next_seqno;
        sndpkt->hdr.ctr_flags = ACK;
        sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, (struct sockaddr *)&clientaddr, clientlen);
    }

    close(sockfd);
    return 0;
}