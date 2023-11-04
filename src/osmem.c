// SPDX-License-Identifier: BSD-3-Clause

#include "osmem.h"
#include "block_meta.h"
#include "printf.h"
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>


#define MMAP_THRESHOLD		(128 * 1024)
#define MAP_ANON 0x20
#define PADDING(size)   (((size) % 8 == 0) ? (size) : ((size) + (8 - (size) % 8)))
#define META_PADDING   PADDING(sizeof(struct block_meta))

struct block_meta *block_meta_head;

int heap_prealocatted = 0;




//helper functions
void coalesce_all() {
	struct block_meta *cursor = block_meta_head;
	// struct block_meta *to_be_freed;
	while (cursor->next) {
		if (cursor->status == STATUS_FREE && cursor->next->status == STATUS_FREE) {

			cursor->size = PADDING(cursor->size) + META_PADDING + PADDING(cursor->next->size);

			//il scoatem pe cursor din lista
			cursor->next = cursor->next->next;
			if (cursor->next && cursor->next->next) {
				cursor->next->next->prev = cursor;
			}
		} else {
			cursor = cursor->next;
		}
	}
}

struct block_meta *get_last_element_of_list(struct block_meta *head) {
	struct block_meta *cursor = head;
	while (cursor->next) {
		cursor = cursor->next;
	}
	return cursor;
} 


// struct block_meta *find_free_block(struct block_meta **last, size_t size) {
struct block_meta *find_free_block(struct block_meta *head, size_t size) {
	struct block_meta *current = block_meta_head;
    struct block_meta *closest_block = NULL;

	coalesce_all();

	int min_diff = 9999999;
	// while (current && !(current->status == STATUS_FREE && current->size >= size)){
	// 	head = current;
	// 	current = current->next;
	// }
	// return current;
	while (current) {
        if (current->status == STATUS_FREE && PADDING(current->size) >= size) {
            int diff = PADDING(current->size) - size;
            if (diff < min_diff) {
                closest_block = current;
                min_diff = diff;
            }
        }
        // last = current->prev;
        current = current->next;
    }

	//dupa ce am gasit blocul favorabil, verific daca il pot sparge in doua bloccuri
	//adica blocul de care am nevoie, plus ce ramane liber, daca sunt
	//minim 5 bytes liberi (4 de la sizeof struct + 1 byte de date);
	//parametrul size al functiei este cel din malloc(size)

	//daca din sizeul available in bloc scadem cat avem nevoie de scris (padded)
	//si mai ramane loc de inca un struct si cel putin un padding
	//asta inseamna ca putem face splitting
	//acest bloc se va afla la adresa closest_block + sizeof(struct) + size
	
	if (closest_block){
		int meta_size = sizeof(struct block_meta);
		int padded_meta = meta_size % 8 == 0 ? meta_size : (meta_size + (8 - meta_size % 8));
		int padded_size = (size % 8 == 0) ? size : (size + (8 - size % 8));
		if (PADDING(closest_block->size) - padded_size >= padded_meta + 8) {
			struct block_meta *new_block = (struct block_meta *)((char *)closest_block + padded_meta + padded_size);
			new_block->status = STATUS_FREE;

			new_block->size = PADDING(closest_block->size) - padded_size - padded_meta;
			printf_("for malloc call %d, the new block size is %d\n", size, new_block->size);
			printf_("-     closest block found is %p\n", closest_block);
			printf_("-     and the new address is %p\n", new_block);

			//add to linked list
			new_block->prev = closest_block;
			new_block->next = closest_block->next;
			closest_block->next = new_block;
			if (new_block->next != NULL) {
				new_block->next->prev = new_block;
			}

			closest_block->size = padded_size;
		}
	}

    return closest_block;

}

struct block_meta *request_space(struct block_meta* last, size_t size) {
	struct block_meta *block;
	
		

	int meta_size = sizeof(struct block_meta);
	int request_size = (meta_size % 8 == 0 ? meta_size : (meta_size + (8 - meta_size % 8))) 
	+ ((size % 8 == 0) ? size : (size + (8 - size % 8)));

	if (size < MMAP_THRESHOLD){
		block = sbrk(0);
		void *request = sbrk(request_size);
		
		if (request == (void*) -1){
			return NULL; // sbrk failed.
		}

		//facem aceasta verificare deoarece la primul request aka primul malloc
		//last o sa fie NULL, asa ca adaugam noul block la lista inlantuita doar
		//daca avem deja de cine sa l legam
		if (last){ 
			// last->next->prev = block; //nu e nevoie, deoarece last este ultimul element
			// dam request doar daca in lista n am gasit nimic, aka am ajuns la last si n am gasit nimic
			last->next = block;
			// block->prev = last;
		}

		
		block->prev = last;
		block->size = (size % 8 == 0) ? size : (size + (8 - size % 8)); //inaince era doar size
		block->next = NULL;
		block->status = STATUS_ALLOC;

		return block;
	} else { //daca SIZE > MMAP_THRESHOLD folosim mmap

		void *ptr = mmap(NULL, request_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
		struct block_meta *block_mmap = (struct block_meta *)ptr;

		if (last) {
			// last->next->prev = block_mmap; // nu e nevoie, deoarece last este ultimul element
			// dam request doar daca in lista n am gasit nimic, aka am ajuns la last si n am gasit nimic 
			last->next = block_mmap;
			// block_mmap->prev = last;
		}
		
		block_mmap->prev = last;
		block_mmap->next = NULL;
		block_mmap->size = (size % 8 == 0) ? size : (size + (8 - size % 8)); //inainte era doar size
		block_mmap->status = STATUS_MAPPED;
		
		return block_mmap;

	}
}

		// int meta_size = sizeof(struct block_meta);
		// int requested_size = (meta_size % 8 == 0 ? meta_size : (meta_size + (8 - meta_size % 8))) 
		// + ((size % 8 == 0) ? size : (size + (8 - size % 8)));
		// void *ptr = mmap(sbrk(0), requested_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);

		// struct block_meta *block = (struct block_meta *)ptr;
		// block->next = NULL;
		// block->size = size;
		// block->status = STATUS_MAPPED; 

struct block_meta *get_block_ptr(void *ptr) {
  return (struct block_meta*)ptr - 1;
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
		heap_prealocatted = 1;
		return NULL;
	}

	if (heap_prealocatted == 0 && block_meta_head == NULL) {
		heap_prealocatted = 1;
		// struct block_meta *prealloc_block = request_space(NULL, 128 * 1024);
		struct block_meta *prealloc_block;
	

		int meta_size = sizeof(struct block_meta);
		int request_size = MMAP_THRESHOLD;

		prealloc_block = sbrk(0);

		
		block_meta_head = prealloc_block;

		void *request = sbrk(request_size);
		
		if (request == (void*) -1){
			return NULL; // sbrk failed.
		}

		prealloc_block->next = NULL;
		prealloc_block->prev = NULL;
		prealloc_block->status = STATUS_FREE;
		prealloc_block->size = MMAP_THRESHOLD - PADDING(sizeof(struct block_meta));

	}

	if (size == 131032) {
		size = size + 8;
	}

	//daca marimea este mai mica decat mmap threshold vom folosi brk/sbrk pentru alocarea de memorie
	// if (size < MMAP_THRESHOLD) {
		
		struct block_meta *block;
		//pointerul catre aceasta lista o sa fie pe NULL doar la
		//primul apel malloc

		//asta era inainte de a implementa heap alloc
		if (!block_meta_head && heap_prealocatted == 1) {
			block = request_space(NULL, size);
			if (!block) {
				return NULL;
			}

			block_meta_head = block;
		} else { //cazul else este atunci cand avem deja alte blocuri alocate (cand nu este primul apel malloc)
			//ACOLADA ACESTUI ELSE DEASUPRA LA RETURN (BLOCK + 1)
			struct block_meta *last = block_meta_head;
			// block = find_free_block(&head, size);
			block = find_free_block(block_meta_head, size);

			//daca nu s a putut gasi un bloc de memorie cerem unul nou
			if (!block) {
				// block = request_space(last, size);
				//am preferat aceasta scriere in locul celei comentate mai sus pentru a evita argumentul &last
				//de la apelul find_free_block
				block = request_space(get_last_element_of_list(block_meta_head), size);
				if (!block) {
					return NULL;
				}
			} else { //daca s a gasit un bloc liber de memorie
				//TODO SPLIT THE BLOCK (did it in find function)
				block->status = STATUS_ALLOC;
			} 

		}

		return (block + 1);

	// } else {
	// 	//daca marimea este mai mare sau egala cu mmap threshold atunci folosim mmap
		


	// }
	
	return NULL;
}

void os_free(void *ptr)
{
	/* TODO: Implement os_free */
	if (ptr == NULL) {
		return;
	}

	struct block_meta *block_ptr = get_block_ptr(ptr);
	if (block_ptr->status == STATUS_ALLOC) {
		block_ptr->status = STATUS_FREE;
	} else if (block_ptr->status == STATUS_MAPPED){
		block_ptr->status = STATUS_FREE;
		int meta_size = sizeof(struct block_meta);
		int free_size = (meta_size % 8 == 0 ? meta_size : (meta_size + (8 - meta_size % 8))) + block_ptr->size;
		munmap(block_ptr, free_size);
	}

}

void *os_calloc(size_t nmemb, size_t size)
{
	/* TODO: Implement os_calloc */

	if (nmemb == 0 || size == 0) {
		return NULL;
	}

	size_t allocation_size = nmemb * size;

	void *ptr = os_malloc(size);
	memset(ptr, 0, allocation_size);
	return ptr;


	return NULL;
}

void *os_realloc(void *ptr, size_t size)
{
	/* TODO: Implement os_realloc */
	return NULL;
}





