/*
 ** client.c -- a stream socket client demo
 */

/*#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <time.h>
#include <arpa/inet.h>
#include <inttypes.h>
*/
#include "./lib/538_utils.h"


/////////////////////////////////// TO DOs ////////////////////////////////////
//	1. IPv6 conversion method
//			| global routing prefix (48) | subnet ID (16) | pointer (64)|
///////////////////////////////////////////////////////////////////////////////

struct PointerMap {
	char* AddrString;
	uint64_t* Pointer;
};

/*
 * Sends message to allocate memory
 */
uint64_t allocateMem(int sockfd) {
	print_debug("Mallocing send and receive buffers");
	char * sendBuffer = malloc(BLOCK_SIZE*sizeof(char));
	char * receiveBuffer = malloc(BLOCK_SIZE*sizeof(char));
	char * splitResponse;

	print_debug("Memcopying ALLOCATE message into send buffer");
	// Send message to server to allocate memory
	memcpy(sendBuffer, "ALLOCATE", sizeof("ALLOCATE"));
	sendMsg(sockfd, sendBuffer, sizeof("ALLOCATE"));

	print_debug("Waiting to receive replyin receive buffer");
	// Wait to receive a message from the server
	receiveMsg(sockfd, receiveBuffer, BLOCK_SIZE);

	print_debug("Parsing response");
	// Parse the response
	splitResponse = strtok(receiveBuffer, ":");

	uint64_t retVal = 0;

	if (strcmp(receiveBuffer,"ACK") == 0) {
		print_debug("Response was ACK");
		// If the message is ACK --> successful
		uint64_t remotePointer =  0;

		print_debug("Memcopying the pointer");
		memcpy(&remotePointer, strtok(NULL, ":"), 8);

		// char formatted_string[100] = {};
		// sprintf(formatted_string, "Got %" PRIx64 " from server", remotePointer);
		print_debug("Got %" PRIx64 " from server", remotePointer);

		print_debug("Setting remotePointer to be return value");
		retVal = remotePointer;
	} else {
		print_debug("Response was not successful");
		// Not successful so we send another message?
		memcpy(sendBuffer, "What's up?", sizeof("What's up?"));
		sendMsg(sockfd, sendBuffer, sizeof("What's up?"));
		// TODO: why do we return 0? Isn't there a better number 
		// (i.e. -1)

		print_debug("Keeping return value as 0");
	}

	print_debug("Freeing sendBuffer and receiveBuffer memory");
	free(sendBuffer);
	free(receiveBuffer);

	print_debug("Returning value");
	return retVal;
}

/*
 * Sends a write command to the sockfd for pointer remotePointer
 */
int writeToMemory(int sockfd, uint64_t * remotePointer, int index) {
	print_debug("Mallocing sendBuffer and receiveBuffer");
	char * sendBuffer = malloc(BLOCK_SIZE*sizeof(char));
	char * receiveBuffer = malloc(BLOCK_SIZE*sizeof(char));

	int size = 0;

	print_debug("Copying the data to the send buffer");

	// Create the data
	memcpy(sendBuffer, "WRITE:", sizeof("WRITE:"));
	size += sizeof("WRITE:") - 1; // Want it to rewrite null terminator
	memcpy(sendBuffer+size,remotePointer,sizeof(remotePointer));
	size += sizeof(remotePointer);

	print_debug("Creating payload");
	char * payload = get_rdm_string((BLOCK_SIZE-size), index);
	memcpy(sendBuffer+size+1,payload, strlen(payload));
	size += strlen(payload);

	// Send the data
	printf("Sending Data: %d bytes to ", size);
	printBytes((char*) remotePointer);

	print_debug("Sending message");
	sendMsg(sockfd, sendBuffer, size);

	print_debug("Waiting for response");
	// Wait for the response
	receiveMsg(sockfd, receiveBuffer, BLOCK_SIZE);

	print_debug("Freeing sendBuffer and receiveBuffer");
	free(sendBuffer);
	free(receiveBuffer);

	print_debug("Returning");
	// TODO change to be meaningful, i.e., error message
	return 0;
}

/*
 * Releases the remote memory
 */
int releaseMemory(int sockfd, uint64_t * remotePointer) {
	char * sendBuffer = malloc(BLOCK_SIZE*sizeof(char));
	char * receiveBuffer = malloc(BLOCK_SIZE*sizeof(char));

	int size = 0;

	// Create message
	memcpy(sendBuffer+size, "FREE:", sizeof("FREE:"));
	size += sizeof("FREE:") - 1; // Should be 5
	memcpy(sendBuffer+size, remotePointer, sizeof(remotePointer));
	size += sizeof(remotePointer); // Should be 8, total of 13

	printf("Releasing Data with pointer: ");
	printBytes((char*)remotePointer);

	// Send message
	sendMsg(sockfd, sendBuffer, size);

	// Receive response
	receiveMsg(sockfd, receiveBuffer, BLOCK_SIZE);

	free(sendBuffer);
	free(receiveBuffer);

	// TODO change to be meaningful, i.e., error message
	return 0;
}

/*
 * Reads the remote memory
 */
char * getMemory(int sockfd, uint64_t * remotePointer) {
	char * sendBuffer = malloc(BLOCK_SIZE*sizeof(char));
	char * receiveBuffer = malloc(4096*1000);

	int size = 0;

	// Prep message
	memcpy(sendBuffer, "GET:", sizeof("GET:"));
	size += sizeof("GET:") - 1; // Should be 4
	memcpy(sendBuffer+size, remotePointer, sizeof(remotePointer));
	size += sizeof(remotePointer); // Should be 8, total 12

	printf("Retrieving Data with pointer: ");
	printBytes((char*)remotePointer);

	// Send message
	sendMsg(sockfd, sendBuffer, size);
	// Receive response
	receiveMsg(sockfd, receiveBuffer, BLOCK_SIZE);

	free(sendBuffer);

	return receiveBuffer;
}

/*
 * Main workhorse method. Parses arguments, setups connections
 * Allows user to issue commands on the command line.
 */
int main(int argc, char *argv[]) {
	int sockfd;
	struct addrinfo hints, *servinfo, *p;
	int rv;
	char s[INET6_ADDRSTRLEN];

	srand(time(NULL));

	if (argc < 2) {
		printf("Defaulting to standard values...\n");
		argv[1] = "::1";
		argv[2] = "5000";
	}

	// Tells the getaddrinfo to only return sockets
	// which fit these params.
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_STREAM;

	if ((rv = getaddrinfo(argv[1], argv[2], &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and connect to the first we can
	for (p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol))
				== -1) {
			perror("client: socket");
			continue;
		}

		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("client: connect");
			continue;
		}

		break;
	}

	if (p == NULL) {
		fprintf(stderr, "client: failed to connect\n");
		return 2;
	}

	inet_ntop(p->ai_family, get_in_addr((struct sockaddr *) p->ai_addr), s,
			sizeof s);
	printf("client: connecting to %s\n", s);

	int active = 1;
	long unsigned int len = 200;
	char input[len];
	char * localData;
	int count = 0;
	int i;
	struct PointerMap remotePointers[100];
	// Initialize remotePointers array
	for (i = 0; i < 100; i++) {
		remotePointers[i].AddrString = (char *) malloc(100);
		remotePointers[i].Pointer = malloc(sizeof(uint64_t));
	}

	while (active) {
		memset(input, 0, len);
		getLine("Please specify if you would like to (L)ist pointers, (A)llocate, (F)ree, (W)rite, or (R)equest data.\nPress Q to quit the program.\n", input, sizeof(input));
		if (strcmp("A", input) == 0) {
			memset(input, 0, len);
			getLine("Specify a number of address or press enter for one.\n", input, sizeof(input));

			if (strcmp("", input) == 0) {
				print_debug("Calling allocateMem");
				
				uint64_t remoteMemory = allocateMem(sockfd);
				
				// char formatted_string[100] = {};
				// sprintf(formatted_string, "Got %" PRIx64 " from call", remoteMemory);
				print_debug("Got %" PRIx64 " from call", remoteMemory);

				print_debug("Creating pointer to remote memory address");

				char addrString[100] = {};
				sprintf(addrString, "%" PRIx64, remoteMemory);
				strcpy(remotePointers[count].AddrString, addrString);
				memcpy(remotePointers[count++].Pointer, &remoteMemory, sizeof(remoteMemory));
				printf("Allocated pointer 0x%s\n", addrString);
				printf("Allocated pointer address %p\n", &remoteMemory);
			} else {
				int num = atoi(input);

				// char message[100] = {};
				// sprintf(message, "Received %d as input", num);
				print_debug("Received %d as input", num);

				int j; 
				for (j = 0; j < num; j++) {
					print_debug("Calling allocateMem");

					uint64_t remoteMemory = allocateMem(sockfd);
					
					// char formatted_string[100] = {};
					// sprintf(formatted_string, "Got %" PRIx64 " from call", remoteMemory);
					print_debug("Got %" PRIx64 " from call", remoteMemory);

					print_debug("Creating pointer to remote memory address");

					char addrString[100] = {};
					sprintf(addrString, "%" PRIx64, remoteMemory);
					strcpy(remotePointers[count].AddrString, addrString);
					memcpy(remotePointers[count++].Pointer, &remoteMemory, sizeof(remoteMemory));
					printf("Allocated pointer 0x%s\n", addrString);
					printf("Allocated pointer address %p\n", &remoteMemory);
				}
			}
		} else if (strcmp("L", input) == 0){
			printf("Remote Address\tPointer\n");
			for (i = 0; i < 100; i++){
				if (strcmp(remotePointers[i].AddrString, "") != 0) {
					printf("0x%s\t%p\n", remotePointers[i].AddrString, remotePointers[i].Pointer);
				}
			}
		} else if (strcmp("R", input) == 0) {
			memset(input, 0, len);
			getLine("Please specify the memory address.\n", input, sizeof(input));

			if (strcmp("A", input) == 0) {
				for (i = 0; i < 100; i++) {
					if (strcmp(remotePointers[i].AddrString, "") != 0) {
						// char message[100] = {};
						// sprintf(message, "Retrieving data from pointer 0x%s\n", remotePointers[i].AddrString);
						print_debug("Retriving data from pointer 0x%s", remotePointers[i].AddrString);
						localData = getMemory(sockfd, remotePointers[i].Pointer);
						printf("Retrieved Data (first 80 bytes): %.*s\n", 80, localData);
					}
				}
			} else {
				uint64_t pointer = getPointerFromString(input);
				// char message[100] = {};
				// sprintf(message, "Retrieving data from pointer 0x%p\n", (void *) pointer);
				print_debug("Retriving data from pointer 0x%p", (void *) pointer);
				localData = getMemory(sockfd, &pointer);
				printf("Retrieved Data (first 80 bytes): %.*s\n", 80, localData);
			}
		} else if (strcmp("W", input) == 0) {
			memset(input, 0, len);
			getLine("Please specify the memory address.\n", input, sizeof(input));

			if (strcmp("A", input) == 0) {
				for (i = 0; i < 100; i++) {
					if (strcmp(remotePointers[i].AddrString, "") != 0) {
						printf("Writing data to pointer 0x%s\n", remotePointers[i].AddrString);
						writeToMemory(sockfd, remotePointers[i].Pointer, i);
					}
				}
			} else {
				uint64_t pointer = getPointerFromString(input);
				printf("Writing data to pointer %p\n", (void *) pointer);
				writeToMemory(sockfd, &pointer, rand()%100);
			}
		} else if (strcmp("F", input) == 0) {
			memset(input, 0, len);
			getLine("Please specify the memory address.\n", input, sizeof(input));
			
			if (strcmp("A", input) == 0) {
				for (i = 0; i < 100; i++) {
					if (strcmp(remotePointers[i].AddrString, "") != 0) {
						printf("Freeing pointer 0x%s\n", remotePointers[i].AddrString);
						releaseMemory(sockfd, remotePointers[i].Pointer);
						remotePointers[i].AddrString = "";
						remotePointers[i].Pointer = 0;
					}
				}
			} else {
				uint64_t pointer = getPointerFromString(input);
				printf("Freeing pointer %p\n", (void *) pointer);
				releaseMemory(sockfd, &pointer);
				for (i = 0; i < 100; i++) {
					if (strcmp(remotePointers[i].AddrString, input+2) == 0){
						remotePointers[i].AddrString = "";
						remotePointers[i].Pointer = 0;
					}
				}
			}
		} else if (strcmp("T", input) == 0) {
			memset(input, 0, len);
			getLine("Please specify the memory address.\n", input, sizeof(input));
			uint64_t pointer = getPointerFromString(input);
			printf("Input pointer: %p\n", (void *) pointer);

			struct in6_addr temp = getIPv6FromPointer(pointer);

			printf("Received struct...\n");

			uint64_t newpointer = getPointerFromIPv6(temp);

			printf("New pointer: %" PRIx64 ", Old pointer: %" PRIx64 "\n", newpointer, pointer);

		} else if (strcmp("Q", input) == 0) {
			active = 0;
			printf("Ende Gelaende\n");
		} else {
			printf("Try again.\n");
		}
	}
	freeaddrinfo(servinfo); // all done with this structure

	// TODO: send close message so the server exits
	close(sockfd);

	return 0;
}
