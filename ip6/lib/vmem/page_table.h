#ifndef PAGE_TABLE_H
#define PAGE_TABLE_H

#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/mman.h>
#include <limits.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <ucontext.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

struct page_table;
typedef void (*page_fault_handler_t) ( struct page_table *pt, uint64_t page );
struct page_table {
    int fd;
    uint8_t *virtmem;
    uint64_t npages;
    uint8_t *physmem;
    uint64_t nframes;
    uint64_t *page_mapping;
    int *page_bits;
    page_fault_handler_t handler;
};


/* Create a new page table, along with a corresponding virtual memory
that is "npages" big and a physical memory that is "nframes" big
 When a page fault occurs, the routine pointed to by "handler" will be called. */

struct page_table * page_table_create( uint64_t npages, uint64_t nframes, page_fault_handler_t handler );

/* Delete a page table and the corresponding virtual and physical memories. */

void page_table_delete( struct page_table *pt );

void page_table_flush();
void clear_frame_table();
void register_vmem_threads();
void init_vmem_thread(int t_id);

void set_vmem_config(char *filename);
struct page_table *init_virtual_memory(uint64_t npages, uint64_t nframes, const char* system, const char* algo);
void print_page_faults();
void clean_page_table(struct page_table *pt);
void init_thread_table(int num_threads);
void close_thread_sockets();
/*
Set the frame number and access bits associated with a page.
The bits may be any of PROT_READ, PROT_WRITE, or PROT_EXEC logical-ored together.
*/

void page_table_set_entry( struct page_table *pt, uint64_t page, uint64_t frame, int bits );

/*
Get the frame number and access bits associated with a page.
"frame" and "bits" must be pointers to integers which will be filled with the current values.
The bits may be any of PROT_READ, PROT_WRITE, or PROT_EXEC logical-ored together.
*/

void page_table_get_entry( struct page_table *pt, uint64_t page, uint64_t *frame, int *bits );

/* Return a pointer to the start of the virtual memory associated with a page table. */

uint8_t *page_table_get_virtmem( struct page_table *pt );

/* Return a pointer to the start of the physical memory associated with a page table. */

uint8_t *page_table_get_physmem( struct page_table *pt );

/* Return the total number of frames in the physical memory. */

uint64_t page_table_get_nframes( struct page_table *pt );

/* Return the total number of pages in the virtual memory. */

uint64_t page_table_get_npages( struct page_table *pt );

/* Print out the page table entry for a single page. */

void page_table_print_entry( struct page_table *pt, uint64_t page );

/* Print out the state of every page in a page table. */

void page_table_print();
void frame_table_print();
void frame_table_print_entry();

struct hashNode {
	uint64_t key;
	struct hashNode *next;
	void *listNodePointer;
};

struct dllListNode {
	uint64_t key;
	struct dllListNode *next;
	struct dllListNode *prev;
};

struct hash {
	struct hashNode *head;
	int count;
};

struct dllList {
	struct dllListNode *head;
	struct dllListNode *tail;
	uint64_t count;
};

struct freqListNode {
	uint64_t useCount;
	struct freqListNode *next;
	struct freqListNode *prev;
	struct lfuListNode *head;
	struct lfuListNode *tail;
};

struct lfuListNode {
	uint64_t key;
	struct lfuListNode *next;
	struct lfuListNode *prev;
	struct freqListNode *parent;
};

struct freqList {
	struct freqListNode *head;
	uint64_t count;
};

void deleteLRU(uint64_t *page);

void deleteLFU(uint64_t *page);

void* getHashNode(uint64_t key);

struct dllListNode *createdllListNode(uint64_t key);

void moveListNodeToFront(struct dllListNode *node);

void moveNodeToNextFreq(struct lfuListNode *node);

struct lfuListNode *createlfuListNode(uint64_t key);

void insertHashNode(uint64_t key, void* listNode);

void deleteHashNode(uint64_t key);

void deletedllListNode(struct dllListNode *node);

#endif
