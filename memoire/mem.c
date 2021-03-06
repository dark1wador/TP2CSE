#include "mem.h"
#include <stdio.h>

struct fb {
	size_t size;
	struct fb *next;
};

// Head of free block chained list
struct fb *head;
// Adress of the end of memory
void *end;
// Adress of the beginning of the memory
void *begin;

// Current selected free block searching function
mem_fit_function_t *searchFunction;

// The total size of the memory
size_t sizeOfMem;

void mem_init(char* mem, size_t size) {

	// The total size of the memory
	sizeOfMem = size;
	
	// Create the head of chained list at 0x0
	struct fb *firstFb = (struct fb *) mem;
	firstFb->size = size;
	firstFb->next = NULL;
	head = firstFb;

	end = (void *)firstFb + firstFb->size;
	begin = (void *)mem;

	// Reinitialization of search function to fit_first
	mem_fit(mem_fit_first);
}

void* mem_alloc(size_t size) {

	// Like standard malloc, return NULL pointer on size 0
	if(size == 0) {
		return NULL;
	}

	// The rounded modified size asked
	size_t sizeAsked = size;
	// Round the size asked to a multiple of struct fb
	if( size % sizeof(struct fb) != 0) {
		sizeAsked = size + (sizeof(struct fb) - size % sizeof(struct fb));
	}

	// The total size that won't be available any more
	size_t totalAllocated = sizeAsked + sizeof(size_t);
	// free block found
	struct fb *freeB = NULL;
	// We search for a free block with enough space to insert the allocated size
	// Also the search function will ensure that the free block will be either just small enough to be 
	// totally allocated or big enough to still have a correct struct fb
	freeB = searchFunction(freeB, totalAllocated);

	// If we didn't find a free block with rounded size, then search with exact size
	if(freeB == NULL) {
		sizeAsked = size;
		totalAllocated = sizeAsked + sizeof(size_t);
		freeB = searchFunction(freeB, totalAllocated);
	}
	// If we found nothing here, then there isn't enough available memory
	if(freeB == NULL) {
		fprintf(stderr, "No free block available in memory\n");
		return NULL;
	}

	// Rechain free block list if needed
	if (totalAllocated == freeB->size) {
		// Searching for previous free block pointer
		struct fb *prev = head;
		// If head is not the allocated one
		if(prev != freeB) {
			while (prev->next != freeB) {
				if(prev->next == NULL) {
					fprintf(stderr, "Internal error in mem_alloc\n");
					return NULL;
				}

				prev = prev->next;
			}
		}

		// Reachain free block list
		prev->next = freeB->next;
	}


	// The beginning of the allocated space for the user
	void *userPointer = (void*)freeB + freeB->size - sizeAsked;

	size_t freeBSize = freeB->size;


	size_t *allocatedBlockSizePointer = (void *) userPointer - sizeof(size_t);
	/*printf("fb : %p, fbSize : %zu, up : %p, sizeof(size_t) : %zu, sizeof(struct) : %zu, sizeAsked : %zu\n", (size_t *)freeB, freeBSize, userPointer, sizeof(size_t), sizeof(struct fb), sizeAsked);
	printf("user pointer - sizeof(size_t) = %p\n", 	allocatedBlockSizePointer);
	printf("user pointer - 1 = %p\n", 	((size_t*) userPointer - 1));
	printf("whats %zu\n", *allocatedBlockSizePointer);*/

	//printf("user pointer : %p; size pointer : %p, size writen %zu\n", userPointer, allocatedBlockSizePointer, sizeAsked);
	// Write size of the allocated block before the allocated block wich is at the end of the free block
	*allocatedBlockSizePointer = sizeAsked;


	// Update the available size in this free block only if freeB will stay (if the block wasn't totally allocated)
	// So we don't override the size of the allocated block
	if(freeBSize >= totalAllocated + sizeof(struct fb)) {
		freeB->size -= totalAllocated;
	}

	return userPointer;
}

// Get the size of an allocated block by reading it with a (little) validity check
size_t mem_get_size(void * p) {
	// Get the size of the allocated block
	size_t blockSize = *((size_t *)((void*)p - sizeof(size_t)));

	return blockSize;
}

void mem_free(void* p) {

	// Get the size of the allocated block
	size_t blockSize = mem_get_size(p);

	// The total size that need to be freed
	size_t totalAllocatedSize = blockSize + sizeof(size_t);

	// Find the previous free block
	struct fb *prev = head;
	if (head != NULL) {
		while (prev->next != NULL && (void*)prev->next < (void*)p) {

			prev = prev->next;
		}

		// If trying to free inside a free block
		if(prev != NULL && (void *)prev + prev->size > (void *)p) {
			fprintf(stderr, "Error : trying to free a free block\n");
			return;
		}
	}

	// Get the following one
	struct fb *nextFb = NULL;
	if(prev != NULL) {
		nextFb = prev->next;
	}
	else {
		if (head != NULL) {

			nextFb = head;
			while (nextFb != NULL && (void*)nextFb > (void*)p) {

				nextFb = nextFb->next;
			}
		}
	}


	//printf("prev : %p, p : %p, size : %zu\n", prev, p, blockSize);
	// Boolean to flag if we need to fusion with previouse and/or next free block
	int prevFusion = 0;
	int nextFusion = 0;

	if(prev != NULL) {
		prevFusion = (void*)prev + prev->size == (void *)p - sizeof(size_t);
	}
	// Test if next fb is right next to it (no allocated space between)
	if(nextFb != NULL) {
		nextFusion = (void*)nextFb == (void *)p + blockSize;
	}

	// For each cases
	if(prevFusion && nextFusion) {
		prev->size += totalAllocatedSize + nextFb->size;
		prev->next = nextFb->next;
		//printf("patate 1\n");
	}
	else if(prevFusion) {
		prev->size += totalAllocatedSize;
		//printf("patate 2\n");
	}
	else if(nextFusion) {
		size_t nextFbSize = nextFb->size + totalAllocatedSize;
		struct fb *nextFbNext = nextFb->next;
		//printf("next %p, next size %zu, newFbSize : %zu\n", nextFb, nextFb->size, nextFbSize);
		nextFb = (struct fb *)((void*)p - sizeof(size_t));
		nextFb->size = nextFbSize;
		nextFb->next = nextFbNext;
		// If there was a previous free block, reassign its pointer
		if(prev != NULL) {
			prev->next = nextFb;
		}
		// If the next free block was the first in the list, then update head
		else {
			head = nextFb;
		}
		//printf("patate 3\n");
	}
	// Create a new fb
	else {
		struct fb *newFb = ((void*)p - sizeof(size_t));
		newFb->size = totalAllocatedSize;

		// If there is no free block then this become head
		if (head == NULL) {
			head = newFb;
			//printf("patate 4.1\n");
		}
		// If this is the new first free block
		else if (newFb < head) {
			newFb->next = head;
			head = newFb;
			//printf("patate 4.2\n");
		}
		// Normal case (previous free block exists)
		else {
			newFb->next = prev->next;
			prev->next = newFb;
			//printf("patate 4.3\n");
		}
	}
}

/* Itérateur sur le contenu de l'allocateur */
void mem_show(void (*print)(void *, size_t, int free)) {

	struct fb *currentFb = head;

	size_t totalSize = 0;

	// Display all allocated blocks which are before head
	if((void *)currentFb != begin) {
		// The pointer to the current allocated block
		void *allocatedPointer = begin;

		// If there is no free block then it's end of memory
		if(currentFb == NULL) {
			currentFb = (struct fb *)end;
		}

		// Iterate over all allocated blocks before first free block
		while( allocatedPointer <= (void*)currentFb) {

			//printf("allocated pointer : %p\n", allocatedPointer);
			//printf("currentFb %p\n", currentFb);

			// Get the size of the allocated block
			size_t blockSize = *(size_t *)((void *)allocatedPointer - sizeof(size_t));

			print(allocatedPointer, blockSize, 0);
			totalSize += blockSize + sizeof(size_t);

			//printf("prev : %p; prevSize : %zu; allocated pointer : %p, size : %zu\n", currentFb, currentFb->size, allocatedPointer, blockSize);
			

			allocatedPointer = (void*) ((void*)allocatedPointer + blockSize + sizeof(size_t));
		}
	}

	// Iterate over free blocks
	while(currentFb != NULL) {
			
		print(currentFb, currentFb->size, 1);
		totalSize += currentFb->size;

		// If we are at the end
		if ((void *)currentFb + currentFb->size >= end) {
			break;
		}

		// The pointer to the current allocated block
		void *allocatedPointer = (void *)((void *)currentFb + currentFb->size + sizeof(size_t));

		// Where is the next free block
		struct fb *nextBlock;
		// If there is one then it's its memory
		if(currentFb->next != NULL) {
			nextBlock = currentFb->next;
		}
		// If there is no next free block then it's the end of memory
		else {
			nextBlock = (void *)end;
		}
		// Iterate over all allocated blocks between 2 free blocks
		while( allocatedPointer <= (void*)nextBlock) {

			//printf("allocated pointer : %p\n", allocatedPointer);
			//printf("nextBlock %p\n", nextBlock);

			// Get the size of the allocated block
			size_t blockSize = *(size_t *)((void *)allocatedPointer - sizeof(size_t));

			print(allocatedPointer, blockSize, 0);
			totalSize += blockSize + sizeof(size_t);

			//printf("prev : %p; prevSize : %zu; allocated pointer : %p, size : %zu\n", currentFb, currentFb->size, allocatedPointer, blockSize);
			

			allocatedPointer = (void*) ((void*)allocatedPointer + blockSize + sizeof(size_t));
		}


		currentFb = currentFb->next;
	}

	printf("total size : %zu\n", totalSize);
}

void mem_fit(mem_fit_function_t* function) {
	searchFunction = function;
}

// We search for the first free block with enough space to insert the allocated size
// Also the search function will ensure that the free block will be either just small enough to be 
// totally allocated or big enough to still have a correct struct fb
struct fb* mem_fit_first(struct fb* fb, size_t size) {
	fb = head;

	while (fb != NULL) {

		//printf("Zone libre, Adresse : %p, Taille : %lu, next = %p\n", fb, (unsigned long) fb->size, fb->next);
		// We ensure that either the block will be totally allocated, or there will be still enough place to 
		// have the struct fb
		if(fb->size >= size && (fb->size - size == 0 || fb->size - size >= sizeof(struct fb)) ) {
			break;
		}
		fb = fb->next;
	}

	return fb;
}

/* Si vous avez le temps */
struct fb* mem_fit_best(struct fb* fb, size_t size) {
	return fb;
}

struct fb* mem_fit_worst(struct fb* fb, size_t size) {
	return fb;
}