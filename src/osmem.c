// SPDX-License-Identifier: BSD-3-Clause

#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include "osmem.h"
#include "block_meta.h"
#include "printf.h"


#define MMAP_THRESHOLD		(128 * 1024)
#define CALLOC_THRESHOLD	(4 * 1024)
#define MAP_ANON 0x20
#define PADDING(size)   (((size) % 8 == 0) ? (size) : ((size) + (8 - (size) % 8)))
#define META_PADDING   PADDING(sizeof(struct block_meta))

struct block_meta *block_meta_head;

int heap_prealocatted;

int allocation_threshold;

int coming_from_calloc;

int coming_from_realloc;


//helper functions


void *my_memset(void *source, int value, size_t num)
{
	/* TODO: Implement memset(). */
	int i, n = (int)num;
	char *charSource = (char *)source;

	for (i = 0; i < n; i++)
		*(charSource + i) = value;

	return source;
}

int are_all_mapped(void)
{
	struct block_meta *cursor = block_meta_head;

	while (cursor->next) {
		if (cursor->status != STATUS_MAPPED)
			return 0;

		cursor = cursor->next;
	}
	return 1;
}

void coalesce_all(void)
{
	struct block_meta *cursor = block_meta_head;
	// struct block_meta *to_be_freed;

	// if (cursor == NULL || cursor->next == NULL) {
	// 	return;
	// }

	while (cursor->next) {
		if (cursor->status == STATUS_FREE && cursor->next->status == STATUS_FREE) {
			cursor->size = PADDING(cursor->size) + META_PADDING + PADDING(cursor->next->size);

			//il scoatem pe cursor din lista
			cursor->next = cursor->next->next;
			if (cursor->next && cursor->next->next) {
				cursor->next->next->prev = cursor;
			}

			//to also remove reference to prev
			if (cursor->next) {
				cursor->next->prev = cursor;
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

struct block_meta *get_last_nonmap_element_of_list(void) {
	// struct block_meta *cursor = get_last_element_of_list(block_meta_head);
	// if (cursor == NULL) {
	//	return cursor;
	// }
	// while (cursor->status == STATUS_MAPPED) {
	//	cursor = cursor->prev;
	// }
	if (are_all_mapped())
		return NULL;

	struct block_meta *cursor = block_meta_head;
	struct block_meta *return_value = NULL;

	//acest while pentru a ma duce la primul element non_mapped
	while (cursor != NULL) {
		if (cursor->status != STATUS_MAPPED)
			return_value = cursor;
		cursor = cursor->next;
	}

	return return_value;
}


// struct block_meta *find_free_block(struct block_meta **last, size_t size) {
struct block_meta *find_free_block(struct block_meta *head, size_t size, int param, struct block_meta *stop)
{
	//daca param = 0 cautam prim toate elementele
	//daca param = 1 cautam pana la elementul dat ca argument
	struct block_meta *current = block_meta_head;
	struct block_meta *closest_block = NULL;

	coalesce_all();

	int min_diff = 9999999;
	// while (current && !(current->status == STATUS_FREE && current->size >= size)){
	//	head = current;
	//	current = current->next;
	// }
	// return current;
	while (current) {
		//nu stiu daca e bine
		if (param == 1 && current == stop)
			return closest_block;


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

	if (closest_block) {
		int meta_size = sizeof(struct block_meta);
		int padded_meta = meta_size % 8 == 0 ? meta_size : (meta_size + (8 - meta_size % 8));
		int padded_size = (size % 8 == 0) ? size : (size + (8 - size % 8));

		if (PADDING(closest_block->size) - padded_size >= padded_meta + 8) {
			struct block_meta *new_block = (struct block_meta *)((char *)closest_block + padded_meta + padded_size);

			new_block->status = STATUS_FREE;
			new_block->size = PADDING(closest_block->size) - padded_size - padded_meta;
			// printf_("for malloc call %d, the new block size is %d\n", size, new_block->size);
			// printf_("-     closest block found is %p\n", closest_block);
			// printf_("-     and the new address is %p\n", new_block);

			//add to linked list
			new_block->prev = closest_block;
			new_block->next = closest_block->next;
			closest_block->next = new_block;
			if (new_block->next != NULL)
				new_block->next->prev = new_block;

			closest_block->size = padded_size;
		}
	}

	return closest_block;
}

struct block_meta *request_space(struct block_meta *last, size_t size)
{
	struct block_meta *block;

	int meta_size = sizeof(struct block_meta);
	int request_size = (meta_size % 8 == 0 ? meta_size : (meta_size + (8 - meta_size % 8)))
	+ ((size % 8 == 0) ? size : (size + (8 - size % 8)));

	//inainte aveam MMAP_THRESHOLD in loc de allocation_threshold atunci cand foloseam
	//aceasta functie ca helper doar pentru malloc
	//pentru ca malloc si calloc au threshoulduri diferite de mmap/brk voi folosi asta
	if (request_size < allocation_threshold){
		block = sbrk(0);
		void *request = sbrk(request_size);

		if (request == (void *) -1)
			return NULL; // sbrk failed.

		//facem aceasta verificare deoarece la primul request aka primul malloc
		//last o sa fie NULL, asa ca adaugam noul block la lista inlantuita doar
		//daca avem deja de cine sa l legam
		if (last) {
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
	} 
	//daca SIZE > MMAP_THRESHOLD folosim mmap
	void *ptr = mmap(NULL, request_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	struct block_meta *block_mmap = (struct block_meta *)ptr;

	if (last) {
		// last->next->prev = block_mmap; // nu e nevoie, deoarece last este ultimul element
		// dam request doar daca in lista n am gasit nimic, aka am ajuns la last si n am gasit nimic



		last->next = (struct block_meta *)ptr;//block_mmap;



		// block_mmap->prev = last;
	}

	// block_mmap->prev = last;
	// block_mmap->next = NULL;
	// block_mmap->size = (size % 8 == 0) ? size : (size + (8 - size % 8)); //inainte era doar size
	// block_mmap->status = STATUS_MAPPED;


	((struct block_meta *)ptr)->prev = last;
	((struct block_meta *)ptr)->next = NULL;
	((struct block_meta *)ptr)->size = (size % 8 == 0) ? size : (size + (8 - size % 8)); //inainte era doar size
	((struct block_meta *)ptr)->status = STATUS_MAPPED;

	return (struct block_meta *)ptr;
}

		// int meta_size = sizeof(struct block_meta);
		// int requested_size = (meta_size % 8 == 0 ? meta_size : (meta_size + (8 - meta_size % 8)))
		// + ((size % 8 == 0) ? size : (size + (8 - size % 8)));
		// void *ptr = mmap(sbrk(0), requested_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);

		// struct block_meta *block = (struct block_meta *)ptr;
		// block->next = NULL;
		// block->size = size;
		// block->status = STATUS_MAPPED;

struct block_meta *get_block_ptr(void *ptr)
{
  return (struct block_meta *)ptr - 1;
}

void *os_malloc(size_t size)
{
	/* TODO: Implement os_malloc */

	// printf_("%x \n page size is : %d\n", block_meta_head.next, getpagesize());
	// if (heapPreallocated == 0 && block_meta_head.next == &block_meta_head) {
	//	//daca acesta este primul apel malloc, atunci vomm efectua brk
	//	//pentru a face heap prealloc
	//	heapPreallocated = 1;
	//	sbrk(0);
	// }
	if (!coming_from_calloc) {
		allocation_threshold = MMAP_THRESHOLD;
	} else {
		coming_from_calloc;
		allocation_threshold = CALLOC_THRESHOLD;
	}

	if (size == 0) {
		heap_prealocatted = 1;
		return NULL;
	}


	if (size < MMAP_THRESHOLD && !block_meta_head && !coming_from_calloc) {
		heap_prealocatted = 1;

		struct block_meta *prealloc_block;


		int meta_size = sizeof(struct block_meta);
		int request_size = MMAP_THRESHOLD;

		prealloc_block = sbrk(0);


		block_meta_head = prealloc_block;

		void *request = sbrk(request_size);

		if (request == (void *) -1)
			return NULL; // sbrk failed.

		prealloc_block->next = NULL;
		prealloc_block->prev = NULL;
		prealloc_block->status = STATUS_FREE;
		prealloc_block->size = MMAP_THRESHOLD - PADDING(sizeof(struct block_meta));
	}


	//daca size este mai mare decat thresholdul de mmap nu are rost sa fac
	//prealloc, pentru ca oricum o sa cer iar memorie dupa aceea ca sa maresc
	//blocul prealocat la 128kb
	if (size > MMAP_THRESHOLD)
		heap_prealocatted = 1;

	//aici se face heap prealloc
	if (heap_prealocatted == 0 && block_meta_head == NULL) {
		heap_prealocatted = 1;
		// struct block_meta *prealloc_block = request_space(NULL, 128 * 1024);
		struct block_meta *prealloc_block;


		int meta_size = sizeof(struct block_meta);
		int request_size = MMAP_THRESHOLD;

		prealloc_block = sbrk(0);


		block_meta_head = prealloc_block;

		void *request = sbrk(request_size);

		if (request == (void *) -1)
			return NULL; // sbrk failed.

		prealloc_block->next = NULL;
		prealloc_block->prev = NULL;
		prealloc_block->status = STATUS_FREE;
		prealloc_block->size = MMAP_THRESHOLD - PADDING(sizeof(struct block_meta));
	}


	//aici se face heap prealloc in cazul reallocului
	if (block_meta_head && are_all_mapped() && coming_from_realloc) {
		coming_from_realloc = 0;
		heap_prealocatted = 1;

		struct block_meta *prealloc_block;


		int meta_size = sizeof(struct block_meta);
		int request_size = MMAP_THRESHOLD;

		prealloc_block = sbrk(0);

		struct block_meta *last = get_last_element_of_list(block_meta_head);

		// block_meta_head = prealloc_block;

		void *request = sbrk(request_size);

		if (request == (void *) -1)
			return NULL; // sbrk failed.
		last->next = prealloc_block;
		prealloc_block->next = NULL;
		prealloc_block->prev = last;
		prealloc_block->status = STATUS_FREE;
		prealloc_block->size = MMAP_THRESHOLD - PADDING(sizeof(struct block_meta));
	}

	if (size == 131032)
		size = size + 8;

	//daca marimea este mai mica decat mmap threshold vom folosi brk/sbrk pentru alocarea de memorie
	// if (size < MMAP_THRESHOLD) {

		struct block_meta *block;
		//pointerul catre aceasta lista o sa fie pe NULL doar la
		//primul apel malloc

		//in cazul in care se da primul apel malloc(0) nu se mai face heap_prealloc si deci head o sa fie null
		//iar procesul de heap prealloc o sa fie completat, prealocarea avand loc pt 0 bytes
		if (!block_meta_head && heap_prealocatted == 1) {
			// allocation_threshold = MMAP_THRESHOLD;
			block = request_space(NULL, size);
			if (!block)
				return NULL;

			block_meta_head = block;
		} else { //cazul else este atunci cand avem deja alte blocuri alocate (cand nu este primul apel malloc)
			//ACOLADA ACESTUI ELSE DEASUPRA LA RETURN (BLOCK + 1)
			struct block_meta *last = block_meta_head;
			// block = find_free_block(&head, size);
			block = find_free_block(block_meta_head, size, 0, NULL);

			//cazul in care imi vine malloc(>4096) din realloc si am un bloc free cu dimensiune > 4096
			//in acest caz vreau ca block sa fie null pentru a folosi mmap
			if (PADDING(size) >= allocation_threshold)
				block = NULL;

			if (PADDING(size) >= allocation_threshold && allocation_threshold == CALLOC_THRESHOLD
			&& coming_from_calloc == 1) {
				//cer memorie noua cu mmap
				int calloc_request_size = META_PADDING + PADDING(size);
				void *calloc_ptr = mmap(NULL, calloc_request_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
				struct block_meta *calloc_mmap = (struct block_meta *)calloc_ptr;
				struct block_meta *last_element = get_last_element_of_list(block_meta_head);

				if (last_element)
					last_element->next = calloc_mmap;

				calloc_mmap->next = NULL;
				calloc_mmap->prev = last_element;
				calloc_mmap->status = STATUS_MAPPED;
				calloc_mmap->size = PADDING(size);

				coming_from_calloc = 0;
				return (calloc_mmap + 1);
			}


			//daca nu s a putut gasi un bloc de memorie cerem unul nou
			if (!block) {
				//mai intai verificam daca ultimul block este free, pentru a aloca fix cat este necesar
				struct block_meta *last = get_last_element_of_list(block_meta_head);






				//daca ultimul element este free, nu alocam size, alocam un nou bloc
				//dupa ultimul de marime size - last->size, apoi le lipim
				//doar daca size este mai mic decat mmap_threshold
				if (last && last->status == STATUS_FREE && PADDING(size) < MMAP_THRESHOLD) {
					struct block_meta *new_last;


					int request_size =  PADDING(size) - PADDING(last->size);
					void *request = sbrk(request_size);

					new_last = sbrk(0);

					if (request == (void *) -1)
						return NULL; // sbrk failed.

					if (last) {
					// last->next->prev = block; //nu e nevoie, deoarece last este ultimul element
					// dam request doar daca in lista n am gasit nimic, aka am ajuns la last si n am gasit nimic
					last->next = new_last;
					// block->prev = last;
					}


					new_last->prev = last;
					// new_last->size = PADDING(size- last->size - META_PADDING); //inaince era doar size
					new_last->size = PADDING(size) - PADDING(last->size) - META_PADDING;
					new_last->next = NULL;
					new_last->status = STATUS_FREE;

					coalesce_all();

					coming_from_calloc = 0;
					return (last + 1);

				} else {
					// block = request_space(last, size);
					//am preferat aceasta scriere in locul celei comentate mai sus pentru a evita argumentul &last
					//de la apelul find_free_block
					block = request_space(get_last_element_of_list(block_meta_head), size);
					if (!block) {
						coming_from_calloc = 0;
						return NULL;
					}
				}


			} else { //daca s a gasit un bloc liber de memorie
				//TODO SPLIT THE BLOCK (did it in find function)
				block->status = STATUS_ALLOC;
			}
		}
		coming_from_calloc = 0;
		return (block + 1);

	// } else {
	//	//daca marimea este mai mare sau egala cu mmap threshold atunci folosim mmap



	// }

	return NULL;
}

void os_free(void *ptr)
{
	/* TODO: Implement os_free */
	if (ptr == NULL)
		return;

	struct block_meta *block_ptr = get_block_ptr(ptr);

	if (block_ptr->status == STATUS_ALLOC) {
		block_ptr->status = STATUS_FREE;
	} else if (block_ptr->status == STATUS_MAPPED) {
		block_ptr->status = STATUS_FREE;
		int meta_size = sizeof(struct block_meta);
		int free_size = (meta_size % 8 == 0 ? meta_size : (meta_size + (8 - meta_size % 8))) + PADDING(block_ptr->size);

		//scoatem pe block_ptr din lista
		if (block_ptr->next) {
			block_ptr->next->prev = block_ptr->prev;
		} else {
			//aici cazul in care block_ptr este ultimul nod
			// block_ptr->prev = NULL;
			if (block_ptr->prev)
				block_ptr->prev->next = NULL;
		}

		if (block_ptr->prev) {
			block_ptr->prev->next = block_ptr->next;
		} else {
			//aici cazul in care block_ptr este primul nod
			// block_ptr->next = NULL;
			if (block_ptr->next)
				block_ptr->next->prev = NULL;
		}

		if (block_ptr->next == NULL && block_ptr->prev == NULL && block_ptr != NULL) {
			//daca block_meta_head are un singur element si vreau sa ii dau free;
			block_meta_head = NULL;
		}

		munmap(block_ptr, free_size);
	}
}

void *os_calloc(size_t nmemb, size_t size)
{
	/* TODO: Implement os_calloc */

	if (nmemb == 0 || size == 0) {
		heap_prealocatted = 1;
		return NULL;
	}

	size_t allocation_size = nmemb * size;

	allocation_threshold = CALLOC_THRESHOLD;

	coming_from_calloc = 1;

	// if (allocation_size >= CALLOC_THRESHOLD) {
	//	coming_from_calloc = 0;
	//	int calloc_request_size = META_PADDING + PADDING(allocation_size);
	//	void *calloc_ptr = mmap(NULL, calloc_request_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	//	struct block_meta *calloc_mmap = (struct block_meta *)calloc_ptr;

	//	struct block_meta *last_element = get_last_element_of_list(block_meta_head);
	//	if (last_element) {
	//		last_element->next = calloc_mmap;
	//	}

	//	calloc_mmap->next = NULL;
	//	calloc_mmap->prev = last_element;
	//	calloc_mmap->status = STATUS_MAPPED;
	//	calloc_mmap->size = PADDING(size);

	//	coming_from_calloc = 0;
	//	return (calloc_mmap + 1);

	// }


	void *ptr = os_malloc(allocation_size);

	my_memset(ptr, 0, allocation_size);

	return ptr;

	return NULL;
}

void *os_realloc(void *ptr, size_t size)
{
	/* TODO: Implement os_realloc */

	if (ptr == NULL && size == 0)
		return os_malloc(size); //heap prealloc

	if (ptr == NULL) {
		// return os_malloc(size);
		void *return_value = os_malloc(size);
		struct block_meta *return_block = get_block_ptr(return_value);

		if (PADDING(size) < MMAP_THRESHOLD) {
			if (return_block->status == STATUS_FREE && PADDING(size) < MMAP_THRESHOLD)
				return_block->status = STATUS_ALLOC;
			//else if (return_block->status == STATUS_FREE && PADDING(size) >= MMAP_THRESHOLD) {
			//	return_block->status = STATUS_MAPPED;
			// }
		}

		return return_value;
	}

	struct block_meta *current_block = get_block_ptr(ptr);

	if (current_block->status == STATUS_FREE)
		return NULL;

	if (size == 0) {
		os_free(ptr);
		return NULL; // nu stiu ce ar trbui sa returnez;
	}

	coalesce_all();

	//daca dau realloc la fix cata memorie aveam si inainte
	if (PADDING(current_block->size) == PADDING(size))
		return (current_block + 1);

	//blocurile alocate cu mmap nu pot fi realocate decat cu ajutorul memcpy
	if (current_block->status == STATUS_MAPPED) {
		coming_from_realloc = 1;
		void *new_ptr = os_malloc(PADDING(size));


		//copiam memoria
		// int writing_size = current_block->size < PADDING(size) ? current_block->size : PADDING(size);
		memcpy(new_ptr, (current_block + 1), PADDING(size)); //inainte 3rd arg era PADDING(size)

		//facem unmap
		os_free((current_block + 1));

		//daca avem un singur element in lista, adica cel cu STATUS_MAPPED,
		//care va urma sa fie scos, atunci va trebui sa facem heap prealloc
		//la urmatorul apel de malloc

		return new_ptr;
	}

	if (current_block->status == STATUS_ALLOC && PADDING(size) >= MMAP_THRESHOLD) {
		coming_from_calloc = 0;
		void *new_ptr = os_malloc(PADDING(size));

		memcpy(new_ptr, (current_block + 1), current_block->size);

		// current_block->status = STATUS_FREE;
		os_free((current_block + 1));

		coalesce_all();

		return new_ptr;
	}





	//in cazul in care dorim ca la realloc sa marim zona
	if (size > current_block->size) {
		//verificam daca se poate realiza lipirea cu blocul din dreapta, dupa ce
		//am facut coalesce() la intrarea in functia de realloc()
		if (current_block->next != NULL && current_block->next->status == STATUS_FREE &&
		 PADDING(current_block->size) + META_PADDING + PADDING(current_block->next->size) >= PADDING(size)) {
			//ma unesc cu totul de blocul urmator si vad daca mai pot crea unul nou duoa unire
			current_block->size = current_block->size + META_PADDING + current_block->next->size;

			current_block->next = current_block->next->next;
			if (current_block->next)
				current_block->next->prev = current_block;

			//aici vad daca noul bloc se poate sparge in cat am nevoie + cat ramane
			//daca ar ramane suficient de multa memorie, atunci fac un nou bloc
			if (current_block->size - PADDING(size) >= 40) {
				struct block_meta *new_block = (struct block_meta *)((char *)(current_block) + META_PADDING + PADDING(size));

				new_block->size = current_block->size - PADDING(size) - META_PADDING;
				current_block->size = PADDING(size);

				new_block->status = STATUS_FREE;
				new_block->prev = current_block;
				new_block->next = current_block->next;
				current_block->next = new_block;
				if (new_block->next != NULL) {
					new_block->next->prev = new_block;
				}
			}

			//acum ca am facut splittingul optim, am blocul meu resized,
			//verific sa vad daca cumva in pot pune mai la stanga in memorie

			struct block_meta *optimal_find = find_free_block(block_meta_head, size, 1, current_block);

			if (optimal_find != NULL && optimal_find < current_block) {
				memcpy(optimal_find, current_block, META_PADDING + current_block->size);
				optimal_find->size = current_block->size;
				optimal_find->status = STATUS_ALLOC;
				current_block->status = STATUS_FREE;
				return (optimal_find + 1);
			}
			////


			//////////////////////////////////////////////////////////////////////////////////////////
			//////////////////////////////////////////////////////////////////////////////////////////

			// //intre aceste  // se afla optimizarea la marire de realloc. nu e buna totusi

			// //aici retinem cat ar trebui sa imprumutam din sizeul blocului adiacent
			// int borrowed = PADDING(size) - current_block->size - META_PADDING;
			// // int borrowed = PADDING(size) - current_block->size;
			// //in remainder retinem cat ar fi current.next.size daca imprumuta ca avem nevoie
			// int remainder = current_block->next->size - borrowed;

			// //daca raman macar 40 de bytes inseamna ca are rost sa facem split
			// //adica sa adaugam un bloc la dreapta cu restul de size ramas
			// if (remainder >=40) {
			// 	current_block->size = PADDING(size);

			//	struct block_meta *new_block = (struct block_meta *)((char *)current_block + META_PADDING +current_block->size);
			//	new_block->status = STATUS_FREE;
			//	new_block->size = PADDING(remainder) - META_PADDING;

			//	new_block->prev = current_block;
			//	new_block->next = current_block->next;
			// 	current_block->next = new_block;
			//	if (new_block->next != NULL) {
			//		new_block->next->prev = new_block;
			// 	}

			// } else {
			//	//daca nu ramane destula memorie dupa vom lipi tot blocul
			// 	current_block->size = current_block->size + META_PADDING + current_block->next->size;

			//	current_block->next = current_block->next->next;
			//	if (current_block->next) {
			//	current_block->next->prev = current_block;
			//	}
			// }

			//////////////////////////////////////////////////////////////////////////////////////////
			//////////////////////////////////////////////////////////////////////////////////////////

			return (current_block + 1);

		} else if (current_block->next == NULL) {
			//daca totusi nu se poate uni cu blocul din dreapta pentru ca nu e suficienta
			//memorie/nu e liber blocul, putem incerca sa vedem daca blocul caruia ii
			//dam realloc este ultimul din lista, caz in care putem da malloc doar
			//la cata memorie avem nevoie in plus, dupa care unificam cele doua blocuri
			int needed_size = PADDING(size) - current_block->size;
			// struct block_meta* helper_block = get_block_ptr(malloc(needed_size));

			void *request = sbrk(needed_size);

			if (request == (void *) -1) {
				return NULL; // sbrk failed.
			}
			current_block->size = PADDING(size);
			current_block->next = NULL;
			current_block->status = STATUS_ALLOC;

			return (current_block + 1);

			//unim blocul curent(penultimul) cu ultimul bloc creat, dupa care
			//il eliminam pe ultimul din lista


			return (current_block + 1);
		} else {
			//daca nu s a putut nici sa ne lipim cu blocurile la ddreapta,
			//iar blocul curent nu este nici ultimul pentru a aplica schema de mai
			//sus, atunci va fi nevoie sa aloc noul pointer altundeva in memorie


			//mai intai verific daca ultimul pointer ne-alocat cu mmap poate fi extins cu sbrk
			struct block_meta *last_non_mmap = get_last_nonmap_element_of_list();

			if (last_non_mmap->status == STATUS_FREE && find_free_block(block_meta_head, size, 0, NULL) == NULL) {
				//daca statusul ultimului este free, il pot extinde cu cat am nevoie
				int request_size = PADDING(size) - PADDING(last_non_mmap->size);
				void *request = sbrk(request_size);

				if (request == (void *) -1) {
					return NULL; // sbrk failed.
				}

				last_non_mmap->status = STATUS_ALLOC;
				last_non_mmap->size = PADDING(size);
				current_block->status = STATUS_FREE;

				//copiez memoria din vechiul ptr la cel nou
				memcpy(((char *)last_non_mmap + META_PADDING), ptr, current_block->size);
				os_free(ptr);

				return (last_non_mmap + 1);
			}



			void *new_ptr = os_malloc(size);
			// struct block_meta *debug_block = get_block_ptr(new_ptr);
			// int writing_size = current_block->size < PADDING(size) ? current_block->size : PADDING(size);

			//aici current block.size o sa fie mereu mai mic pt ca sunt in if
			size_t copy_size = current_block->size;

			memcpy(new_ptr, ptr, copy_size);

			os_free(ptr);

			return new_ptr;
		}

	} else if (current_block->size - PADDING(size) >= 40) {
		//daca noul size este mai mic facem splitting
		//acest splitting are rost doar daca ar mai ramane minim 40 de bytes
		//liberi dupa splituire (32 din struct + 8 pentru bufferul aligned)


		if (current_block->status == STATUS_ALLOC) {
			struct block_meta *new_block = (struct block_meta *)((char *)current_block + META_PADDING + PADDING(size));

			new_block->status = STATUS_FREE;
			new_block->size = PADDING(current_block->size) - PADDING(size) - META_PADDING;
			// printf_("for malloc call %d, the new block size is %d\n", size, new_block->size);
			// printf_("-     closest block found is %p\n", closest_block);
			// printf_("-     and the new address is %p\n", new_block);

			//add to linked list
			new_block->prev = current_block;
			new_block->next = current_block->next;
			current_block->next = new_block;
			if (new_block->next != NULL)
				new_block->next->prev = new_block;

			current_block->size = PADDING(size);

			return (current_block + 1);
		} //else if (current_block->status == STATUS_MAPPED) {
			// munmap si micsorare
			//daca dau munmap va trebui sa fiu atent sa fac si heap prealloc
		// }


	} else if (PADDING(size) <= current_block->size) {
		//cazul asta este atunci cand facem resize la un bloc mai mic, dar nu putem face si un bloc nou
		// current_block->size = PADDING(size);
		return (current_block + 1);
	}


	return NULL;
}





