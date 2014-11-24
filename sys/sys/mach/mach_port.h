
#ifdef _KERNEL
#ifndef	_mach_port_user_
#define	_mach_port_user_

/* Module mach_port */

#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/mach/ndr.h>
#include <sys/mach/kern_return.h>
#include <sys/mach/notify.h>
#include <sys/mach/mach_types.h>
#include <sys/mach/message.h>
#include <sys/mach/mig_errors.h>
#include <sys/mach/port.h>

#ifdef AUTOTEST
#ifndef FUNCTION_PTR_T
#define FUNCTION_PTR_T
typedef void (*function_ptr_t)(mach_port_t, char *, mach_msg_type_number_t);
typedef struct {
        char            *name;
        function_ptr_t  function;
} function_table_entry;
typedef function_table_entry 	*function_table_t;
#endif /* FUNCTION_PTR_T */
#endif /* AUTOTEST */

#ifndef	mach_port_MSG_COUNT
#define	mach_port_MSG_COUNT	36
#endif	/* mach_port_MSG_COUNT */

#include <sys/mach/std_types.h>
#include <sys/mach/mig.h>
#include <sys/mach/mach_types.h>
#include <sys/mach_debug/mach_debug_types.h>

#ifdef __BeforeMigUserHeader
__BeforeMigUserHeader
#endif /* __BeforeMigUserHeader */


/* Routine mach_port_names */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
kern_return_t _kernelrpc_mach_port_names
#if	defined(LINTLIBRARY)
    (task, names, namesCnt, types, typesCnt)
	ipc_space_t task;
	mach_port_name_array_t *names;
	mach_msg_type_number_t *namesCnt;
	mach_port_type_array_t *types;
	mach_msg_type_number_t *typesCnt;
{ return _kernelrpc_mach_port_names(task, names, namesCnt, types, typesCnt); }
#else
(
	ipc_space_t task,
	mach_port_name_array_t *names,
	mach_msg_type_number_t *namesCnt,
	mach_port_type_array_t *types,
	mach_msg_type_number_t *typesCnt
);
#endif	/* defined(LINTLIBRARY) */

/* Routine mach_port_type */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
kern_return_t _kernelrpc_mach_port_type
#if	defined(LINTLIBRARY)
    (task, name, ptype)
	ipc_space_t task;
	mach_port_name_t name;
	mach_port_type_t *ptype;
{ return _kernelrpc_mach_port_type(task, name, ptype); }
#else
(
	ipc_space_t task,
	mach_port_name_t name,
	mach_port_type_t *ptype
);
#endif	/* defined(LINTLIBRARY) */

/* Routine mach_port_rename */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
kern_return_t _kernelrpc_mach_port_rename
#if	defined(LINTLIBRARY)
    (task, old_name, new_name)
	ipc_space_t task;
	mach_port_name_t old_name;
	mach_port_name_t new_name;
{ return _kernelrpc_mach_port_rename(task, old_name, new_name); }
#else
(
	ipc_space_t task,
	mach_port_name_t old_name,
	mach_port_name_t new_name
);
#endif	/* defined(LINTLIBRARY) */

/* Routine mach_port_allocate_name */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
kern_return_t _kernelrpc_mach_port_allocate_name
#if	defined(LINTLIBRARY)
    (task, right, name)
	ipc_space_t task;
	mach_port_right_t right;
	mach_port_name_t name;
{ return _kernelrpc_mach_port_allocate_name(task, right, name); }
#else
(
	ipc_space_t task,
	mach_port_right_t right,
	mach_port_name_t name
);
#endif	/* defined(LINTLIBRARY) */

/* Routine mach_port_allocate */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
kern_return_t _kernelrpc_mach_port_allocate
#if	defined(LINTLIBRARY)
    (task, right, name)
	ipc_space_t task;
	mach_port_right_t right;
	mach_port_name_t *name;
{ return _kernelrpc_mach_port_allocate(task, right, name); }
#else
(
	ipc_space_t task,
	mach_port_right_t right,
	mach_port_name_t *name
);
#endif	/* defined(LINTLIBRARY) */

/* Routine mach_port_destroy */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
kern_return_t _kernelrpc_mach_port_destroy
#if	defined(LINTLIBRARY)
    (task, name)
	ipc_space_t task;
	mach_port_name_t name;
{ return _kernelrpc_mach_port_destroy(task, name); }
#else
(
	ipc_space_t task,
	mach_port_name_t name
);
#endif	/* defined(LINTLIBRARY) */

/* Routine mach_port_deallocate */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
kern_return_t _kernelrpc_mach_port_deallocate
#if	defined(LINTLIBRARY)
    (task, name)
	ipc_space_t task;
	mach_port_name_t name;
{ return _kernelrpc_mach_port_deallocate(task, name); }
#else
(
	ipc_space_t task,
	mach_port_name_t name
);
#endif	/* defined(LINTLIBRARY) */

/* Routine mach_port_get_refs */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
kern_return_t _kernelrpc_mach_port_get_refs
#if	defined(LINTLIBRARY)
    (task, name, right, refs)
	ipc_space_t task;
	mach_port_name_t name;
	mach_port_right_t right;
	mach_port_urefs_t *refs;
{ return _kernelrpc_mach_port_get_refs(task, name, right, refs); }
#else
(
	ipc_space_t task,
	mach_port_name_t name,
	mach_port_right_t right,
	mach_port_urefs_t *refs
);
#endif	/* defined(LINTLIBRARY) */

/* Routine mach_port_mod_refs */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
kern_return_t _kernelrpc_mach_port_mod_refs
#if	defined(LINTLIBRARY)
    (task, name, right, delta)
	ipc_space_t task;
	mach_port_name_t name;
	mach_port_right_t right;
	mach_port_delta_t delta;
{ return _kernelrpc_mach_port_mod_refs(task, name, right, delta); }
#else
(
	ipc_space_t task,
	mach_port_name_t name,
	mach_port_right_t right,
	mach_port_delta_t delta
);
#endif	/* defined(LINTLIBRARY) */

/* Routine mach_port_peek */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
kern_return_t _kernelrpc_mach_port_peek
#if	defined(LINTLIBRARY)
    (task, name, trailer_type, request_seqnop, msg_sizep, msg_idp, trailer_infop, trailer_infopCnt)
	ipc_space_t task;
	mach_port_name_t name;
	mach_msg_trailer_type_t trailer_type;
	mach_port_seqno_t *request_seqnop;
	mach_msg_size_t *msg_sizep;
	mach_msg_id_t *msg_idp;
	mach_msg_trailer_info_t trailer_infop;
	mach_msg_type_number_t *trailer_infopCnt;
{ return _kernelrpc_mach_port_peek(task, name, trailer_type, request_seqnop, msg_sizep, msg_idp, trailer_infop, trailer_infopCnt); }
#else
(
	ipc_space_t task,
	mach_port_name_t name,
	mach_msg_trailer_type_t trailer_type,
	mach_port_seqno_t *request_seqnop,
	mach_msg_size_t *msg_sizep,
	mach_msg_id_t *msg_idp,
	mach_msg_trailer_info_t trailer_infop,
	mach_msg_type_number_t *trailer_infopCnt
);
#endif	/* defined(LINTLIBRARY) */

/* Routine mach_port_set_mscount */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
kern_return_t _kernelrpc_mach_port_set_mscount
#if	defined(LINTLIBRARY)
    (task, name, mscount)
	ipc_space_t task;
	mach_port_name_t name;
	mach_port_mscount_t mscount;
{ return _kernelrpc_mach_port_set_mscount(task, name, mscount); }
#else
(
	ipc_space_t task,
	mach_port_name_t name,
	mach_port_mscount_t mscount
);
#endif	/* defined(LINTLIBRARY) */

/* Routine mach_port_get_set_status */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
kern_return_t _kernelrpc_mach_port_get_set_status
#if	defined(LINTLIBRARY)
    (task, name, members, membersCnt)
	ipc_space_t task;
	mach_port_name_t name;
	mach_port_name_array_t *members;
	mach_msg_type_number_t *membersCnt;
{ return _kernelrpc_mach_port_get_set_status(task, name, members, membersCnt); }
#else
(
	ipc_space_t task,
	mach_port_name_t name,
	mach_port_name_array_t *members,
	mach_msg_type_number_t *membersCnt
);
#endif	/* defined(LINTLIBRARY) */

/* Routine mach_port_move_member */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
kern_return_t _kernelrpc_mach_port_move_member
#if	defined(LINTLIBRARY)
    (task, member, after)
	ipc_space_t task;
	mach_port_name_t member;
	mach_port_name_t after;
{ return _kernelrpc_mach_port_move_member(task, member, after); }
#else
(
	ipc_space_t task,
	mach_port_name_t member,
	mach_port_name_t after
);
#endif	/* defined(LINTLIBRARY) */

/* Routine mach_port_request_notification */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
kern_return_t _kernelrpc_mach_port_request_notification
#if	defined(LINTLIBRARY)
    (task, name, msgid, sync, notify, notifyPoly, previous)
	ipc_space_t task;
	mach_port_name_t name;
	mach_msg_id_t msgid;
	mach_port_mscount_t sync;
	mach_port_t notify;
	mach_msg_type_name_t notifyPoly;
	mach_port_t *previous;
{ return _kernelrpc_mach_port_request_notification(task, name, msgid, sync, notify, notifyPoly, previous); }
#else
(
	ipc_space_t task,
	mach_port_name_t name,
	mach_msg_id_t msgid,
	mach_port_mscount_t sync,
	mach_port_t notify,
	mach_msg_type_name_t notifyPoly,
	mach_port_t *previous
);
#endif	/* defined(LINTLIBRARY) */

/* Routine mach_port_insert_right */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
kern_return_t _kernelrpc_mach_port_insert_right
#if	defined(LINTLIBRARY)
    (task, name, poly, polyPoly)
	ipc_space_t task;
	mach_port_name_t name;
	mach_port_t poly;
	mach_msg_type_name_t polyPoly;
{ return _kernelrpc_mach_port_insert_right(task, name, poly, polyPoly); }
#else
(
	ipc_space_t task,
	mach_port_name_t name,
	mach_port_t poly,
	mach_msg_type_name_t polyPoly
);
#endif	/* defined(LINTLIBRARY) */

/* Routine mach_port_extract_right */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
kern_return_t _kernelrpc_mach_port_extract_right
#if	defined(LINTLIBRARY)
    (task, name, msgt_name, poly, polyPoly)
	ipc_space_t task;
	mach_port_name_t name;
	mach_msg_type_name_t msgt_name;
	mach_port_t *poly;
	mach_msg_type_name_t *polyPoly;
{ return _kernelrpc_mach_port_extract_right(task, name, msgt_name, poly, polyPoly); }
#else
(
	ipc_space_t task,
	mach_port_name_t name,
	mach_msg_type_name_t msgt_name,
	mach_port_t *poly,
	mach_msg_type_name_t *polyPoly
);
#endif	/* defined(LINTLIBRARY) */

/* Routine mach_port_set_seqno */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
kern_return_t _kernelrpc_mach_port_set_seqno
#if	defined(LINTLIBRARY)
    (task, name, seqno)
	ipc_space_t task;
	mach_port_name_t name;
	mach_port_seqno_t seqno;
{ return _kernelrpc_mach_port_set_seqno(task, name, seqno); }
#else
(
	ipc_space_t task,
	mach_port_name_t name,
	mach_port_seqno_t seqno
);
#endif	/* defined(LINTLIBRARY) */

/* Routine mach_port_get_attributes */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
kern_return_t _kernelrpc_mach_port_get_attributes
#if	defined(LINTLIBRARY)
    (task, name, flavor, port_info_out, port_info_outCnt)
	ipc_space_t task;
	mach_port_name_t name;
	mach_port_flavor_t flavor;
	mach_port_info_t port_info_out;
	mach_msg_type_number_t *port_info_outCnt;
{ return _kernelrpc_mach_port_get_attributes(task, name, flavor, port_info_out, port_info_outCnt); }
#else
(
	ipc_space_t task,
	mach_port_name_t name,
	mach_port_flavor_t flavor,
	mach_port_info_t port_info_out,
	mach_msg_type_number_t *port_info_outCnt
);
#endif	/* defined(LINTLIBRARY) */

/* Routine mach_port_set_attributes */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
kern_return_t _kernelrpc_mach_port_set_attributes
#if	defined(LINTLIBRARY)
    (task, name, flavor, port_info, port_infoCnt)
	ipc_space_t task;
	mach_port_name_t name;
	mach_port_flavor_t flavor;
	mach_port_info_t port_info;
	mach_msg_type_number_t port_infoCnt;
{ return _kernelrpc_mach_port_set_attributes(task, name, flavor, port_info, port_infoCnt); }
#else
(
	ipc_space_t task,
	mach_port_name_t name,
	mach_port_flavor_t flavor,
	mach_port_info_t port_info,
	mach_msg_type_number_t port_infoCnt
);
#endif	/* defined(LINTLIBRARY) */

/* Routine mach_port_allocate_qos */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
kern_return_t _kernelrpc_mach_port_allocate_qos
#if	defined(LINTLIBRARY)
    (task, right, qos, name)
	ipc_space_t task;
	mach_port_right_t right;
	mach_port_qos_t *qos;
	mach_port_name_t *name;
{ return _kernelrpc_mach_port_allocate_qos(task, right, qos, name); }
#else
(
	ipc_space_t task,
	mach_port_right_t right,
	mach_port_qos_t *qos,
	mach_port_name_t *name
);
#endif	/* defined(LINTLIBRARY) */

/* Routine mach_port_allocate_full */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
kern_return_t _kernelrpc_mach_port_allocate_full
#if	defined(LINTLIBRARY)
    (task, right, proto, qos, name)
	ipc_space_t task;
	mach_port_right_t right;
	mach_port_t proto;
	mach_port_qos_t *qos;
	mach_port_name_t *name;
{ return _kernelrpc_mach_port_allocate_full(task, right, proto, qos, name); }
#else
(
	ipc_space_t task,
	mach_port_right_t right,
	mach_port_t proto,
	mach_port_qos_t *qos,
	mach_port_name_t *name
);
#endif	/* defined(LINTLIBRARY) */

/* Routine task_set_port_space */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
kern_return_t _kernelrpc_task_set_port_space
#if	defined(LINTLIBRARY)
    (task, table_entries)
	ipc_space_t task;
	int table_entries;
{ return _kernelrpc_task_set_port_space(task, table_entries); }
#else
(
	ipc_space_t task,
	int table_entries
);
#endif	/* defined(LINTLIBRARY) */

/* Routine mach_port_get_srights */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
kern_return_t _kernelrpc_mach_port_get_srights
#if	defined(LINTLIBRARY)
    (task, name, srights)
	ipc_space_t task;
	mach_port_name_t name;
	mach_port_rights_t *srights;
{ return _kernelrpc_mach_port_get_srights(task, name, srights); }
#else
(
	ipc_space_t task,
	mach_port_name_t name,
	mach_port_rights_t *srights
);
#endif	/* defined(LINTLIBRARY) */

/* Routine mach_port_space_info */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
kern_return_t _kernelrpc_mach_port_space_info
#if	defined(LINTLIBRARY)
    (task, space_info, table_info, table_infoCnt, tree_info, tree_infoCnt)
	ipc_space_t task;
	ipc_info_space_t *space_info;
	ipc_info_name_array_t *table_info;
	mach_msg_type_number_t *table_infoCnt;
	ipc_info_tree_name_array_t *tree_info;
	mach_msg_type_number_t *tree_infoCnt;
{ return _kernelrpc_mach_port_space_info(task, space_info, table_info, table_infoCnt, tree_info, tree_infoCnt); }
#else
(
	ipc_space_t task,
	ipc_info_space_t *space_info,
	ipc_info_name_array_t *table_info,
	mach_msg_type_number_t *table_infoCnt,
	ipc_info_tree_name_array_t *tree_info,
	mach_msg_type_number_t *tree_infoCnt
);
#endif	/* defined(LINTLIBRARY) */

/* Routine mach_port_dnrequest_info */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
kern_return_t _kernelrpc_mach_port_dnrequest_info
#if	defined(LINTLIBRARY)
    (task, name, dnr_total, dnr_used)
	ipc_space_t task;
	mach_port_name_t name;
	unsigned *dnr_total;
	unsigned *dnr_used;
{ return _kernelrpc_mach_port_dnrequest_info(task, name, dnr_total, dnr_used); }
#else
(
	ipc_space_t task,
	mach_port_name_t name,
	unsigned *dnr_total,
	unsigned *dnr_used
);
#endif	/* defined(LINTLIBRARY) */

/* Routine mach_port_kernel_object */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
kern_return_t _kernelrpc_mach_port_kernel_object
#if	defined(LINTLIBRARY)
    (task, name, object_type, object_addr)
	ipc_space_t task;
	mach_port_name_t name;
	unsigned *object_type;
	unsigned *object_addr;
{ return _kernelrpc_mach_port_kernel_object(task, name, object_type, object_addr); }
#else
(
	ipc_space_t task,
	mach_port_name_t name,
	unsigned *object_type,
	unsigned *object_addr
);
#endif	/* defined(LINTLIBRARY) */

/* Routine mach_port_insert_member */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
kern_return_t _kernelrpc_mach_port_insert_member
#if	defined(LINTLIBRARY)
    (task, name, pset)
	ipc_space_t task;
	mach_port_name_t name;
	mach_port_name_t pset;
{ return _kernelrpc_mach_port_insert_member(task, name, pset); }
#else
(
	ipc_space_t task,
	mach_port_name_t name,
	mach_port_name_t pset
);
#endif	/* defined(LINTLIBRARY) */

/* Routine mach_port_extract_member */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
kern_return_t _kernelrpc_mach_port_extract_member
#if	defined(LINTLIBRARY)
    (task, name, pset)
	ipc_space_t task;
	mach_port_name_t name;
	mach_port_name_t pset;
{ return _kernelrpc_mach_port_extract_member(task, name, pset); }
#else
(
	ipc_space_t task,
	mach_port_name_t name,
	mach_port_name_t pset
);
#endif	/* defined(LINTLIBRARY) */

/* Routine mach_port_get_context */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
kern_return_t _kernelrpc_mach_port_get_context
#if	defined(LINTLIBRARY)
    (task, name, context)
	ipc_space_t task;
	mach_port_name_t name;
	mach_vm_address_t *context;
{ return _kernelrpc_mach_port_get_context(task, name, context); }
#else
(
	ipc_space_t task,
	mach_port_name_t name,
	mach_vm_address_t *context
);
#endif	/* defined(LINTLIBRARY) */

/* Routine mach_port_set_context */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
kern_return_t _kernelrpc_mach_port_set_context
#if	defined(LINTLIBRARY)
    (task, name, context)
	ipc_space_t task;
	mach_port_name_t name;
	mach_vm_address_t context;
{ return _kernelrpc_mach_port_set_context(task, name, context); }
#else
(
	ipc_space_t task,
	mach_port_name_t name,
	mach_vm_address_t context
);
#endif	/* defined(LINTLIBRARY) */

/* Routine mach_port_kobject */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
kern_return_t _kernelrpc_mach_port_kobject
#if	defined(LINTLIBRARY)
    (task, name, object_type, object_addr)
	ipc_space_t task;
	mach_port_name_t name;
	natural_t *object_type;
	mach_vm_address_t *object_addr;
{ return _kernelrpc_mach_port_kobject(task, name, object_type, object_addr); }
#else
(
	ipc_space_t task,
	mach_port_name_t name,
	natural_t *object_type,
	mach_vm_address_t *object_addr
);
#endif	/* defined(LINTLIBRARY) */

/* Routine mach_port_construct */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
kern_return_t _kernelrpc_mach_port_construct
#if	defined(LINTLIBRARY)
    (task, options, context, name)
	ipc_space_t task;
	mach_port_options_ptr_t options;
	uint64_t context;
	mach_port_name_t *name;
{ return _kernelrpc_mach_port_construct(task, options, context, name); }
#else
(
	ipc_space_t task,
	mach_port_options_ptr_t options,
	uint64_t context,
	mach_port_name_t *name
);
#endif	/* defined(LINTLIBRARY) */

/* Routine mach_port_destruct */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
kern_return_t _kernelrpc_mach_port_destruct
#if	defined(LINTLIBRARY)
    (task, name, srdelta, guard)
	ipc_space_t task;
	mach_port_name_t name;
	mach_port_delta_t srdelta;
	uint64_t guard;
{ return _kernelrpc_mach_port_destruct(task, name, srdelta, guard); }
#else
(
	ipc_space_t task,
	mach_port_name_t name,
	mach_port_delta_t srdelta,
	uint64_t guard
);
#endif	/* defined(LINTLIBRARY) */

/* Routine mach_port_guard */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
kern_return_t _kernelrpc_mach_port_guard
#if	defined(LINTLIBRARY)
    (task, name, guard, strict)
	ipc_space_t task;
	mach_port_name_t name;
	uint64_t guard;
	boolean_t strict;
{ return _kernelrpc_mach_port_guard(task, name, guard, strict); }
#else
(
	ipc_space_t task,
	mach_port_name_t name,
	uint64_t guard,
	boolean_t strict
);
#endif	/* defined(LINTLIBRARY) */

/* Routine mach_port_unguard */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
kern_return_t _kernelrpc_mach_port_unguard
#if	defined(LINTLIBRARY)
    (task, name, guard)
	ipc_space_t task;
	mach_port_name_t name;
	uint64_t guard;
{ return _kernelrpc_mach_port_unguard(task, name, guard); }
#else
(
	ipc_space_t task,
	mach_port_name_t name,
	uint64_t guard
);
#endif	/* defined(LINTLIBRARY) */

/* Routine mach_port_space_basic_info */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
kern_return_t _kernelrpc_mach_port_space_basic_info
#if	defined(LINTLIBRARY)
    (task, basic_info)
	ipc_space_t task;
	ipc_info_space_basic_t *basic_info;
{ return _kernelrpc_mach_port_space_basic_info(task, basic_info); }
#else
(
	ipc_space_t task,
	ipc_info_space_basic_t *basic_info
);
#endif	/* defined(LINTLIBRARY) */

#ifndef subsystem_to_name_map_mach_port
#define subsystem_to_name_map_mach_port \
    { "mach_port_names", 3200 },\
    { "mach_port_type", 3201 },\
    { "mach_port_rename", 3202 },\
    { "mach_port_allocate_name", 3203 },\
    { "mach_port_allocate", 3204 },\
    { "mach_port_destroy", 3205 },\
    { "mach_port_deallocate", 3206 },\
    { "mach_port_get_refs", 3207 },\
    { "mach_port_mod_refs", 3208 },\
    { "mach_port_peek", 3209 },\
    { "mach_port_set_mscount", 3210 },\
    { "mach_port_get_set_status", 3211 },\
    { "mach_port_move_member", 3212 },\
    { "mach_port_request_notification", 3213 },\
    { "mach_port_insert_right", 3214 },\
    { "mach_port_extract_right", 3215 },\
    { "mach_port_set_seqno", 3216 },\
    { "mach_port_get_attributes", 3217 },\
    { "mach_port_set_attributes", 3218 },\
    { "mach_port_allocate_qos", 3219 },\
    { "mach_port_allocate_full", 3220 },\
    { "task_set_port_space", 3221 },\
    { "mach_port_get_srights", 3222 },\
    { "mach_port_space_info", 3223 },\
    { "mach_port_dnrequest_info", 3224 },\
    { "mach_port_kernel_object", 3225 },\
    { "mach_port_insert_member", 3226 },\
    { "mach_port_extract_member", 3227 },\
    { "mach_port_get_context", 3228 },\
    { "mach_port_set_context", 3229 },\
    { "mach_port_kobject", 3230 },\
    { "mach_port_construct", 3231 },\
    { "mach_port_destruct", 3232 },\
    { "mach_port_guard", 3233 },\
    { "mach_port_unguard", 3234 },\
    { "mach_port_space_basic_info", 3235 }
#endif

#ifdef __AfterMigUserHeader
__AfterMigUserHeader
#endif /* __AfterMigUserHeader */

#endif	 /* _mach_port_user_ */
#endif
