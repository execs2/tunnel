#include "tunnel.h"
#include "server.h"
#include "client.h"
#include "buffer.h"

#include <signal.h>
#include <unistd.h>

static int pid = -1;

static void
set_terminated(int siga) {
	int buffer = 1;
	write(pid, &buffer, 1);

	fprintf(stderr, "%s Receive exit signal,please wait.\n", get_time());

	struct sigaction sa;
	sa.sa_handler = SIG_IGN;
	sigaction(SIGTERM, &sa, 0);
	sigaction(SIGINT, &sa, 0);
}

static void
deal_signal() {
	struct sigaction sa, oldsa;
	sa.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &sa, 0);

	sa.sa_handler = set_terminated;
	sa.sa_flags = SA_NODEFER;
	sigemptyset(&sa.sa_mask);

	//kill
	sigaction(SIGTERM, &sa, &oldsa);
	//ctrl + c
	sigaction(SIGINT, &sa, &oldsa);
}

static int
check_port(int port_1, int port_2) {
	if (port_1 <= 0 || port_1 > 65535 || port_1 <= 0 || port_2 >= 65535) {
		return 0;
	}

	return 1;
}

char *
get_time() {
	//not for mul theread
	static char st[50] = { 0 };
	time_t tNow = time(NULL);

	struct tm* ptm = localtime(&tNow);
	strftime(st, 50, "%Y-%m-%d %H:%M:%S", ptm);

	return st;
}


static void
do_server(int p1, int p2, int pid) {
	struct server_param sp;
	sp.listen_port[0] = p1;
	sp.listen_port[1] = p2;
	sp.pid = pid;

	pthread_t tid = start_server(&sp);

	pthread_join(tid, NULL);

	fprintf(stderr, "%s SERVER EXIT SUCCESS.................\n", get_time());
}

static void
do_client(const char* ip, int p1, int p2, int pid) {
	struct client_param cp;
	cp.p1 = p1;
	cp.p2 = p2;
	cp.pid = pid;
	strncpy(cp.remote_ip, ip, sizeof(cp.remote_ip));

	pthread_t tid = start_client(&cp);
	pthread_join(tid, NULL);

	fprintf(stderr, "%s CLIENT EXIT SUCCESS.................\n", get_time());
}

static void
error_param_tip() {
	fprintf(stderr, "Usage:\n1.transfer -s port1[bind port for ssh server] prot2[bind port for ssh client]\n2.transfer -c remoteIp[remote server ip] port1[connect port to remote server] prot2[connect port to local ssh server]\n");
}

int
main(int argc, char *argv[]) {
	//usage:
	//-s port1[bind port for ssh server] prot2[bind port for ssh client]
	//-c port1[connect port to local ssh server] prot2[connect port to remote server]

	if (argc <= 1){
		error_param_tip();
		return 0;
	}

	int port_1, port_2;
	int fd[2];
	if (pipe(fd)) {
		fprintf(stderr, "%s create pipe error.................\n", get_time());
		return -1;
	}

	pid = fd[1];
	deal_signal();

	if (argc == 4){
		if (strncmp(argv[1], "-s", 2) == 0){
			//do server
			port_1 = atoi(argv[2]);
			port_2 = atoi(argv[3]);
			if (!check_port(port_1, port_2)) {
				error_param_tip();
				goto __fails;
			}

			do_server(port_1, port_2, fd[0]);
		} else{
			goto __fails;
		}
	} else if (argc == 5) {
		if (strncmp(argv[1], "-c", 2) == 0){
			// do client
			const char* ip = argv[2];
			port_1 = atoi(argv[3]);
			port_2 = atoi(argv[4]);
			if (!check_port(port_1, port_2)) {
				error_param_tip();
				goto __fails;
			}

			do_client(ip, port_1, port_2, fd[0]);
		} else{
			error_param_tip();
		}
	}

__fails:
	close(fd[0]);
	close(fd[1]);
	return 0;

}
