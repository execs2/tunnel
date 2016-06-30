#ifndef SERVER_H
#define SERVER_H

#include <pthread.h>

struct buffer_array;

struct server_param {
        int listen_port[2];
		int pid;
};

pthread_t start_server(struct server_param* tp);
void accept_info_init();

#endif
