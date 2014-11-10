
#include "SYS.h"
#include "cerror.S"	
	MACH_RSYSCALL(mach_reply_port)	;
	MACH_RSYSCALL(mach_thread_self_trap)	;
	MACH_RSYSCALL(mach_task_self_trap)	;
	MACH_RSYSCALL(mach_host_self_trap)	;
	MACH_RSYSCALL(mach_msg_trap)	;
	MACH_RSYSCALL(mach_msg_overwrite_trap)	;
	MACH_RSYSCALL(mach_semaphore_signal_trap)	;
	MACH_RSYSCALL(mach_semaphore_signal_all_trap)	;
	MACH_RSYSCALL(mach_semaphore_signal_thread_trap)	;
	MACH_RSYSCALL(mach_semaphore_wait_trap)	;
	MACH_RSYSCALL(mach_semaphore_wait_signal_trap)	;
	MACH_RSYSCALL(mach_semaphore_timedwait_trap)	;
	MACH_RSYSCALL(mach_semaphore_timedwait_signal_trap)	;
	MACH_RSYSCALL(mach_init_process)	;
	MACH_RSYSCALL(mach_map_fd)	;
	MACH_RSYSCALL(mach_task_for_pid)	;
	MACH_RSYSCALL(mach_pid_for_task)	;
	MACH_RSYSCALL(mach_macx_swapon)	;
	MACH_RSYSCALL(mach_macx_swapoff)	;
	MACH_RSYSCALL(mach_macx_triggers)	;
	MACH_RSYSCALL(mach_swtch_pri)	;
	MACH_RSYSCALL(mach_syscall_thread_switch)	;
	MACH_RSYSCALL(mach_clock_sleep_trap)	;
	MACH_RSYSCALL(mach_timebase_info)	;
	MACH_RSYSCALL(mach_wait_until)	;
	MACH_RSYSCALL(mach_timer_create)	;
	MACH_RSYSCALL(mach_timer_destroy)	;
	MACH_RSYSCALL(mach_timer_arm)	;
	MACH_RSYSCALL(mach_timer_cancel)	;
	MACH_RSYSCALL(mach_get_time_base_info)	;
	
	
	
