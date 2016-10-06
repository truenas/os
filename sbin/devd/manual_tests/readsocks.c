#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

int open_socket(const char *, int);
int read_fd(char *, size_t, int fd);
void sigcatch(int sig);

static int s1, s2;

int
open_socket(const char *name, int socktype)
{
	int fd, slen;
	struct sockaddr_un sun;

	if ((fd = socket(PF_LOCAL, socktype, 0)) < 0)
		err(1, "socket");
	bzero(&sun, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strlcpy(sun.sun_path, name, sizeof(sun.sun_path));
	slen = SUN_LEN(&sun);
	if (connect(fd, (struct sockaddr *) &sun, slen) < 0)
		err(1, "connect");
	return (fd);
}

int
read_fd(char *buf, size_t bsize, int fd)
{
	int rv;

	rv = recv(fd, buf, bsize - 1, 0);
	if (rv < 0) {
		if (errno == EINTR)
			return 0;
		err(1, "recv");
	}
	if (rv == 0) {
		printf("EOF!\n");
		return 1;
	}
	buf[rv] = 0;
	return 0;
}

void
sigcatch(int sig)
{
	if (sig == SIGHUP) {
		if (s1 >= 0) {
			printf("closing s1 (packet)\n");
			(void) close(s1);
			s1 = -1;
		}
	} else if (sig == SIGINT) {
		if (s2 >= 0) {
			printf("closing s2 (stream)\n");
			(void) close(s2);
			s2 = -1;
		}
	}
}

int main(void)
{
	int rv;
	int maxfd;
	fd_set fds;
	char buf[512];

	s1 = open_socket("/tmp/btest.pkt", SOCK_SEQPACKET);
	s2 = open_socket("/tmp/btest.stream", SOCK_STREAM);

	maxfd = s2 + 1;

	(void) signal(SIGHUP, sigcatch);
	(void) signal(SIGINT, sigcatch);

	while (s1 >= 0 || s2 >= 0) {
		FD_ZERO(&fds);
		if (s1 >= 0)
			FD_SET(s1, &fds);
		if (s2 >= 0)
			FD_SET(s2, &fds);
		rv = select(maxfd, &fds, NULL, NULL, NULL);
		if (rv < 0) {
			if (errno == EINTR)
				continue;
			err(1, "select");
		}
		if (s1 >= 0 && FD_ISSET(s1, &fds)) {
			rv = read_fd(buf, sizeof buf, s1);
			if (rv == 0)
				printf("received (packet): <%s>\n", buf);
			else {
				(void)close(s1);
				s1 = -1;
			}
		}
		if (s2 >= 0 && FD_ISSET(s2, &fds)) {
			rv = read_fd(buf, sizeof buf, s2);
			if (rv == 0)
				printf("received (stream): <%s>\n", buf);
			else {
				(void)close(s2);
				s2 = -1;
			}
		}
	}
	return 0;
}
