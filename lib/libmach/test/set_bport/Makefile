.include <src.opts.mk>
PROG= set_bport
SRCS= set_bport.c

MK_MAN=no
CFLAGS+= -I. -I../../../../apsl/include 
DEBUG_FLAGS= -g -O0
LDADD+= -lmach -pthread

.include <bsd.prog.mk>
