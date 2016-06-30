#ifndef SOCKET_COMM_H
#define SOCKET_COMM_H

#include "buffer.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define SOCKET_CONNECTED        1
#define SOCKET_CONNECTING       2

struct client_info
{
	int id;
	int fd;
	int to_id;
	int connect_type;

	struct ring_buffer* buffer;
	char client_ip[128];
};

union sockaddr_all {
	struct sockaddr s;          /*normal storage*/
	struct sockaddr_in v4;      /*ipv4 storage*/
	struct sockaddr_in6 v6;     /*ipv6 storage*/
};

void sp_nonblocking(int fd);
void set_keep_alive(int fd);

#endif
