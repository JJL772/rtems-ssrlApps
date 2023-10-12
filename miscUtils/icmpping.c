/*
 *  This file contains a simple ICMP ping client.
 *
 *  COPYRIGHT (c) 2005.
 *  LOYTEC electronics GmbH, 2005.
 *
 *  Thomas Rauscher <trauscher@loytec.com>
 *
 *  Modified by Till Straumann <strauman@slac.stanford.edu>
 *
 *  The license and distribution terms for this file may be
 *  found in the file LICENSE in this distribution or at
 *  http://www.OARcorp.com/rtems/license.html.
 *
 */
#ifndef __rtems__
#include "compat.h"
#else
#include <rtems.h>
#endif

#include <stdint.h>

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>

#include <arpa/inet.h>

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "icmpping.h"

rtems_ping_cfg_t rtems_ping_standard =
{
    {1, 0},           /* Timeout (1s) */
    64-ICMP_MINLEN    /* Payload      */
};

static void rtems_ping_pattern(rtems_ping_t *ping)
{
  unsigned i;
  int n;
  uint8_t *p = (uint8_t *)ping->icmp_req->icmp_data;

  /* Write payload pattern */
  for(i=0, n=0x80+ping->seq; i<ping->payload_size; i++, n++)
  {
    *p++ = n;
  }
}

static uint16_t rtems_ping_cksum(uint16_t *p, int len)
{
  uint32_t accu = 0;

  /* IP checksum */
  
  for(; len>1; len-=2)
  {
    accu += *p++;
  }

  if (len == 1) {
    accu += *p & 0xff00;
  }

  accu = (accu >> 16) + (accu & 0xffff);
  accu += (accu >> 16);
  return((~accu) & 0x0000ffff);
}

rtems_ping_t *rtems_ping_open(uint32_t              ip_addr,
			      const rtems_ping_cfg_t *cfg)
{
  rtems_ping_t *ping;
  size_t        payload_size;

  /* Check if to use standard config. */
  if(!cfg)
  {
    cfg = &rtems_ping_standard;
  }

  /* Determine payload size. */
  payload_size = cfg->payload_size;
  if(payload_size < 64 - ICMP_MINLEN)
  {
    payload_size = 64 - ICMP_MINLEN;
  }

  /* Get memory for ping control structure. */
  ping = calloc(sizeof(*ping), 1);
  if(!ping)
  {
    rtems_ping_close(ping);
    return NULL;
  }

  /* Get memory for packets */
  ping->req_size     = ((unsigned char *)ping->icmp_req->icmp_data - 
			(unsigned char *)ping->icmp_req) + payload_size;
  ping->resp_size    = IP_MAXPACKET;
  ping->raw_req      = calloc(ping->req_size, 1);
  ping->raw_resp     = calloc(ping->resp_size, 1);
  ping->payload_size = payload_size;
  ping->timeout      = cfg->timeout;
  ping->seq          = rand();

  if(!ping->raw_req || !ping->raw_resp)
  {
    rtems_ping_close(ping);
    return NULL;
  }
  
  /* Open RAW ICMP socket */
  ping->socket = socket(AF_INET, SOCK_RAW, 1);
  if(ping->socket == -1)
  {
    rtems_ping_close(ping);
    return NULL;
  }

  /* Set destination address */
  memset(&ping->dest_u, 0, sizeof(ping->dest_u));
  ping->dest_u.dest_in.sin_family = AF_INET;
  ping->dest_u.dest_in.sin_addr.s_addr = htonl(ip_addr);

  /* Initialize request */
  ping->icmp_req->icmp_type  = ICMP_ECHO;
  ping->icmp_req->icmp_code  = 0;
  ping->icmp_req->icmp_id    = (int) ping;

  return ping;
}

/* Disable for now; Read_timer is not really portable nor implemented
 * by most CPUs; i386 version was renamed and is not ticking in ns
 * anyways...
 */
#if defined(__rtems__) && (defined(__PPC__) || defined(__mcf528x__) || defined(__i386__)) && 0
#define USE_TIMER
#define TICKS_PER_S 1000000
#define US_PER_TICK 1
#ifdef __PPC__
/* Unfortunately the PPC Read_timer routine contains an
 * assert() statement which prevents it from overflowing :-(
 *
 * Also, the long-timer apparently returns ns...
 */
uint32_t ppc_Read_timer()
{
extern unsigned long long Read_long_timer();
	return (uint32_t)(Read_long_timer() / (uint64_t)1000);
}
#define Read_timer() ppc_Read_timer()
#endif
#else
#include <rtems/rtems/clock.h>
static inline unsigned long
ticks_per_s_fn(void)
{
	return (unsigned long)rtems_clock_get_ticks_per_second();
}

static inline unsigned long
us_per_tick_fn(void)
{
	return 1000000/ticks_per_s_fn();
}
#define TICKS_PER_S ticks_per_s_fn()
#define US_PER_TICK us_per_tick_fn()
#endif

int rtems_ping_send(rtems_ping_t *ping, rtems_interval *trip_time)
{
  int                retval;
  int                sent;
  int                rcvd;
  socklen_t          srclen;
  union	{
  	struct sockaddr_in src_in;
	struct sockaddr    src_sa;
  } src_u;
  rtems_interval     send_time;
  rtems_interval     rcv_time;
  n_short            id = (n_short) (((unsigned long) ping) & 0xffff);
  struct timeval     tv;
  unsigned long      tps  = TICKS_PER_S;
  unsigned long      uspt = US_PER_TICK;
  rtems_interval     timeout;

  if(!ping) {
    return EINVAL;
  }

  /* Increment sequence number */
  ping->seq++;

  /* Set values for this ping ... */
  rtems_ping_pattern(ping);
  ping->icmp_req->icmp_cksum = 0;
  ping->icmp_req->icmp_seq   = ping->seq;
  ping->icmp_req->icmp_id    = id;

  /* ... and update checksum. */
  ping->icmp_req->icmp_cksum = rtems_ping_cksum((uint16_t*)ping->raw_req,
						ping->req_size);

#ifdef USE_TIMER
  send_time = Read_timer();
#else
  /* Get time for ping. */
  send_time = rtems_clock_get_ticks_since_boot();
#endif

  rcv_time = send_time; /* For first timeout calculation */
#ifndef USE_TIMER
  timeout = send_time + (ping->timeout.tv_sec * tps) +
    ((ping->timeout.tv_usec * tps) / 1000000);
#else
  timeout = send_time + (ping->timeout.tv_sec * tps) +
    ping->timeout.tv_usec * (tps/1000000);
#endif

  /* Send ping. */
  sent = sendto(ping->socket, ping->raw_req, ping->req_size, 0,
		&ping->dest_u.dest_sa, sizeof(ping->dest_u.dest_sa));
  
  /* Wait for pong. */
  while(1) 
  {
    if(rcv_time >= timeout) 
    {
      retval = ETIMEDOUT;
      break;
    }

    rcv_time = timeout - rcv_time;
    /* Set timeout */
    tv.tv_sec  = (rcv_time / tps);
    tv.tv_usec = (rcv_time % tps) * uspt;
    if(setsockopt(ping->socket, SOL_SOCKET,
		  SO_RCVTIMEO, (char*)&(tv),  sizeof(tv))<0)
    {
      retval = -1;
      break;
    }

    /* Wait for response. */
    srclen = sizeof(src_u.src_sa);
    memset(ping->raw_resp, 0, ping->req_size);
    rcvd = recvfrom(ping->socket, ping->raw_resp, ping->resp_size, 0, 
		    &src_u.src_sa, &srclen);
    
    /* Get ICMP response */
    ping->icmp_resp = ((struct icmp *)
		       ((unsigned char*)ping->raw_resp +
			ping->raw_resp->ip_hl * 4));

    if(rcvd < 0)
    {
      /* Nothing received ... */
      retval = ETIMEDOUT;
      break;
    }

    /* Packet received, remember receive time */
#ifdef USE_TIMER
	rcv_time = Read_timer();
#else
	rcv_time = rtems_clock_get_ticks_since_boot();
#endif

    /* Check if packet is response to our ping */
    if((ping->icmp_resp->icmp_type == ICMP_ECHOREPLY) &&
       (ping->icmp_resp->icmp_id == id) && 
       (ping->icmp_resp->icmp_seq == ping->icmp_req->icmp_seq) &&
       (src_u.src_in.sin_addr.s_addr == ping->dest_u.dest_in.sin_addr.s_addr))
    {
      if(trip_time)
      {
	*trip_time = (rtems_interval)((int)rcv_time - (int)send_time) * uspt;
      }
      /* Check if payload matches. */
      retval = (memcmp(ping->icmp_req->icmp_data,
		       ping->icmp_resp->icmp_data +
		       (((struct ip *) ping->icmp_resp)->ip_hl<<2),
		       ping->payload_size)==0) ? 0 : EIO;
      break;
    }
  }

  /* Done. */
  return retval;
}

int rtems_ping_close(rtems_ping_t *ping)
{
  int retval = 0;
  
  if(ping)
  {
    /* Close socket. */
    if(ping->socket > 2)
    {
      retval = close(ping->socket);
    }
    
    /* Free memory. */
    free(ping->raw_resp);
    free(ping->raw_req);
    free(ping);
  }

  return retval;
}

int rtems_ping(char *ipaddr, int nretries, int quiet)
{
int             err, errs = 0, retries = nretries;
in_addr_t ipa = inet_addr(ipaddr);
rtems_interval  trip;
unsigned long long atrip = 0;
rtems_ping_t	*pp;

	if ( INADDR_ANY == ipa ) {
		fprintf(stderr,"rtems_ping: invalid IP addr [dot notation]\n");
		return -(nretries+2);
	}

	if ( ! (pp = rtems_ping_open(ntohl(ipa), 0)) ) {
		fprintf(stderr,"rtems_ping_open: error\n");
		return -(nretries+2);
	}
	do {
		if ( (err = rtems_ping_send(pp, &trip)) ) {
			fprintf(stderr,"rtems_ping_send: %s\n",strerror(err));
			errs++;
		} else {
			if ( !quiet )
				printf("Got reply -- trip time %lu us\n",trip);
			atrip += trip;
		}
	} while ( --retries >= 0 );

	rtems_ping_close(pp);

	atrip /= (nretries+1);

	return errs ? -errs : (atrip > 0x7fffffff ? 0x7fffffff : (uint32_t)atrip);
}

#ifdef HAVE_CEXP
#include <cexpHelp.h>
CEXP_HELP_TAB_BEGIN(icmpping)
	HELP(
"'ping' an IP address and print information to stdout.\n"
"(Zero trip times could be due to coarse timer resolution)\n"
"RETURNS: trip time in us (success) or value < 0 (failure)",
	int, rtems_ping, (char *ipaddr, int retries, int quiet)
	),
CEXP_HELP_TAB_END
#endif
