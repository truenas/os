#
# Copyright 1991-1998 by Open Software Foundation, Inc. 
#              All Rights Reserved 
#  
# Permission to use, copy, modify, and distribute this software and 
# its documentation for any purpose and without fee is hereby granted, 
# provided that the above copyright notice appears in all copies and 
# that both the copyright notice and this permission notice appear in 
# supporting documentation. 
#  
# OSF DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE 
# INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS 
# FOR A PARTICULAR PURPOSE. 
#  
# IN NO EVENT SHALL OSF BE LIABLE FOR ANY SPECIAL, INDIRECT, OR 
# CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM 
# LOSS OF USE, DATA OR PROFITS, WHETHER IN ACTION OF CONTRACT, 
# NEGLIGENCE, OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION 
# WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. 
# 
# 
# cmk1.1

MK_MAN=no

.include <src.opts.mk>
PROG= mig
SRCS= error.c global.c header.c ident.c \
	lexxer.l mig.c parser.y routine.c server.c \
	statement.c string.c type.c user.c utils.c
#handler.c

CFLAGS+= -I${.CURDIR}/../../include -I${.CURDIR}/../../sys -I${.CURDIR} -DYY_NO_UNPUT -DYY_NO_INPUT


.if ${MACHINE_CPUARCH} == "amd64" || ${MACHINE_CPUARCH} == "i386"
.PATH: i386
CFLAGS+= -I${.CURDIR}/i386
.endif


ident.c:
	echo "#include \"alloc.h\" " >>${.TARGET}
	echo "const char *MigGenerationDate = \""`date`"\";" >>${.TARGET}
	echo "const char *MigMoreData = \""`whoami`"@"`hostname`"\";" >>${.TARGET}

ident1.c: migcom
	echo "char *MigGenerationDate = \""`date`"\";" >${.TARGET}
	echo "char *MigMoreData = \""`whoami`"@"`hostname`"\";" >>${.TARGET}
.include <bsd.prog.mk>
