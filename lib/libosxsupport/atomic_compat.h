#include <sys/types.h>
#include <machine/atomic.h>
#define OSAtomicIncrement32Barrier(v) atomic_fetchadd_int((volatile int *)v, 1)
#define OSAtomicDecrement32Barrier(v) atomic_fetchadd_int((volatile int *)v, -1)
#define OSAtomicTestAndSetBarrier(i, v) atomic_testandset_int((volatile int *)v, i)
