/*
 * mm-segregated_fits - Memory allocator of the segregated fits family using the 
 * "size classes with range lists" technique, in which free blocks are organized in
 * several lists (one for each size class) and the lookup within them can employ
 * either the best-fit or the first-fit policy. Operations of splitting and coalescing
 * (this last one with the use of boundary tags) are also performed in order to
 * reduce fragmentation and improve memory usage performances.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/* MACROS */

/* alignment of addresses to 8 bytes */
#define ALIGNMENT 8
#define CLASSES 20
#define LIMIT_COALESCE 2
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

#define ADDRESS_SIZE (sizeof(void*))
#define LOOKUP_TABLE_SIZE ((CLASSES) * (ADDRESS_SIZE))
#define LOOKUP_TABLE(i) ((char*)(lookup_table) + (ADDRESS_SIZE * i))

/* compute the class corresponding to a size (saving it into ind) and the maximum size allowed in a class */
#define GET_CLASS(ind, size) { size_t value = 64; for (ind = 0 ; ind < CLASSES; ++ind) { if (value > size) break; else value *= 2;}}
#define GET_MAX_DIM(class) ((1<<(class+6)) - 1)

#define HEADER_SIZE (sizeof(size_t))
#define FOOTER_SIZE (sizeof(size_t))
#define MAX_OVERHEAD (HEADER_SIZE + FOOTER_SIZE + 2 * ADDRESS_SIZE)
#define HF_OVERHEAD (HEADER_SIZE + FOOTER_SIZE)

/* pointers to header and footer of ptr */
#define HEADER(ptr) ((char*)(ptr) - HEADER_SIZE)
#define FOOTER(ptr) ((char*)(ptr) + (GET_SIZE(HEADER(ptr))) - HF_OVERHEAD)

#define MBS (ALIGN(MAX_OVERHEAD))

/* write and read to/from a memory address values and adresses */
#define WRITE_DATA(ptr, data) (*(size_t*)(ptr) = data)
#define READ_DATA(ptr) (*(size_t*)(ptr))
#define WRITE_ADDR(ptr, addr) (*(void**)(ptr) = (addr))
#define READ_ADDR(ptr) (*(void**)(ptr))

/* PACK packs an integer value and a flag bit together, GET_SIZE and BIT read them */
#define PACK(data, bit) ((data) + (bit))
#define GET_SIZE(p) ((READ_DATA(p)) & ~7)
#define BIT(p) ((READ_DATA(p)) & 1)

#define PREV(ptr) (ptr)
#define NEXT(ptr) ((char*)(ptr) + ADDRESS_SIZE)

/* pointers to the blocks in the heap immediately to the left and to the right of the block pointed by ptr */
#define LEFT(ptr) ((char*)(ptr) - GET_SIZE((char*)(ptr) - HF_OVERHEAD))
#define RIGHT(ptr) ((char*)(ptr) + GET_SIZE(HEADER(ptr)))

/* commenting it we switch from best-fit to first-fit policy (and viceversa) */
#define BEST_FIT


/* GLOBAL VARIABLES */

/* lookup table address */
static void *lookup_table;
/* address of the first block allocated in the heap */
static void *first_block;
/* address of the last block allocated in the heap */
static void *end_heap;


/* FUNCTIONS */

/* 
 * mm_init: allocates an initial portion of the heap, initializes the lookup table
 * (with extra padding at the beginning in order to maintain addresses aligned to 8 bytes)
 * and allocates a first block of minimum size. It returns 0 if there are no errors
 * and -1 if the function mem_sbrk fails for some reason
 */
int mm_init(void) {
	size_t padd;
	int i;
	
	/* padding to be inserted at the beginning of the heap */
	padd = ALIGN(LOOKUP_TABLE_SIZE + HEADER_SIZE) - LOOKUP_TABLE_SIZE - HEADER_SIZE;
	
	if ((lookup_table = mem_sbrk(padd + LOOKUP_TABLE_SIZE + MBS)) == NULL)
		return -1;
	
	lookup_table += padd;
	
	/* initializing the lookup table with a NULL value for each class */
	for (i = 0; i < CLASSES; ++i)
		WRITE_ADDR(LOOKUP_TABLE(i), NULL);
	
	/* manually creating a first block... */
	first_block = (char*)(lookup_table) + LOOKUP_TABLE_SIZE + HEADER_SIZE;
	WRITE_DATA(HEADER(first_block), PACK(MBS, 0));
	WRITE_DATA(FOOTER(first_block), PACK(MBS, 0));
	
	/* ...and inserting it at the beginning of the corresponding list (the one for class 0) */
	put_on_front_of_class_list(0, first_block);
	
	end_heap = first_block;
	
    return 0;
}

/* 
 * mm_malloc: allocates a new block of memory from the lists of free blocks and, when not finding
 * a suitable one, increasing the heap size by calling the mem_sbrk function.
 * It returns the address of the newly allocated block (guaranteed to be aligned to 8 bytes)
 * or NULL if mem_sbrk fails
 */
void *mm_malloc(size_t size) {
	void *ptr;
	size_t remaining_size, newsize;
	int class;
	
	newsize = ALIGN(size + HF_OVERHEAD);
	
	/* initial checks on size and newsize */
	if (size <= 0)
		return NULL;
	
	if (newsize < MBS)
		newsize = MBS;
	
	/* saving in 'class' the class to which a block of size 'newsize' belongs */
	GET_CLASS(class, newsize);
	
	/* if we come out of this cycle... */ 
	while (class < CLASSES) {
		if ((ptr = search_free_list(class, newsize)) != NULL) {
			remaining_size = GET_SIZE(HEADER(ptr)) - newsize;
			
			/* if the remaining block would be too small we use the whole block without splitting it... */
			if (remaining_size <= MBS) {
				WRITE_DATA(HEADER(ptr), PACK(GET_SIZE(HEADER(ptr)), 1));	
				WRITE_DATA(FOOTER(ptr), PACK(GET_SIZE(HEADER(ptr)), 1));
			
				remove_from_free_list(class, ptr);
			}
			
			/* ...otherwise we delegate all the work to the split function */
			else {
				split(ptr, newsize);
			}
			
			return ptr;
		}
		/* if search_free_lists doesn't find a suitable block, we go on looking in the next class (if not the last) */
		else class++;
	}
	
	/* ...we didn't find a sufficiently large block in any of the free lists, we ask for additional memory */
	if ((ptr = mem_sbrk(newsize)) == NULL)
		return NULL;
	
	/* setting header and footer of the new block */
	ptr += HEADER_SIZE;
	WRITE_DATA(HEADER(ptr), PACK(newsize, 1));
	WRITE_DATA(FOOTER(ptr), PACK(newsize, 1));
	
	/* updating end_heap, since now the last block of the heap is the one we just allocated */
	end_heap = ptr;
	
	return ptr;
}

/*
 * mm_free: frees a block by setting to 0 the "free/in use bit" and by inserting it at the 
 * beginning of the free list of the class to which it belongs (depending on its size).
 * It doesn't have any return values
 */
void mm_free(void *ptr) {
	int class;
	
	if (ptr == NULL)
		return;
	
	/* marking the block as free (setting the bit to 0) */
	WRITE_DATA(HEADER(ptr), PACK(GET_SIZE(HEADER(ptr)), 0));	
	WRITE_DATA(FOOTER(ptr), PACK(GET_SIZE(HEADER(ptr)), 0));
	
	GET_CLASS(class, GET_SIZE(HEADER(ptr)));
	
	/* if the class to which the block belongs is greater than the "limit class", we call coalesce */
	if (class > LIMIT_COALESCE) {
		ptr = coalesce(ptr);
		GET_CLASS(class, GET_SIZE(HEADER(ptr)));
	}
	
	put_on_front_of_class_list(class, ptr);

	return;
}

/*
 * mm_realloc: modifies the size of a block, either making it bigger or smaller. In the first case
 * it looks for free space in the blocks immediately on the right or, alternatively, it calls
 * mm_malloc and copies the payload byte to byte in the new block; in the second case it splits the
 * block so to reduce it to the requested size
 */
void *mm_realloc(void *ptr, size_t size) {
    /* simple cases in which the reallocation can just be done in terms of mm_malloc or mm_free */
    if (ptr == NULL && size > 0) {
    	return mm_malloc(size);
    }
    else if (size == 0) {
    	mm_free(ptr);
    	return ptr;
    }
 	
 	else if (ptr != NULL && size > 0) {
    	size_t block_size = GET_SIZE(HEADER(ptr));
    	size_t newsize = ALIGN(size + HF_OVERHEAD);
    	
    	if (newsize < MBS)
			newsize = MBS;
		
    	if (newsize == block_size)
    		return ptr;
    	
    	/* case in which the requested block is bigger that the original */
    	else if (newsize > block_size) {  			
  			size_t diff = newsize - block_size;
			
			/* we simulate the coalesce to the right in order to check if there is sufficient space... */
			if (simulate_right_coalesce(ptr, diff)) {
				size_t total_size = 0;
				void *iterator = ptr, *last = ptr;
				int class;
				
				/* searching on the right until there are free blocks and the requested size is achieved */ 
				while (iterator != end_heap && !BIT(HEADER(RIGHT(iterator)))){
					total_size += GET_SIZE(HEADER(RIGHT(iterator)));
					GET_CLASS(class, GET_SIZE(HEADER(RIGHT(iterator))));
					remove_from_free_list(class, RIGHT(iterator));
					last = RIGHT(iterator);
					iterator = RIGHT(iterator);
					if (total_size >= diff)
						break;
				}
				
				/* writing in the block the overall size obtained by merging all the "neighbours" */
				WRITE_DATA(HEADER(ptr), PACK(block_size + total_size, 1));
				WRITE_DATA(FOOTER(ptr), PACK(block_size + total_size, 1));
				
				if (last == end_heap)
					end_heap = ptr;
					
				return ptr;
			}
			
			/* ...and if there is no sufficient space we allocate a new block with mm_malloc
			 * and copy byte to byte the payload to the new block */
			else {
				void *new_ptr;
				if ((new_ptr = mm_malloc(newsize)) == NULL)
					return NULL;
				
				memcpy(new_ptr, ptr, block_size - HF_OVERHEAD);
				
				mm_free(ptr);
				return new_ptr;
			}
		}
		
		/* if we are here the block size must be reduced, we just call split */
		else {   
	    	size_t diff = block_size - newsize;
	    	if (diff <= MBS)
	    		return ptr;
	    		
	    	else {
	    		split(ptr, newsize);
	    		return ptr;
			}
 		}
    }
    
    return NULL;
}

/*
 * split: divides a block of size x into two sub-blocks of size newsize and x - newsize respectively,
 * marking the first one as "in use" and the second one as "free" (and inserting it
 * into the corresponding free list)
 */
void split(void *ptr, size_t newsize) {
	int old_class, new_class;	
	void *newblock;
	size_t remaining_size = GET_SIZE(HEADER(ptr)) - newsize;
	
	GET_CLASS(old_class, GET_SIZE(HEADER(ptr)));
	
	/* removing the block from the free list, if it was marked as free */
	if (!BIT(HEADER(ptr)))
		remove_from_free_list(old_class, ptr);
	
	/* setting header and footer of the right block */
	WRITE_DATA(HEADER(ptr), PACK(newsize, 1));
	WRITE_DATA(FOOTER(ptr), PACK(newsize, 1));
		
	/* computing the address of the left block... */
	newblock = (char*)(ptr) + GET_SIZE(HEADER(ptr));
		
	/* ...and setting its header and footer */
	WRITE_DATA(HEADER(newblock), PACK(remaining_size, 0));
	WRITE_DATA(FOOTER(newblock), PACK(remaining_size, 0));
	
	GET_CLASS(new_class, remaining_size);
	
	/* inserting the right block into the corresponding free list */
	put_on_front_of_class_list(new_class, newblock);
	
	if (end_heap == ptr)
		end_heap = newblock;

	return;
}

/*
 * coalesce: merges a block with all the adjacent blocks which are marked as free and 
 * belong to sufficiently large classes by performing the search in both directions.
 * It also removes these merged blocks from their respective free lists and sets
 * header and footer of the new giant block obtained
 */
void *coalesce(void *ptr) {
	size_t total_size = GET_SIZE(HEADER(ptr));
	void *iterator = ptr;
	int class;
	
	/* scanning free blocks to the right (until the end of the heap or the first in-use block) */
	while (iterator != end_heap && !BIT(HEADER(RIGHT(iterator))) && GET_SIZE(HEADER(RIGHT(iterator))) > GET_MAX_DIM(LIMIT_COALESCE)) {
		total_size += GET_SIZE(HEADER(RIGHT(iterator)));
		GET_CLASS(class, GET_SIZE(HEADER(RIGHT(iterator))));
		remove_from_free_list(class, RIGHT(iterator));
		iterator = RIGHT(iterator);
	}
	
	/* scanning free blocks to the left (until the beginning of the heap or the first in-use block) */
	while (ptr != first_block && !BIT(HEADER(LEFT(ptr))) && GET_SIZE(HEADER(LEFT(ptr))) > GET_MAX_DIM(LIMIT_COALESCE)) {
		total_size += GET_SIZE(HEADER(LEFT(ptr)));
		GET_CLASS(class, GET_SIZE(HEADER(LEFT(ptr))));
		remove_from_free_list(class, LEFT(ptr));
		ptr = LEFT(ptr);
	}
	
	/* setting header and footer of the new giant block created */
	WRITE_DATA(HEADER(ptr), PACK(total_size, 0));
	WRITE_DATA(FOOTER(ptr), PACK(total_size, 0));
		
	if (end_heap == iterator)
		end_heap = ptr;
	
	return ptr;
}

/*
 * search_free_list: it scans the free list of a given class looking for a block
 * with a given minimum size using either a best-fit or a first-fit policy.
 * If it founds a suitable block it returns its address, or returns NULL otherwise
 */
void *search_free_list(int class, size_t size_req) {
	if (class < 0 || class >= CLASSES)
		return NULL;
	
	void *rover = READ_ADDR(LOOKUP_TABLE(class));
	
	#ifdef BEST_FIT
		void *best = NULL;
	#endif
	
	if (rover == NULL)
		return NULL;
	
	#ifndef BEST_FIT
	/* First-fit case: we return the first sufficiently big block (if it exists), otherwise we break the loop */
	while(1) {
		if (size_req <= GET_SIZE(HEADER(rover)))
			return rover;
		
		if (READ_ADDR(NEXT(rover)) == NULL)
			break;
		else rover = READ_ADDR(NEXT(rover));
	}
	
	#else
	/* Best-fit case: we return the smallest block which is sufficiently big*/
	while(1) {
		if (size_req <= GET_SIZE(HEADER(rover)) && (best == NULL || GET_SIZE(HEADER(rover)) < GET_SIZE(HEADER(best)))) {
			best = rover;
			if (GET_SIZE(HEADER(best)) == size_req)
				break;
		}
		if (READ_ADDR(NEXT(rover)) == NULL)
			break;
		else rover = READ_ADDR(NEXT(rover));
	}
	
	if (best != NULL)
		return best;
	else return NULL;
	#endif
	
	return NULL;
}

/*
 * remove_from_free_list: removes a block from the free list of a given class by
 * linking its predecessor with its successor (if they exist, otherwise setting to
 * NULL the corresponding fields)
 */
void remove_from_free_list(int class, void *ptr) {
	void *class_addr = LOOKUP_TABLE(class);
	
	if (READ_ADDR(class_addr) == NULL || ptr == NULL)
		return;

	if (READ_ADDR(PREV(ptr)) == NULL && READ_ADDR(NEXT(ptr)) == NULL) {
			WRITE_ADDR(class_addr, NULL);
			return;
	}
	
	/* linking its predecessor in the free list (if it exists) to its successor */
	if (READ_ADDR(PREV(ptr)) != NULL)
		WRITE_ADDR(NEXT(READ_ADDR(PREV(ptr))), READ_ADDR(NEXT(ptr)));
	else WRITE_ADDR(class_addr, READ_ADDR(NEXT(ptr)));
	
	/* linking its successor in the free list (if it exists) to its predecessor */
	if (READ_ADDR(NEXT(ptr)) != NULL)
		WRITE_ADDR(PREV(READ_ADDR(NEXT(ptr))), READ_ADDR(PREV(ptr)));

	return;
}

/*
 * put_on_front_of_class_list: inserts a block on top of the free list
 * of a given class (employing a LIFO scheme)
 */
void put_on_front_of_class_list(int class, void *ptr) {
	if (class <0 || class >= CLASSES)
		return;

	void *class_addr = LOOKUP_TABLE(class);

	/* if the list is empty, the block is alone in the list and therefore doesn't have neither predecessor nor successor */
	if (READ_ADDR(class_addr) == NULL) {
		WRITE_ADDR(PREV(ptr), NULL);
		WRITE_ADDR(NEXT(ptr), NULL);
	}
	
	/* otherwise, we manipulate the fields so to insert the block before the first one in the list */
	else {
		WRITE_ADDR(PREV(READ_ADDR(class_addr)), ptr);
		WRITE_ADDR(NEXT(ptr), READ_ADDR(class_addr));
		WRITE_ADDR(PREV(ptr), NULL);
	}
	
	/* in any case, the first block is now the newly inserted one */
	WRITE_ADDR(class_addr, ptr);

	return;
}

/*
 * simulate_right_coalesce: auxiliary function of mm_realloc that, given a block, checks if by scanning 
 * the heap to the right there are sufficiently big free blocks to expand the starting block without 
 * having to reallocate another one with mm_malloc. It returns 1 if the check is positive
 * and 0 otherwise
 */
int simulate_right_coalesce (void *ptr, size_t diff) {
	size_t total_size = 0;
	
	while (ptr != end_heap && !BIT(HEADER(RIGHT(ptr)))) {
		total_size += GET_SIZE(HEADER(RIGHT(ptr)));
		ptr = RIGHT(ptr);
		if (total_size >= diff)
			return 1;
	}
	return 0;
}

/*
 * mm_check: executes two basic sanity checks, checking that all blocks 
 * inserted in free lists are really free blocks and that there are no adjacent blocks
 * which should have been merged and were not
 */
void mm_check() {
	void *ptr, *fl;
	int i;
	
	/* making sure that coalesce didn't fail in some cases */
	for (ptr = first_block; ptr != end_heap; ptr = RIGHT(ptr)) {
		if (!BIT(HEADER(ptr)) && GET_SIZE(HEADER(ptr)) > GET_MAX_DIM(LIMIT_COALESCE)
				&& !BIT(HEADER(RIGHT(ptr))) && GET_SIZE(HEADER(RIGHT(ptr))) > GET_MAX_DIM(LIMIT_COALESCE))
			printf("\n*ERROR: the two adjacent blocks %p e %p escaped the coalescing process", ptr, RIGHT(ptr));	
	}
	
	/* making sure that all blocks inserted in free lists are really free blocks */
	for (i = 0; i < CLASSES; i++) {
		fl = READ_ADDR(LOOKUP_TABLE(i));
		if (fl == NULL) {
			break;
		}
		else {
			while (READ_ADDR(NEXT(fl)) != NULL) {
				if (BIT(HEADER(READ_ADDR(NEXT(fl)))) != 0)
					printf("\n*ERROR: block %p is in a free list but it's not marked as free", READ_ADDR(NEXT(fl)));
				fl = READ_ADDR(NEXT(fl));
			}
		}
	}

	return;
}
