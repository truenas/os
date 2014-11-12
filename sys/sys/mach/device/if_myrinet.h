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
 */
/*
 * MkLinux
 */

#ifndef _DEVICE_IF_MYRINET_H_
#define	_DEVICE_IF_MYRINET_H_

#include <device/if_ether.h>

/*
 * Current implementation constants
 */
#define	IF_MYRINET_MID_SIZE		8	/* Myrinet ID size */
#define	IF_MYRINET_UID_SIZE		6	/* Myrinet UID size */

/*
 * Structure of an EEP (Ethernet Encapsulated Packet) Myrinet header
 */
struct if_myrinet_eep {
    unsigned char		myri_size;	/* MYRINET padding size */
    unsigned char  		myri_pad;	/* MYRINET padding value */
    struct ether_header		myri_ether;	/* Ethernet header */	
};

#define	IF_MYRINET_EEP_PAD_SIZE		2	/* Myrinet Pad size */
#define	IF_MYRINET_EEP_PAD_VALUE     0xAB	/* Myrinet Pad value */
#define	IF_MYRINET_EEP_MTU    (8192 + 256 - 16)	/* Myrinet EEP MTU */

#endif /* _DEVICE_IF_MYRINET_H_ */
