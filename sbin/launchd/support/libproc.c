#include <sys/types.h>
#include <sys/errno.h>
#include <mach/boolean.h>
#include <libproc.h>

int 
proc_listpids(uint32_t type __unused, uint32_t typeinfo __unused, void *buffer __unused, int buffersize __unused) 
{
	
	return (ENOTSUP);
}

int
proc_listallpids(void * buffer __unused, int buffersize __unused)
{

	return (ENOTSUP);
}


int 
proc_listpgrppids(pid_t pgrpid __unused, void * buffer __unused, int buffersize __unused)
{

	return (ENOTSUP);
}

int 
proc_listchildpids(pid_t ppid __unused, void * buffer __unused, int buffersize __unused)
{

	return (ENOTSUP);
}

int 
proc_pidinfo(int pid __unused, int flavor __unused, uint64_t arg __unused,  void *buffer __unused, int buffersize __unused)
{

	return (ENOTSUP);
}

int
proc_setpcontrol(const int control __unused)
{

	return (ENOTSUP);
}

int
proc_track_dirty(pid_t pid __unused, uint32_t flags __unused)
{

	return (ENOTSUP);
}

int
proc_set_dirty(pid_t pid __unused, bool dirty __unused)
{

	return (ENOTSUP);
}

int
proc_get_dirty(pid_t pid __unused, uint32_t *flags __unused)
{

	return (ENOTSUP);
}

int
proc_terminate(pid_t pid __unused, int *sig __unused)
{

	return (ENOTSUP);
}
