#include <sys/cdefs.h>
#include <sys/types.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <machine/atomic.h>
#include <xlocale.h>


#define INCLUDE_OBJC 0
#define __strong
#define __DARWIN_NULL (void *)0

#define __APPLE__
#define CF_PRIVATE __attribute__((visibility("hidden")))
#define CF_PRIVATE_EXTERN __private_extern__
#define CF_INLINE static __inline

#if INCLUDE_OBJC
#include <objc/objc.h>
#else
typedef signed char	BOOL; 
typedef char * id;
typedef char * Class;
#define YES (BOOL)1
#define NO (BOOL)0
#define nil NULL
#endif
typedef struct _auto_zone_t auto_zone_t;

#define OSMemoryBarrier() mb()

extern int __CFConstantStringClassReference[];

#define CRSetCrashLogMessage printf
enum {
    AUTO_TYPE_UNKNOWN = -1,
    AUTO_UNSCANNED = (1 << 0),
    AUTO_OBJECT = (1 << 1),
    AUTO_POINTERS_ONLY = (1 << 2),
    AUTO_MEMORY_SCANNED = !AUTO_UNSCANNED,
    AUTO_MEMORY_UNSCANNED = AUTO_UNSCANNED,
    AUTO_MEMORY_ALL_POINTERS = AUTO_POINTERS_ONLY,
    AUTO_MEMORY_ALL_WEAK_POINTERS = (AUTO_UNSCANNED | AUTO_POINTERS_ONLY),
    AUTO_OBJECT_SCANNED = AUTO_OBJECT,
    AUTO_OBJECT_UNSCANNED = AUTO_OBJECT | AUTO_UNSCANNED, 
    AUTO_OBJECT_ALL_POINTERS = AUTO_OBJECT | AUTO_POINTERS_ONLY
};

typedef unsigned long auto_memory_type_t;


CF_INLINE id objc_assign_strongCast(id val, id *dest) 
{ return (*dest = val); }

CF_INLINE id objc_assign_global(id val, id *dest) 
{ return (*dest = val); }

CF_INLINE id objc_assign_ivar(id val, id dest, ptrdiff_t offset) 
{ return (*(id*)(uintptr_t)((char *)dest+offset) = val); }

CF_INLINE id objc_read_weak(id *location) 
{ return *location; }

CF_INLINE id objc_assign_weak(id value, id *location) 
{ return (*location = value); }

CF_INLINE void *objc_collectableZone(void) { return NULL; }
CF_INLINE unsigned int auto_zone_release(void *zone __unused, void *ptr __unused) { return 0; }
CF_INLINE const void *auto_zone_base_pointer(void *zone __unused, const void *ptr __unused) { return NULL; }
CF_INLINE void auto_zone_set_unscanned(void *zone __unused, void *ptr __unused) {}
CF_INLINE void *objc_memmove_collectable(void *a, const void *b, size_t c) { return memmove(a, b, c); }
CF_INLINE void *auto_zone_allocate_object(void *zone __unused, size_t size __unused, auto_memory_type_t type __unused, int rc __unused, int clear __unused) { return 0; }
CF_INLINE void auto_zone_set_nofinalize(void *zone __unused, void *ptr __unused) {}
CF_INLINE int auto_zone_is_valid_pointer(void *zone __unused, const void *ptr __unused) { return 0; }
CF_INLINE void auto_zone_retain(void *zone __unused, void *ptr __unused) {}

CF_INLINE uintptr_t _object_getExternalHash(id obj) {
    return (uintptr_t)obj;
}
CF_INLINE BOOL objc_collectingEnabled(void) { return NO; }
CF_INLINE BOOL objc_isAuto(id object __unused) { return 0; }

#define OSAtomicCompareAndSwapPtrBarrier(old, new, p) atomic_cmpset_ptr((volatile uintptr_t *)(p), (uintptr_t)old, (uintptr_t)new)
#define OSAtomicIncrement32(v) atomic_fetchadd_int(v, 1)
extern size_t sallocx(void *, int);
static inline size_t
malloc_size(void * ptr)
{

        return (sallocx(ptr, 0));
}
#include <CoreFoundation/CFBase.h>
