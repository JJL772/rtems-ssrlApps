/* $Id$ */

/* Network configuration utilities */

/* T.S, 2005,2006,2007 */

#include <rtems.h>
#include <rtems/rtems_bsdnet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/sockio.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <net/route.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <bsp.h>

#define DEBUG

typedef union {
	struct sockaddr    soa;
	struct sockaddr_in sin;
} sockaddr_alias_u;

/* configure interface 'name' to use IP address 'addr' and netmask 'msk'
 * (both strings in IP 'dot' notation). Bring IF up.
 *
 * 'addr' may be NULL to bring the IF down.
 */
int
ifconf(char *nam, char *addr, char *msk_s)
{
sockaddr_alias_u   sin,msk;
int	               flags;
int                rval = -1;

	if ( !nam ) {
		fprintf(stderr,"usage: ifconf(char *if_name, char *ip_addr, char *ip_mask)\n");
		return -1;
	}

	memset( &sin, 0, sizeof(sin) );
	memset( &msk, 0, sizeof(msk) );
	sin.sin.sin_len     = msk.sin.sin_len    = sizeof(sin);
	sin.sin.sin_family  = msk.sin.sin_family = AF_INET;
	sin.sin.sin_port    = msk.sin.sin_port   = htons(0);

	flags = addr ? IFF_UP : 0;

	if ( rtems_bsdnet_ifconfig(nam, SIOCSIFFLAGS, &flags ) ) {
		fprintf(stderr,"Unable to bring '%s' %s\n", nam, addr ? "UP" : "DOWN");
		return -1;
	}
	if ( !addr ) {
		if ( rtems_bsdnet_ifconfig(nam, SIOCGIFADDR, &sin.sin) ) {
			fprintf(stderr,"Unable to retrieve interface address (cannot delete route through this IF)\n");
			return -1;
		}
		if ( rtems_bsdnet_ifconfig(nam,SIOCGIFNETMASK,&msk.sin) ) {
			fprintf(stderr,"Unable to retrieve interface netmask (cannot delete route through this IF)\n");
			return -1;
		}
		sin.sin.sin_addr.s_addr &= msk.sin.sin_addr.s_addr;
		if ( rtems_bsdnet_rtrequest( RTM_DELETE, &sin.soa, 0, &msk.soa, RTF_UP, NULL) ) {
			perror("Unable to delete route through this IF");
			return -1;
		}
		return 0;
	}

	if ( !inet_aton(addr, &sin.sin.sin_addr) ) {
		fprintf(stderr,"Invalid IP address '%s'\n", addr);
		goto cleanup;
	}
	if ( rtems_bsdnet_ifconfig(nam,SIOCSIFADDR,&sin.sin) ) {
		fprintf(stderr,"Unable to set IP address on '%s' (%s)\n", nam, strerror(errno));
		goto cleanup;
	}		
	if ( msk_s ) {
		if ( !inet_aton(msk_s, &sin.sin.sin_addr) ) {
			fprintf(stderr,"Invalid IP MASK '%s'\n", msk_s);
			goto cleanup;
		}
		if ( rtems_bsdnet_ifconfig(nam,SIOCSIFNETMASK,&sin.sin) ) {
			fprintf(stderr,"Unable to set NETMASK address on '%s' (%s)\n", nam, strerror(errno));
			goto cleanup;
		}
	}
	rval = 0;
cleanup:
	if ( rval ) {
		flags = 0;
		if ( rtems_bsdnet_ifconfig(nam, SIOCSIFFLAGS, &flags ) ) {
			fprintf(stderr,"Unable to bring '%s' DOWN (%s)\n", nam, strerror(errno));
		}
	}
	return rval;
}

/* Attach interface, optionally using given ethernet address
 * (if supported by driver -- some drivers might require the address)
 *
 * enet_addr must be a pointer to an array of 6 octets defining
 * the ethernet address (or a NULL pointer to let the driver use
 * the predefined address programmed into the hardware).
 *
 * RETURNS: 0 on success, nonzero on error.
 */
int
ifattach(char *name, int (*attach_fn)(struct rtems_bsdnet_ifconfig *,int), uint8_t *enet_addr)
{
int                          rval;
struct rtems_bsdnet_ifconfig cfg;

	memset(&cfg, 0, sizeof(cfg));

	if ( !name ) {
		fprintf(stderr,"Usage: ifattach(char *drv_name, int (*drv_attach_fn)(struct rtems_bsdnet_ifconfig *, int), uint8_t *enet_addr)\n");
		return -1;
	}

	cfg.name   = name;
	cfg.attach = attach_fn;
	cfg.next   = 0;
	/* configure the interface later */
	cfg.ip_address = 0;
	cfg.ip_netmask = 0;
	cfg.hardware_address = enet_addr;

	cfg.ignore_broadcast = 0;
	cfg.mtu              = 0;
	cfg.rbuf_count       = 0;
	cfg.xbuf_count       = 0;

	cfg.port             = 0;
	cfg.irno             = 0;
	cfg.bpar             = 0;

	cfg.drv_ctrl         = 0;

	rval = cfg.attach(&cfg, 1);

	return rval;
}

/* Manage routing table entries
 *
 * 'add': add route if nonzero, delete if zero
 * 'dst': destination/target of route (string in IP dot notation)
 *        If the destination ANDed with the complement of the netmask 
 *        is empty (zero) the route is treated as a 'net' route
 *        otherwise as a 'host' route.
 * 'gwy': gateway (destination reached through 'gwy')
 *        (string in IP dot notation). May be NULL for ordinary routes.
 * 'msk': netmask. May be NULL (in this case a default according
 *        to the class of the 'dst' network will be computed.
 *
 * RETURNS: 0 on success; prints error info and returns nonzero if
 *          a error is encountered.
 */

int
rtconf(int add, char *dst_s, char *gwy_s, char *msk_s)
{
sockaddr_alias_u dst, gwy, msk;
int              flags = 0;

	if ( !dst_s ) {
		fprintf(stderr,"Need destination address\n");
		return -1;
	}

	memset( &dst, 0, sizeof(dst) );
	memset( &gwy, 0, sizeof(gwy) );
	memset( &msk, 0, sizeof(msk) );

	dst.sin.sin_len    = gwy.sin.sin_len = msk.sin.sin_len = sizeof(dst.sin);
	dst.sin.sin_family = gwy.sin.sin_family = msk.sin.sin_family = AF_INET;

	gwy.sin.sin_addr.s_addr = INADDR_ANY;
	msk.sin.sin_addr.s_addr = INADDR_ANY;

	if ( !inet_aton(dst_s, &dst.sin.sin_addr) ) {
		fprintf(stderr,"Destination: Invalid IP address '%s'\n", dst_s);
		goto cleanup;
	}
	if ( gwy_s && !inet_aton(gwy_s, &gwy.sin.sin_addr) ) {
		fprintf(stderr,"Gateway: Invalid IP address '%s'\n", gwy_s);
		goto cleanup;
	}
	if ( msk_s && !inet_aton(msk_s, &msk.sin.sin_addr) ) {
		fprintf(stderr,"Netmask: Invalid IP address '%s'\n", msk_s);
		goto cleanup;
	}

	if ( gwy_s )
		flags |= RTF_GATEWAY;

	if ( msk_s ) {
		if (dst.sin.sin_addr.s_addr & ~msk.sin.sin_addr.s_addr)
			flags |= RTF_HOST;
	} else {
		struct in_addr a = inet_makeaddr(inet_netof(dst.sin.sin_addr),0);
		struct in_addr b = inet_makeaddr(inet_netof(dst.sin.sin_addr),-1);

		if ( inet_lnaof(dst.sin.sin_addr) )
			flags |= RTF_HOST;

		msk.sin.sin_addr.s_addr = ~ ( a.s_addr ^ b.s_addr );
	}

	if ( add )
		flags |= RTF_UP;
		
	flags |= RTF_STATIC;

#ifdef DEBUG
	printf("DST: %s, ", inet_ntoa(dst.sin.sin_addr));
	printf("GWY: %s, ", inet_ntoa(gwy.sin.sin_addr));
	printf("MSK: %s, ", inet_ntoa(msk.sin.sin_addr));
	printf("FLGS: 0x%08x\n", flags);
#endif

	if ( rtems_bsdnet_rtrequest(
			add ? RTM_ADD : RTM_DELETE,
			&dst.soa,
			&gwy.soa,
			&msk.soa,
			flags,
			NULL) ) {
		perror("Route request");
		goto cleanup;
	}

	return 0;

cleanup:
	return -1;
}

#ifdef HAVE_CEXP
#include <cexpHelp.h>
CEXP_HELP_TAB_BEGIN(miscNetUtil)
	HELP(
        "Configure interface 'name' to use IP address 'addr' and\n"
		"netmask 'msk' (both strings in IP 'dot' notation) and bring\n"
		"IF up. 'addr' may be NULL to bring the IF down.\n\n"
		"RETURNS: zero on success, nonzero on error\n",
	int, ifconf, (char *nam, char *addr, char *msk_s)
	),
	HELP(
		"Manage routing table entries\n\n"
		"  'add': add route if nonzero, delete if zero\n"
        "  'dst': destination/target of route (string in IP dot notation)\n"
        "         If the destination ANDed with the complement of the netmask\n"
        "         is empty (zero) the route is treated as a 'net' route\n"
        "         otherwise as a 'host' route.\n"
        "  'gwy': gateway (destination reached through 'gwy')\n"
        "         (string in IP dot notation). May be NULL for ordinary routes.\n"
        "  'msk': netmask. May be NULL (in this case a default according\n"
        "         to the class of the 'dst' network will be computed.\n\n"
		"RETURNS: zero on success, nonzero on error\n",
	int, rtconf, (int add, char *dst_s, char *gwy_s, char *msk_s)
	),
CEXP_HELP_TAB_END
#endif
