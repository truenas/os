#ifndef _MACH_MACH_PORT_H_
#define _MACH_MACH_PORT_H_
#include <sys/mach/mach_port.h>

extern kern_return_t mach_port_allocate(
				mach_port_name_t target,
				mach_port_right_t right,
				mach_port_name_t *name
	);

extern kern_return_t mach_port_destroy(
				mach_port_name_t target,
				mach_port_name_t name
);

extern kern_return_t mach_port_deallocate(
				mach_port_name_t target,
				mach_port_name_t name
);

extern kern_return_t mach_port_mod_refs(
				mach_port_name_t target,
				mach_port_name_t name,
				mach_port_right_t right,
				mach_port_delta_t delta
);

extern kern_return_t mach_port_move_member(
				mach_port_name_t target,
				mach_port_name_t member,
				mach_port_name_t after
);

extern kern_return_t mach_port_insert_right(
				mach_port_name_t target,
				mach_port_name_t name,
				mach_port_name_t poly,
				mach_msg_type_name_t polyPoly
);

extern kern_return_t mach_port_extract_right(	
				mach_port_name_t target,
				mach_port_name_t name,
				mach_msg_type_name_t msgt_name,
				mach_port_name_t *poly,
				mach_msg_type_name_t *polyPoly
);

extern kern_return_t mach_port_insert_member(
				mach_port_name_t target,
				mach_port_name_t name,
				mach_port_name_t pset
);

extern kern_return_t mach_port_extract_member(
				mach_port_name_t target,
				mach_port_name_t name,
				mach_port_name_t pset
);

extern kern_return_t mach_port_construct(
				mach_port_name_t target,
				mach_port_options_t *options,
				uint64_t context,
				mach_port_name_t *name
);

extern kern_return_t mach_port_destruct(
				mach_port_name_t target,
				mach_port_name_t name,
				mach_port_delta_t srdelta,
				uint64_t guard
);

extern kern_return_t mach_port_guard(
				mach_port_name_t target,
				mach_port_name_t name,
				uint64_t guard,
				boolean_t strict
);

extern kern_return_t mach_port_unguard(
				mach_port_name_t target,
				mach_port_name_t name,
				uint64_t guard
);
#endif
