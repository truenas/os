
#include "SYS.h"
#include "cerror.S"	
	RSYSCALL(mach_reply_port)	;
	MACH_RSYSCALL(thread_self_trap)	;
	MACH_RSYSCALL(task_self_trap)	;
	MACH_RSYSCALL(host_self_trap)	;
	RSYSCALL(mach_msg_trap)	;
	RSYSCALL(mach_msg_overwrite_trap)	;
	MACH_RSYSCALL(semaphore_signal_trap)	;
	MACH_RSYSCALL(semaphore_signal_all_trap)	;
	MACH_RSYSCALL(semaphore_signal_thread_trap)	;
	MACH_RSYSCALL(semaphore_wait_trap)	;
	MACH_RSYSCALL(semaphore_wait_signal_trap)	;
	MACH_RSYSCALL(semaphore_timedwait_trap)	;
	MACH_RSYSCALL(semaphore_timedwait_signal_trap)	;
/*
	MACH_RSYSCALL(mach_init_process)	;
	MACH_RSYSCALL(mach_map_fd)	;
*/
	MACH_RSYSCALL(task_for_pid)	;
	MACH_RSYSCALL(pid_for_task)	;
	MACH_RSYSCALL(macx_swapon)	;
	MACH_RSYSCALL(macx_swapoff)	;
	MACH_RSYSCALL(macx_triggers)	;
	MACH_RSYSCALL(swtch_pri)	;
	MACH_RSYSCALL(syscall_thread_switch)	;
	MACH_RSYSCALL(clock_sleep_trap)	;
	RSYSCALL(mach_timebase_info)	;
	RSYSCALL(mach_wait_until)	;
	MACH_RSYSCALL(timer_create)	;
	MACH_RSYSCALL(timer_destroy)	;
	MACH_RSYSCALL(timer_arm)	;
	MACH_RSYSCALL(timer_cancel)	;
	MACH_RSYSCALL(get_time_base_info)	;
