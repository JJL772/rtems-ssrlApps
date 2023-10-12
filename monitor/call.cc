#include <rtems.h>
#include <rtems/monitor.h>
#include <rtems/cpuuse.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <libtecla.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ISMINVERSION(ma,mi,re) \
	(    __RTEMS_MAJOR__  > (ma)	\
	 || (__RTEMS_MAJOR__ == (ma) && __RTEMS_MINOR__  > (mi))	\
	 || (__RTEMS_MAJOR__ == (ma) && __RTEMS_MINOR__ == (mi) && __RTEMS_REVISION__ >= (re)) \
    )

#if ISMINVERSION(4,6,99)
typedef rtems_monitor_command_arg_t *monfargt;
#define ARG2PTR(arg) ((arg)->symbol_table)
#define CMD2ARG(cmd) (&(cmd)->command_arg)
#else
typedef rtems_monitor_command_arg_t monfargt;
#define ARG2PTR(arg) (arg)
#define CMD2ARG(cmd) ((cmd)->command_arg)
#endif

#if ISMINVERSION(4,8,99)
#define rtems_get_node rtems_object_id_get_node
#else
#define bool boolean
#endif

#if ISMINVERSION(4,8,0)
/* BAD: somewhere along the line the API for cpuuse was changed :-( but there
 * was no change in the RTEMS version numbers, so you might have
 * to tune things up here manually...
 */
#define CPU_usage_Dump  rtems_cpu_usage_report
#define CPU_usage_Reset rtems_cpu_usage_reset
#endif

static rtems_id monitorMutex=0;

extern rtems_monitor_command_entry_t rtems_monitor_commands[];

int
taskStack(rtems_id thread_id);

int
memUsageDump(void);

static void
fnwrap(int argc, char **argv, monfargt arg, bool verbose)
{
unsigned long	iarg;
char			*endp;
if ( argc > 0 ) {
		fprintf(stderr,"1 arg %s\n",argv[1]);
	if (argv[1]) {
		iarg=strtol(argv[1],&endp,0);
		if (!*argv[1] || *endp) {
			fprintf(stderr,"Invalid Argument\n");
			return;
		}
	} else {
		iarg=0;
	}
	((void (*)(unsigned long))ARG2PTR(arg))(iarg);
} else {
		fprintf(stderr,"no args\n");
	((void (*)(void))ARG2PTR(arg))();
}
}

#define NUMBEROF(arr) (sizeof(arr)/sizeof(arr[0]))

class command_entry_builder : public rtems_monitor_command_entry_t {
public:
	command_entry_builder(
		const char *name, const char *help, unsigned args_req, 
		void *fp)
		{
		command                  = (char*)name;
		usage                    = (char*)help;
		arguments_required       = args_req;
		command_function         = (rtems_monitor_command_function_t)fnwrap;
#if ISMINVERSION(4,6,99)
		command_arg.symbol_table = (rtems_symbol_table_t**)fp;
#else
		command_arg              = (rtems_monitor_command_arg_t)fp;
#endif
		next                     = 0;
		}
};

static
rtems_monitor_command_entry_t entries[] = {
	command_entry_builder(
		"cpuUsageDump",
		"cpuUsageDump",
		0,
		(void*)CPU_usage_Dump
	),
	command_entry_builder(
		"cpuUsageReset",
		"cpuUsageReset",
		0,
		(void*)CPU_usage_Reset
	),
	command_entry_builder(
		"threadStack",
		"threadStack [taskId]; give stacktrace of a task (0 for self)",
		1,
		(void*)taskStack
	),
	command_entry_builder(
		"memUsageDump",
		"memUsageDump; show used/free amount of RTEMS workspace and malloc heap",
		1,
		(void*)memUsageDump
	)
};

int
rtemsMonitor(int verbose)
{
GetLine	*gl=0;
char	*line,*buf=0;

	rtems_semaphore_obtain(monitorMutex, RTEMS_WAIT, RTEMS_NO_TIMEOUT);

	if (rtems_monitor_task_id) {
		rtems_semaphore_release(monitorMutex);
		fprintf(stderr,"RTEMS Monitor already in use, giving up...\n");
		return -1;
	}
	/* acquire the monitor task id and store our id there;
	 * serves as 'busy' mark
	 */
	rtems_task_ident(RTEMS_SELF, 0, &rtems_monitor_task_id);
	rtems_semaphore_release(monitorMutex);

	rtems_monitor_default_node=
	rtems_monitor_node = rtems_get_node(rtems_monitor_task_id);

	/* make sure symbols are loaded */
	rtems_monitor_symbols_loadup();

	/* create a line editor */
	if ( ! (gl = new_GetLine(200,100)) ) {
		fprintf(stderr,"No memory for line editor\n");
		goto cleanup;
	}

	printf("Entering the RTEMS Monitor; it has its own shell -- try 'help'...\n");

	while ( (line=gl_get_line(gl,"monitor>",NULL,0)) ) {
		int		argc;
		char	*argv[20]; /* no overrun protection */
#if ISMINVERSION(4,9,99)
		const
#endif
		rtems_monitor_command_entry_t	*cmd;

		free(buf);
		if ( ! (buf=strdup(line)) ){
			fprintf(stderr,"No memory for strdup()\n");
			goto cleanup;
		}

		assert(rtems_monitor_make_argv(buf,&argc,argv) < (int)(sizeof(argv)/sizeof(argv[0])));

		/* catch exit/quit which call the fatal error handler */
		if (!strcmp("exit",argv[0]) ||
		    !strcmp("quit",argv[0]))
			break;

#if ISMINVERSION(4,9,99)
		cmd = rtems_monitor_command_lookup(argv[0]);
#else
		cmd=rtems_monitor_command_lookup(rtems_monitor_commands, argc, argv);
#endif

		if ( cmd ) {
        	cmd->command_function(argc, argv, CMD2ARG(cmd), verbose);
		} else {
			fprintf(stderr,"Command '%s' not found\n",argv[0]);
		}
	}

	printf("Leaving the RTEMS Monitor\n");

cleanup:
	/* destroy the line editor */
	if (gl)
		del_GetLine(gl);

	free(buf);

	/* release the monitor */
	rtems_semaphore_obtain(monitorMutex, RTEMS_WAIT, RTEMS_NO_TIMEOUT);
	rtems_monitor_task_id=0;
	rtems_semaphore_release(monitorMutex);
	return 0;
}

#if ISMINVERSION(4,9,99)
int
_cexpModuleFinalize(void *unused)
{
	fprintf(stderr,"RTEMS 4.10 eliminated 'rtems_monitor_erase_cmd()' -- cannot unload this module\n");
	return -1;
}
#endif


#ifdef __cplusplus
};

class CallUtils {
public:
	CallUtils() {
		unsigned i;
		if (nest++) return;
		rtems_semaphore_create( rtems_build_name('c','u','t','m'),
								1,
						        RTEMS_PRIORITY|RTEMS_BINARY_SEMAPHORE|
								RTEMS_INHERIT_PRIORITY|
								RTEMS_NO_PRIORITY_CEILING|RTEMS_LOCAL,
								0,
								&monitorMutex);
		for (i=0; i<NUMBEROF(entries); i++)
			rtems_monitor_insert_cmd(entries+i);
	};
	~CallUtils() {
		if (--nest) return;
#if ISMINVERSION(4,9,99)
		nest++;
#else
		for (int i=0; i<NUMBEROF(entries); i++)
			rtems_monitor_erase_cmd(entries+i);
		rtems_semaphore_delete(monitorMutex);
#endif
	};
static int nest;
};

static CallUtils oneOnly;

int CallUtils::nest=0;

#endif
