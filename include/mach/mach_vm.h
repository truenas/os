#ifndef _MACH_MACH_VM_H_
#define _MACH_MACH_VM_H_

kern_return_t vm_allocate
(
        mach_port_name_t target,
        vm_address_t *address,
        vm_size_t size,
        int flags
);

kern_return_t vm_deallocate
(
        mach_port_name_t target,
        vm_address_t address,
        vm_size_t size
);

#endif
