#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "bwrite.h"

static int s1, s2;

int create_socket(const char *, int);
int read_stdin(void);
void check_clients(void);
void new_client(int fd, int socktype);
void notify_clients(char *buf);

int
create_socket(const char *name, int socktype)
{
	int fd, slen;
	struct sockaddr_un sun;

	if ((fd = socket(PF_LOCAL, socktype, 0)) < 0)
		err(1, "socket");
	bzero(&sun, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strlcpy(sun.sun_path, name, sizeof(sun.sun_path));
	slen = SUN_LEN(&sun);
	unlink(name);
	if (bind(fd, (struct sockaddr *) &sun, slen) < 0)
		err(1, "bind");
	listen(fd, 4);
	chmod(name, 0666);
	return (fd);
}

struct client {
	struct client *next;
	struct bwrite bw;
	char dbuf[1000];
	size_t recbuf[100];
};
static struct client *clients;
static int num_clients;

void
check_clients(void)
{
	struct client *c, **pc;
	enum bw_state state;
	int dorm;

	dorm = 0;
	for (pc = &clients; (c = *pc) != NULL;) {
		state = bw_check(&c->bw, BW_CHECK_HUP);
		if (state == BW_OPEN)
			printf("%d: open\n", c->bw.bw_fd);
		else if (state == BW_BLOCKED)
			printf("%d: blocked\n", c->bw.bw_fd);
		else if (state == BW_INTR)
			printf("%d: interrupt\n", c->bw.bw_fd);
		else if (state == BW_WFAIL) {
			warn("%d: write fail", c->bw.bw_fd);
			dorm = 1;
		} else if (state == BW_RHUP) {
			printf("%d: read HUP\n", c->bw.bw_fd);
			dorm = 1;
		} else if (state == BW_RREADY) {
			printf("%d: read ready ?!\n", c->bw.bw_fd);
		} else if (state == BW_ERROR) {
			warn("%d: error", c->bw.bw_fd);
			dorm = 1;
		}
		if (dorm) {
			*pc = c->next;
			bw_fini(&c->bw);
			free(c);
			num_clients--;
		} else
			pc = &c->next;
	}
}

void
new_client(int fd, int socktype)
{
	struct client *newc;
	int cfd;
	int error;

	newc = malloc(sizeof *newc);
	if (newc == NULL)
		err(1, "malloc");
	check_clients();
	cfd = accept(fd, NULL, NULL);
	if (cfd == -1)
		err(1, "accept");
	shutdown(cfd, SHUT_RD);
	error = bw_init(&newc->bw, cfd, newc->dbuf, sizeof newc->dbuf,
	    newc->recbuf, sizeof(newc->recbuf) / sizeof(*newc->recbuf),
	    socktype == SOCK_STREAM ? BW_STREAMING : BW_RECORDS);
	if (error)
		err(1, "bw_init");
	newc->next = clients;
	clients = newc;
	num_clients++;
	printf("added client: fd %d (%s)\n", cfd,
	    socktype == SOCK_STREAM ? "stream" : "record");
}

void
notify_clients(char *buf)
{
	struct client *c;
	size_t len = strlen(buf);
	enum bw_put_result rv;

	check_clients();
	for (c = clients; c != NULL; c = c->next) {
		rv = bw_put(&c->bw, buf, len);
		if (rv == BW_PUT_OK)
			printf("put to fd %d: ok\n", c->bw.bw_fd);
		else if (rv == BW_PUT_RFULL)
			printf("put to fd %d: rfull\n", c->bw.bw_fd);
		else if (rv == BW_PUT_DFULL)
			printf("put to fd %d: dfull\n", c->bw.bw_fd);
		else if (rv == BW_PUT_ERROR)
			warn("put to fd %d: error", c->bw.bw_fd);
	}
}

int
read_stdin(void)
{
	int rv;
	char buf[512];

	rv = read(0, buf, sizeof(buf) - 1);
	if (rv < 0) {
		if (errno == EINTR)
			return 0;
		err(1, "read(stdin)");
	}
	if (rv == 0) {
		printf("EOF on stdin!\n");
		return 1;
	}
	buf[rv] = 0;
	notify_clients(buf);
	return 0;
}

int main(void)
{
	struct timeval tv;
	fd_set fds;
	int maxfd;
	int rv;

	s1 = create_socket("/tmp/btest.stream", SOCK_STREAM);
	s2 = create_socket("/tmp/btest.pkt", SOCK_SEQPACKET);

	maxfd = s2 + 1;
	for (;;) {
		FD_ZERO(&fds);
		FD_SET(0, &fds);
		FD_SET(s1, &fds);
		FD_SET(s2, &fds);
		tv.tv_sec = 2;
		tv.tv_usec = 0;
		rv = select(maxfd, &fds, NULL, NULL, &tv);
		if (rv == -1) {
			if (errno == EINTR)
				continue;
			err(1, "select");
		} else if (rv == 0)
			check_clients();
		if (FD_ISSET(0, &fds))
			if (read_stdin())
				break;
		if (FD_ISSET(s1, &fds))
			new_client(s1, SOCK_STREAM);
		if (FD_ISSET(s2, &fds))
			new_client(s2, SOCK_SEQPACKET);
	}
	return 0;
}
