#define __RTEMS_VIOLATE_KERNEL_VISIBILITY__
#include <rtems.h>
#include <rtems/score/cpu.h>
#include <rtems/score/thread.h>
#include <rtems/score/threadimpl.h>
#include <rtems/score/percpu.h>
#include <stdio.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef __PPC__
#include <libcpu/stackTrace.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#ifdef HAVE_CEXP
#include <cexp.h>
#endif
#endif


#define NumberOf(arr) (sizeof(arr)/sizeof((arr)[0]))

#define GETREG(var, nr)	__asm__ __volatile__("mr %0, %1":"=r"(var##nr):"i"(nr))

#ifdef __PPC__
int
taskStack(rtems_id id)
{
Thread_Control		*tcb;
void				*stackbuf[30];
int					i;
Context_Control		ctrl;
ppc_context			regs;
ISR_lock_Context	lctx;
rtems_interrupt_level isrl;
int isr_disabled = 0;

	/* silence compiler warning about unused regs */
	memset(&ctrl,0,sizeof(ctrl));

	tcb = _Thread_Get(id, &lctx);
	if (!_Objects_Is_local_id(id) || !tcb) {
		if (tcb) {
			rtems_interrupt_disable(isrl);
			isr_disabled = 1;
		}
		fprintf(stderr,"Id %x not found on local node\n",(unsigned)id);
		return -1;
	}
	stackbuf[0]=0;
	if (_Thread_Executing==tcb)
		tcb=0;
	ppc_context* ctx = ppc_get_context(&tcb->Registers);
	CPU_stack_take_snapshot(
					stackbuf,
					NumberOf(stackbuf),
					(void*)0,
					0,/*(void*)(ctx ? ctx->pc : 0), TODO!!! */
					(void*)(ctx ? ctx->gpr1 : 0));
	if (tcb) {
		memcpy(&regs, ppc_get_context(&tcb->Registers), sizeof(regs));
	}

	if (isr_disabled)
		rtems_interrupt_enable(isrl);

	if (!tcb) {
		GETREG(regs.gpr,1);
		GETREG(regs.gpr,14);
		GETREG(regs.gpr,15);
		GETREG(regs.gpr,16);
		GETREG(regs.gpr,17);
		GETREG(regs.gpr,18);
		GETREG(regs.gpr,19);
		GETREG(regs.gpr,20);
		GETREG(regs.gpr,21);
		GETREG(regs.gpr,22);
		GETREG(regs.gpr,23);
		GETREG(regs.gpr,24);
		GETREG(regs.gpr,25);
		GETREG(regs.gpr,26);
		GETREG(regs.gpr,27);
		GETREG(regs.gpr,28);
		GETREG(regs.gpr,29);
		GETREG(regs.gpr,30);
		GETREG(regs.gpr,31);
		__asm__ __volatile__("mfcr %0":"=r"(regs.cr));
		__asm__ __volatile__("mfmsr %0":"=r"(regs.msr));
	}
	printf("\nRegisters:\n");
	printf("GPR1:  0x%08x\n",
			(unsigned)regs.gpr1);
	printf("GPR14: 0x%08x, GPR15: 0x%08x\n",
			(unsigned)regs.gpr14, (unsigned)regs.gpr15);
	printf("GPR16: 0x%08x, GPR17: 0x%08x, GPR18: 0x%08x, GPR19: 0x%08x\n",
			(unsigned)regs.gpr16, (unsigned)regs.gpr17, (unsigned)regs.gpr18, (unsigned)regs.gpr19);
	printf("GPR20: 0x%08x, GPR21: 0x%08x, GPR22: 0x%08x, GPR23: 0x%08x\n",
			(unsigned)regs.gpr20, (unsigned)regs.gpr21, (unsigned)regs.gpr22, (unsigned)regs.gpr23);
	printf("GPR24: 0x%08x, GPR25: 0x%08x, GPR26: 0x%08x, GPR27: 0x%08x\n",
			(unsigned)regs.gpr24, (unsigned)regs.gpr25, (unsigned)regs.gpr26, (unsigned)regs.gpr27);
	printf("GPR28: 0x%08x, GPR29: 0x%08x, GPR30: 0x%08x, GPR31: 0x%08x\n\n",
			(unsigned)regs.gpr28, (unsigned)regs.gpr29, (unsigned)regs.gpr30, (unsigned)regs.gpr31);
	printf("CR:    0x%08x\n",
			(unsigned)regs.cr);
	printf("MSR:   0x%08x\n",
			(unsigned)regs.msr);

	printf("\nStack Trace:\n");

	for (i=0; stackbuf[i] && i<NumberOf(stackbuf); i++) {
		void		*symaddr=stackbuf[i];
		unsigned	diff=(unsigned)stackbuf[i];
		char		buf[250];
		printf("0x%08x",(unsigned)stackbuf[i]);
#ifdef HAVE_CEXP
		if (0==cexpAddrFind(&symaddr,buf,sizeof(buf))) {
			diff=(unsigned)stackbuf[i]-(unsigned)symaddr;
			printf(" == <%s",buf);
			if (diff) {
				printf(" + 0x%x",diff);
			}
			fputc('>',stdout);
		}
#endif
		fputc('\n',stdout);
	}

	return 0;
}
#else
#warning CPU_stack_take_snapshot() needs to be implemented for your architecture
#warning register access/printing not implemented for this CPU architecture
#warning task stack dumping not implemented.

/* dummy on architectures where we don't have GETREG and CPU_stack_take_snapshot */
int
taskStack(rtems_id id)
{
	fprintf(stderr,"Dumping a task's stack is not implemented for this architecture\n");
	fprintf(stderr,"You need to implement CPU_stack_take_snapshot() and more, see\n");
	fprintf(stderr,"    %s\n",__FILE__);
	return -1;
}

#endif
