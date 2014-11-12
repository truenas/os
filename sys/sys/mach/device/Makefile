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
# MkLinux

MIG_DEFS		= device.defs device_reply.defs device_request.defs
DATAFILES		= device_types.h net_status.h param.h tty_status.h \
			  tape_status.h device_types.defs ds_status.h\
			  test_device_status.h disk_status.h cdrom_status.h \
			  scsi_info.h \
			  ${MIG_DEFS}

INCLUDES		= ${DATAFILES} ${_MIG_HDRS_}
INCFLAGS		= -I..

ILIST			= ${INCLUDES}
IDIR			= ${MACH3_INCLUDE_IDIR}device/
IMODE			= 644

.include <${RULES_MK}>

${_MIG_HDRS_}: device_types.defs
