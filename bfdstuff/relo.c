#include <bfd.h>
#include <libiberty.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "tab.h"

#ifdef USE_MDBG
#include <mdbg.h>
#endif

#ifdef USE_ELF_STUFF
#include "elf-bfd.h"
#endif

#define CACHE_LINE_SIZE 32

#define MAX_NUM_MODULES 256
#define LD_WORDLEN		5
#define BITMAP_DEPTH	((MAX_NUM_MODULES)>>(LD_WORDLEN))
#define BITMAP_SET(bm,bitno) (((bm)[(bitno)>>LD_WORDLEN]) |= (1<<((bitno)&((1<<LD_WORDLEN)-1))))
#define BITMAP_TST(bm,bitno) (((bm)[(bitno)>>LD_WORDLEN]) &  (1<<((bitno)&((1<<LD_WORDLEN)-1))))
typedef	unsigned		BitmapWord;

/* an output segment description */
typedef struct SegmentRec_ {
	PTR				chunk;		/* pointer to memory */
	unsigned long	vmacalc;	/* working counter */
	unsigned long	size;
	unsigned		attributes; /* such as 'read-only' etc.; currently unused */
} SegmentRec, *Segment;

#define NUM_SEGS 1

typedef struct LinkDataRec_ {
	SegmentRec	segs[NUM_SEGS];
	asymbol		**st;
	int			errors;
} LinkDataRec, *LinkData;

/* how to decide where a particular section should go */
static Segment
segOf(LinkData ld, asection *sect)
{
	/* multiple sections not supported (yet) */
	return &ld->segs[0];
}

/* determine the alignment power of a common symbol
 * (currently only works for ELF)
 */
#ifdef USE_ELF_STUFF
static __inline__ int
get_align_pwr(bfd *abfd, asymbol *sp)
{
register unsigned long rval=0,tst;
elf_symbol_type *esp;
	if (esp=elf_symbol_from(abfd, sp))
		for (tst=1; tst<esp->internal_elf_sym.st_size; rval++)
			tst<<=1;
	return rval;
}

/* we sort in descending order; hence the routine must return b-a */
static int
align_size_compare(const void *a, const void *b)
{
elf_symbol_type *espa, *espb;
asymbol			*spa=*(asymbol**)a;
asymbol			*spb=*(asymbol**)b;

	return
		((espa=elf_symbol_from(bfd_asymbol_bfd(spa),spa)) &&
	     (espb=elf_symbol_from(bfd_asymbol_bfd(spb),spb)))
		? (espb->internal_elf_sym.st_size - espa->internal_elf_sym.st_size)
		: 0;
}
#else
#define get_align_pwr(abfd,sp)	0
#define align_size_compare		0
#endif

static asection *sdatasect = 0;

static void
s_basic(bfd *abfd, asection *sect, PTR arg)
{
	/* TSILL */
	if (!strcmp(bfd_get_section_name(abfd,sect),".sdata2")) {
		sdatasect = sect;
		printf("found an SDATA section\n");
	}
}


static void
s_count(bfd *abfd, asection *sect, PTR arg)
{
Segment		seg=segOf((LinkData)arg, sect);
	printf("Section %s, flags 0x%08x\n",
			bfd_get_section_name(abfd,sect), sect->flags);
	printf("size: %i, alignment %i\n",
			bfd_section_size(abfd,sect),
			(1<<bfd_section_alignment(abfd,sect)));
	if (SEC_ALLOC & sect->flags) {
		seg->size+=bfd_section_size(abfd,sect);
		seg->size+=(1<<bfd_get_section_alignment(abfd,sect));
	}
}

static void
s_setvma(bfd *abfd, asection *sect, PTR arg)
{
Segment		seg=segOf((LinkData)arg, sect);

	if (SEC_ALLOC & sect->flags) {
		seg->vmacalc=align_power(seg->vmacalc, bfd_get_section_alignment(abfd,sect));
		printf("%s allocated at 0x%08x\n",
				bfd_get_section_name(abfd,sect),
				seg->vmacalc);
		bfd_set_section_vma(abfd,sect,seg->vmacalc);
		seg->vmacalc+=bfd_section_size(abfd,sect);
		sect->output_section = sect;
	}
	if (sect && sect == sdatasect)
		sect->output_section->symbol->value = 32768;
}


static void
s_reloc(bfd *abfd, asection *sect, PTR arg)
{
LinkData	ld=(LinkData)arg;
int			i;
long		err;
char		buf[1000];

	if ( ! (SEC_ALLOC & sect->flags) )
		return;

	/* read section contents to its memory segment
	 * NOTE: this automatically clears section with
	 *       ALLOC set but with no CONTENTS (such as
	 *       bss)
	 */
	bfd_get_section_contents(
		abfd,
		sect,
		(PTR)bfd_get_section_vma(abfd,sect),
		0,
		bfd_section_size(abfd,sect)
	);

	/* if there are relocations, resolve them */
	if ((SEC_RELOC & sect->flags)) {
		arelent **cr=0,r;
		long	sz;
		sz=bfd_get_reloc_upper_bound(abfd,sect);
		if (sz<=0) {
			fprintf(stderr,"No relocs for section %s???\n",
					bfd_get_section_name(abfd,sect));
			return;
		}
		/* slurp the relocation records; build a list */
		cr=(arelent**)xmalloc(sz);
		sz=bfd_canonicalize_reloc(abfd,sect,cr,ld->st);
		if (sz<=0) {
			fprintf(stderr,"ERROR: unable to canonicalize relocs\n");
			free(cr);
			return;
		}
		for (i=0; i<sect->reloc_count; i++) {
			arelent *r=cr[i];
			printf("relocating (%s=",
					bfd_asymbol_name(*(r->sym_ptr_ptr))
					);
			if (bfd_is_und_section(bfd_get_section(*r->sym_ptr_ptr))) {
				printf("UNDEFINED), skipping...\n");
			} else {
				printf("0x%08x)->0x%08x\n",
				bfd_asymbol_value(*(r->sym_ptr_ptr)),
				r->address);

				if ((err=bfd_perform_relocation(
					abfd,
					r,
					(PTR)bfd_get_section_vma(abfd,sect),
					sect,
					0 /* output bfd */,
					0)))
				fprintf(stderr,"Relocation failed (err %i)\n",err);
			}
		}
		free(cr);
	}
}

/* resolve undefined and common symbol references;
 * The routine also rearranges the symbol table for
 * all common symbols that must be created to be located
 * at the beginning of the symbol list.
 *
 * RETURNS:
 *   value >= 0 : OK, number of new common symbols
 *   value <0   : -number of errors (unresolved refs or multiple definitions)
 *
 * SIDE EFFECTS:
 *   - all new common symbols moved to the beginning of the table
 *   - resolved undef or common slots are replaced by new symbols
 *     pointing to the ABS section
 *   - KEEP flag is set for all globals defined by this object.
 *   - raise a bit for each module referenced by this new one.
 */
static int
resolve_syms(bfd *abfd, asymbol **syms, BitmapWord *depend)
{
asymbol *sp;
int		i,num_new_commons=0,errs=0,modind;

	/* resolve undefined and common symbols;
	 * find name clashes
	 */
	for (i=0; sp=syms[i]; i++) {
		asection *sect=bfd_get_section(sp);
		TstSym		ts;

		/* we only care about global symbols
		 * (NOTE: undefined symbols are neither local
		 *        nor global)
		 */
		if ( (BSF_LOCAL & sp->flags) )
			continue;

		ts=tstSymLookup(bfd_asymbol_name(sp), &modind);

		if (bfd_is_und_section(sect)) {
printf("TSILL undef (value 0x%08x, flags 0x%08x): %s\n",
				bfd_asymbol_value(sp),
				sp->flags,
				bfd_asymbol_name(sp));
			if (ts) {
				/* Resolved reference; replace the symbol pointer
				 * in this slot with a new asymbol holding the
				 * resolved value.
				 */
				sp=syms[i]=bfd_make_empty_symbol(abfd);
				/* copy name pointer */
				bfd_asymbol_name(sp) = bfd_asymbol_name(ts);
				sp->value=ts->value;
				sp->section=sdatasect ? sdatasect : bfd_abs_section_ptr;
				sp->flags=BSF_GLOBAL;
				/* mark the referenced module in the bitmap */
				assert(modind < MAX_NUM_MODULES);
				BITMAP_SET(depend,modind);
			} else {
				fprintf(stderr,"Unresolved symbol: %s\n",bfd_asymbol_name(sp));
				errs++;
			}
		}
		else if (bfd_is_com_section(sect)) {
			if (ts) {
				/* use existing value of common sym */
				sp = bfd_make_empty_symbol(abfd);

				/* TODO: check size and alignment */

				/* copy pointer to old name */
				bfd_asymbol_name(sp) = bfd_asymbol_name(syms[i]);
				sp->value=ts->value;
				sp->section=bfd_abs_section_ptr;
				sp->flags=BSF_GLOBAL;
				/* mark the referenced module in the bitmap */
				assert(modind < MAX_NUM_MODULES);
				BITMAP_SET(depend,modind);
			} else {
				/* it's the first definition of this common symbol */
				asymbol *swap;

				/* we'll have to add it to our internal symbol table */
				sp->flags |= BSF_KEEP;

				/* this is a new common symbol; we move all of these
				 * to the beginning of the 'st' list
				 */
				swap=syms[num_new_commons];
				syms[num_new_commons++]=sp;
				sp=swap;
			}
			syms[i]=sp; /* use new instance */
		} else {
			if (ts) {
				fprintf(stderr,"Symbol '%s' already exists\n",bfd_asymbol_name(sp));

				errs++;
				/* TODO: check size and alignment; allow multiple
				 *       definitions??? - If yes, we have to track
				 *		 the dependencies also.
				 */
			} else {
				/* new symbol defined by the loaded object; account for it */
				/* mark for second pass */
				sp->flags |= BSF_KEEP;
			}
		}
	}

	return errs ? -errs : num_new_commons;
}

/* make a dummy section holding the new common data introduced by
 * the newly loaded file.
 *
 * RETURNS: 0 on failure, nonzero on success
 */
static int
make_new_commons(bfd *abfd, asymbol **syms, int num_new_commons)
{
unsigned long	i,val;
asection	*csect;

	if (num_new_commons) {
		/* make a dummy section for new common symbols */
		csect=bfd_make_section(abfd,bfd_get_unique_section_name(abfd,".dummy",0));
		if (!csect) {
			bfd_perror("Creating dummy section");
			return -1;
		}
		csect->flags |= SEC_ALLOC;

		/* our common section alignment is the maximal alignment
		 * found during the sorting process which is the alignment
		 * of the first element...
		 */
		bfd_section_alignment(abfd,csect) = get_align_pwr(abfd,syms[0]);

		/* set new common symbol values */
		for (val=0,i=0; i<num_new_commons; i++) {
			asymbol *sp;
			int tsill;

			sp = bfd_make_empty_symbol(abfd);

			val=align_power(val,(tsill=get_align_pwr(abfd,syms[i])));
printf("TSILL align_pwr %i\n",tsill);
			/* copy pointer to old name */
			bfd_asymbol_name(sp) = bfd_asymbol_name(syms[i]);
			sp->value=val;
			sp->section=csect;
			sp->flags=syms[i]->flags;
			val+=syms[i]->value;
			syms[i] = sp;
		}
		
		bfd_set_section_size(abfd, csect, val);
	}
	return 0;
}

static asymbol **
slurp_symtab(bfd *abfd, BitmapWord *depend)
{
asymbol 		**rval=0;
long			i;
long			num_new_commons;

	if (!(HAS_SYMS & bfd_get_file_flags(abfd))) {
		fprintf(stderr,"No symbols found\n");
		return 0;
	}
	if ((i=bfd_get_symtab_upper_bound(abfd))<0) {
		fprintf(stderr,"Fatal error: illegal symtab size\n");
		return 0;
	}
	if (i) {
		rval=(asymbol**)xmalloc(i);
	}
	if (bfd_canonicalize_symtab(abfd,rval) <= 0) {
		bfd_perror("Canonicalizing symtab");
		free(rval);
		return 0;
	}


	if ((num_new_commons=resolve_syms(abfd,rval,depend))<0) {
		free(rval);
		return 0;
	}
		
	/*
	 *	sort st[0]..st[num_new_commons-1] by alignment
	 */
	if (align_size_compare && num_new_commons)
		qsort((void*)rval, num_new_commons, sizeof(*rval), align_size_compare);

	/* Now, everything is in place to build our internal symbol table
	 * representation.
	 * We cannot do this later, because the size information will be lost.
	 * However, we cannot provide the values yet; this can only be done
	 * after relocation has been performed.
	 */

	/* TODO buildCexpSymtab(); */

	if (0!=make_new_commons(abfd,rval,num_new_commons)) {
		free(rval);
		/* TODO destroyCexpSymtab(); */
		return 0;
	}

	return rval;
}

static int
flushCache(LinkData ld)
{
#if defined(_ARCH_PPC) || defined(__PPC__) || defined(__PPC) || defined(PPC)
int i,j;
	for (i=0; i<NUM_SEGS; i++) {
		for (j=0; j<= ld->segs[i].size; j+=CACHE_LINE_SIZE)
			__asm__ __volatile__(
				"dcbf %0, %1\n"	/* flush out one data cache line */
				"icbi %0, %1\n" /* invalidate cached instructions for this line */
				::"b"(ld->segs[i].chunk),"r"(j));
	}
	/* enforce flush completion and discard preloaded instructions */
	__asm__ __volatile__("sync; isync");
#endif
}

#if 0
typedef struct CexpModuleRec_ {
	SymTab
	char *name;
	Segments					/* memory segments */
	CexpModuleList	depend;		/* modules referenced this one */
	CexpModule		next;		/* linked list of modules */
} CexpModuleRec, *CexpModule;
#endif

#ifdef __rtems__
#define main relo_main

int relo(char *feil)
{
char *argv[2]={"relo",0};
int argc = 1;
	if (feil)
		argv[argc++]=feil;
	return relo_main(argc, argv);
}

#endif

int
main(int argc, char **argv)
{
bfd 			*abfd=0;
LinkDataRec		ldr;
int				rval=1,i,j;
TstSym			sm;
BitmapWord		depend[BITMAP_DEPTH];

	memset(&ldr,0,sizeof(ldr));
	memset(depend,0,sizeof(depend));

	/* basic check for our bitmaps */
	assert( (1<<LD_WORDLEN) <= sizeof(BitmapWord)*8 );

	/* TODO: locking, module name */

	if (argc<2) {
		fprintf(stderr,"Need filename arg\n");
		goto cleanup;
	}
printf("TSILL: bfd_log2(1025)=%i\n",bfd_log2(1025));

	bfd_init();
#ifdef USE_MDBG
	mdbgInit();
#endif
	if ( ! (abfd=bfd_openr(argv[1],0)) ) {
		bfd_perror("Opening object file");
		goto cleanup;
	}
	if (!bfd_check_format(abfd, bfd_object)) {
		bfd_perror("Checking format");
		goto cleanup;
	}

	ldr.errors=0;
	bfd_map_over_sections(abfd, s_basic, &ldr);

	if (!(ldr.st=slurp_symtab(abfd,depend))) {
		fprintf(stderr,"Error creating symbol table\n");
		goto cleanup;
	}

	ldr.errors=0;
	bfd_map_over_sections(abfd, s_count, &ldr);

	/* allocate segment space */
	for (i=0; i<NUM_SEGS; i++)
		ldr.segs[i].vmacalc=(unsigned long)ldr.segs[i].chunk=xmalloc(ldr.segs[i].size);

	ldr.errors=0;
	bfd_map_over_sections(abfd, s_setvma, &ldr);

	ldr.errors=0;
memset(ldr.segs[0].chunk,0xee,ldr.segs[0].size); /*TSILL*/
	bfd_map_over_sections(abfd, s_reloc, &ldr);

	/* TODO: setCexpSymtabValues() */

	flushCache(&ldr);

	/* TODO: call constructors */

	/* TODO: set dependency lists

	for (m=first, i=0; m; m=m->next, i++) {
		if (BITMAP_TST(depend,i))
			add_to_dep_lists(thismod, m);
	}

	 */

	for (i=0; ldr.st[i]; i++) {
		if (0==strcmp(bfd_asymbol_name(ldr.st[i]),"blah")) {
			printf("FOUND blah; is 0x%08x\n",bfd_asymbol_value(ldr.st[i]));
			((void (*)(int))bfd_asymbol_value(ldr.st[i]))(0xfeedcafe);
		}
	}
#ifdef __rtems__
	{
	extern int cexpDisassemble();
	extern int cexpDisassemblerInit();
	char *di = malloc(500);
	cexpDisassemblerInit(di,stdout);
	cexpDisassemble(ldr.segs[0].chunk,10,di);
	free(di);
	}
#endif

	rval=0;

cleanup:
	if (ldr.st) free(ldr.st);
	if (abfd) bfd_close_all_done(abfd);
	for (i=0; i<NUM_SEGS; i++)
		if (ldr.segs[i].chunk) free(ldr.segs[i].chunk);

	/* TODO unlock */
#ifdef USE_MDBG
	printf("Memory leaks found: %i\n",mdbgPrint(0,0));
#endif
	return rval;
}

/* TODO: unload a module
	
	if (check_references())
		error("still needed");

	remove_from_module_list(this);

	invalidate_caches();
	free_resources();
	remove_from_deplists();
 */
