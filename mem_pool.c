#include "mem_pool.h"

#define TEST                            int
#define OFFSET_SIZE                     sizeof(int)
#define OFFSET_NEXT_PTR_SIZE		sizeof(void *)
#define OFFSET_PRE_PTR_BEGIN            OFFSET_SIZE + OFFSET_NEXT_PTR_SIZE
#define OFFSET_PRE_PTR_SIZE             sizeof(void *)
#define MIN_ALLOC_CNT                   OFFSET_SIZE + OFFSET_NEXT_PTR_SIZE + OFFSET_PRE_PTR_SIZE

struct msg_pool *
create_pool(int max_size) {
	struct msg_pool* p = (struct msg_pool*)malloc(sizeof(*p));
	memset(p, 0, sizeof(*p));

	p->max_size = max_size;
	p->free_cnt = max_size;
	p->list_cnt = 1;
	p->data_ptr = malloc(max_size);
	p->free_list = p->data_ptr;

	*(TEST*)p->free_list = max_size;
	void** next = (void**)((char *)p->data_ptr + OFFSET_SIZE);
	*next = NULL;
	void** pre = (void**)((char *)p->data_ptr + OFFSET_PRE_PTR_BEGIN);
	*pre = NULL;

	return p;
}

static struct msg_pool *
msg_pool_expand(struct msg_pool* p) {
	assert(p && p->max_size > 0);

	while (p->next) {
		p = p->next;
	}

	p->next = create_pool(p->max_size);

	return p->next;
}

void *
msg_pool_alloc(struct msg_pool* p, size_t size) {
	size_t real_size = size + OFFSET_SIZE;
	real_size = real_size < MIN_ALLOC_CNT ? MIN_ALLOC_CNT : real_size;
	assert(real_size <= p->max_size);

	void* ret = NULL;
	if (p->free_cnt >= real_size) {
		void* block = p->free_list;
		assert(block != NULL);

		while (block) {
			void* next = *(void **)((char*)block + OFFSET_SIZE);
			TEST block_cnt = *(TEST*)block;
			assert(block_cnt >= MIN_ALLOC_CNT && block_cnt <= p->max_size);
			if (block_cnt < real_size) {
				block = next;
				continue;
			}

			if (block_cnt - real_size < MIN_ALLOC_CNT) {
				real_size = block_cnt;
			}

			void **pre_next = NULL;
			void *pre_block = *(void **)((char *)block + OFFSET_PRE_PTR_BEGIN);
			if (pre_block) {
				pre_next = (void **)((char*)pre_block + OFFSET_SIZE);
				assert(*pre_next == block);
			}

			if (0 == block_cnt - real_size) {
				if (pre_next && *pre_next) {
					*pre_next = next;
				}
				else {
					p->free_list = next;
				}

				if (next) {
					*(void **)((char *)next + OFFSET_PRE_PTR_BEGIN) = pre_block;
				}

				p->list_cnt -= 1;
			}
			else {
				void *rest_block = (char *)block + real_size;
				*(TEST*)rest_block = block_cnt - real_size;
				*(void **)((char *)rest_block + OFFSET_PRE_PTR_BEGIN) = pre_block;
				*(void **)((char *)rest_block + OFFSET_SIZE) = *(void **)((char *)block + OFFSET_SIZE);

				if (next) {
					*(void **)((char *)next + OFFSET_PRE_PTR_BEGIN) = rest_block;
				}

				if (pre_next && *pre_next) {
					*pre_next = rest_block;
				}
				else {
					p->free_list = rest_block;
				}
			}

			*(TEST*)block = real_size;
			ret = (char*)block + OFFSET_SIZE;
			p->free_cnt -= real_size;
			return ret;
		}
	}

	if (p->next) {
		return msg_pool_alloc(p->next, size);
	}
	else {
		return msg_pool_alloc(msg_pool_expand(p), size);
	}
}

static void
msg_pool_delete_one(struct msg_pool* p) {
	free(p->data_ptr);
	free(p);
}

void
msg_pool_free(struct msg_pool* p, void* ptr, char* freed) {
	if (ptr == NULL || p == NULL) return;

	*freed = 0;
	ptr = (char*)ptr - OFFSET_SIZE;

	struct msg_pool *pre_p = p;
	while (p != NULL) {
		void *max_ptr = (char*)p->data_ptr + p->max_size;

		if (ptr >= p->data_ptr && ptr < max_ptr) {
			break;
		}

		pre_p = p;
		p = p->next;
	}

	if (p == NULL) {
		fprintf(stderr, "error: call msg_pool free a block not alloc from pool");
		return;
	}

	TEST size = *(TEST*)ptr;
	memset((char*)ptr + OFFSET_SIZE, 0, MIN_ALLOC_CNT - OFFSET_SIZE);
	assert(size >= MIN_ALLOC_CNT && size <= p->max_size);

	void *block = p->free_list;
	void *pre_block = NULL;
	while (block && block > ptr) {
		void* next = *(void **)((char*)block + OFFSET_SIZE);
		pre_block = block;
		block = next;
	}

	if (block) {
		*(void **)((char *)block + OFFSET_PRE_PTR_BEGIN) = ptr;
	}

	if (pre_block) {
		void **pre_next = (void **)((char*)pre_block + OFFSET_SIZE);
		*pre_next = ptr;
	}

	*(void **)((char *)ptr + OFFSET_PRE_PTR_BEGIN) = pre_block;
	*(void **)((char*)ptr + OFFSET_SIZE) = block;

	//merge
	int merge_cnt = block ? 2 : 1;
	void* merge_ptr = block ? block : ptr;

	while (merge_cnt > 0 && merge_ptr) {
		TEST size = *(TEST*)merge_ptr;
		void* pre_ptr = *(void **)((char *)merge_ptr + OFFSET_PRE_PTR_BEGIN);

		if ((char *)merge_ptr + size == pre_ptr) {
			TEST pre_size = *(TEST*)pre_ptr;
			*(TEST*)merge_ptr = size + pre_size;

			void* pre_pre_ptr = *(void **)((char *)pre_ptr + OFFSET_PRE_PTR_BEGIN);
			*(void **)((char *)merge_ptr + OFFSET_PRE_PTR_BEGIN) = pre_pre_ptr;

			if (pre_pre_ptr) {
				*(void **)((char*)pre_pre_ptr + OFFSET_SIZE) = merge_ptr;
			}

			if (pre_ptr == p->free_list) {
				p->free_list = merge_ptr;
			}

			--p->list_cnt;
		}
		else {
			if (p->free_list < merge_ptr) {
				p->free_list = merge_ptr;
			}
			merge_ptr = pre_ptr;
		}
		--merge_cnt;
	}

	p->free_cnt += size;
	p->list_cnt += 1;
	if (p->free_cnt == p->max_size && pre_p != p) {
		pre_p->next = p->next;
		//delete p
		msg_pool_delete_one(p);

		*freed = 1;
	}
}

void
msg_pool_delete(struct msg_pool *p) {
	if (p == NULL) return;

	do {
		struct msg_pool* next = p->next;
		msg_pool_delete_one(p);
		p = next;
	} while (p != NULL);
}
/*
int
main(int argc, char *argv[]) {
	msg_pool *p = create_pool(1024);

	void *b1 = msg_pool_alloc(p, 300);
	void *b2 = msg_pool_alloc(p        , 600);

	bool b;
	msg_pool_free(p, b1, b);
	void *b3 = msg_pool_alloc(p, 96);
	void *b4 = msg_pool_alloc(p, 100);
	void *b5 = msg_pool_alloc(p, 90);

	msg_pool_free(p, b2, b);
	msg_pool_free(p, b3, b);
	msg_pool_free(p, b4, b);
	msg_pool_free(p, b5, b);
       

	int alloced = 0;
	int max_alloced = 1024;
	void **vec_alloc = (void **)malloc(sizeof(void *) * 1024);
	int tmp1 = 0, tmp2 = 0;
	while (1) {
		srand(time(0));
		int r = rand() % 2;
		if (r) {
			if (alloced >= max_alloced) {
				void **tmp = (void **)malloc(sizeof(void *) * max_alloced * 2);
				memcpy(tmp, vec_alloc, max_alloced * sizeof(void *));
				max_alloced *= 2;
				free(vec_alloc);
				vec_alloc = tmp;
			}

			int size = rand() % 1020 + 1;
			void* b = msg_pool_alloc(p, size);
			vec_alloc[alloced++] = b;

			memset(b, 0, size);
			tmp1++;
		}
		else {
			if (alloced > 0) {
				void *b = vec_alloc[0];
				char freed = 0;
				msg_pool_free(p, b, &freed);

				alloced -= 1;
				memmove(vec_alloc, (char*)vec_alloc + sizeof(void *), alloced * sizeof(void *));

				if (!freed) {
					void *ptr = (char*)b - OFFSET_SIZE;
					TEST size = *(TEST*)ptr;
					memset((char *)ptr + MIN_ALLOC_CNT, 0, size - MIN_ALLOC_CNT);
				}

				tmp2++;
			}
		}

		usleep(1);
	}
	msg_pool_delete(p);
	return 0;
}
*/
