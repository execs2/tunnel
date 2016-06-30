#include "tunnel.h"
#include "client.h"
#include "socket_comm.h"
#include "sys/time.h"

#include <assert.h>

struct client {
	char remote_ip[128];
	int remote_port;
	int ssh_port;
	int free_connection;
	int id_idx;

	struct ring_buffer* wait_closed;

	int max_fd;
	fd_set fd_rset;
	fd_set fd_wset;

	int time;
	int cnt;
	struct client_info all_fds[TOTAL_CONNECTION];
	int all_ids[TOTAL_CONNECTION];
};

static int
get_id(struct client* c) {
	int i, ret = -1;
	for (i = c->id_idx; i < c->id_idx + TOTAL_CONNECTION; ++i) {
		int idx = i % TOTAL_CONNECTION;
		++c->id_idx;

		if (c->all_fds[idx].fd == 0) {
			ret = i;
			break;
		}
	}

	return ret;
}

static int
connect_to(struct client* c, int ssh_id){
	if (c->cnt >= TOTAL_CONNECTION) {
		fprintf(stderr, "%s client max connection.....\n", get_time());
		return -1;
	}
	
	const char* ip;
	int port;
	if (ssh_id < 0) {
		if (c->free_connection > 0) return -1;

		struct timeval tv;
		gettimeofday(&tv, NULL);
		if (tv.tv_sec - c->time < FREE_CONNECT_TIME) {
			return -1;
		}else {
			c->time = tv.tv_sec;
		}

		ip = c->remote_ip;
		port = c->remote_port;
	} else {
		ip = "0.0.0.0";
		port = c->ssh_port;
	}

	int id;
	int idx;
	struct client_info* info;
	struct ring_buffer* rb;

	struct addrinfo hints;
	struct addrinfo* res = NULL;
	struct addrinfo* ai_ptr = NULL;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	char portstr[16];
	sprintf(portstr, "%d", port);
	int status = getaddrinfo(ip, portstr, &hints, &res);
	if (status != 0) {
		return -1;
	}

	int sock = -1;
	for (ai_ptr = res; ai_ptr != NULL; ai_ptr = ai_ptr->ai_next) {
		sock = socket(ai_ptr->ai_family, ai_ptr->ai_socktype, ai_ptr->ai_protocol);
		if (sock < 0) {
			continue;
		}

		set_keep_alive(sock);
		sp_nonblocking(sock);
		status = connect(sock, ai_ptr->ai_addr, ai_ptr->ai_addrlen);
		if (status != 0 && errno != EINPROGRESS) {
			close(sock);
			sock = -1;
			continue;
		}

		break;
	}

	if (sock < 0) {
		goto _failed;
	}

	id = get_id(c);
	assert(id != -1);

	idx = id % TOTAL_CONNECTION;
	info = &c->all_fds[idx];
	info->fd = sock;
	info->id = id;
	info->to_id = ssh_id;
	snprintf(info->client_ip, sizeof(info->client_ip), "%s:%d", ip, port);

	rb = alloc_ring_buffer(MAX_CLIENT_BUFFER);
	info->buffer = rb;

	c->all_ids[c->cnt++] = id;

	if (ssh_id < 0) {
		c->free_connection += 1;
	}

	if (status != 0) {
		//connect no block, need check after
		FD_SET(sock, &c->fd_wset);
		info->connect_type = SOCKET_CONNECTING;
	}else {
		//success
		FD_SET(sock, &c->fd_rset);
		info->connect_type = SOCKET_CONNECTED;

		struct sockaddr* addr = ai_ptr->ai_addr;
		void* sin_addr = (ai_ptr->ai_family == AF_INET) ? (void*)&((struct sockaddr_in*)addr)->sin_addr : (void*)&((struct sockaddr_in6*)addr)->sin6_addr;

		inet_ntop(ai_ptr->ai_family, sin_addr, info->client_ip, sizeof(info->client_ip));
		fprintf(stderr, "%s connected to %s. \n", get_time(), info->client_ip);
	}

	if (c->max_fd < sock + 1) {
		c->max_fd = sock + 1;
	}

	return id;

_failed:
	freeaddrinfo(res);
	return -1;
}

static void
do_close(struct client* c, struct client_info* info) {
	int i;
	for (i = 0; i < c->cnt; ++i) {
		if (c->all_ids[i] == info->id) {
			memcpy(c->all_ids + i, c->all_ids + i + 1, (c->cnt - i - 1) * sizeof(int));
			--c->cnt;
			break;
		}
	}

	FD_CLR(info->fd, &c->fd_rset);
	FD_CLR(info->fd, &c->fd_wset);
	close(info->fd);

	if (info->to_id == -1) {
		assert(c->free_connection == 1);

		c->free_connection = 0;
	}

	if (info->to_id >= 0 && c->all_fds[info->to_id % TOTAL_CONNECTION].id == info->to_id ) {
		int len;
		char* id_buffer = get_ring_buffer_write_ptr(c->wait_closed, &len);
		int id_len = sizeof(int);
		assert(id_buffer && len >= id_len);
		memcpy(id_buffer, &info->to_id, id_len);
		move_ring_buffer_write_pos(c->wait_closed, id_len);
	}

	if (info->connect_type == SOCKET_CONNECTED)
	{
		fprintf(stderr, "%s client disconnect from %s.\n", get_time(), info->client_ip);
	}

	info->to_id = -1;
	info->id = -1;
	info->fd = 0;
	info->connect_type = 0;
	free_ring_buffer(info->buffer);
	info->buffer = NULL;

	memset(info->client_ip, 0, sizeof(info->client_ip));
}

static int
report_connect(struct client* c, struct client_info* info) {
	int error;
	socklen_t len = sizeof(error);
	int code = getsockopt(info->fd, SOL_SOCKET, SO_ERROR, &error, &len);
	if (code != 0 || error != 0) {
		//connect fail, close it
		fprintf(stderr, "%s client: connect to %s error :%s. \n", get_time(), info->client_ip, strerror(error));
		do_close(c, info);
		return -1;
	}

	info->connect_type = SOCKET_CONNECTED;
	FD_SET(info->fd, &c->fd_rset);

	union sockaddr_all u;
	socklen_t slen = sizeof(u);
	if (getpeername(info->fd, &u.s, &slen) == 0){
		void* sin_addr = (u.s.sa_family == AF_INET) ? (void*)&u.v4.sin_addr : (void*)&u.v6.sin6_addr;
		inet_ntop(u.s.sa_family, sin_addr, info->client_ip, sizeof(info->client_ip));
	}

	fprintf(stderr, "%s connected to %s. \n", get_time(), info->client_ip);

	return 0;
}

static int
do_read(struct client* c, struct client_info* info) {
	assert(info->connect_type == SOCKET_CONNECTED);

	int to_id = info->to_id;
	if (to_id < 0) {
		to_id = connect_to(c, info->id);
		if (to_id == -1) {
			do_close(c, info);
			return -1;
		}

		info->to_id = to_id;

		c->free_connection -= 1;
		c->time = 0;
	}

	int len;
	struct client_info* to_info = &c->all_fds[to_id % TOTAL_CONNECTION];
	if (to_info->id != to_id) {
		do_close(c, info);
		return -1;
	}

	char* buffer = get_ring_buffer_write_ptr(to_info->buffer, &len);
	if (!buffer) {
		return 0; //buff fulled
	}

	int n = (int)read(info->fd, buffer, len);
	if (n == -1) {
		switch (errno) {
		case EAGAIN:
			fprintf(stderr, "%s read fd error:EAGAIN.\n", get_time());
			break;
		case EINTR:
			break;
		default:
			fprintf(stderr, "%s client: read (id=%d) error :%s. \n", get_time(), info->id, strerror(errno));
			do_close(c, info);
			return -1;
		}

		return 1;
	}

	if (n == 0) {
		do_close(c, info); //normal close
		return -1;
	}

	move_ring_buffer_write_pos(to_info->buffer, n);
	FD_SET(to_info->fd, &c->fd_wset);

	if (n == len && !is_ring_buffer_empty(to_info->buffer)) {
		fprintf(stderr, "%s client: read again.\n", get_time());
		return do_read(c, info);
	}

	return 1;
}

static int
do_write(struct client* c, struct client_info* info, int wait_closed) {
	if (info->connect_type == SOCKET_CONNECTING) {
		if (wait_closed) return 0;

		if (report_connect(c, info) == -1) {
			return -1;
		}else if (is_ring_buffer_empty(info->buffer)) {
			FD_CLR(info->fd, &c->fd_wset);
		}

		return 0;
	}

	int len;
	char* buffer = get_ring_buffer_read_ptr(info->buffer, &len);
	if (!buffer) {
		return 0;
	}

	int writed_len = 0;
	char need_break = 0;
	while (!need_break && writed_len < len) {
		int n = write(info->fd, buffer, len - writed_len);
		if (n < 0) {
			switch (errno) {
			case EINTR:
				n = 0;
				break;
			case EAGAIN:
				n = 0;
				need_break = 1;
				break;
			default:
				need_break = 1;
				fprintf(stderr, "%s socket-client: write to (id=%d) error :%s.\n", get_time(), info->id, strerror(errno));
				do_close(c, info);
				return -1;
			}
		}
		else {
			writed_len += n;
			buffer += n;
		}
	}

	move_ring_buffer_read_pos(info->buffer, writed_len);

	if (is_ring_buffer_empty(info->buffer)) {
		FD_CLR(info->fd, &c->fd_wset);
	} else if (writed_len == len) {
		fprintf(stderr, "%s client: write again.\n", get_time());
		return do_write(c, info, wait_closed);
	}

	return 1;
}

static void
pre_check_close(struct client* c) {
	int len;
	char* id_buffer = get_ring_buffer_read_ptr(c->wait_closed, &len);
	if (!id_buffer) return;

	int id_len = sizeof(int);
	assert(len % id_len == 0);
        int tmp = len;

	while (len > 0) {
		int* id = (int*)id_buffer;
		int idx = *id % TOTAL_CONNECTION;
		id_buffer += id_len;
		len -= len;

		struct client_info* info = c->all_fds + idx;
		if (info->fd > 0 && info->id == *id) {
			if (do_write(c, info, 1) != -1) {
				do_close(c, info);
			}
		}
	}

        move_ring_buffer_read_pos(c->wait_closed, tmp);
}

static void*
client_thread(void* param) {
	struct client_param* cp = (struct client_param*)param;

	struct client c;
	memset(&c, 0, sizeof(c));
	sprintf(c.remote_ip, "%s", cp->remote_ip);
	c.remote_port = cp->p1;
	c.ssh_port = cp->p2;
	c.wait_closed = alloc_ring_buffer(sizeof(int) * TOTAL_CONNECTION);

	FD_ZERO(&c.fd_rset);
	FD_ZERO(&c.fd_wset);
	FD_SET(cp->pid, &c.fd_rset);
	c.max_fd = cp->pid + 1;
	sp_nonblocking(cp->pid);

	while (1) {
		pre_check_close(&c);
		
		if (connect_to(&c, -1) == -1 && c.cnt == 0) {
			c.max_fd = cp->pid + 1;

			int buff = 0;
			int n = (int)read(cp->pid, &buff, sizeof(int));
			if (n > 0) {
				break;
			}

			sleep(1);
			continue;
		}

		fd_set r_set = c.fd_rset;
		fd_set w_set = c.fd_wset;

		int cnt = select(c.max_fd, &r_set, &w_set, NULL, NULL);
		if (cnt == -1) {
			fprintf(stderr, "%s select error: %s.\n", get_time(), strerror(errno));
			continue;
		}

		int i;
		for (i = c.cnt - 1; i >= 0 && cnt > 0; --i) {
			int id = c.all_ids[i] % TOTAL_CONNECTION;
			struct client_info* info = &c.all_fds[id];
			assert(c.all_ids[i] == info->id);

			int fd = info->fd;
			assert(fd > 0);

			if (FD_ISSET(fd, &r_set)) {
				// read
				--cnt;
				if (do_read(&c, info) == -1) continue;
			}

			if (FD_ISSET(fd, &w_set)) {
				//write
				--cnt;
				if (do_write(&c, info, 0) == -1) continue;
			}
		}

		if (FD_ISSET(cp->pid, &r_set)) {
			//exit
			break;
		}
	}

	fprintf(stderr, "%s ====================CLIENT: SEND LAST DATA BEGIN===================.\n", get_time());

	int i;
	for (i = c.cnt - 1; i >= 0; --i) {
		int id = c.all_ids[i] % TOTAL_CONNECTION;
		struct client_info* info = &c.all_fds[id];
		assert(c.all_ids[i] == info->id);

		if (do_write(&c, info,	1) != -1) {
			do_close(&c, info);
		}
	}

	fprintf(stderr, "%s ====================CLIENT: SEND LAST DATA END=====================.\n", get_time());

	free_ring_buffer(c.wait_closed);
	assert(c.cnt == 0);

	return NULL;
}

pthread_t
start_client(struct client_param* cp) {
	pthread_t pid;
	if (pthread_create(&pid, NULL, client_thread, cp)) {
		fprintf(stderr, "%s Create client thread failed.\n", get_time());
		exit(1);

		return 0;
	}

	return pid;
}
