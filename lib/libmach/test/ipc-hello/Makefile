.include <src.opts.mk>
PROG= ipc-hello
SRCS= ipc-hello.c

MK_MAN=no
CFLAGS+= -g
CFLAGS+= -I. -I../../../../apsl/include 
LDADD+= -lmach -pthread

.include <bsd.prog.mk>
