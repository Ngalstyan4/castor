
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <castor/debug.h>
#include <castor/rrlog.h>
#include "ft.h"

#define RRFT_PORT	8051

int rrsock = -1;

void
RRFT_InitMaster()
{
    int lsock;
    socklen_t clilen;
    int yes = 1;
    struct sockaddr_in srvaddr;
    struct sockaddr_in cliaddr;

    lsock = socket(AF_INET, SOCK_STREAM, 0);
    if (lsock < 0) {
	PERROR("socket");
    }

    if (setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0)
    {   
	PERROR("setsockopt");
    }

    srvaddr.sin_family = AF_INET;
    srvaddr.sin_addr.s_addr = INADDR_ANY;
    srvaddr.sin_port = htons(RRFT_PORT);

    if (bind(lsock, (struct sockaddr *)&srvaddr, sizeof(srvaddr)) < 0) {
	PERROR("bind");
    }

    if (listen(lsock, 5) < 0) {
	PERROR("listen");
    }

    fprintf(stderr, "Waiting for slave ...\n");

    clilen = sizeof(cliaddr);
    rrsock = accept(lsock, (struct sockaddr *)&cliaddr, &clilen);
    if (rrsock < 0) {
	PERROR("accept");
    }

    // Handshake
    char handshake[8];

    memcpy(&handshake, "RRFTSRVR", 8);
    write(rrsock, &handshake, 8);
    read(rrsock, &handshake, 8);
    if (memcmp(&handshake, "RRFTCLNT", 8) != 0) {
	fprintf(stderr, "Handshake failed!\n");
	abort();
    }

    // XXX: Report RTT
}

void
RRFT_InitSlave(const char *hostname)
{
    struct sockaddr_in srvaddr;

    rrsock = socket(AF_INET, SOCK_STREAM, 0);
    if (rrsock < 0) {
	PERROR("socket");
    }

    // XXX: gethostbyname

    srvaddr.sin_family = AF_INET;
    srvaddr.sin_addr.s_addr = INADDR_ANY;
    srvaddr.sin_port = htons(RRFT_PORT);

    if (connect(rrsock, (struct sockaddr *)&srvaddr, sizeof(srvaddr)) < 0) {
	PERROR("connect");
    }

    // Handshake
    char handshake[8];

    read(rrsock, &handshake, 8);
    if (memcmp(&handshake, "RRFTSRVR", 8) != 0) {
	fprintf(stderr, "Handshake failed!\n");
	abort();
    }
    memcpy(&handshake, "RRFTCLNT", 8);
    write(rrsock, &handshake, 8);

    // XXX: Report RTT
}

void
RRFT_Send(uint64_t count, RRLogEntry *evt)
{
    int result;

    result = write(rrsock, evt, count * sizeof(RRLogEntry));
    if (result < 0) {
	PERROR("write");
    }
}

uint64_t
RRFT_Recv(uint64_t count, RRLogEntry *evt)
{
    int result;

    result = read(rrsock, evt, count * sizeof(RRLogEntry));
    if (result < 0) {
	PERROR("read");
    }

    return (uint64_t)result / sizeof(RRLogEntry);
}

