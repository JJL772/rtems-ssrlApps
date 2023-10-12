#define __RTEMS_VIOLATE_KERNEL_VISIBILITY__
#include <rtems.h>
#include <rtems/libcsupport.h>
#include <rtems/score/heapimpl.h>
#include <rtems/score/wkspace.h>
#include <rtems/score/apimutex.h>
#include <stdio.h>

#define ISMINVERSION(ma,mi,re) \
	(    __RTEMS_MAJOR__  > (ma)	\
	 || (__RTEMS_MAJOR__ == (ma) && __RTEMS_MINOR__  > (mi))	\
	 || (__RTEMS_MAJOR__ == (ma) && __RTEMS_MINOR__ == (mi) && __RTEMS_REVISION__ >= (re)) \
    )

#if ISMINVERSION(4,6,99)
#define hi_free	Free.total
#define hi_used	Used.total
#else
#define hi_free	free_size
#define hi_used	used_size
#endif

int
memUsageDump(int doit)
{
Heap_Information_block info;
int	rval, heapsz;

	fprintf(stderr,"WARNING: 'memUsageDump' temporarily disables thread dispatching\n");
	fprintf(stderr,"         thus killing real-time preformance! Use for diagnostics only...\n");

	if ( !doit ) {
		fprintf(stderr,"...bailing out. Call with a nonzero argument to proceed\n");
		return -1;
	} 

	_RTEMS_Lock_allocator();
	rval = (
			_Heap_Get_information( &_Workspace_Area, &info)
#if ISMINVERSION(4,9,99)
			, 0
#else
			!= HEAP_GET_INFORMATION_SUCCESSFUL
#endif
	       );
	_RTEMS_Unlock_allocator();

	if ( rval ) {
		fprintf(stderr,"ERROR: unable to retrieve RTEMS workspace info\n");
	} else {
		printf("Workspace usage: free %lub, used %lub, configured size %lub\n",
				(unsigned long)info.hi_free,
		        (unsigned long)info.hi_used,
#if ISMINVERSION(4,9,0)
				(unsigned long)rtems_configuration_get_work_space_size()
#else
				(unsigned long)_Configuration_Table->work_space_size
#endif
		      );
	}

	heapsz = malloc_free_space();

	if ( -1 == heapsz ) {
		fprintf(stderr,"ERROR: unable to retrieve malloc heap info\n");
		rval = -1;
	} else {
		printf("Free space on the malloc heap: %ib\n",heapsz);
	}

	/* The malloc heap is semaphore protected */
	return rval;
}
