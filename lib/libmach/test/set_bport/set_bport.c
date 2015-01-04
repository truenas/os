#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <mach/mach.h>

static void
error(int exitcode, int macherr, const char *funcname)
{
#if 0
	mach_error(funcname, macherr);
#endif	
	printf("%s failed with %x/%d\n", funcname, macherr, macherr);
	exit(1);
}

int
main(int argc, char **argv)
{
	int err;
	mach_port_t port;


	err = mach_port_allocate(mach_task_self(),
							 MACH_PORT_RIGHT_RECEIVE, &port);
	if (err)
		error(1, err, "mach_port_allocate");

	err = mach_port_insert_right (mach_task_self (),
				      port,
				      port,
				      MACH_MSG_TYPE_MAKE_SEND);

	if (err)
		error(10, err, "mach_port_insert_right");

	printf("setting bootstrap port to %d\n", port);
	sleep(1);
	err = task_set_bootstrap_port(mach_task_self(), port);
	if (err)
		error(1, err, "task_set_bootstrap_port");

	return (0);
}
