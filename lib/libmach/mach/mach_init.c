#include <sys/cdefs.h>
#include <sys/types.h>

#include <mach.h>
#include <mach/boolean.h>
#include <mach/mach_traps.h>
#include <mach/mach_init.h>
#include <mach/mach_types.h>
#include <mach/mach_port.h>
#include <stdbool.h>

static void mach_init(void) __attribute__((constructor));

extern mach_port_t _task_reply_port;

mach_port_t bootstrap_port = MACH_PORT_NULL;
mach_port_t mach_task_self_ = MACH_PORT_NULL;
 __attribute__((visibility("hidden"))) mach_port_t _task_reply_port;


static void
mach_init(void)
{
	static bool mach_inited = false;

	if (!mach_inited) {
		mach_task_self_ = task_self_trap();
        _task_reply_port = mach_reply_port();
		mach_inited = true;
	}
}
