#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>

#define BUFFER_LENGTH		64
#define MAX_EVENTS		4096
#define MAX_PORT		100
#define WORKER_THREADS		4

static volatile long g_connections = 0;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
	int epfd;
	int start_idx;
	int end_idx;
	int *listen_fds;
} worker_ctx_t;

int islistenfd(int fd, int *fds, int start, int end) {
	int i;
	for (i = start; i < end; i ++) {
		if (fd == *(fds+i)) return fd;
	}
	return 0;
}

void set_nonblock(int fd) {
	int flags = fcntl(fd, F_GETFL, 0);
	flags |= O_NONBLOCK;
	fcntl(fd, F_SETFL, flags);
}

void *worker_routine(void *arg) {
	worker_ctx_t *ctx = (worker_ctx_t*)arg;
	struct epoll_event events[MAX_EVENTS];
	char buffer[BUFFER_LENGTH];

	while (1) {
		int nready = epoll_wait(ctx->epfd, events, MAX_EVENTS, 5);
		if (nready == -1) continue;

		int i = 0;
		for (i = 0; i < nready; i ++) {
			int sockfd = events[i].data.fd;
			int listen_fd = islistenfd(sockfd, ctx->listen_fds, ctx->start_idx, ctx->end_idx);

			if (listen_fd) {
				while (1) {
					struct sockaddr_in client_addr;
					memset(&client_addr, 0, sizeof(client_addr));
					socklen_t client_len = sizeof(client_addr);

					int clientfd = accept4(listen_fd, (struct sockaddr*)&client_addr, &client_len, SOCK_NONBLOCK);
					if (clientfd < 0) {
						if (errno == EAGAIN || errno == EWOULDBLOCK) break;
						continue;
					}

					int nodelay = 1;
					setsockopt(clientfd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

					pthread_mutex_lock(&g_lock);
					g_connections++;
					pthread_mutex_unlock(&g_lock);

					struct epoll_event ev;
					ev.events = EPOLLIN | EPOLLET;
					ev.data.fd = clientfd;
					epoll_ctl(ctx->epfd, EPOLL_CTL_ADD, clientfd, &ev);
				}
			} else {
				int clientfd = events[i].data.fd;
				
				while (1) {
					int len = recv(clientfd, buffer, BUFFER_LENGTH, 0);
					if (len > 0) {
						send(clientfd, buffer, len, 0);
					} else if (len == 0) {
						close(clientfd);
						pthread_mutex_lock(&g_lock);
						g_connections--;
						pthread_mutex_unlock(&g_lock);
						break;
					} else {
						if (errno == EAGAIN || errno == EWOULDBLOCK) break;
						close(clientfd);
						pthread_mutex_lock(&g_lock);
						g_connections--;
						pthread_mutex_unlock(&g_lock);
						break;
					}
				}
			}
		}
	}
	return NULL;
}

void *stats_routine(void *arg) {
	(void)arg;
	while (1) {
		sleep(5);
		printf("[Server] Total connections: %ld\n", g_connections);
	}
	return NULL;
}

int main(int argc, char *argv[]) {
	if (argc < 2) {
		printf("Usage: %s <start_port>\n", argv[0]);
		return -1;
	}
	
	int port = atoi(argv[1]);
	int sockfds[MAX_PORT] = {0};

	int i = 0;
	for (i = 0; i < MAX_PORT; i ++) {
		int sockfd = socket(AF_INET, SOCK_STREAM, 0);
		if (sockfd < 0) {
			perror("socket");
			return 1;
		}

		int reuse = 1;
		setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
		setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));

		int nodelay = 1;
		setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

		set_nonblock(sockfd);

		struct sockaddr_in addr;
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port + i);
		addr.sin_addr.s_addr = INADDR_ANY;

		if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
			perror("bind");
			return 2;
		}

		if (listen(sockfd, 8192) < 0) {
			perror("listen");
			return 3;
		}
		printf("tcp server listen on port : %d\n", port + i);

		sockfds[i] = sockfd;
	}

	int ports_per_worker = MAX_PORT / WORKER_THREADS;
	worker_ctx_t workers[WORKER_THREADS];
	pthread_t threads[WORKER_THREADS];

	for (int w = 0; w < WORKER_THREADS; w++) {
		workers[w].epfd = epoll_create1(0);
		workers[w].start_idx = w * ports_per_worker;
		workers[w].end_idx = (w == WORKER_THREADS - 1) ? MAX_PORT : (w + 1) * ports_per_worker;
		workers[w].listen_fds = sockfds;

		for (int idx = workers[w].start_idx; idx < workers[w].end_idx; idx++) {
			struct epoll_event ev;
			ev.events = EPOLLIN;
			ev.data.fd = sockfds[idx];
			epoll_ctl(workers[w].epfd, EPOLL_CTL_ADD, sockfds[idx], &ev);
		}

		pthread_create(&threads[w], NULL, worker_routine, &workers[w]);
	}

	pthread_t st;
	pthread_create(&st, NULL, stats_routine, NULL);

	for (int w = 0; w < WORKER_THREADS; w++) {
		pthread_join(threads[w], NULL);
	}

	return 0;
}
