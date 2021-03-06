/*
Do not modify this file.
Make all of your changes to main.c instead.
*/

#include "disk.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <assert.h>

#include "../client_lib.h"
#include "rmem.h"
#include "rrmem.h"
#include "../utils.h"


extern ssize_t pread (int __fd, void *__buf, size_t __nbytes, __off_t __offset);
extern ssize_t pwrite (int __fd, const void *__buf, size_t __nbytes, __off_t __offset);
//trrmem is a temporary implementation of raid memory
struct rrmem {
    struct sockaddr_in6 *target_ip;
    struct rmem *rsmem; //a striped array of remote memory, one for each host
    int raid;    //the raid level implemented
    int block_size;
    int nblocks;
};



struct config myConf;
void configure_rrmem(char *filename) {
    myConf = set_bb_config(filename, 0);
    set_host_list(myConf.hosts, myConf.num_hosts);
}

void rrmem_init_sockets(struct rrmem *r) {
    r->target_ip = init_sockets(&myConf, 0);
}


void fill_rrmem(struct rrmem *r) {
    struct rmem *hostlist = malloc(sizeof(struct rmem) * get_num_hosts());

    for (int i = 0; i<get_num_hosts(); i++) {
        ip6_memaddr *memList = malloc(sizeof(ip6_memaddr) *r->nblocks );
        struct in6_addr *ipv6Pointer = get_ip6_target(i);
        memcpy(&(r->target_ip->sin6_addr), ipv6Pointer, sizeof(*ipv6Pointer));
        ip6_memaddr *temp = allocate_bulk_rmem(r->target_ip, r->nblocks);
        memcpy(memList,temp,r->nblocks *sizeof(ip6_memaddr) );
        free(temp);
        hostlist[i].memList = memList;
    }
    r->rsmem = hostlist;
}

struct rrmem *rrmem_allocate(int nblocks) {
    //allocate space for raided memeory
    struct rrmem *r;
    r = malloc(sizeof(*r));
    if(!r) return 0;

    //initalize soccet connection
    rrmem_init_sockets(r);
    r->raid=4;
    r->block_size = BLOCK_SIZE;
    r->nblocks = nblocks;
    fill_rrmem(r);
    return r;
}

uint8_t wbufs [MAX_HOSTS][BLOCK_SIZE];
ip6_memaddr *wremote_addrs[MAX_HOSTS];
void rrmem_write(struct rrmem *r, int block, uint8_t *data) {
    for (int i = 0; i<MAX_HOSTS;i++){
        memset(wbufs[i],0,BLOCK_SIZE);
    }
    if(block<0 || block>=r->nblocks) {
        fprintf(stderr,"rr_write: invalid block #%d\n",block);
        abort();
    }
    // Get pointer to page data in (simulated) physical memory
    // Slice up data based on given raid configuration
    //char parity [PAGE_SIZE];
    int alloc;
    int base;
    switch (r->raid) {
        case 0 :
            //printf("Writing Raid %d\n",r->raid);
            assert(get_num_hosts() >= 2 && get_num_hosts() <= MAX_HOSTS);
            alloc = r->block_size / (get_num_hosts()); //Raid 1
            if (r->block_size % (get_num_hosts()) > 0) {
                alloc++;
            }
            base = 0;
            //ip6_memaddr **remote_addrs = malloc(sizeof(ip6_memaddr*) * get_num_hosts());
            //calculate parity
            //parity45(data,r->block_size,get_num_hosts()-1,(char *)&wbufs[get_num_hosts()-1]);
            for (int i = 0; i<get_num_hosts(); i++) {
                if (r->block_size % (get_num_hosts()) <= i) {
                    alloc = r->block_size / (get_num_hosts());
                }
                //printf("Copying read data to buffers\n");
                memcpy(&wbufs[i],&(data[base]),alloc);
                //printf("Copying Remote Adders\n");
                wremote_addrs[i] = &r->rsmem[i].memList[block];
                base += alloc;
            }
            //wremote_addrs[get_num_hosts()-1] = &r->rsmem[get_num_hosts()-1].memList[block];
            //printf("Writing with parallel raid\n");
            //printf("Writing Remote\n");
            write_raid_mem(r->target_ip,get_num_hosts(),&wbufs, (ip6_memaddr**)wremote_addrs,get_num_hosts());
            //printf("FINISHED :: Writing with parallel raid\n");
            break;
        case 4 :
            //printf("Writing Raid %d\n",r->raid);
            assert(get_num_hosts() >= 2 && get_num_hosts() <= MAX_HOSTS);
            alloc = r->block_size / (get_num_hosts() - 1); //Raid 4
            if (r->block_size % (get_num_hosts() -1) > 0) {
                alloc++;
            }
            base = 0;
            //ip6_memaddr **remote_addrs = malloc(sizeof(ip6_memaddr*) * get_num_hosts());
            //calculate parity
            parity45(data,r->block_size,get_num_hosts()-1,(uint8_t *)&wbufs[get_num_hosts()-1]);
            for (int i = 0; i<get_num_hosts()-1; i++) {
                if (r->block_size % (get_num_hosts() -1) <= i) {
                    alloc = r->block_size / (get_num_hosts() -1);
                }
                //printf("Copying read data to buffers\n");
                memcpy(&wbufs[i],&(data[base]),alloc);
                //printf("Copying Remote Adders\n");
                wremote_addrs[i] = &r->rsmem[i].memList[block];
                base += alloc;
            }
            wremote_addrs[get_num_hosts()-1] = &r->rsmem[get_num_hosts()-1].memList[block];
            //printf("Writing with parallel raid\n");
            //printf("Writing Remote\n");
            write_raid_mem(r->target_ip,get_num_hosts(),&wbufs, (ip6_memaddr**)wremote_addrs,get_num_hosts()-1);
            //printf("FINISHED :: Writing with parallel raid\n");
            break;
        case 5 :
            assert(get_num_hosts() >= 2 && get_num_hosts() <= MAX_HOSTS);
            alloc = r->block_size / (get_num_hosts() - 1); //Raid 5
            if (r->block_size % (get_num_hosts() -1) > 0) {
                alloc++;
            }
            base = 0;
            //ip6_memaddr **remote_addrs = malloc(sizeof(ip6_memaddr*) * get_num_hosts());
            //calculate parity
             
            parity45(data,r->block_size,get_num_hosts()-1,(uint8_t *)&wbufs[block % get_num_hosts()]);
            for (int i = 0; i<get_num_hosts(); i++) {
                //printf("looping %d\n",i);
                if (i == block % get_num_hosts()) {
                    //parity
                    continue;
                }

                //TODO verify if this works or not
                int t = ( i > block % get_num_hosts());
                //printf("block %d mod %d t=%d\n",block, block%get_num_hosts(),t);
                if (r->block_size % (get_num_hosts() -1) <= i - t) {
                    alloc = r->block_size / (get_num_hosts() -1);
                }
                //printf("Copying read data to buffers\n");
                memcpy(&wbufs[i],&(data[base]),alloc);
                //printf("Copying Remote Adders\n");
                wremote_addrs[i] = &r->rsmem[i].memList[block];
                //printf("done copy\n");
                base += alloc;
            }
            wremote_addrs[block % get_num_hosts()] = &r->rsmem[block % get_num_hosts()].memList[block];
            //printf("Writing with parallel raid\n");
            //printf("Writing Remote\n");
            write_raid_mem(r->target_ip,get_num_hosts(),&wbufs, (ip6_memaddr**)wremote_addrs,get_num_hosts()-1);

            //printf("FINISHED :: Writing with parallel raid\n");
            break;


        default:
            printf("Raid %d not implemented FATAL write request\n",r->raid);
            //TODO Free memory and exit
            exit(0);
            break;
    }
}

uint8_t rbufs [MAX_HOSTS][BLOCK_SIZE];
ip6_memaddr *rremote_addrs[MAX_HOSTS];
void rrmem_read( struct rrmem *r, int block, uint8_t *data ) {
    for (int i = 0; i<MAX_HOSTS;i++){
        memset(rbufs[i],0,BLOCK_SIZE);
    }
    if(block<0 || block>=r->nblocks) {
        fprintf(stderr,"disk_read: invalid block #%d\n",block);
        abort();
    }
    int missingIndex;
    int alloc;
    int base;
    switch (r->raid) {
        case 0:
            assert(get_num_hosts() >= 2 && get_num_hosts() <= MAX_HOSTS);
            for (int i = 0; i<get_num_hosts() ; i++) {
                //remoteReads[i] = malloc(sizeof(char) * r->block_size);
                rremote_addrs[i] = &r->rsmem[i].memList[block];
            }
            //printf("Reading Remotely (Parallel Raid)\n");
            missingIndex = read_raid_mem(r->target_ip,get_num_hosts(),&rbufs,(ip6_memaddr**)rremote_addrs,get_num_hosts());
            if (missingIndex != -1) {
                printf("RAID 0 DOES NOT HANDLE FAILURES!!!\n");
                exit(0);
            }

            alloc = r->block_size / (get_num_hosts()); //Raid 0
            if (r->block_size % (get_num_hosts()) > 0) {
                alloc++;
            }
            base = 0;
            for (int i = 0; i<get_num_hosts(); i++) {
                if (r->block_size % (get_num_hosts()) <= i) {
                    alloc = r->block_size / (get_num_hosts());
                }
                //memcpy(&(data[base]),&((*rbufs)[i]),alloc);
                memcpy(&(data[base]),&rbufs[i],alloc);
                base += alloc;
            }
            
            //printf("PAGE:\n%s\n",data);

            break;
        case 4:
            assert(get_num_hosts() >= 2 && get_num_hosts() <= MAX_HOSTS);
            for (int i = 0; i<get_num_hosts() ; i++) {
                //remoteReads[i] = malloc(sizeof(char) * r->block_size);
                rremote_addrs[i] = &r->rsmem[i].memList[block];
            }
            //printf("Reading Remotely (Parallel Raid)\n");
            missingIndex = read_raid_mem(r->target_ip,get_num_hosts(),&rbufs,(ip6_memaddr**)rremote_addrs,get_num_hosts()-1);
            if (missingIndex == -1) {
                //printf("All stripes retrieved checking for correctness\n");
                /*
                if (!checkParity45(&rbufs,get_num_hosts()-1,&(rbufs[get_num_hosts()-1]),r->block_size)) {
                    //TODO reissue requests and try again, or correct the
                    //broken page
                    printf("Parity Not correct!!! Crashing");
                    //exiit(1);
                }
                */
            } else if (missingIndex == (get_num_hosts() - 1)) {
                //printf("(P*)");
                //printf("parity missing, just keep going without correctness\n");
            } else {
                //printf("missing page %d, repairing from parity\n", missingIndex);
                repairStripeFromParity45(&rbufs[missingIndex],&rbufs,&rbufs[get_num_hosts()-1],missingIndex,get_num_hosts()-1,r->block_size);
                //printf("(R*)");
                //printf("repair complete\n");
            }

            alloc = r->block_size / (get_num_hosts() - 1); //Raid 4
            if (r->block_size % (get_num_hosts() -1) > 0) {
                alloc++;
            }
            base = 0;
            for (int i = 0; i<get_num_hosts()-1; i++) {
                if (r->block_size % (get_num_hosts() -1) <= i) {
                    alloc = r->block_size / (get_num_hosts() -1);
                }
                //memcpy(&(data[base]),&((*rbufs)[i]),alloc);
                memcpy(&(data[base]),&rbufs[i],alloc);
                base += alloc;
            }
            break;
        case 5:
            assert(get_num_hosts() >= 2 && get_num_hosts() <= MAX_HOSTS);
            
            for (int i = 0; i<get_num_hosts() ; i++) {
                //remoteReads[i] = malloc(sizeof(char) * r->block_size);
                rremote_addrs[i] = &r->rsmem[i].memList[block];
            }
            //printf("Reading Remotely (Parallel Raid)\n");
            missingIndex = read_raid_mem(r->target_ip,get_num_hosts(),&rbufs,(ip6_memaddr**)rremote_addrs,get_num_hosts()-1);
            if (missingIndex == -1) {
                //printf("All stripes retrieved checking for correctness\n");
                /*
                if (!checkParity45(&rbufs,get_num_hosts()-1,&(rbufs[get_num_hosts()-1]),r->block_size)) {
                    //TODO reissue requests and try again, or correct the
                    //broken page
                    printf("Parity Not correct!!! Crashing");
                    //exit(1);
                }
                */
            } else if (missingIndex == (block % get_num_hosts())) {
                //printf("(P*)");
                //printf("parity missing, just keep going without correctness\n");
            } else {
                //printf("missing page %d, repairing from parity\n", missingIndex);
                repairStripeFromParity45(&rbufs[missingIndex],&rbufs,&rbufs[block % get_num_hosts()],missingIndex,get_num_hosts()-1,r->block_size);
                //printf("(R*)");
                //printf("repair complete\n");
            }

            alloc = r->block_size / (get_num_hosts() - 1); //Raid 5
            if (r->block_size % (get_num_hosts() -1) > 0) {
                alloc++;
            }
            base = 0;
            for (int i = 0; i<get_num_hosts(); i++) {
                if ( i == block % get_num_hosts()) {
                    //parity
                    continue;
                }
                int t = ( i > block % get_num_hosts());
                if (r->block_size % (get_num_hosts() -1) <= i - t) {
                    alloc = r->block_size / (get_num_hosts() -1);
                }
                //memcpy(&(data[base]),&((*rbufs)[i]),alloc);
                memcpy(&(data[base]),&rbufs[i],alloc);
                base += alloc;
            }
            
            printf("PAGE:\n%s\n",data);

            break;
        
        default:
            printf("Raid %d not implemented FATAL read request\n",r->raid);
            break;
    }


}



//stripes are assumed to be ordered
void repairStripeFromParity45(uint8_t (*repair)[BLOCK_SIZE], uint8_t (*stripes)[MAX_HOSTS][BLOCK_SIZE], uint8_t (*parity)[BLOCK_SIZE], int missing, int numStripes, int size) {
    int alloc = size / numStripes;
    for (int i=0; i< alloc; i++) {
        char repairbyte = 0;
        for (int j=0;j<numStripes;j++) {
            //this may seem like inverted access but i'm checking
            //across stripes
            if ( j != missing ) {
                repairbyte = repairbyte ^ (*stripes)[j][i];
            }
        }
        (*repair)[i] = repairbyte ^ (*parity)[i];
    }
    //This is only called if the page size does not evenly divide
    //across strips, and the missing page is one of the larger stripes
    if ( size % numStripes > missing) {
        char repairbyte = 0;
        for (int i=0; i<size%numStripes;i++) {
            if ( i != missing)  {
                repairbyte = repairbyte ^ (*stripes)[i][alloc];
            }
        }
        (*repair)[alloc] = repairbyte ^ (*parity)[alloc];
    }
    return;
    
}


void parity45(uint8_t *data, int size, int stripes, uint8_t *parity) {
    //clock_t start = clock(), diff;
    //Malloc the correct ammount of space for the parity
    int alloc = size / stripes;
    /*
    char * parity;
    if (size %stripes > 0) {
        parity = malloc(sizeof(char) * (alloc + 1));
    } else {
        parity = malloc(sizeof(char) * alloc);
    }*/

    //calculate the majority of the parity (final byte may not be
    //calculated
    for (int i=0; i< alloc; i++) {
        char paritybyte = 0;
        int base = 0;
        for (int j=0;j<stripes;j++) {
            paritybyte = paritybyte ^ data[i + base];
            /*
            if (i == 0) {
                printf("PARITY i:%d base:%d byte:%02x TransParity:%02x \n",i, base ,data[i + base],paritybyte);
            }
            */
            base += alloc;
            if (j < size % stripes) {
                base++;
            }
        }
        parity[i] = paritybyte;
    }
    //printf("final parody\n");
    if (size%stripes > 0) {
        char paritybyte = 0;
        int base = alloc;
        for (int i=0; i<size%stripes;i++) {
            paritybyte = paritybyte ^ data[base];
            //printf("PARITY i:%d base:%d byte:%02x TransParity:%02x \n",i, base ,data[base],paritybyte);
            base += alloc + 1;
        }
        parity[alloc] = paritybyte;
    }
    //diff = (double)clock() - start;
    //int microsec = diff * 1000000 / CLOCKS_PER_SEC;
    //printf("mics :%d\n",microsec);
    return;
}

//Here stripes are assumed to be in order from biggest to smallest
int checkParity45(uint8_t (*stripes)[MAX_HOSTS][BLOCK_SIZE], int numStripes, uint8_t (*parity)[BLOCK_SIZE], int size) {
    int alloc = size / numStripes;
    for (int i=0; i< alloc; i++) {
        char paritybyte = 0;
        //int base = 0;
        for (int j=0;j<numStripes;j++) {
            //this may seem like inverted access but i'm checking
            //across stripes
            paritybyte = paritybyte ^ (*stripes)[j][i];
            /*
            if (i == 0) {
                printf("CHECK index :%d stripe:%d byte:%02x TransParity:%02x CompParity:%02x \n",j, i ,stripes[j][i],paritybyte,parity[i]);
            }
            */
        }
        if (paritybyte != (*parity)[i]) {
            printf("Fault on byte %d : Calculated Parity Byte %02x != %02x written parity\n",i,paritybyte,(*parity)[i]);
            return 0;
        }
    }
    //This is only called if the page size does not evenly divide
    //across strips, it checks the parody of 
    //printf("final parody check\n");
    if ( size % numStripes > 0) {
        char paritybyte = 0;
        for (int i=0; i<size%numStripes;i++) {
            //printf("CHECK index:%d stripe:%d byte:%02x TransParity:%02x \n",alloc, i ,stripes[i][alloc],paritybyte);
            paritybyte = paritybyte ^ (*stripes)[i][alloc];
        }
        if (paritybyte != (*parity)[alloc]) {
            printf("Fault on final byte %d : Calculated Parity Byte %02x != %02x written parity\n",alloc,paritybyte,*parity[alloc]);
            return 0;
        }
    }
    //The parity has been verified
    return 1;
}



int rrmem_blocks(struct rrmem *r) {
    return r->nblocks;
}

void rrmem_deallocate(struct rrmem *r) {
    for(int i =0; i<get_num_hosts();i++){
        free_rmem(r->target_ip, r->rsmem[i].memList);
    }
    close_sockets();
    free(r);
}
