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
#ifndef _KERN_LEDGER_H_
#define _KERN_LEDGER_H_

#if 0
#include <kern/lock.h>
#include <ipc/ipc_port.h>
#endif
#include <sys/mach/ipc_types.h>

#define decl_simple_lock_data(a,b)
typedef integer_t ledger_item_t;

#define LEDGER_ITEM_INFINITY	(~0)

struct ledger {
        ipc_port_t	ledger_self;
        ipc_port_t	ledger_service_port;
        ledger_item_t	ledger_balance;
        ledger_item_t	ledger_limit;
        struct ledger	*ledger_ledger;
        struct ledger	*ledger_parent;
	decl_simple_lock_data(,lock)
};

typedef struct ledger ledger_data_t;
typedef struct ledger *ledger_t;

#define LEDGER_NULL ((ledger_t)0)

#define ledger_lock(ledger)	simple_lock(&(ledger)->lock)
#define ledger_unlock(ledger)	simple_unlock(&(ledger)->lock)
#define	ledger_lock_init(ledger) \
	simple_lock_init(&(ledger)->lock, ETAP_MISC_LEDGER)

extern ledger_t	root_wired_ledger;
extern ledger_t	root_paged_ledger;

#define root_wired_ledger_port root_wired_ledger->ledger_self
#define root_paged_ledger_port root_paged_ledger->ledger_self

extern ledger_t convert_port_to_ledger(ipc_port_t);
extern ipc_port_t convert_ledger_to_port(ledger_t);
extern ipc_port_t ledger_copy(ledger_t);

extern kern_return_t ledger_enter(ledger_t, ledger_item_t);
extern void ledger_init(void);

#endif	/* _KERN_LEDGER_H_ */
