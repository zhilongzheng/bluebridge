#define _GNU_SOURCE
#include <stdio.h>            // printf() and sprintf()
#include <stdlib.h>           // free(), alloc, and calloc()
#include <string.h>           // strcpy, memset(), and memcpy()
#include <sys/mman.h>         // mmap()

#include "utils.h"
#include "network.h"
#include "config.h"

// TODO Error handling in case of allocation failure
int allocate_mem(struct sockaddr_in6 *target_ip) {
#ifdef SOCK_RAW
    void *allocated = calloc(1 ,BLOCK_SIZE);
#else
    void *allocated = rte_calloc(NULL, 1 ,BLOCK_SIZE, 64);
#endif
    // Set the source address of the packet as our destination ID
    ip6_memaddr *returnID = (ip6_memaddr *)&target_ip->sin6_addr;
    returnID->cmd = ALLOC_CMD;
    // Copy the pointer into the IPv6 destination header
    memcpy(&returnID->paddr, &allocated, POINTER_SIZE);
    // There is no need for a send buffer here, just send zero
    send_udp_raw(NULL, 0, (ip6_memaddr *)&target_ip->sin6_addr, target_ip->sin6_port);
    return EXIT_SUCCESS;
}

// TODO Error handling in case of allocation failure
int allocate_mem_bulk( struct sockaddr_in6 *target_ip, uint64_t size) {
    // If we get a zero value, just allocate one pointer
    if (!size)
        size = 1;
    #ifdef SOCK_RAW
        void *allocated = calloc(size, BLOCK_SIZE);
    #else
        void *allocated = rte_calloc(NULL, size, BLOCK_SIZE, 64);
    #endif
    // Set the source address of the packet as our destination ID
    ip6_memaddr *returnID = (ip6_memaddr *)&target_ip->sin6_addr;
    returnID->cmd = ALLOC_BULK_CMD;
    // Copy the pointer into the IPv6 destination header
    memcpy(&returnID->paddr, &allocated, POINTER_SIZE);
    // There is no need for a send buffer here, just send zero
    send_udp_raw(NULL, 0, (ip6_memaddr *)&target_ip->sin6_addr, target_ip->sin6_port);
    return EXIT_SUCCESS;
}

// TODO error handling, avoid segmentation faults
int read_mem(struct sockaddr_in6 *target_ip, ip6_memaddr *r_addr) {
    // Set the source address of the client packet as our destination ID
    ip6_memaddr *returnID = (ip6_memaddr *) (&target_ip->sin6_addr);
    returnID->cmd = r_addr->cmd;
    returnID->paddr = r_addr->paddr;
    // Get the length we want to read from the client arguments
    // Note: args is uint32_t but the max IP packet size is uint16_t 
    //       We only care about the first 16 bits
    uint16_t length = (uint16_t) r_addr->args;
    // Send the data directly from the converted memory pointer
    send_udp_raw((void *) *&r_addr->paddr, length, (ip6_memaddr *) &target_ip->sin6_addr, target_ip->sin6_port);
    return EXIT_SUCCESS;
}

// TODO error handling, avoid segmentation faults
int read_mem_ptr(char *rcv_buffer, struct sockaddr_in6 *target_ip, ip6_memaddr *r_addr) {
    // Set the source address of the client packet as our destination ID
    ip6_memaddr *returnID = (ip6_memaddr *) (&target_ip->sin6_addr);
    returnID->cmd = r_addr->cmd;
    uint16_t length = (uint16_t) r_addr->args;
    memcpy(&returnID->paddr, rcv_buffer, POINTER_SIZE);
    send_udp_raw((void *) *&r_addr->paddr, length, (ip6_memaddr *) &target_ip->sin6_addr, target_ip->sin6_port);
    // TODO change to be meaningful, i.e., error message
    return EXIT_SUCCESS;
}

int write_mem(char *rcv_buffer, struct sockaddr_in6 *target_ip, ip6_memaddr *r_addr, uint16_t size) {
#ifdef SOCK_RAW
    memcpy((void *) *(&r_addr->paddr), rcv_buffer, size); 
#else
    rte_memcpy((void *) *(&r_addr->paddr), rcv_buffer, size);
#endif
    //__sync_synchronize();
    ip6_memaddr *returnID = (ip6_memaddr *) (&target_ip->sin6_addr);
    returnID->cmd = r_addr->cmd;
    returnID->paddr = r_addr->paddr;
    send_udp_raw(NULL, 0, (ip6_memaddr *)&target_ip->sin6_addr, target_ip->sin6_port);
    // TODO change to be meaningful, i.e., error message
    return EXIT_SUCCESS;
}

int write_mem_bulk(char *rcv_buffer, struct sockaddr_in6 *target_ip, ip6_memaddr *r_addr, uint16_t size) {
    // Copy the first POINTER_SIZE bytes of receive buffer into the target
#ifdef SOCK_RAW
    memcpy((void *) *(&r_addr->paddr), rcv_buffer, size); 
#else
    rte_memcpy((void *) *(&r_addr->paddr), rcv_buffer, size);
#endif
    if ((r_addr->args & 0x0000ffff) == ((r_addr->args & 0xffff0000) >> 16)) {
        ip6_memaddr *returnID = (ip6_memaddr *) (&target_ip->sin6_addr);
        returnID->cmd = r_addr->cmd;
        returnID->paddr = r_addr->paddr;
        send_udp_raw("", 0, (ip6_memaddr *)&target_ip->sin6_addr, target_ip->sin6_port);
    }
    // TODO change to be meaningful, i.e., error message
    return EXIT_SUCCESS;
}

int free_mem(struct sockaddr_in6 *target_ip, ip6_memaddr *r_addr) {
    free((void *) *&r_addr->paddr);
    ip6_memaddr *returnID = (ip6_memaddr *)&target_ip->sin6_addr;
    returnID->cmd = r_addr->cmd;
    returnID->paddr = r_addr->paddr;
    send_udp_raw(NULL, 0, (ip6_memaddr *)&target_ip->sin6_addr, target_ip->sin6_port);
    return EXIT_SUCCESS;
}

void process_request(struct sockaddr_in6 *target_ip, ip6_memaddr *remote_addr, char *rcv_buffer, uint16_t size) {
    // Switch on the client command
    if (remote_addr->cmd == ALLOC_CMD) {
        print_debug("******ALLOCATE******");
        allocate_mem(target_ip);
    } else if (remote_addr->cmd == WRITE_CMD) {
        print_debug("******WRITE DATA: ");
        if (DEBUG) print_n_bytes((char *) remote_addr, IPV6_SIZE);
        write_mem(rcv_buffer, target_ip, remote_addr, size);
    } else if (remote_addr->cmd == WRITE_BULK_CMD) {
        print_debug("******WRITE DATA BULK: ");
        if (DEBUG) print_n_bytes((char *) remote_addr, IPV6_SIZE);
        write_mem_bulk(rcv_buffer, target_ip, remote_addr, size);
    } else if (remote_addr->cmd == READ_CMD) {
        print_debug("******READ DATA: ");
        if (DEBUG) print_n_bytes((char *) remote_addr, IPV6_SIZE);
        read_mem(target_ip, remote_addr);
    } else if (remote_addr->cmd == READ_BULK_CMD) {
        print_debug("******READ DATA PTR: ");
        if (DEBUG) print_n_bytes((char *) remote_addr, IPV6_SIZE);
        read_mem_ptr(rcv_buffer, target_ip, remote_addr);
    } else if (remote_addr->cmd == FREE_CMD) {
        print_debug("******FREE DATA: ");
        if (DEBUG) print_n_bytes((char *) remote_addr, IPV6_SIZE);
        free_mem(target_ip, remote_addr);
    } else if (remote_addr->cmd == ALLOC_BULK_CMD) {
        print_debug("******ALLOCATE BULK DATA: ");
        if (DEBUG) print_n_bytes((char *) remote_addr,IPV6_SIZE);
        uint64_t *alloc_size = (uint64_t *) rcv_buffer;
        allocate_mem_bulk(target_ip, *alloc_size);
    } else {
        printf("Cannot match command %d!\n",remote_addr->cmd);
    }
}