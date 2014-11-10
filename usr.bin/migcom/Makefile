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

SETUP_PROGRAMS		= migcom migcom1

PROGRAMS		= migcom migcom1

ILIST			= migcom  ${INCLUDES}
IDIR			= ${MACH3_LIBRARY_IDIR} 

VPATH			= ${MACHINE}
#VPATH			= ${${TARGET_MACHINE}_VPATH} 

DATAFILES		= alloc.h      global.h     routine.h    strdefs.h    utils.h \
				  error.h      lexxer.h     statement.h  type.h       write.h   

INCLUDES        = 
EXPDIR          = ${MACH3_INCLUDE_IDIR}lib/ 

.ifdef	LINUX
NO_STARTFILES		=
NO_STRICT_ANSI		=
.undef	FLEXSKEL
.endif

USE_STATIC_LIBRARIES    =
DEPENDENCIES			=
CFLAGS					= -D_ANSI_C_SOURCE  
INCFLAGS				= -I${MACHINE}  -I${EXPORTBASE}/include

.if ${TARGET_MACHINE} == "ALPHA"
LDFLAGS					= -non_shared
.endif
error.o_CFLAGS			= ${CFLAGS} ${NO_VFPRINTF:D-DNO_VFPRINTF}
COMMON_OFILES			= parser.o lexxer.o mig.o \
			  error.o string.o type.o routine.o \
			  statement.o global.o \
			  header.o user.o server.o utils.o
migcom1_OFILES			= ${COMMON_OFILES} ident1.o
migcom_OFILES			= ${COMMON_OFILES} ident.o
.ifdef	LINUX
LIBS					= -lfl
.else
LIBS					= -ll
.endif
GARBAGE					= parser.c parser.h ident.c

.if exists(${TARGET_MACHINE}/machdep.mk)
.include "${TARGET_MACHINE}/machdep.mk"
.endif
.include <${RULES_MK}>

lexxer.o routine.o type.o server.o user.o:	mig_machine.h

ident.c:
	echo "char *MigGenerationDate = \""`date`"\";" >${.TARGET}
	echo "char *MigMoreData = \""`whoami`"@"`hostname`"\";" >>${.TARGET}

ident1.c: migcom
	echo "char *MigGenerationDate = \""`date`"\";" >${.TARGET}
	echo "char *MigMoreData = \""`whoami`"@"`hostname`"\";" >>${.TARGET}

# force generation of intermediate .c files
lexxer.o: lexxer.c parser.h
parser.o: parser.c

# cause the 2 yacc passes to be serialized.
.ORDER: parser.c parser.h

.if exists(depend.mk)
.include "depend.mk"
.endif
