
#ifndef _MACH_VOUCHER_TYPES_H_
#define _MACH_VOUCHER_TYPES_H_

typedef mach_port_t             mach_voucher_t;
#define MACH_VOUCHER_NULL       ((mach_voucher_t) 0)

typedef mach_port_name_t        mach_voucher_name_t;
#define MACH_VOUCHER_NAME_NULL  ((mach_voucher_name_t) 0)

typedef mach_voucher_name_t     *mach_voucher_name_array_t;
#define MACH_VOUCHER_NAME_ARRAY_NULL ((mach_voucher_name_array_t) 0)


#if !defined(_KERNEL)
typedef mach_voucher_t          ipc_voucher_t;
#else
#if !defined(MACH_KERNEL_PRIVATE)
struct ipc_voucher              ;
#endif
typedef struct ipc_voucher      *ipc_voucher_t;
#endif
#define IPC_VOUCHER_NULL        ((ipc_voucher_t) 0)

typedef uint32_t mach_voucher_selector_t;

#endif
