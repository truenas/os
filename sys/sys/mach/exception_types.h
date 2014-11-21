#ifndef _MACH_EXCEPTION_TYPES_H_
#define _MACH_EXCEPTION_TYPES_H_

#include <machine/mach/exception.h>

#define EXC_MASK_BAD_ACCESS             (1 << EXC_BAD_ACCESS)
#define EXC_MASK_BAD_INSTRUCTION        (1 << EXC_BAD_INSTRUCTION)
#define EXC_MASK_ARITHMETIC             (1 << EXC_ARITHMETIC)
#define EXC_MASK_EMULATION              (1 << EXC_EMULATION)
#define EXC_MASK_SOFTWARE               (1 << EXC_SOFTWARE)
#define EXC_MASK_BREAKPOINT             (1 << EXC_BREAKPOINT)
#define EXC_MASK_SYSCALL                (1 << EXC_SYSCALL)
#define EXC_MASK_MACH_SYSCALL           (1 << EXC_MACH_SYSCALL)
#define EXC_MASK_RPC_ALERT              (1 << EXC_RPC_ALERT)
#define EXC_MASK_CRASH                  (1 << EXC_CRASH)
#define EXC_MASK_RESOURCE               (1 << EXC_RESOURCE)
#define EXC_MASK_GUARD                  (1 << EXC_GUARD)

#define EXC_MASK_ALL    (EXC_MASK_BAD_ACCESS |                  \
                         EXC_MASK_BAD_INSTRUCTION |             \
                         EXC_MASK_ARITHMETIC |                  \
                         EXC_MASK_EMULATION |                   \
                         EXC_MASK_SOFTWARE |                    \
                         EXC_MASK_BREAKPOINT |                  \
                         EXC_MASK_SYSCALL |                     \
                         EXC_MASK_MACH_SYSCALL |                \
                         EXC_MASK_RPC_ALERT |                   \
                         EXC_MASK_RESOURCE |                    \
                         EXC_MASK_GUARD |                       \
                         EXC_MASK_MACHINE)

#define EXCEPTION_DEFAULT			1
#define EXCEPTION_STATE				2
#define EXCEPTION_STATE_IDENTITY	3

#include <mach/port.h>
#include <mach/thread_status.h>
#include <machine/mach/vm_types.h>
/*
 * Exported types
 */

typedef int                             exception_type_t;
typedef int		                        exception_data_type_t;
typedef int64_t                         mach_exception_data_type_t;
typedef int                             exception_behavior_t;
typedef exception_data_type_t           *exception_data_t;
typedef mach_exception_data_type_t      *mach_exception_data_t;
typedef unsigned int                    exception_mask_t;
typedef exception_mask_t                *exception_mask_array_t;
typedef exception_behavior_t            *exception_behavior_array_t;
typedef thread_state_flavor_t           *exception_flavor_array_t;
typedef mach_port_t                     *exception_port_array_t;
typedef mach_exception_data_type_t      mach_exception_code_t;
typedef mach_exception_data_type_t      mach_exception_subcode_t;

#endif
