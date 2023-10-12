/* $Id$ lightweight CPU usage */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <rtems/rtems/tasks.h>
#include <rtems/score/threadimpl.h>
#include <rtems/score/timestampimpl.h>

#if defined(RTEMS_VERSION_ATLEAST) && RTEMS_VERSION_ATLEAST(4,8,99)
#define HAVE_HIGHRES_TIME
#endif

#ifdef HAVE_HIGHRES_TIME
#include <rtems/score/timespec.h>
#endif

#include <math.h>

#include <ssrlAppsMiscUtils.h>

rtems_status_code
miscu_get_idle_uptime(struct timespec *pts)
{
#if defined(HAVE_HIGHRES_TIME)
rtems_status_code sc = RTEMS_SUCCESSFUL;

	if ( ! _Thread_Executing ) {
		sc   = RTEMS_NOT_DEFINED;
	} else {
		_Timestamp_To_timespec(&_Thread_Executing->cpu_time_used, pts);
	}

	return sc;
#else
	return RTEMS_NOT_IMPLEMENTED;
#endif
}

rtems_status_code
miscu_get_task_uptime(rtems_id tid, struct timespec *pts)
{
#if defined(HAVE_HIGHRES_TIME)
Thread_Control    *tcb;
ISR_lock_Context lctx;

	tcb = _Thread_Get (tid, &lctx);

	if (_Objects_Is_local_id(tid)) {
		_Timestamp_To_timespec(&tcb->cpu_time_used, pts);
		return RTEMS_SUCCESSFUL;
	}
	return RTEMS_INVALID_ID;
#else
	return RTEMS_NOT_IMPLEMENTED;
#endif
}

/* Returns percentage or NAN (if difference to last uptime == 0) */
double
miscu_cpu_load_percentage(struct timespec *lst_uptime, struct timespec *lst_idletime)
{
#if defined(HAVE_HIGHRES_TIME)
static struct timespec internal_up   = { 0., 0. };
static struct timespec internal_idle = { 0., 0. };

struct timespec diff, now_uptime, now_idletime;

double res;

	if ( !lst_uptime )
		lst_uptime   = &internal_up;
	if ( !lst_idletime )
		lst_idletime = &internal_idle;

	if ( RTEMS_SUCCESSFUL != rtems_clock_get_uptime( &now_uptime ) )
		return nan("");

	_Timespec_Subtract(lst_uptime, &now_uptime, &diff);

	res = diff.tv_sec + diff.tv_nsec * 1.0E-9;

	if ( 0.0 == res )
		return nan("");

	if ( RTEMS_SUCCESSFUL != miscu_get_idle_uptime(&now_idletime) )
		return nan("");

	_Timespec_Subtract(lst_idletime, &now_idletime, &diff);

	res = 100.0 * ( 1.0 - (diff.tv_sec + diff.tv_nsec * 1.0E-9) / res );

	*lst_uptime   = now_uptime;
	*lst_idletime = now_idletime;

	return res;
#else
	return nan("");
#endif
}

void
miscu_cpu_load_percentage_init(struct timespec *lst_uptime, struct timespec *lst_idletime)
{
	if ( lst_uptime ) {
		lst_uptime->tv_sec  = 0;
		lst_uptime->tv_nsec = 0;
	}
	if ( lst_idletime ) {
		lst_idletime->tv_sec  = 0;
		lst_idletime->tv_nsec = 0;
	}
}
