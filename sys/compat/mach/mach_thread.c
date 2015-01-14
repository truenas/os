#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/eventhandler.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/resource.h>
#include <sys/resourcevar.h>
#include <sys/rwlock.h>
#include <sys/sched.h>
#include <sys/sleepqueue.h>
#include <sys/signal.h>

#include <sys/mach/mach_types.h>
#include <sys/mach/mach_traps.h>

#include <sys/mach/ipc/ipc_kmsg.h>
#include <sys/mach/thread.h>
#include <sys/mach/ipc_tt.h>
#include <sys/mach/thread_switch.h>

#define MT_SETRUNNABLE 0x1

int mach_swtch_pri(int pri);

#ifdef notyet
/*
 * Am assuming that Mach lacks the concept of uninterruptible
 * sleep - this may need to be changed back to what is in pci_pass.c
 */
static int
_intr_tdsigwakeup(struct thread *td, int intrval)
{
	struct proc *p = td->td_proc;
	int rc = 0;

	PROC_SLOCK(p);
	thread_lock(td);
	if (TD_ON_SLEEPQ(td)) {
		/*
		 * Give low priority threads a better chance to run.
		 */
		if (td->td_priority > PUSER)
			sched_prio(td, PUSER);

		sleepq_abort(td, intrval);
		rc = 1;
	}
	PROC_SUNLOCK(p);
	thread_unlock(td);
	return (rc);
}
#endif


int
mach_thread_switch(mach_port_name_t thread_name, int option, mach_msg_timeout_t option_time)
{
       int timeout;
       struct mach_emuldata *med;
	   struct thread *td = curthread;

       med = (struct mach_emuldata *)td->td_proc->p_emuldata;
       timeout = option_time * hz / 1000;

       /*
        * The day we will be able to find out the struct proc from
        * the port number, try to use preempt() to call the right thread.
        * [- but preempt() is for _involuntary_ context switches.]
        */
       switch(option) {
       case SWITCH_OPTION_NONE:
               sched_relinquish(curthread);
               break;

       case SWITCH_OPTION_WAIT:
#ifdef notyet		   
               med->med_thpri = 1;
               while (med->med_thpri != 0) {
                       rw_wlock(&med->med_rightlock);
                       (void)msleep(&med->med_thpri, &med->med_rightlock, PZERO|PCATCH,
                                                "thread_switch", timeout);
                       rw_wunlock(&med->med_rightlock);
              }
               break;
#endif
       case SWITCH_OPTION_DEPRESS:
               /* Use a callout to restore the priority after depression? */
               td->td_priority = PRI_MAX_TIMESHARE;
               break;

       default:
              uprintf("sys_mach_syscall_thread_switch(): unknown option %d\n", option);
               break;
       }
       return (0);
}

int
mach_swtch_pri(int pri)
{
	struct thread *td = curthread;
	
	thread_lock(td);
	if (td->td_state == TDS_RUNNING)
		td->td_proc->p_stats->p_cru.ru_nivcsw++;        /* XXXSMP */
	mi_switch(SW_VOL, NULL);
	thread_unlock(td);

	return (0);
}



void
thread_go(thread_t thread)
{
	int needunlock = 0;
	struct mtx *block_lock = thread->ith_block_lock_data;

	if (block_lock != NULL && !mtx_owned(block_lock)) {
		needunlock = 1;
		mtx_lock(block_lock);
	}
	wakeup(thread);
	if (needunlock)
		mtx_unlock(block_lock);
}

void
thread_block(void (*continuation)(void) __unused) /* ignore continuation for now */
{
	thread_t thread = current_thread();
	int rc;

	rc = msleep(thread, thread->ith_block_lock_data, PCATCH|PSOCK, "thread_block", thread->timeout);
	thread->ith_block_lock_data = NULL;
	if (rc == EINTR || rc == ERESTART)
		thread->wait_result = THREAD_INTERRUPTED;
	else if (rc == EWOULDBLOCK)
		thread->wait_result = THREAD_TIMED_OUT;
	else
		thread->wait_result = 0;
}

void
thread_will_wait_with_timeout(thread_t thread, int timeout)
{

	thread->sleep_stamp = ticks;
	thread->timeout = timeout;
}


void
thread_will_wait(thread_t thread)
{

	thread->sleep_stamp = ticks;
}

static void
mach_thread_create(struct thread *td, thread_t thread)
{

	thread->ref_count = 1;
	thread->ith_td = td;
	ipc_thread_init(thread);
	ipc_thr_act_init(thread);
}


static uma_zone_t thread_shuttle_zone;

static int
uma_thread_init(void *_thread, int a, int b)
{
	/* allocate thread substructures */
	return (0);
}

static void
uma_thread_fini(void *_thread, int a)
{
	/* deallocate thread substructures */
}

static void
mach_thread_init(void *arg __unused, struct thread *td)
{
	thread_t thread;

	thread = uma_zalloc(thread_shuttle_zone, M_WAITOK);
	mtx_init(&thread->ith_lock_data, "mach_thread lock", NULL, MTX_DEF);

	td->td_machdata = thread;
}

static void
mach_thread_ctor(void *arg __unused, struct thread *td)
{
	thread_t thread = td->td_machdata;

	mach_thread_create(td, thread);
}

static void
thread_sysinit(void *arg __unused)
{
	thread_shuttle_zone = uma_zcreate("thread_shuttle_zone",
									  sizeof(struct thread_shuttle),
									  NULL, NULL, uma_thread_init,
									  uma_thread_fini, 1, 0);

	EVENTHANDLER_REGISTER(thread_ctor, mach_thread_ctor, NULL, EVENTHANDLER_PRI_ANY);
	EVENTHANDLER_REGISTER(thread_init, mach_thread_init, NULL, EVENTHANDLER_PRI_ANY);
}

/* before SI_SUB_INTRINSIC and after SI_SUB_EVENTHANDLER */
SYSINIT(mach_thread, SI_SUB_KLD, SI_ORDER_ANY, thread_sysinit, NULL);
