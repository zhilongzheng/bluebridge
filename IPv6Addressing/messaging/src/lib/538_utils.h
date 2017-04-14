#ifndef PROJECT_UTILS
#define PROJECT_UTILS

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>
#include <unistd.h>


#define BLOCK_SIZE 4096 // max number of bytes we can get at once
#define POINTER_SIZE sizeof(void*)
#define IPV6_SIZE 16

#define ALLOC_CMD		"01"
#define WRITE_CMD		"02"
#define GET_CMD			"03"
#define FREE_CMD		"04"
#define GET_ADDR_CMD	"05"


void *get_in_addr(struct sockaddr *sa);

int getLine(char *prmpt, char *buff, size_t sz);

unsigned char *gen_rdm_bytestream(size_t num_bytes);

struct in6_addr * gen_rdm_IPv6Target();
struct in6_addr * gen_fixed_IPv6Target(uint8_t rndHost);


int sendUDP(int sockfd, char * sendBuffer, int msgBlockSize, struct addrinfo * p);
int sendUDPIPv6(int sockfd, char * sendBuffer, int msgBlockSize, struct addrinfo * p, struct in6_addr  ipv6Pointer);


int receiveUDP(int sockfd, char * receiveBuffer, int msgBlockSize, struct addrinfo * p);
int receiveUDPIPv6(int sockfd, char * receiveBuffer, int msgBlockSize, struct addrinfo * p, struct in6_addr * ipv6Pointer);

uint64_t getPointerFromIPv6(struct in6_addr addr);
struct in6_addr getIPv6FromPointer(uint64_t pointer);



#endif