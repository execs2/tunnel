#ifndef MEM_POOL_H
#define MEN_POOL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
//#include <time.h>

struct msg_pool {
        int max_size;

        void *data_ptr;

        int list_cnt;
        void *free_list;
        int free_cnt;

        struct msg_pool *next;
};

void* msg_pool_alloc(struct msg_pool* p, size_t size);
struct msg_pool* create_pool(int max_size);
void msg_pool_free(struct msg_pool *p, void *ptr, char* freed);
void msg_pool_delete(struct msg_pool *p);

#endif
