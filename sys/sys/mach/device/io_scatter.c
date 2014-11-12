/*
 * Copyright 1991-1998 by Open Software Foundation, Inc. 
 *              All Rights Reserved 
 *  
 * Permission to use, copy, modify, and distribute this software and 
 * its documentation for any purpose and without fee is hereby granted, 
 * provided that the above copyright notice appears in all copies and 
 * that both the copyright notice and this permission notice appear in 
 * supporting documentation. 
 *  
 * OSF DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE 
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS 
 * FOR A PARTICULAR PURPOSE. 
 *  
 * IN NO EVENT SHALL OSF BE LIABLE FOR ANY SPECIAL, INDIRECT, OR 
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM 
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN ACTION OF CONTRACT, 
 * NEGLIGENCE, OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION 
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. 
 * 
 */
/*
 * MkLinux
 */

#include <mach/std_types.h>
#include <device/io_scatter.h>
#include <kern/misc_protos.h>

void
ios_copy(io_scatter_t from, io_scatter_t to, vm_offset_t size)
{
	vm_offset_t from_offset, to_offset, wsize;
	from_offset = to_offset = 0;
	while (size) {
		if (from->ios_length - from_offset >
		    to->ios_length - to_offset) {
			wsize = to->ios_length - to_offset;
		} else {
			wsize = from->ios_length - from_offset;
		}
		bcopy((const char *)(from->ios_address + from_offset),
		      (char *)(to->ios_address + to_offset), wsize);
		from_offset += wsize;
		if (from_offset == from->ios_length) {
			from_offset = 0;
			from++;
		}
		to_offset += wsize;
		if (to_offset == to->ios_length) {
			to_offset = 0;
			to++;
		}
		size -= wsize;
	}
}

void
ios_copy_from(io_scatter_t from, char *to, vm_offset_t size)
{
	vm_offset_t wsize;
	while (size) {
		wsize = (from->ios_length > size)?size:from->ios_length;
		bcopy((const char *)(from->ios_address), to, wsize);
		size -= wsize;
		from++;
	}
}

void
ios_copy_to(char *from, io_scatter_t to, vm_offset_t size)
{
	vm_offset_t wsize;
	while (size) {
		wsize = (to->ios_length > size)?size:to->ios_length;
		bcopy((const char *)from, (char *)(to->ios_address), wsize);
		size -= wsize;
		to++;
	}
}
