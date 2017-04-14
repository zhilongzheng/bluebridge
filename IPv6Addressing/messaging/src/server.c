/*
 ** server.c -- a stream socket server demo
 */

#include "./lib/538_utils.h"
#include "./lib/debug.h"

#define BACKLOG 10     // how many pending connections queue will hold

char *varadr_char[1000];
int countchar = 0;

/*
 * Frees global memory
 */
int cleanMemory() {
	int i;
	for (i = 0; i <= countchar; i++) {
		free(varadr_char[i]);
	}

	return EXIT_SUCCESS;
}

/*
 * Adds a character to the global memory
 */
int addchar(char* charadr) {
	if (charadr == NULL ) {
		perror("\nError when trying to allocate a pointer! \n");
		cleanMemory();
		exit(EXIT_FAILURE);
	}

	varadr_char[countchar] = charadr;
	countchar++;
	return EXIT_SUCCESS;
}

/*
 * TODO: explain.
 * Allocates local memory and exposes it to a client requesting it
 */
int providePointer(int sock_fd, struct addrinfo * p) {
	//TODO: Error handling if we runt out of memory, this will fail
	char * allocated = (char *) calloc(BLOCK_SIZE,sizeof(char));
	char * sendBuffer = (char *) calloc(BLOCK_SIZE,sizeof(char));

	int size = 0;
	printf("Input pointer: %p\n", (void *) allocated);
	struct in6_addr  ipv6Pointer = getIPv6FromPointer((uint64_t) &allocated);
	memcpy(sendBuffer, "ACK:", sizeof("ACK:"));
	size += sizeof("ACK:") - 1;
	memcpy(sendBuffer+size, &ipv6Pointer, IPV6_SIZE); 
	sendUDP(sock_fd, sendBuffer, BLOCK_SIZE, p);

	printf("Allocated Pointer: %p -> %s\n",allocated, allocated);
	free(sendBuffer);

	// TODO change to be meaningful, i.e., error message
	return 0;
}

int sendAddr(int sock_fd, struct addrinfo * p, char* receiveBuffer) {
	//TODO: Error handling if we runt out of memory, this will fail
	char * sendBuffer = (char *) calloc(BLOCK_SIZE,sizeof(char));
	char * pointerStr = (char *) calloc(POINTER_SIZE, sizeof(char));

	// memcpy(pointerStr, receiveBuffer, POINTER_SIZE);

	int size = 0;
	uint64_t pointer = 0;
	memcpy(&pointer, &receiveBuffer, POINTER_SIZE);

	// printf("Pointer %" PRIx64 "\n", pointer);
	// = getPointerFromString(pointerStr);
	struct in6_addr ipv6Pointer = getIPv6FromPointer(pointer);
	memcpy(sendBuffer, "ACK:", sizeof("ACK:"));
	size += sizeof("ACK:") - 1;
	memcpy(sendBuffer+size, &ipv6Pointer, IPV6_SIZE); 

	sendUDP(sock_fd, sendBuffer, BLOCK_SIZE, p);

	free(sendBuffer);

	// TODO change to be meaningful, i.e., error message
	return 0;
}

/*
 * TODO: explain.
 * Writes a piece of memory?
 */
int writeMem(int sock_fd, char * receiveBuffer, struct addrinfo * p, struct in6_addr * ipv6Pointer) {
	char * sendBuffer = (char *) calloc(BLOCK_SIZE,sizeof(char));
	//Point to data after command
	char * dataToWrite = receiveBuffer + 1;

	print_debug("Data received (first 50 bytes): %.50s\n", dataToWrite);

	uint64_t pointer = getPointerFromIPv6(*ipv6Pointer);
	// Copy the first POINTER_SIZE bytes of receive buffer into the target
	print_debug("Target pointer: %p", (void *) pointer);

	memcpy((void *) pointer, dataToWrite, strlen(dataToWrite));	
	
	printf("Content length %lu is currently stored at %p!\n", strlen((char *)pointer), (void*)pointer);
	printf("Content preview (50 bytes): %.50s\n", (char *)pointer);
	
	memcpy(sendBuffer, "ACK", sizeof("ACK"));
	sendUDP(sock_fd, sendBuffer, BLOCK_SIZE, p);
	free(sendBuffer);

	// TODO change to be meaningful, i.e., error message
	return 0;
}

/*
 * TODO: explain.
 * This is freeing target memory?
 */
int freeMem(int sock_fd, struct addrinfo * p, struct in6_addr * ipv6Pointer) {
	char * sendBuffer = (char *) calloc(BLOCK_SIZE,sizeof(char));
	uint64_t pointer = getPointerFromIPv6(*ipv6Pointer);	
	
	printf("Content stored at %p has been freed!\n", (void*)pointer);
	
	free((void *) pointer);
	memcpy(sendBuffer, "ACK", sizeof("ACK"));
	sendUDP(sock_fd, sendBuffer, BLOCK_SIZE, p);
	free(sendBuffer);
	// TODO change to be meaningful, i.e., error message
	return 0;
}

/*
 * Gets memory and sends it
 */
int getMem(int sock_fd, struct addrinfo * p, struct in6_addr * ipv6Pointer) {
	char * sendBuffer = (char *) calloc(BLOCK_SIZE,sizeof(char));
	uint64_t pointer = getPointerFromIPv6(*ipv6Pointer);

	printf("Content length %lu is currently stored at %p!\n", strlen((char *)pointer), (void*)pointer);
	printf("Content preview (50 bytes): %.50s\n", (char *)pointer);

	memcpy(sendBuffer, (void *) pointer, BLOCK_SIZE);
	// Send the sendBuffer (entire BLOCK_SIZE) to sock_fd
	print_debug("Content length %lu will be delivered to client!\n", strlen((char *)pointer));
	sendUDP(sock_fd, sendBuffer, BLOCK_SIZE, p);
	free(sendBuffer);

	// TODO change to be meaningful, i.e., error message
	return 0;
}

/*
 * Request handler for socket sock_fd
 * TODO: get message format
 */
void handleClientRequests(int sock_fd,	struct addrinfo * p) {
	char * receiveBuffer = (char *) calloc(BLOCK_SIZE,sizeof(char));
	char * splitResponse;
	struct in6_addr * ipv6Pointer = (struct in6_addr *) calloc(1,sizeof(struct in6_addr));

	while (1) {
		printf("Waiting for client message...\n");
		// Get the message

		//TODO: Error handling (numbytes = -1)
		receiveUDPIPv6(sock_fd, receiveBuffer, BLOCK_SIZE, p, ipv6Pointer);


		// Switch on the client command
		if (memcmp(receiveBuffer, ALLOC_CMD,2) == 0) {
			printf("******ALLOCATE******\n");
			splitResponse = receiveBuffer+2;
			providePointer(sock_fd, p);
		} else if (memcmp(receiveBuffer, WRITE_CMD,2) == 0) {
			splitResponse = receiveBuffer+2;
			printf("******WRITE DATA: ");
			printNBytes((char *) ipv6Pointer, IPV6_SIZE);
			writeMem(sock_fd, splitResponse, p, ipv6Pointer);
		} else if (memcmp(receiveBuffer, GET_CMD,2) == 0) {
			splitResponse = receiveBuffer+2;
			printf("******GET DATA: ");
			printNBytes((char *) ipv6Pointer,IPV6_SIZE);
			getMem(sock_fd, p, ipv6Pointer);
		} else if (memcmp(receiveBuffer, FREE_CMD,2) == 0) {
			splitResponse = receiveBuffer+2;
			printf("******FREE DATA: ");
			printNBytes((char *) ipv6Pointer,IPV6_SIZE);
			freeMem(sock_fd, p, ipv6Pointer);
		} else if (memcmp(receiveBuffer, GET_ADDR_CMD,2) == 0) {
			splitResponse = receiveBuffer+2;
			printf("******GET ADDRESS: \n");
			printf("Calling send address\n");
			// printNBytes((char *) ipv6Pointer,IPV6_SIZE);
			printNBytes(splitResponse, POINTER_SIZE);
			sendAddr(sock_fd, p, splitResponse);
			printf("Done send address\n");
		} else {
			printf("Cannot match command!\n");
			if (sendUDP(sock_fd, "Hello, world!", 13, p) == -1) {
				perror("ERROR writing to socket");
				exit(1);
			}
		}
	}
}

/*
 * TODO: explain
 * Binds to the next available address?
 */
struct addrinfo* bindSocket(struct addrinfo* p, struct addrinfo* servinfo, int* sockfd) {
	int blocking = 0;
	//Socket operator variables
	const int on=1, off=0;

	// loop through all the results and bind to the first we can
	for (p = servinfo; p != NULL; p = p->ai_next) {
		if ((*sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol))
				== -1) {
			perror("server: socket");
			continue;
		}

		if (setsockopt(*sockfd, SOL_SOCKET, SO_REUSEADDR, &blocking, sizeof(int))
				== -1) {
			perror("setsockopt");
			exit(1);
		}
		setsockopt(*sockfd, IPPROTO_IP, IP_PKTINFO, &on, sizeof(on));
		setsockopt(*sockfd, IPPROTO_IPV6, IPV6_RECVPKTINFO, &on, sizeof(on));
		setsockopt(*sockfd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));

		if (bind(*sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(*sockfd);
			perror("server: bind");
			continue;
		}

		break;
	}

	return p;
}

/*
 * Accept a connection on sockfd
 */
//TODO: Remove?
int acceptConnections(int sockfd) {
	// connector's address information
	struct sockaddr_storage their_addr; 
	char s[INET6_ADDRSTRLEN];
	socklen_t sin_size = sizeof their_addr;
	//wait for incoming connection
	int temp_fd = accept(sockfd, (struct sockaddr*) &their_addr, &sin_size);
	inet_ntop((&their_addr)->ss_family,
			get_in_addr((struct sockaddr*) &their_addr), s, sizeof s);

	printf("server: got connection from %s\n", s);

	return temp_fd;
}

/*
 * Main workhorse method. Parses command args and does setup.
 * Blocks waiting for connections.
 */
int main(int argc, char *argv[]) {
	int sockfd;  // listen on sock_fd, new connection on sock_fd
	struct addrinfo hints, *servinfo;
	int rv;

	// hints = specifies criteria for selecting the socket address
	// structures
	memset(&hints, 0, sizeof(hints));
	if (argc < 2) {
		printf("Defaulting to standard values...\n");
		argv[1] = "5000";
		hints.ai_flags = AI_PASSIVE; // use my IP

	}
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_DGRAM;
	if ((rv = getaddrinfo(NULL, argv[1], &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}
	struct addrinfo *p;

	// loop through all the results and bind to the first we can
	p = bindSocket(p, servinfo, &sockfd);

/*		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("client: connect");
			continue;
		}
*/

	// Frees the memory allocated for servinfo (list of possible sockets)
	//freeaddrinfo(servinfo);

	// TODO: should free hints as well?
	// -> not a pointer

	// Check to make sure the server was able to bind
	if (p == NULL) {
		fprintf(stderr, "server: failed to bind\n");
		exit(1);
	}
	// Listen on the socket with a queue size of BACKLOG
/*	if (listen(sockfd, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}*/



	// Start waiting for connections
	while (1) {

		//int sock_fd = acceptConnections(sockfd);
		/*if (sock_fd == -1) {
			perror("accept");
			exit(1);
		}*/
		//if (!fork()) {
			// fork the process
		//	close(sockfd); // child doesn't need the listener

		handleClientRequests(sockfd, p);
		//}

		//close(sock_fd); // parent doesn't need this
	}
	freeaddrinfo(p);
	close(sockfd);
	return 0;
}