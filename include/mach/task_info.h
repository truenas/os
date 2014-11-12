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
/* CMU_HIST */
/*
 * Revision 2.4  91/05/14  17:00:41  mrt
 * 	Correcting copyright
 * 
 * Revision 2.3  91/02/05  17:36:25  mrt
 * 	Changed to new Mach copyright
 * 	[91/02/01  17:21:17  mrt]
 * 
 * Revision 2.2  90/05/03  15:48:36  dbg
 * 	Added TASK_THREAD_TIMES_INFO flavor.
 * 	[90/04/03            dbg]
 * 
 * Revision 2.1  89/08/03  16:04:49  rwd
 * Created.
 * 
 * Revision 2.3  89/02/25  18:41:06  gm0w
 * 	Changes for cleanup.
 * 
 * 15-Jan-88  David Golub (dbg) at Carnegie-Mellon University
 *	Created, based on old task_statistics.
 *
 */
/* CMU_ENDHIST */
/* 
 * Mach Operating System
 * Copyright (c) 1991,1990,1989,1988,1987 Carnegie Mellon University
 * All Rights Reserved.
 * 
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 * 
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 * 
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */
/*
 */
/*
 *	Machine-independent task information structures and definitions.
 *
 *	The definitions in this file are exported to the user.  The kernel
 *	will translate its internal data structures to these structures
 *	as appropriate.
 *
 */

#ifndef	TASK_INFO_H_
#define	TASK_INFO_H_

#include <machine/mach/vm_types.h>
#include <mach/time_value.h>
#include <mach/policy.h>

/*
 *	Generic information structure to allow for expansion.
 */
typedef	natural_t	task_flavor_t;
typedef	integer_t	*task_info_t;		/* varying array of int */

#define	TASK_INFO_MAX	(1024)		/* maximum array size */
typedef	integer_t	task_info_data_t[TASK_INFO_MAX];

/*
 *	Currently defined information structures.
 */

#define TASK_BASIC_INFO         4       /* basic information */

struct task_basic_info {
        integer_t       suspend_count;  /* suspend count for task */
        vm_size_t       virtual_size;   /* number of virtual pages */
        vm_size_t       resident_size;  /* number of resident pages */
        time_value_t    user_time;      /* total user run time for
                                           terminated threads */
        time_value_t    system_time;    /* total system run time for
                                           terminated threads */
	policy_t	policy;		/* default policy for new threads */
};

typedef struct task_basic_info          task_basic_info_data_t;
typedef struct task_basic_info          *task_basic_info_t;
#define TASK_BASIC_INFO_COUNT   \
                (sizeof(task_basic_info_data_t) / sizeof(natural_t))


#define	TASK_EVENTS_INFO	2	/* various event counts */

struct task_events_info {
	natural_t	faults;		/* number of page faults */
	natural_t 	zero_fills;	/* number of zero fill pages */
	natural_t 	reactivations;	/* number of reactivated pages */
	natural_t 	pageins;	/* number of actual pageins */
	natural_t 	cow_faults;	/* number of copy-on-write faults */
	natural_t 	messages_sent;	/* number of messages sent */
	natural_t 	messages_received; /* number of messages received */
};
typedef struct task_events_info		task_events_info_data_t;
typedef struct task_events_info		*task_events_info_t;
#define	TASK_EVENTS_INFO_COUNT	\
		(sizeof(task_events_info_data_t) / sizeof(natural_t))

#define	TASK_THREAD_TIMES_INFO	3	/* total times for live threads -
					   only accurate if suspended */

struct task_thread_times_info {
	time_value_t	user_time;	/* total user run time for
					   live threads */
	time_value_t	system_time;	/* total system run time for
					   live threads */
};

typedef struct task_thread_times_info	task_thread_times_info_data_t;
typedef struct task_thread_times_info	*task_thread_times_info_t;
#define	TASK_THREAD_TIMES_INFO_COUNT	\
		(sizeof(task_thread_times_info_data_t) / sizeof(natural_t))

#define TASK_USER_DATA		5	/* arbitrary user data settable
					   via IPC */

struct task_user_data {
	void 	*user_data;
};

typedef struct task_user_data	task_user_data_data_t;
typedef struct task_user_data	*task_user_data_t;
#define TASK_USER_DATA_COUNT	\
		(sizeof(task_user_data_data_t) / sizeof(natural_t))

#define TASK_SCHED_TIMESHARE_INFO	10
#define TASK_SCHED_RR_INFO		11
#define TASK_SCHED_FIFO_INFO		12

#define TASK_SECURITY_TOKEN		13
#define TASK_SECURITY_TOKEN_COUNT	\
		(sizeof(security_token_t) / sizeof(natural_t))

#endif	/* TASK_INFO_H_ */
