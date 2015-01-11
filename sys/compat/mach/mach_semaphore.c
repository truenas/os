

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/rwlock.h>
#include <sys/signal.h>

#include <vm/uma.h>

#include <compat/mach/mach_types.h>
#include <compat/mach/mach_clock.h>
#include <compat/mach/mach_errno.h>
#include <compat/mach/mach_port.h>

#include <sys/mach/task_server.h>

int
semaphore_create(
	task_t task,
	semaphore_t *semaphore,
	int policy,
	int value
)
UNSUPPORTED;

int
semaphore_destroy(
	task_t task,
	semaphore_t semaphore
	)
UNSUPPORTED;


void
semaphore_dereference(semaphore_t semaphore)
{
	;
}


