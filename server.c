#include "tunnel.h"
#include "server.h"
#include "buffer.h"
#include "socket_comm.h"

#include <assert.h>
#include <unistd.h>

struct server_info
{
	int listen_port[2];
	int listen_fd[2];
	struct client_info client[TOTAL_CONNECTION];
	int client_id[TOTAL_CONNECTION];
	int client_cnt;
	int max_fd;
	int id_idx;

	struct ring_buffer* wait_closed;

	int listen_id;

	fd_set fd_rset;
	fd_set fd_wset;
};

static int
server_init(const char* host, int port) {
	int fd;
	int reuse = 1;
	struct addrinfo ai_hints;
	struct addrinfo *ai_list = NULL;
	char portstr[16];
	if (host == NULL || host[0] == 0){
		host = "0.0.0.0";
	}
	sprintf(portstr, "%d", port);
	memset(&ai_hints, 0, sizeof(ai_hints));
	ai_hints.ai_protocol = IPPROTO_TCP;
	ai_hints.ai_socktype = SOCK_STREAM;
	ai_hints.ai_family = AF_UNSPEC;

	int status = getaddrinfo(host, portstr, &ai_hints, &ai_list);
	if (status != 0) {
		return -1;
	}

	fd = socket(ai_list->ai_family, ai_list->ai_socktype, 0);
	if (fd < 0) {
		goto _failed_fd;
	}

	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void *)&reuse, sizeof(int)) == -1) {
		goto _failed;
	}

	/*bind*/
	status = bind(fd, (struct sockaddr *)ai_list->ai_addr, ai_list->ai_addrlen);
	if (status != 0)
		goto _failed;

	//listen
	if (listen(fd, 32) == -1) {
		close(fd);
		fprintf(stderr, "%s listen port %d failed.\n", get_time(), port);
		return -1;
	}

	freeaddrinfo(ai_list);
	sp_nonblocking(fd);
	return fd;

_failed:
	close(fd);
_failed_fd:
	freeaddrinfo(ai_list);
	return -1;
}

static int
get_id(struct server_info* s) {
	int i, ret = -1;
	for (i = s->id_idx; i < s->id_idx + TOTAL_CONNECTION; ++i) {
		int idx = i % TOTAL_CONNECTION;
		++s->id_idx;

		if (s->client[idx].fd == 0) {
			ret = i;
			break;
		}
	}

	return ret;
}

static void
do_accept(struct server_info* s, int listen_fd) {
	union sockaddr_all addr;
	socklen_t len = sizeof(addr);

	int fd = accept(listen_fd, &addr.s, &len);
	if (fd == -1) {
		int err = errno;
		if (err != EAGAIN) {
			fprintf(stderr, "%s accept error: %s.\n", get_time(), strerror(err));
		}

		return;
	}

	if (s->client_cnt >= TOTAL_CONNECTION) {
		close(fd);
		fprintf(stderr, "%s accept error, connection max.............\n", get_time());
		return;
	}

	if (listen_fd == s->listen_fd[0]) {
		if (s->listen_id < 0) {
			//TODO:可以加个连接缓存，等待下个tunnel上来后，直接连上
			close(fd);
			fprintf(stderr, "%s accept error, no available connection for now.............\n", get_time());
			return;
		}
	}else {
		if (s->listen_id >= 0) {
			close(fd);
			fprintf(stderr, "%s accept error, tunnel only need one available.............\n", get_time());
			return;
		}
	}

	int id = get_id(s);
	assert(id != -1);

	struct ring_buffer* rb = alloc_ring_buffer(MAX_CLIENT_BUFFER);

	struct client_info* nc = s->client + id % TOTAL_CONNECTION;
	assert(nc->fd == 0);

	nc->id = id;
	nc->fd = fd;
	nc->buffer = rb;
	nc->to_id = -1;

	void* sin_addr = (addr.s.sa_family == AF_INET) ? (void*)&addr.v4.sin_addr : (void *)&addr.v6.sin6_addr;
	int sin_port = ntohs((addr.s.sa_family == AF_INET) ? addr.v4.sin_port : addr.v6.sin6_port);

	static char tmp[128];
	if (inet_ntop(addr.s.sa_family, sin_addr, tmp, sizeof(tmp))) {
		snprintf(nc->client_ip, sizeof(nc->client_ip), "%s:%d", tmp, sin_port);
	}

	fprintf(stderr, "%s client %s connected.\n", get_time(), nc->client_ip);

	s->client_id[s->client_cnt++] = id;

	FD_SET(fd, &s->fd_rset);
	if (s->max_fd < fd + 1) {
		s->max_fd = fd + 1;//TODO:最大的fd被close后是否要处理下
	}

	set_keep_alive(fd);
	sp_nonblocking(fd);

	if (listen_fd == s->listen_fd[0]) {
		nc->to_id = s->listen_id;
		struct client_info* listen_nc = s->client + nc->to_id % TOTAL_CONNECTION;
		assert(listen_nc->fd >= 0 && listen_nc->id == nc->to_id);
		listen_nc->to_id = nc->id;
		s->listen_id = -1;
	}else {
		s->listen_id = id;
	}
}

static void
do_close(struct server_info* s, struct client_info* c) {
	int i;
	for (i = 0; i < s->client_cnt; ++i) {
		if (s->client_id[i] == c->id) {
			memcpy(s->client_id + i, s->client_id + i + 1, (s->client_cnt - i - 1) * sizeof(int));
			--s->client_cnt;
			break;
		}
	}

	FD_CLR(c->fd, &s->fd_rset);
	FD_CLR(c->fd, &s->fd_wset);
	close(c->fd);

	int idx = c->id % TOTAL_CONNECTION;
	assert(&s->client[idx] == c);

	if (c->id == s->listen_id) {
		s->listen_id = -1;
	}

	if (c->to_id >= 0 && s->client[c->to_id % TOTAL_CONNECTION].id == c->to_id) {
		int len;
		char* id_buffer = get_ring_buffer_write_ptr(s->wait_closed, &len);
		int id_len = sizeof(int);
		assert(id_buffer && len >= id_len);
		memcpy(id_buffer, &c->to_id, id_len);
		move_ring_buffer_write_pos(s->wait_closed, id_len);
	}

	c->to_id = -1;
	c->id = -1;
	c->fd = 0;
	free_ring_buffer(c->buffer);
	c->buffer = NULL;

	fprintf(stderr, "%s client %s disconnect.\n", get_time(), c->client_ip);
	memset(c->client_ip, 0, sizeof(c->client_ip));
}

/*
static int
try_write(struct server_info* s, struct client_info* c) {
	int len;
	char* buffer = get_ring_buffer_read_ptr(c->buffer, &len);
	if (!buffer) {
		return 0; //empty
	}

	int n = write(c->fd, buffer, len);
	if (n < 0) {
		switch (errno) {
		case EINTR:
		case EAGAIN:
			break;
		default:
			fprintf(stderr, "server: write to (id=%d) error :%s.\n", c->id, strerror(errno));
			do_close(s, c);
			return -1;
		}
	}else {
		move_ring_buffer_read_pos(c->buffer, n);
	}

	if (!is_ring_buffer_empty(c->buffer)) {
		FD_SET(c->fd, &s->fd_wset);
	}

	return 1;
}
*/

static int
do_read(struct server_info* s, struct client_info* c) {
	int id = c->to_id;
	if (id < 0) {
		do_close(s, c); //only when client disconnect
		return -1;
	}

	struct client_info* to_c = s->client + id % TOTAL_CONNECTION;
	if (to_c->id != id) {
		do_close(s, c);
		return -1;
	}

	struct ring_buffer* rb = to_c->buffer;

	int len;
	char* start_buffer = get_ring_buffer_write_ptr(rb, &len);
	if (!start_buffer) {
		return 0; //buff fulled
	}

	int n = (int)read(c->fd, start_buffer, len);
	if (n == -1) {
		switch (errno) {
		case EAGAIN:
			fprintf(stderr, "%s read fd error:EAGAIN.\n", get_time());
			break;
		case EINTR:
			break;
		default:
			fprintf(stderr, "%s server: read (id=%d) error :%s.\n", get_time(), c->id, strerror(errno));
			do_close(s, c);
			return -1;
		}

		return 1;
	}

	if (n == 0) {
		do_close(s, c); //normal close
		return -1;
	}

	move_ring_buffer_write_pos(rb, n);
	FD_SET(to_c->fd, &s->fd_wset);

	if (n == len && !is_ring_buffer_empty(rb)) {
		fprintf(stderr, "%s server: read again.\n", get_time());
		return do_read(s, c);
	}

	return 1;
}

static int
do_write(struct server_info* s, struct client_info* c) {
	int len;
	char* buffer = get_ring_buffer_read_ptr(c->buffer, &len);
	if (!buffer) {
		return 0;
	}

	int writed_len = 0;
	char need_break = 0;
	while (!need_break && writed_len < len) {
		int n = write(c->fd, buffer, len - writed_len);
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
				fprintf(stderr, "%s socket-server: write to (id=%d) error :%s.\n", get_time(), c->id, strerror(errno));
				do_close(s, c);
				return -1;
			}
		} else {
			writed_len += n;
			buffer += n;
		}
	}

	move_ring_buffer_read_pos(c->buffer, writed_len);

	if (is_ring_buffer_empty(c->buffer)) {
		FD_CLR(c->fd, &s->fd_wset);
	} else if (writed_len == len) {
		fprintf(stderr, "%s server: write again.\n", get_time());
		return do_write(s, c);
	}

	return 1;
}

static void
pre_check_close(struct server_info* s) {
	int len;
	char* id_buffer = get_ring_buffer_read_ptr(s->wait_closed, &len);
	if (!id_buffer) return;

	int id_len = sizeof(int);
	assert(len % id_len == 0);
        int tmp = len;
        
	while (len > 0) {
		int* id = (int*)id_buffer;
		int idx = *id % TOTAL_CONNECTION;
		id_buffer += id_len;
		len -= len;

		struct client_info* c = s->client + idx;
		if (c->fd > 0) {
			if (do_write(s, c) != -1) {
				do_close(s, c);
			}
		}
	}

        move_ring_buffer_read_pos(s->wait_closed, tmp);
}

static void*
server_thread(void* param) {
	struct server_param *tp = (struct server_param*)param;
	int fd1 = server_init(NULL, tp->listen_port[0]);
	if (fd1 == -1) {
		return NULL;
	}
	
	int fd2 = server_init(NULL, tp->listen_port[1]);
	if (fd2 == -1) {
		close(fd1);
		return NULL;
	}

	struct server_info s;
	memset(&s, 0, sizeof(s));

	s.listen_fd[0] = fd1;
	s.listen_fd[1] = fd2;
	s.listen_port[0] = tp->listen_port[0];
	s.listen_port[1] = tp->listen_port[1];
	int tmp_fd = fd1 > fd2 ? fd1: fd2;
	tmp_fd = tp->pid > tmp_fd ? tp->pid : tmp_fd;

	s.max_fd = tmp_fd + 1;
	s.listen_id = -1;
	s.wait_closed = alloc_ring_buffer(TOTAL_CONNECTION * sizeof(int));

	FD_ZERO(&s.fd_wset);
	FD_ZERO(&s.fd_rset);
	FD_SET(fd1, &s.fd_rset);
	FD_SET(fd2, &s.fd_rset);
	FD_SET(tp->pid, &s.fd_rset);

	while (1) {
		pre_check_close(&s);

		fd_set r_set = s.fd_rset;
		fd_set w_set = s.fd_wset;

		int cnt = select(s.max_fd, &r_set, &w_set, NULL, NULL);
		if (cnt == -1) {
			fprintf(stderr, "%s select error %s.\n", get_time(), strerror(errno));
			continue;
		}

		if (FD_ISSET(s.listen_fd[1], &r_set)) {
			//accept
			--cnt;
			do_accept(&s, s.listen_fd[1]);
		}

		if (FD_ISSET(s.listen_fd[0], &r_set)) {
			//accept
			--cnt;
			do_accept(&s, s.listen_fd[0]);
		}

		int i;
		for (i = s.client_cnt - 1; i >= 0 && cnt > 0; --i) {
			int id = s.client_id[i] % TOTAL_CONNECTION;
			struct client_info* c = &s.client[id];
			int fd = c->fd;
			assert(fd > 0);

			if (FD_ISSET(fd, &r_set)) {
				//read
				--cnt;
				if (do_read(&s, c) == -1) continue;
			}

			if (FD_ISSET(fd, &w_set)) {
				//write
				--cnt;
				if (do_write(&s, c) == -1) continue;
			}
		}

		if (FD_ISSET(tp->pid, &r_set)) {
			//exit
			break;
		}
	}

	close(s.listen_fd[0]);
	close(s.listen_fd[1]);

	//try send the last buffer
	fprintf(stderr, "%s ====================SERVER: SEND LAST DATA BEGIN===================.\n", get_time());

	int i;
	for (i = s.client_cnt - 1; i >= 0; --i) {
		int id = s.client_id[i] % TOTAL_CONNECTION;
		struct client_info* c = &s.client[id];
		int fd = c->fd;
		assert(fd > 0);

		if (do_write(&s, c) != -1) {
			do_close(&s, c);
		}
	}

	fprintf(stderr, "%s ====================SERVER SEND LAST DATA END=====================.\n", get_time());

	free_ring_buffer(s.wait_closed);
	assert(s.client_cnt == 0);
	return NULL;
}

pthread_t
start_server(struct server_param* tp) {
	pthread_t pid;
	if (pthread_create(&pid, NULL, server_thread, tp)) {
		fprintf(stderr, "%s Create server thread failed.\n", get_time());
		exit(1);

		return 0;
	}

	return pid;
}
