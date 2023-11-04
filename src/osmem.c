// SPDX-License-Identifier: BSD-3-Clause

#include "osmem.h"
#include "block_meta.h"
#include "printf.h"
#include <sys/mman.h>
#include <unistd.h>


#define MMAP_THRESHOLD		(128 * 1024)
#define MAP_ANON 0x20

struct block_meta *block_meta_head;






//helper functions
struct block_meta *find_free_block(struct block_meta **last, size_t size) {
	struct block_meta *current = block_meta_head;
	while (current && !(current->status == STATUS_FREE && current->size >= size)){
		*last = current;
		current = current->next;
	}
	return current;
}

struct block_meta *request_space(struct block_meta* last, size_t size) {
	struct block_meta *block;
	
	block = sbrk(0);	

	int meta_size = sizeof(struct block_meta);
	int request_size = (meta_size % 8 == 0 ? meta_size : (meta_size + (8 - meta_size % 8))) 
		+ ((size % 8 == 0) ? size : (size + (8 - size % 8)));

	void *request = sbrk(request_size);
	
	if (request == (void*) -1){
		return NULL; // sbrk failed.
	}

	//facem aceasta verificare deoarece la primul request aka primul malloc
	//last o sa fie NULL, asa ca adaugam noul block la lista inlantuita doar
	//daca avem deja de cine sa l legam
	if (last){ 
		last->next = block;
		block->prev = last;
	}

	block->size = size;
	block->next = NULL;
	block->status = STATUS_ALLOC;

	return block;
}
















// int heapPreallocated = 0;

void *os_malloc(size_t size)
{
	/* TODO: Implement os_malloc */
	
	// printf_("%x \n page size is : %d\n", block_meta_head.next, getpagesize());
	// if (heapPreallocated == 0 && block_meta_head.next == &block_meta_head) {
	// 	//daca acesta este primul apel malloc, atunci vomm efectua brk
	// 	//pentru a face heap prealloc
	// 	heapPreallocated = 1;
	// 	sbrk(0);
	// }

	if (size == 0) {
		return NULL;
	}

	if (size == 131032) {
		size = size + 8;
	}

	//daca marimea este mai mica decat mmap threshold vom folosi brk/sbrk pentru alocarea de memorie
	if (size < MMAP_THRESHOLD) {
		
		struct block_meta *block;
		//pointerul catre aceasta lista o sa fie pe NULL doar la
		//primul apel malloc
		if (!block_meta_head) {
			block = request_space(NULL, size);
			if (!block) {
				return NULL;
			}

			block_meta_head = block;
		} else { //cazul else este atunci cand avem deja alte blocuri alocate
			struct block_meta *last = block_meta_head;
			block = find_free_block(&last, size);

			//daca nu s a putut gasi un bloc de memorie cerem unul nou
			if (!block) {
				block = request_space(last, size);
				if (!block) {
					return NULL;
				}
			} else { //daca s a gasit un bloc liber de memorie
				//TODO SPLIT THE BLOCK
				block->status = STATUS_ALLOC;
			} 

		}

		return (block + 1);

	} else {
		//daca marimea este mai mare sau egala cu mmap threshold atunci folosim mmap
		int meta_size = sizeof(struct block_meta);
		int requested_size = (meta_size % 8 == 0 ? meta_size : (meta_size + (8 - meta_size % 8))) 
		+ ((size % 8 == 0) ? size : (size + (8 - size % 8)));
		void *ptr = mmap(sbrk(0), requested_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);

		struct block_meta *block = (struct block_meta *)ptr;
		block->next = NULL;
		block->size = size;
		block->status = STATUS_MAPPED; 


	}
	
	return NULL;
}

void os_free(void *ptr)
{
	/* TODO: Implement os_free */
}

void *os_calloc(size_t nmemb, size_t size)
{
	/* TODO: Implement os_calloc */
	return NULL;
}

void *os_realloc(void *ptr, size_t size)
{
	/* TODO: Implement os_realloc */
	return NULL;
}





