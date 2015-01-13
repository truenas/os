#include <sys/param.h>
#include <sys/event.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/sysent.h>
#include <sys/sysproto.h>

extern struct sysent *mach_sysent_p, mach_sysent[];
extern struct filterops machport_filtops;

static int
mach_mod_init(void)
{
	
	mach_sysent_p = mach_sysent;
	kqueue_add_filteropts(EVFILT_MACHPORT, &machport_filtops);
	return (0);
}

static int
mach_module_event_handler(module_t mod, int what, void *arg)
{
	int err;

	switch (what) {
	case MOD_LOAD:
		if ((err = mach_mod_init()) != 0)
			return (err);
		break;
	case MOD_UNLOAD:
		return (EBUSY);
	default:
		return (EOPNOTSUPP);
	}
	return (0);
}

static moduledata_t mach_moduledata = {
	"mach",
	mach_module_event_handler,
	NULL
};

DECLARE_MODULE(mach, mach_moduledata, SI_SUB_KLD, SI_ORDER_ANY);
