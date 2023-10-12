/* $Id$ */

/* Crude helper to estimate execution time (but note that this
 * does not account for task preemption of any kind!)
 */
#include <rtems.h>
#include <time.h>
#include <sys/time.h>

uint32_t
execUsN(int rep, void (*fn)(),uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6)
{
	struct timeval then, now;
	
	gettimeofday( &then, 0 );
	do {
		fn(a1,a2,a3,a4,a5,a6);
	} while ( rep-- > 0 );
	gettimeofday( &now,  0 );

	if ( now.tv_usec < then.tv_usec ) {
		now.tv_usec += 1000000;
		now.tv_sec--;
	}

	return (now.tv_sec - then.tv_sec) * 1000000 + now.tv_usec - then.tv_usec;
}

uint32_t
execUs(void (*fn)(),uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6)
{
	return execUsN(0,fn,a1,a2,a3,a4,a5,a6);
}
