#ifndef CLIENT_H
#define CLIENT_H

#include <pthread.h>

#define FREE_CONNECT_TIME 60

struct client_param {
	int p1;
	int p2;
	int pid;
	char remote_ip[128];
};

pthread_t start_client(struct client_param* cp);

#endif
