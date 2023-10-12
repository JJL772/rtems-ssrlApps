#ifdef __rtems__
#include <rtems.h>
#include <rtems/libio_.h>
#else
#define rtems_libio_number_iops 2000
#endif
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/socketvar.h>

/* disabled on RTEMS 5+ for now */
#if defined(__RTEMS_MAJOR__) && __RTEMS_MAJOR__ < 5
#define ENABLE_RTEMS_SOCKSTATS
#endif

#ifdef __rtems__
#include <sys/socketvar.h>
#include <sys/protosw.h>
#include <net/if.h>
#include <net/if_var.h>

#define _KERNEL
#include <sys/mbuf.h>
#endif

#if ! defined(ENABLE_RTEMS_SOCKSTATS)

#define LOCK()   do {} while (0)
#define UNLOCK() do {} while (0)

static int notsock(int fd)
{
#if 0
struct stat sb;
	return fstat(fd, &sb) || ! S_ISSOCK(sb.st_mode) ;
#else
	/* getsockname() on non-socket fails anyways */
	return 0;
#endif
}

#else /* __rtems__ */

/* acquire locks in the same order RTEMS syscalls do! */
static inline void LOCK(void)
{
	rtems_bsdnet_semaphore_obtain();
	rtems_semaphore_obtain( rtems_libio_semaphore, RTEMS_WAIT, RTEMS_NO_TIMEOUT );
}

static inline void UNLOCK(void)
{
	rtems_semaphore_release( rtems_libio_semaphore );
	rtems_bsdnet_semaphore_release();
}

/*
 * Extra socket stats from the guts of RTEMS.
 * 'sbstats' caches the info from the so_rcv
 * and so_snd members of 'struct socket'.
 */
struct sbstats {
		u_int sb_cc;     /* chars in buffer                  */
		u_int sb_hiwat;  /* max char count                   */
		u_int sb_mbcnt;  /* chars of mbufs used              */
		u_int sb_mbmax;  /* max chars of mbufs to use        */
		u_int sb_lowat;  /* low-water mark                   */
		u_int nmbufs;    /* number of mbufs in chain         */
		short sb_flags;  /* flags                            */
		int   sb_timeo;  /* timeout for read/write           */
		u_int nmbcl1s;   /* number of single-ref. clusters   */
		u_int nmbclms;   /* number of multiply ref. clusters */
#ifndef NO_FLOATS
		float cl_frac;   /* fractional cluster use           */
#else
		u_int cl_refs;   /* multiple cluster references      */
#endif
};

/*
 * Extra socket stats from the guts of RTEMS.
 * 'sostats' caches the info from 'struct socket'.
 */
struct sostats {
	short	so_type;
	short   so_options;
	/* INET domain was already checked by getsockname        */
	short   pr_protocol;
	short   so_qlen;    /* unaccepted connections            */
	short   so_incqlen; /* unaccepted incomplete connections */
	short   so_qlimit;  /* max. number of queued connections */
	short   so_timeo;   /* timeout                           */
	struct sbstats so_rcv, so_snd;
};

static int
holdsclust(struct mbuf *mb)
{
uintptr_t b = (uintptr_t)mbutl;
uintptr_t e = (uintptr_t)mbutl + MCLBYTES * (mbstat.m_clusters - 1);

	return (    (mb->m_flags & M_EXT)
             && b <= mtod(mb, uintptr_t)
             && e >  mtod(mb, uintptr_t) ) ;
}

static void
getsbstats(struct sbstats *sbs, struct sockbuf *sob)
{
struct mbuf *mb, *mbp;
u_int        k,c1,cm,i;
#ifndef NO_FLOATS
float        r = 0.;
#else
u_int        r = 0;
#endif

	sbs->sb_cc    = sob->sb_cc;
	sbs->sb_hiwat = sob->sb_hiwat;
	sbs->sb_mbcnt = sob->sb_mbcnt;
	sbs->sb_mbmax = sob->sb_mbmax;
	sbs->sb_lowat = sob->sb_lowat;
	/* count mbufs */
	for ( k=r=c1=cm=0, mbp=sob->sb_mb; mbp; mbp=mbp->m_nextpkt ) {
		for ( mb = mbp; mb; mb=mb->m_next ) {
			k++;
			/* is a cluster attached ? */
			if ( holdsclust(mb) ) {
				/* a cluster may be referenced by other
				 * mbufs; 'c' counts the clusters this
				 * mbuf exclusively refers to.
				 * so we can get an idea of, relatively
				 * speaking, how many clusters this mb
				 * holds
				 */
				if ( 1 == (i = mclrefcnt[mtocl(mtod(mb, int))]) ) {
					c1++;
				} else {
					cm++;
#ifndef NO_FLOATS
					r += 1./(float)i;
#else
					r += i;
#endif
				}
			}
		}
	}
	sbs->nmbufs   = k;
	sbs->nmbcl1s  = c1;
	sbs->nmbclms  = cm;
#ifndef NO_FLOATS
	sbs->cl_frac  = r;
#else
	sbs->cl_refs  = r;
#endif
	sbs->sb_flags = sob->sb_flags;
	sbs->sb_timeo = sob->sb_timeo;
}

/*
 * Helper to print the members of 'struct sbstats'
 *
 * RETURNS: Number or chars printed.
 */
static int
prsbstats(FILE *f, struct sbstats *sb, int level)
{
int rval = 0;

	rval += fprintf(f, "     Number of mbufs in chain    : %u\n",      sb->nmbufs);
	rval += fprintf(f, "     Chars of mbufs used         : %u\n",      sb->sb_mbcnt);

	rval += fprintf(f, "     Number of cluster refs      : %u\n",      sb->nmbcl1s + sb->nmbclms);
	rval += fprintf(f, "        single references        : %u\n",      sb->nmbcl1s);
	rval += fprintf(f, "        multiple references      : %u\n",      sb->nmbclms);
#ifndef NO_FLOATS
	rval += fprintf(f, "        total use of clusters    : %g\n",      (double)sb->nmbcl1s + (double)sb->cl_frac);
#else
	rval += fprintf(f, "        referenced by others     : %u\n",      sb->cl_refs - sb->nmbclms);
#endif

	if ( level > 1 ) {
		rval += fprintf(f, "     Chars in buffer             : %u\n",      sb->sb_cc);
		rval += fprintf(f, "     Max char count (hiwat)      : %u\n",      sb->sb_hiwat);
		rval += fprintf(f, "     Max chars of mbufs to use   : %u\n",      sb->sb_mbmax);
		rval += fprintf(f, "     Low-water mark              : %u\n",      sb->sb_lowat);
		rval += fprintf(f, "     Flags                       : 0x%04hx\n", sb->sb_flags);
		rval += fprintf(f, "     Timeout for read/write      : %i\n",      sb->sb_timeo);
	}
	return rval;
}

/*
 * Print a string 'str' using 'fmt'. If 'str' is NULL then
 * print the numerical value of 'num' instead (but using a
 * different format, namely '%i').
 *
 * RETURNS: Number of chars printed.
 */
static int
prstrnum(FILE *f, const char *fmt, const char *str, int num)
{
int rval;

	rval = fprintf(f, fmt, str ? str : "");
	if ( !str ) {
		rval += fprintf(f, "%i", num);
	}

	return rval;
}

static const void   *sockhandlers = 0;

static void init_sockhdlrs()
{
int             dummyfd;
rtems_libio_t   *iop;

	if ( ! sockhandlers ) {
		if ( (dummyfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
			fprintf(stderr,"notsock: unable to initialize dummy socket!\n");
			return ;
		}
		iop = rtems_libio_iop(dummyfd);
		sockhandlers = iop->handlers;
		close(dummyfd);
	}
}

/*
 * fstat() could cause network traffic (if file on network FS) which
 * we must avoid since we hold the network semaphore!
 * Resort to a crude test...
 *
 * NOTE: This is only required because RTEMS' socket operations 
 *       don't test if a file descriptor is a socket but blindly
 *       assume it is (if iop->data1 != NULL).
 *       Ideally 'getsockname()' should return -1 and set
 *       errno = ENOTSOCK if the passed descriptor is not 
 *       referring to a socket.
 */
static int notsock(int fd)
{
rtems_libio_t       *iop;

	iop = rtems_libio_iop(fd);

	return !(iop->flags & LIBIO_FLAGS_OPEN) || iop->handlers != sockhandlers;
}
#endif

int
rtems_bsdnet_show_socket_stats(int level, int sd, FILE *f)
{
int             i,j,e,min,max;

union {
struct sockaddr sa;
struct sockaddr_in sin;
} ss,sp;
socklen_t       l;
char            buf[100];
#ifdef ENABLE_RTEMS_SOCKSTATS
/* grab a copy of some socket statistics */
struct sostats  sostats;
struct socket   *so      = 0;
rtems_libio_t   *iop     = 0;
#endif

	if ( ! f )
		f = stdout;

#ifdef ENABLE_RTEMS_SOCKSTATS
	init_sockhdlrs();
#endif

	/* Since file-descriptor zero is usually the (serial) console
	 * it will rarely be a socket. For convenience we use 'sd==0'
	 * as an indicator that they want info about all sockets.
	 * If there is one day a socket in the first slot they just
	 * can't look *only* at that one, sorry.
	 */

	/*
	 * Maybe we change the semantics one day so that sd<0 means 'all'...
	 */
	if ( sd < 0 )
		sd = 0;

	if ( sd ) {
		if ( sd >= rtems_libio_number_iops ) {
			/* invalid argument */
			return -1;
		}
		min = sd; max = sd+1;
	} else {
		min = 0;  max = rtems_libio_number_iops;
	}

	for ( i=min, j=0; i<max; i++ ) {
		l = sizeof(ss);
		LOCK();
		if ( notsock(i) || getsockname(i, &ss.sa, &l) ) {
			UNLOCK();
			continue;
		}
		if ( AF_INET != ss.sin.sin_family ) {
			UNLOCK();
			fprintf(f, "%i: <NOT INET>\n", i);
		} else {
			l = sizeof(sp);
			e = getpeername(i, &sp.sa, &l);

#ifdef ENABLE_RTEMS_SOCKSTATS
			/* Gather some statistics from the socket */
			if ( level && (iop = rtems_libio_iop(i)) ) {
				if ( (so = (struct socket*)iop->data1) ) {

					sostats.so_type      = so->so_type;
					sostats.so_options   = so->so_options;
					sostats.pr_protocol  = so->so_proto->pr_protocol;
					sostats.so_qlen      = so->so_qlen;
					sostats.so_incqlen   = so->so_incqlen;
					sostats.so_qlimit    = so->so_qlimit;
					sostats.so_timeo     = so->so_timeo;

					/* rx buffer */
					getsbstats(&sostats.so_rcv, &so->so_rcv);

					/* tx buffer */
					getsbstats(&sostats.so_snd, &so->so_snd);
				}
			}
#endif
			UNLOCK();

			j++;
			inet_ntop(AF_INET, &ss.sin.sin_addr, buf, sizeof(buf));
			fprintf(f, "%i: %16s:%5u <-> ", i, buf, ntohs(ss.sin.sin_port));


			if ( e ) {
				fprintf(f,"<UNKNOWN>\n");
			} else {
				inet_ntop(AF_INET, &sp.sin.sin_addr, buf, sizeof(buf));
				fprintf(f, "%16s:%5u\n", buf, ntohs(sp.sin.sin_port));
			}

#ifdef ENABLE_RTEMS_SOCKSTATS
			if ( level ) {
				/* print more information */
				if ( !iop ) {
					fprintf(f,"Unable to find socket IOP (why ?)\n");
				} else if ( ! so ) {
					fprintf(f,"Unable to find socket data (why ?)\n");
				} else {
					/* Everything found OK; print info */
					const char     *str;

					switch ( sostats.so_type ) {
						case SOCK_STREAM: str = "SOCK_STREAM"; break;
						case SOCK_DGRAM : str = "SOCK_DGRAM";  break;
						case SOCK_RAW   : str = "SOCK_RAW";    break;
						default:          str = 0;             break;
					}
					prstrnum(f,"   Type: %11s", str, sostats.so_type);
					fprintf(f,"; Options: 0x%04x;", sostats.so_options);
					switch ( sostats.pr_protocol ) {
						case IPPROTO_ICMP: str = "ICMP"; break;
						case IPPROTO_TCP : str = "TCP";  break;
						case IPPROTO_UDP : str = "UDP";  break;
						default:           str = 0;      break;
					}
					prstrnum(f, "   Protocol: %4s", str, sostats.pr_protocol);
					fprintf(f, "\n");
					if ( level > 1 ) {
						fprintf(f, "   Unaccepted connections           : %hi\n", sostats.so_qlen);
						fprintf(f, "   Unaccepted incomplete connections: %hi\n", sostats.so_incqlen);
						fprintf(f, "   Max. number of queued connection : %hi\n", sostats.so_qlimit);
						fprintf(f, "   Timeout                          : %hi\n", sostats.so_timeo);
					}
					fprintf(f, "   Receiving buffers:\n");
					prsbstats(f, &sostats.so_rcv, level);
					fprintf(f, "   Transmitting buffers:\n");
					prsbstats(f, &sostats.so_snd, level);
				}
			}
#endif
		}
		fprintf(f,"\n");
	}
	return j;
}

#ifdef HAVE_CEXP
#include <cexpHelp.h>
CEXP_HELP_TAB_BEGIN(sockstats)
	HELP(
"Dump local and peer (if applicable) address of all AF_INET\n"
"sockets in the system.\n"
"If 'level' is nonzero then more (internal) information is printed\n"
"If 'sd' is nonzero then only info about 'sd' is printed\n"
"RETURNS: number of live (used) sockets. The max. number of\n"
"         file descriptors can be obtained from the (READ-ONLY)\n"
"         variable 'rtems_libio_number_iops'.\n",
	int, rtems_bsdnet_show_socket_stats,  (int level, int sd, FILE *f)
	),
CEXP_HELP_TAB_END
#endif

#ifndef __rtems__
int main()
{
int sd;
union {
struct sockaddr_in sin;
struct sockaddr    sa;
} sa;
	sa.sin.sin_family = AF_INET;
	sa.sin.sin_addr.s_addr   = inet_addr("127.0.10.10");
	sa.sin.sin_port   = htons(5432);
	sd = socket(PF_INET, SOCK_STREAM, 0);
	bind(sd, &sa.sa, sizeof(sa));
	rtems_bsdnet_show_socket_stats(0,0,0);
}
#endif
