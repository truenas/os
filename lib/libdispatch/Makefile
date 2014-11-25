.include <bsd.init.mk>

LIB= dispatch

.PATH: src
.PATH: resolver

CFLAGS+=	-I./os -I./private -I${.CURDIR} -I${.CURDIR}/../../include -I${.CURDIR}/../../sys
CFLAGS+=	-I${.CURDIR}/../../apsl/include -fblocks
CFLAGS+=	-D__APPLE__ -DTARGET_CPU_X86=1 -DDISPATCH_USE_DTRACE=0

protocolServer.c protocol.h protocolServer.h:
	mig -user /dev/null -server protocolServer.c -header protocol.h \
	-sheader protocolServer.h ${.CURDIR}/src/protocol.defs

SRCS =  			\
	protocolServer.c \
	allocator.c		\
	apply.c			\
	benchmark.c		\
	data.c			\
	init.c			\
	introspection.c	\
	io.c			\
	object.c		\
	once.c			\
	queue.c			\
	semaphore.c		\
	source.c		\
	time.c			\
	transform.c

SRCS+=	resolver.c

# we currently don't support voucher ports
# voucher.c

clean:
	rm -f *.o *.So *.po *~ protocol.h protocolServer.c protocolServer.h
.include <bsd.lib.mk>
