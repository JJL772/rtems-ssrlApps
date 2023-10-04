/* $Id$ */

/* Altivec support for RTEMS; vector register context management.
 * This is implemented as a user extension.
 *
 * Author: Till Straumann <strauman@slac.stanford.edu>, 2005
 */

#if defined(HAVE_NATIVE_ALTIVEC) && !defined(__ALTIVEC__)
#error "Bad configuration -- something is wrong"
#endif

#if defined(HAVE_NATIVE_ALTIVEC)

#include <rtems.h>
#include <libcpu/cpuIdent.h>
#include <rtems/bspIo.h>
#include <rtems/powerpc/registers.h>
#include <stdint.h>
#include <rtems/score/percpu.h>

/* Just implement traditional entry points for sake
 * of binary bwds-compatibility...
 */
int
vec_task_enable()
{
	return 0;
}

int
vec_install_extension()
{
uint32_t msr;

	/* NOTE: Unlike the original version this dummy version can be called
	 *       multiple times...
	 */

	_CPU_MSR_GET(msr);

	/* Verify that MSR_VE is enabled */
	if ( ! (MSR_VE & msr) ) {
		printk("Altivec Extension Dummy: MSR_VE should be enabled already!\n");
		return -1;
	}

#ifndef PSIM
	if ( ! ppc_cpu_has_altivec() ) {
		printk("Altivec Extension Dummy: This CPU seems not to have AltiVec\n");
		return -1;
	}
#endif

	return 0;
}

#else

#define __RTEMS_VIOLATE_KERNEL_VISIBILITY__
#include <rtems.h>
#include <rtems/config.h>
#include <libcpu/spr.h>
#include <libcpu/cpuIdent.h>
#include <assert.h>
#include <rtems/bspIo.h>
#include <rtems/error.h>
#include <rtems/score/wkspace.h>
#include <rtems/powerpc/powerpc.h>

#include <stdio.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#if defined RTEMS_VERSION_LATER_THAN
#if ! RTEMS_VERSION_LATER_THAN(4,8,0)
#define bool boolean
#endif
#endif

/* Configurable parameters */
/* localize symbols */
#define STATIC
/* debugging and paranoia */
#define DEBUG
/* Ignore VRSAVE; just always save/restore full context.
 * Tests (7455) indicate that vrsave provides only extremely marginal
 * gains, if any. OTOH, not having to maintain it might speed up user
 * code... (it is safe to compile applications with -mvrsave=no if
 * IGNORE_VRSAVE is defined)
 */
#define IGNORE_VRSAVE

/* If ALL_THREADS_ALTIVEC is defined then 
 *  a) this extension must be used as an 'initial' extension, i.e., entered
 *     into the application's configuration table.
 *  b) this extension must be the ONLY 'initial' extension to use the task
 *     'extensions' area (slot #0, the other slots are for 'created' extensions
 *     and accessed using their ID as an index. Since indices are 1-based, slot
 *     0 is available to the 'initial' extensions).
 *  c) BSP must set MSR_VE
 *  d) All threads will be Altivec enabled with no need to call threadMkVec()
 */
#ifdef __ALTIVEC__
#define  ALL_THREADS_ALTIVEC
#endif

/* END of configurable section */

/* align on cache line boundary so we can use 'dcbz' */
#define VEC_ALIGNMENT	16

#if PPC_CACHE_ALIGNMENT != 32
#error "Code for now relies on cache line size == 32"
#endif

/* 32 registers + VCSR/VRSAVE; multiples of cache line size (assumed to be 32)
 * so we can speed up the context saving code by using 'dcbz'
 */
typedef uint32_t Vec[4]			__attribute__((aligned(VEC_ALIGNMENT)));
typedef Vec VecCtxtBuf[34]		__attribute__((aligned(PPC_CACHE_ALIGNMENT)));
typedef VecCtxtBuf	*VecCtxt;

#define VRSAVE(ctxt)	((*(ctxt))[0][0])
#define VSCR(ctxt)		((*(ctxt))[0][3])
#define BPTR(ctxt)		((*(ctxt))[0][1])

#define NAM				"AltiVec Extension"

#define ERRID(a,b,c,d)	(((a)<<24) | ((b)<<16) | ((c)<<8) | (d))

/* use java/c9x mode; clear saturation bit */
#define VSCR_INIT_VAL	0

/* save/restore everything; it doesn't make a big difference */ 
#define IGNORE_VRSAVE /* the value doesn't matter if this is defined */
static uint32_t vrsave_init_val = 0xffffffff;

#ifndef MSR_VE
#define MSR_VE	(1<<(31-6))
#endif

STATIC rtems_tcb * volatile vec_owner        = 0;
STATIC rtems_id    volatile vec_extension_id = 0;
STATIC int                  vec_idx          = 0; /* use slot 0 if ALL_THREADS_ALTIVEC */

STATIC bool    vec_thread_create(rtems_tcb *current_task, rtems_tcb *new_task);
STATIC void    vec_thread_start(rtems_tcb *executing, rtems_tcb *startee);
STATIC void    vec_thread_restart(rtems_tcb *executing, rtems_tcb *restartee);
STATIC void    vec_thread_delete(rtems_tcb *executing, rtems_tcb *deletee);
STATIC void    vec_thread_switch(rtems_tcb *executing, rtems_tcb *heir);

STATIC
rtems_extensions_table	altivec_extension_tbl = {
	thread_create: vec_thread_create,
	thread_start:  vec_thread_start,
	thread_restart: vec_thread_restart,
	thread_delete:  vec_thread_delete,
	thread_switch:  vec_thread_switch,
	thread_begin:	0 /* vec_thread_begin */,
	thread_exitted:	0 /* vec_thread_exited */,
	fatal:			0
};

#ifdef DEBUG
STATIC void sdump();
#else
#define sdump() do {} ()
#endif

STATIC inline VecCtxt get_tcp(rtems_tcb *thread)
{
	return thread->extensions[vec_idx];
}

STATIC inline void set_tcp(rtems_tcb *thread, VecCtxt vp)
{
	thread->extensions[vec_idx] = vp;
}

static inline uint32_t
mfmsr()
{
uint32_t v;	
	asm volatile("mfmsr %0":"=r"(v));
	return v;
}

static inline void
mtmsr(uint32_t v)
{
	asm volatile("mtmsr %0"::"r"(v));
}

static inline void
isync()
{
	asm volatile("isync");
}

static inline void
dssall()
{
#ifndef PSIM
	asm volatile("dssall");
#endif
}

static inline uint32_t
set_MSR_VE()
{
uint32_t rval;
	rval=mfmsr();
	mtmsr(rval | MSR_VE);
	isync();
	return rval;
}

static inline void
clr_MSR_VE()
{
	dssall();
	mtmsr(mfmsr() & ~MSR_VE);
	isync();
}

static inline void
rst_MSR_VE(uint32_t old)
{
	dssall();
	mtmsr(old);
	isync();
}

STATIC VecCtxt extalloc()
{
#ifndef USE_ALLOCATE_ALIGNED /* _Heap_Allocate_aligned is broken */
uint32_t p;
#endif
VecCtxt  paligned;
_Thread_Disable_dispatch();
#ifdef USE_ALLOCATE_ALIGNED /* _Heap_Allocate_aligned is broken */
	paligned = _Heap_Allocate_aligned(&_Workspace_Area, sizeof(VecCtxtBuf), PPC_CACHE_ALIGNMENT);
#else
	p = (uint32_t)_Workspace_Allocate(sizeof(VecCtxtBuf) + PPC_CACHE_ALIGNMENT - 4);
	paligned = (VecCtxt)((p + PPC_CACHE_ALIGNMENT - 4 ) & ~(PPC_CACHE_ALIGNMENT-1));
	BPTR(paligned) = p;
#endif
_Thread_Enable_dispatch();
#ifndef USE_ALLOCATE_ALIGNED /* _Heap_Allocate_aligned is broken */
	assert( (uint32_t)paligned >= p && ! ((uint32_t)paligned & 31) );
#endif
	return paligned;
}

STATIC void extfree(VecCtxt vc)
{
_Thread_Disable_dispatch();
#ifdef USE_ALLOCATE_ALIGNED /* _Heap_Allocate_aligned is broken */
	_Workspace_Free(vc);
#else
	_Workspace_Free((void*)BPTR(vc));
#endif
_Thread_Enable_dispatch();
}

#include "ppc_stack_probe.c"

STATIC int check_stack_alignment()
{
int rval = 0;
	if ( VEC_ALIGNMENT > PPC_STACK_ALIGNMENT ) {
		printk(NAM": CPU support has unsufficient stack alignment;\n");
		printk("modify 'cpukit/score/cpu/powerpc/rtems/score/powerpc.h'\n");
		printk("and choose PPC_ABI_SVR4. I'll enable a workaround for now.\n");
		rval |= 1;
	}
	/* Run-time check; should compile with -mabi=altivec */
	if ( probe_ppc_stack_alignment() < VEC_ALIGNMENT ) {
		printk(NAM": run-time stack alignment unsufficient; make sure you compile with -mabi=altivec\n");
		rval |= 2;
	}
	return rval;
}

STATIC int check_vrsave()
{
	printk(NAM": check for -mvrsave=yes / no not implemented\n");
	return -1;
}

int
vec_install_extension()
{
rtems_status_code sc;

	if ( vec_extension_id ) {
		printk(NAM": AltiVec extension already installed\n");
		return -1;
	}

/* VRSAVE currently unused */
	if ( 0xffffffff != vrsave_init_val ) {
		if ( check_vrsave() )
			rtems_fatal_error_occurred(ERRID('V','E','C','0'));
	}

	if ( check_stack_alignment() & 2 )
		rtems_fatal_error_occurred(ERRID('V','E','C','1'));

#ifndef RTEMS_VERSION_ATLEAST
#define RTEMS_VERSION_ATLEAST(M,m,r) 0
#endif

#if RTEMS_VERSION_ATLEAST(4,8,99)
#ifndef PSIM
	if ( ! ppc_cpu_has_altivec() ) {
		printk(NAM": This CPU seems not to have AltiVec\n");
		return -1;
	}
#endif
#else
	{
	unsigned          pvr;
	switch ( (pvr=get_ppc_cpu_type()) ) {
		default:
			printk(NAM": Not a known AltiVec CPU (PVR id 0x%04x)\n", pvr);
			return -1;
		case PPC_PSIM:
		case PPC_7400:
		case PPC_7455:
		case PPC_7457:
		break;
	}
	}
#endif
#ifdef ALL_THREADS_ALTIVEC
	if ( ! (mfmsr() & MSR_VE) ) {
		printk(NAM": Warning: BSP should set MSR_VE early; doing it now...\n");
		set_MSR_VE();	
	}
	if ( _System_state_Get() >= SYSTEM_STATE_UP ) {
		rtems_panic(NAM": If built as 'ALL_THREADS_ALTIVEC' this must be called before the system is up\n");
	}
#endif

	if ( RTEMS_SUCCESSFUL != (sc = rtems_extension_create(
									rtems_build_name('A','v','e','c'),
									&altivec_extension_tbl,
									(rtems_id*)&vec_extension_id)) ) {
		rtems_error(sc, NAM": unable to create user extension\n");
		return -1;
	}
/* Another rtems API change :-( */
#ifndef rtems_get_index
#define rtems_get_index rtems_object_id_get_index
#endif
	vec_idx = rtems_get_index(vec_extension_id);

	return 0;
}

STATIC void
init_vec_ctxt(VecCtxt c)
{
	VRSAVE(c) = vrsave_init_val;
	VSCR  (c) = VSCR_INIT_VAL;
}

int
vec_task_enable()
{
/* if everything (including rtems proper) is compiled -maltivec then
 * all tasks are implicitely altivec enabled.
 */
#ifndef ALL_THREADS_ALTIVEC
VecCtxt tcbext;
int  i;
void **r1, **misa;

	if ( !vec_extension_id ) {
		fprintf(stderr,NAM": extension not installed\n");
		return -1;
	}
	asm volatile("mr %0,1":"=r"(r1));
	for ( i=0, misa=0; i<100 && *r1; i++ ) {
		if ( ((uint32_t)r1) & (VEC_ALIGNMENT-1) ) {
			misa = r1;
		} 
		r1 = *r1;
	}

	assert ( 0 == *r1 );

	if ( ((uint32_t)r1) & (VEC_ALIGNMENT-1) ) {
		misa = r1;
	} 

	if ( misa ) {
		if ( misa == r1 ) {
			fprintf(stderr,NAM": stack misaligned;");
			fprintf(stderr," can only enable AltiVec on tasks created\n");
			fprintf(stderr,"after AltiVec extension was installed\n");
		} else {
			fprintf(stderr,NAM": stack misaligned; everything must be compiled with -mabi=altivec\n");
		}
		sdump();
		return -1;
	}

	if ( MSR_VE & mfmsr() ) {
		/* already vector enabled */
		fprintf(stderr,NAM": AltiVec already enabled for this task\n");
		return -1;
	}

	if ( !(tcbext = extalloc()) ) {
		return RTEMS_NO_MEMORY;
	}

	init_vec_ctxt(tcbext);

	_Thread_Disable_dispatch();

	set_tcp(_Thread_Executing, tcbext);

	vec_thread_switch(_Thread_Executing, _Thread_Executing);

	set_MSR_VE();
	_Thread_Enable_dispatch();
#endif
	return 0;
}

STATIC void
vec_thread_fixup_env(rtems_tcb *thread)
{
#ifdef ALL_THREADS_ALTIVEC
	init_vec_ctxt(get_tcp(thread));
	/* all threads are altivec; msr should be inherited
	 * but we set it anyways
	 */
	thread->Registers.msr  |= MSR_VE;
#else
	thread->Registers.msr  &= ~MSR_VE;
#endif
	/* align stack to vector boundary */
	thread->Registers.gpr1 &= ~(VEC_ALIGNMENT-1);
	/* tag tos */
	*(uint32_t *)thread->Registers.gpr1 = 0;
}

STATIC bool
vec_thread_create(rtems_tcb *current_task, rtems_tcb *new_task)
{
#ifdef ALL_THREADS_ALTIVEC
VecCtxt tcbext;
	if ( !(tcbext = extalloc()) ) {
		return FALSE;
	}
	if ( get_tcp(new_task) ) {
		/* initial extension uses slot 0; there must only be ONE extension using this */
		rtems_fatal_error_occurred(ERRID('V','E','C','2'));
	}
	set_tcp(new_task, tcbext);
#endif
	return TRUE;
}

STATIC void
vec_thread_start(rtems_tcb *executing, rtems_tcb *startee)
{
	vec_thread_fixup_env(startee);
}

STATIC void
vec_thread_restart(rtems_tcb *executing, rtems_tcb *restartee)
{
#ifndef ALL_THREADS_ALTIVEC
	vec_thread_delete(executing, restartee);
#endif
	vec_thread_fixup_env(restartee);
}

STATIC void
vec_thread_delete(rtems_tcb *executing, rtems_tcb *deletee)
{
VecCtxt vc;

	if ( vec_owner == deletee ) {
		if ( _Thread_Executing == deletee ) {
			assert( vec_owner == executing );
			/* paranoia */
			clr_MSR_VE();
		}
		vec_owner = 0;
	}
	deletee->Registers.msr &= ~MSR_VE;

	if ( (vc = get_tcp(deletee)) ) {
		extfree(vc);
		set_tcp(deletee, 0);
	}
}

#ifdef IGNORE_VRSAVE
/* omit the test; slightly faster */
#define VRTST(i)
#else
#define VRTST(i)					\
		"	bc  4,"#i",1f      \n"
#endif

#define STVX(i,b,o)					\
		VRTST(i)					\
	"	stvxl "#i","#b","#o" \n"	\
	"1:						\n"

#define LDVX(i,b,o)					\
		VRTST(i)					\
	"	lvxl "#i","#b","#o" \n"		\
	"1:						\n"

#define S4VEC(i,b0,b1,b2,b3,ro)		\
		STVX(i+0,b0,ro)				\
		STVX(i+1,b1,ro)				\
		STVX(i+2,b2,ro)				\
		STVX(i+3,b3,ro)	

#define L4VEC(i,b0,b1,b2,b3,ro)		\
		LDVX(i+0,b0,ro)				\
		LDVX(i+1,b1,ro)				\
		LDVX(i+2,b2,ro)				\
		LDVX(i+3,b3,ro)

#define Z4VEC(b0,b2,ro)			\
	"	dcbz "#b0","#ro"        \n"	\
	"	dcbz "#b2","#ro"        \n"

#define P4VEC(b0,b2,ri,rj)		\
	"	addi "#rj","#ri",64     \n"	\
		Z4VEC(b0,b2,rj)

#define L4VECA(i,b0,b1,b2,b3,ro) 	\
		L4VEC(i,b0,b1,b2,b3,ro)		\
	"	addi "#ro","#ro",64     \n"	\
	
#define S4VEC_P(i,b0,b1,b2,b3,ri,rj)	\
		P4VEC(b0,b2,ri,rj)		\
		S4VEC(i,b0,b1,b2,b3,ri)

#define S8VEC_P(i,b0,b1,b2,b3,ri,rj)	\
		S4VEC_P(i+0,b0,b1,b2,b3,ri,rj)	\
		S4VEC_P(i+4,b0,b1,b2,b3,rj,ri)

#define L8VECA(i,b0,b1,b2,b3,ro)		\
		L4VECA(i,b0,b1,b2,b3,ro)		\
		L4VECA(i+4,b0,b1,b2,b3,ro)

#define S32VEC(b0,b1,b2,b3,ri,rj)		\
		S8VEC_P(0,b0,b1,b2,b3,ri,rj)	\
		S8VEC_P(8,b0,b1,b2,b3,ri,rj)	\
		S8VEC_P(16,b0,b1,b2,b3,ri,rj)	\
		S4VEC_P(24,b0,b1,b2,b3,ri,rj)	\
		S4VEC(28,b0,b1,b2,b3,rj)		\
		

#define L32VEC(b0,b1,b2,b3,ro)			\
		L8VECA(0,b0,b1,b2,b3,ro)		\
		L8VECA(8,b0,b1,b2,b3,ro)		\
		L8VECA(16,b0,b1,b2,b3,ro)		\
		L4VECA(24,b0,b1,b2,b3,ro)		\
		L4VEC (28,b0,b1,b2,b3,ro)

STATIC void
save_vec_context(VecCtxt buf)
{
	asm volatile(
			"	mfvrsave 0  \n"
#ifndef IGNORE_VRSAVE
			"	mtcr 0		\n"
#endif
			"	li	 5,32   \n"
			"	dcbt 0,%1	\n"	/* preload 1st line where vcsr and vrsave are to be stored */
			                    /* DONT dcbz; the BPTR is stored in the first line         */
			"	dcbz 5,%1	\n" /* preload 2 lines for 1st 4 vectors                       */
			"	dcbz 5,%3	\n"
			S32VEC(%1,%2,%3,%4,5,4)
			"	mfvscr   0  \n"
			"	stw 0,0(%1) \n" /* store vrsave at offset 0                                */
			"	li  5,12    \n" /* select 3rd word of v0                                   */
			"	stvewx 0,%1,5\n"/* store vscr                                             */
			:"=m"(*buf)
			:"b"(*buf+0),"b"(*buf+1),"b"(*buf+2),"b"(*buf+3)
			:"r0","r4","r5"
#ifndef IGNORE_VRSAVE
			 ,"cr2","cr3","cr4"
#endif
			);
}

STATIC void
load_vec_context(VecCtxt buf)
{
	asm volatile(
		"	lwz    4,0(%0)     \n"
#ifndef IGNORE_VRSAVE
		"	mtcr   4           \n"
#endif
		"	mtvrsave 4         \n"
		"	li     4,12        \n"
		"	lvewx  0,4,%0      \n" 
		"	mtvscr 0           \n"
		"	addi   4,4,20      \n"
		L32VEC(%0,%1,%2,%3,4)
		:
		:"b"(*buf+0), "b"(*buf+1), "b"(*buf+2), "b"(*buf+3), "m"(*buf)
		:"r4"
#ifndef IGNORE_VRSAVE
		 ,"cr2","cr3","cr4"
#endif
	);
}

/* block size in 16byte units; count: #blocks (of 'size'); stride in bytes */
static inline void
dst0_new_context(void *ea, uint32_t size, uint32_t count, uint32_t stride)
{
#ifndef PSIM
	asm volatile("dst %0,%1,%2"::"r"(ea),"b"((size<<24)|(count<<16)|stride),"i"(0));
#endif
}

STATIC void
vec_thread_switch(rtems_tcb *executing, rtems_tcb *heir)
{
VecCtxt vp = get_tcp(heir);
	dssall();
	if ( vp && heir != vec_owner ) {
		uint32_t msr = set_MSR_VE();
		dst0_new_context(vp,
						PPC_CACHE_ALIGNMENT/VEC_ALIGNMENT,
						sizeof(VecCtxtBuf)/PPC_CACHE_ALIGNMENT,
						PPC_CACHE_ALIGNMENT);
		if ( vec_owner )
			save_vec_context(get_tcp(vec_owner));
		load_vec_context(vp);
		vec_owner = heir;
		/* Note the potential for corruption: if the executing thread
		 * has VE, it may corrupt the heir's v-context since it is
		 * now loaded.
		 * We cannot switch VE off, however, since this would result
		 * in an incorrect MSR being saved!
		 * Consequence of not having a 'post-switch' hook...
		 */
		rst_MSR_VE(msr);
	}
}

#if 0
STATIC void
vec_thread_begin(rtems_tcb *executing)
{
}

STATIC void
vec_thread_exited(rtems_tcb *executing)
{
}
#endif

#if !defined( ALL_THREADS_ALTIVEC ) && HAVE_CEXP
int
_cexpModuleFinalize(void *mod)
{
rtems_id          id;
rtems_status_code sc;

	_Thread_Disable_dispatch();
	if ( vec_owner ) {
		id = vec_owner->Object.id;
		_Thread_Enable_dispatch();
		fprintf(stderr,"At least one thread (0x%08x) still using AltiVec; cannot shutdown\n", (unsigned)id);
		return -1;
	}
	id = vec_extension_id;
	vec_extension_id = 0;
	_Thread_Enable_dispatch();
	if ( id ) {
		if ( RTEMS_SUCCESSFUL != (sc = rtems_extension_delete( id )) ) {
			vec_extension_id = id;
			rtems_error(sc, NAM": unable to delete extension");
			return -1;
		}
	}
	return 0;
}
#endif

#endif /* defined(HAVE_NATIVE_ALTIVEC) */
