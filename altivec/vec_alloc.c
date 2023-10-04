
/* $Id$ */

/* Brute-force implementation of vec_malloc() & friends.
 *
 * We could also create a region with a page size of 16 bytes.
 */

/* Till Straumann, <strauman@slac.stanford.edu>, 2005   */

#include <rtems.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <rtems/score/percpu.h>
#include <rtems/score/sysstate.h>

/* Check for RTEMS version >= major/minor/revision */
#define ISMINVERSION(ma,mi,re) \
    (    __RTEMS_MAJOR__  > (ma)    \
     || (__RTEMS_MAJOR__ == (ma) && __RTEMS_MINOR__  > (mi))    \
     || (__RTEMS_MAJOR__ == (ma) && __RTEMS_MINOR__ == (mi) && __RTEMS_REVISION__ >= (re)) \
    )


extern rtems_id RTEMS_Malloc_Heap;

void *vec_malloc(size_t s)
{
void *punaligned, *paligned;
	if ( 0 == s )
		return 0;
	if ( 0 == (punaligned = malloc(s+16)) )
		return 0;
	paligned = (void*)((((unsigned long)punaligned)+16) & ~15);
	*(((void**)paligned) - 1) = punaligned;
	return paligned;
}

void vec_free(void *paligned)
{
	if ( paligned )
		free (*(((void**)paligned)-1));
}

void *vec_calloc(size_t nmemb, size_t s)
{
void *rval;
	s*=nmemb;
	if ( (rval = vec_malloc(s)) )
		memset(rval, 0, s);
	return rval;
}

/* Follow along the lines of libcsupport/src/malloc.c */

void *vec_realloc(void *p, size_t s)
{
void              *pu, *pold;
#if ISMINVERSION(4,9,99)
uintptr_t         old_size;
#else
size_t            old_size;
#endif
rtems_status_code sc;

	if (_System_state_Is_up(_System_state_Get())) {
		if (_Thread_Dispatch_disable_level > 0)
			return (void *) 0;

		if (_ISR_Nest_level > 0)
			return (void *) 0;
	}

 	if ( 0 == s ) {
		vec_free(p);
		return 0;
	}

	if ( 0 == p )
		return vec_malloc(s);

	pu = *(((void**)p) - 1);

#if ISMINVERSION(4,6,99)
	sc = rtems_region_resize_segment( RTEMS_Malloc_Heap, pu, s + 16, &old_size );

	if( sc == RTEMS_SUCCESSFUL ) {
		return p;
	} else if ( sc != RTEMS_UNSATISFIED ) {
		errno = EINVAL;
		return (void *) 0;
	}
#endif


	if ( ! (pu = vec_malloc(s)) ) {
		/* must not free (standard says so) */
		return 0;
	}

	sc = rtems_region_get_segment_size( RTEMS_Malloc_Heap, pu, &old_size );
	if ( sc != RTEMS_SUCCESSFUL ) {
		errno = EINVAL;
		return (void *) 0;
	}
	old_size-=16;
	pold     = p;
	p        = (void*)((((unsigned long)pu)+16) & ~15);

	memcpy(p, pold, s < old_size ? s : old_size);

	vec_free(pold);
	
	/* store back pointer */
	*(((void**)p) - 1) = pu;
	return p;
}
