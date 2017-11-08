/****************************************************************************
 * netutils/dhcpd/dhcpd.c
 *
 *   Copyright (C) 2007-2009, 2011-2014 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/


#include "dhcpd.h"        /* Advertised DHCPD APIs */
#include "netlib.h"

#include <sys/socket.h>
#include <sys/ioctl.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

#include <net/if.h>
#include <net/if_arp.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/****************************************************************************
 * Private Data
 ****************************************************************************/

#define DHCP_SERVER_PORT         67
#define DHCP_CLIENT_PORT         68

/* Option codes understood in this file                                     */
/*                              Code    Data   Description                  */
/*                                      Length                              */
#define DHCP_OPTION_PAD           0  /*  1     Pad                          */
#define DHCP_OPTION_SUBNET_MASK   1  /*  1     Subnet Mask                  */
#define DHCP_OPTION_ROUTER        3  /*  4     Router                       */
#define DHCP_OPTION_DNS_SERVER    6  /*  4N    DNS                          */
#define DHCP_OPTION_REQ_IPADDR   50  /*  4     Requested IP Address         */
#define DHCP_OPTION_LEASE_TIME   51  /*  4     IP address lease time        */
#define DHCP_OPTION_OVERLOAD     52  /*  1     Option overload              */
#define DHCP_OPTION_MSG_TYPE     53  /*  1     DHCP message type            */
#define DHCP_OPTION_SERVER_ID    54  /*  4     Server identifier            */
#define DHCP_OPTION_END         255  /*  0     End                          */

/* Values for the dhcp msg 'op' field */

#define DHCP_REQUEST              1
#define DHCP_REPLY                2

/* DHCP message types understood in this file */

#define DHCPDISCOVER              1  /* Received from client only */
#define DHCPOFFER                 2  /* Sent from server only */
#define DHCPREQUEST               3  /* Received from client only */
#define DHCPDECLINE               4  /* Received from client only */
#define DHCPACK                   5  /* Sent from server only */
#define DHCPNAK                   6  /* Sent from server only */
#define DHCPRELEASE               7  /* Received from client only */
#define DHCPINFORM                8  /* Not used */

/* The form of an option is:
 *   code   - 1 byte
 *   length - 1 byte
 *   data   - variable number of bytes
 */

#define DHCPD_OPTION_CODE         0
#define DHCPD_OPTION_LENGTH       1
#define DHCPD_OPTION_DATA         2

/* Size of options in DHCP message */

#define DHCPD_OPTIONS_SIZE        312

/* Values for htype and hlen field */

#define DHCP_HTYPE_ETHERNET       1
#define DHCP_HLEN_ETHERNET        6

/* Values for flags field */

#define BOOTP_BROADCAST           0x8000

/* Legal values for this option are:
 *
 *   1     the 'file' field is used to hold options
 *   2     the 'sname' field is used to hold options
 *   3     both fields are used to hold options
 */

#define DHCPD_OPTION_FIELD        0
#define DHCPD_FILE_FIELD          1
#define DHCPD_SNAME_FIELD         2

#define ETHER_ADDR_LEN            6

#define ERROR                     -1
#define OK                        0

#ifndef CONFIG_NETUTILS_DHCPD_LEASETIME
#  define CONFIG_NETUTILS_DHCPD_LEASETIME (60*60*24*10) /* 10 days */
#  undef CONFIG_NETUTILS_DHCPD_MINLEASETIME
#  undef CONFIG_NETUTILS_DHCPD_MAXLEASETIME
#endif

#ifndef CONFIG_NETUTILS_DHCPD_MINLEASETIME
#  define CONFIG_NETUTILS_DHCPD_MINLEASETIME (60*60*24*1) /* 1 days */
#endif

#ifndef CONFIG_NETUTILS_DHCPD_MAXLEASETIME
#  define CONFIG_NETUTILS_DHCPD_MAXLEASETIME (60*60*24*30) /* 30 days */
#endif

#ifndef CONFIG_NETUTILS_DHCPD_OFFERTIME
#  define CONFIG_NETUTILS_DHCPD_OFFERTIME (60*60) /* 1 hour */
#endif

#ifndef CONFIG_NETUTILS_DHCPD_DECLINETIME
#  define CONFIG_NETUTILS_DHCPD_DECLINETIME (60*60) /* 1 hour */
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* This structure describes one element in the lease table.  There is one slot
 * in the lease table for each assign-able IP address (hence, the IP address
 * itself does not have to be in the table.
 */

struct lease_s {
	/* MAC address (network order) -- could be larger! */
	uint8_t mac[DHCP_HLEN_ETHERNET];
	/* true: IP address is allocated */
	bool allocated;
	/* Lease expiration time (seconds past Epoch) */
	time_t expiry;
};

struct dhcpmsg_s {
	uint8_t op;
	uint8_t htype;
	uint8_t hlen;
	uint8_t hops;
	uint8_t xid[4];
	uint16_t secs;
	uint16_t flags;
	uint8_t ciaddr[4];
	uint8_t yiaddr[4];
	uint8_t siaddr[4];
	uint8_t giaddr[4];
	uint8_t chaddr[16];
	uint8_t sname[64];
	uint8_t file[128];
	uint8_t options[312];
};

struct dhcpd_state_s {
	/* Server configuration */

	in_addr_t ds_serverip;     /* The server IP address */

	/* Message buffers */

	/* Holds the incoming DHCP client message */
	struct dhcpmsg_s ds_inpacket;
	/* Holds the outgoing DHCP server message */
	struct dhcpmsg_s ds_outpacket;

	/* Parsed options from the incoming DHCP client message */

	uint8_t	ds_optmsgtype;   /* Incoming DHCP message type */
	in_addr_t ds_optreqip;     /* Requested IP address (host order) */
	in_addr_t ds_optserverip;  /* Serverip IP address (host order) */
	time_t ds_optleasetime; /* Requested lease time (host order) */

	/* End option pointer for outgoing DHCP server message */

	uint8_t         *ds_optend;

	/* Leases */

	struct lease_s   *ds_leases;
};

typedef struct {

	unsigned int num_leases;

	const char *interface;

	in_addr_t startip;
	in_addr_t endip;
	in_addr_t netmask;
	in_addr_t gw_addr;
	in_addr_t *dns_addr;

	artik_loop_module *loop;
	int sockfd;
	int watch_id;

} dhcp_server_handle;

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const uint8_t	g_magiccookie[4] = {99, 130, 83, 99};
static const uint8_t	g_anyipaddr[4] = {0, 0, 0, 0};
static struct dhcpd_state_s g_state;

int set_arpmapping(const struct sockaddr_in *inaddr,
			const uint8_t *macaddr, const char *interface){
	int ret = -EINVAL;

	if (inaddr != NULL && macaddr != NULL) {
		int sockfd = socket(PF_INET, SOCK_DGRAM, 0);

		if (sockfd >= 0) {
			struct arpreq req;

			memcpy(&req.arp_pa, inaddr, sizeof(struct sockaddr_in));

			req.arp_ha.sa_family = ARPHRD_ETHER;
			memcpy(&req.arp_ha.sa_data, macaddr, ETHER_ADDR_LEN);
			strncpy(req.arp_dev, interface, 16);
			req.arp_flags = ATF_COM;

			ret = ioctl(sockfd, SIOCSARP, (unsigned long)
				((uintptr_t)&req));
			if (ret < 0) {
				ret = -errno;
				log_err("set_arpmapping ioctl : %s\n",
					strerror(errno));
			}
			close(sockfd);
		}
	}
	return ret;
}

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: dhcpd_arpupdate
 ****************************************************************************/

static inline void dhcpd_arpupdate(uint8_t *ipaddr, uint8_t *hwaddr,
							const char *interface)
{
	struct sockaddr_in inaddr;

	/* Put the protocol address in a standard form. ipaddr is assume to be
	 * in network order by the memcpy.
	 */

	inaddr.sin_family = AF_INET;
	inaddr.sin_port = 0;
	memcpy(&inaddr.sin_addr.s_addr, ipaddr, sizeof(in_addr_t));

	/* Update the ARP table */

	(void)set_arpmapping(&inaddr, hwaddr, interface);
}

/****************************************************************************
 * Name: dhcpd_time
 ****************************************************************************/

static time_t dhcpd_time(void)
{
	struct timespec ts;
	time_t ret = 0;

	if (clock_gettime(CLOCK_REALTIME, &ts) == OK)
		ret = ts.tv_sec;

	return ret;
}

/****************************************************************************
 * Name: dhcpd_leaseexpired
 ****************************************************************************/

static inline bool dhcpd_leaseexpired(struct lease_s *lease)
{
	bool ret;

	if (lease->expiry > dhcpd_time())
		ret = false;
	else {
		memset(lease, 0, sizeof(struct lease_s));
		ret = true;
	}

	return ret;
}

/****************************************************************************
 * Name: dhcpd_setlease
 ****************************************************************************/

struct lease_s *dhcpd_setlease(const uint8_t *mac, in_addr_t ipaddr,
			time_t expiry, unsigned int num_leases,
			in_addr_t startip)
{
	/* Calculate the offset from the first IP address managed by DHCPD.
	 * ipaddr must be in host order!
	 */

	int ndx = ipaddr - startip;
	struct lease_s *ret = NULL;

	log_dbg("ipaddr: %08x ipaddr: %08x ndx: %d MAX: %d\n",
	ipaddr, startip, ndx,
	num_leases);

	/* Verify that the address offset is within the supported range */

	if (ndx >= 0 && ndx < num_leases) {
		ret = &g_state.ds_leases[ndx];
		memcpy(ret->mac, mac, DHCP_HLEN_ETHERNET);
		ret->allocated = true;
		ret->expiry = dhcpd_time() + expiry;
	}

	return ret;
}

/****************************************************************************
 * Name: dhcp_leaseipaddr
 ****************************************************************************/

static inline in_addr_t dhcp_leaseipaddr(struct lease_s *lease,
					in_addr_t startip)
{
	/* Return IP address in host order */

	return (in_addr_t)(lease - g_state.ds_leases) + startip;
}

/****************************************************************************
 * Name: dhcpd_findbymac
 ****************************************************************************/

static struct lease_s *dhcpd_findbymac(const uint8_t *mac,
					unsigned int num_leases)
{
	int i;

	for (i = 0; i < num_leases; i++) {
		if (memcmp(g_state.ds_leases[i].mac, mac, DHCP_HLEN_ETHERNET)
									== 0)
			return &(g_state.ds_leases[i]);
	}

	return NULL;
}

/****************************************************************************
 * Name: dhcpd_findbyipaddr
 ****************************************************************************/

static struct lease_s *dhcpd_findbyipaddr(in_addr_t ipaddr, in_addr_t startip,
					in_addr_t endip)
{

	if (ipaddr >= startip && ipaddr <= endip) {
		struct lease_s *lease = &g_state.ds_leases[ipaddr - startip];

		log_dbg("dhcpd_findbyipaddr lease index = %d", ipaddr -
								startip);
		if (lease->allocated > 0) {
			log_dbg("return lease %d %d", ipaddr - startip,
			g_state.ds_leases[ipaddr - startip].allocated);
			return lease;
		}
	}

	log_dbg("return null");

	return NULL;
}

/****************************************************************************
 * Name: dhcpd_allocipaddr
 ****************************************************************************/

static in_addr_t dhcpd_allocipaddr(in_addr_t startip, in_addr_t endip)
{
	struct lease_s *lease = NULL;
	in_addr_t ipaddr;

	ipaddr = startip;
	for (; ipaddr <= endip; ipaddr++) {
		/* Skip over address ending in 0 or 255 */

		struct in_addr addr;

		addr.s_addr = htonl(ipaddr);

		if (verify_ipv4addr_in_used(&addr) == OK)
			continue;

		if ((ipaddr & 0xff) == 0 || (ipaddr & 0xff) == 0xff)
			continue;

		/* Is there already a lease on this address?  If so, has
		 * it expired?
		 */

		lease = dhcpd_findbyipaddr(ipaddr, startip, endip);
		if ((!lease  || dhcpd_leaseexpired(lease))) {
			log_dbg("Lease pass !!");

			log_dbg("Leases table = %d %d", ipaddr - startip,
			g_state.ds_leases[ipaddr - startip].allocated);

			memset(g_state.ds_leases[ipaddr - startip].mac, 0,
							DHCP_HLEN_ETHERNET);
			g_state.ds_leases[ipaddr - startip].allocated = true;
			g_state.ds_leases[ipaddr - startip].expiry =
				dhcpd_time() + CONFIG_NETUTILS_DHCPD_OFFERTIME;

			/* Return the address in host order */

			return ipaddr;
		}
	}

	return 0;
}

/****************************************************************************
 * Name: dhcpd_parseoptions
 ****************************************************************************/

static inline bool dhcpd_parseoptions(void)
{
	uint32_t tmp;
	uint8_t *ptr;
	uint8_t overloaded;
	uint8_t currfield;
	int optlen = 0;
	int remaining;

	/* Verify that the option field starts with a valid magic number */

	ptr = g_state.ds_inpacket.options;
	if (memcmp(ptr, g_magiccookie, 4) != 0) {
		/* Bad magic number... skip g_state.ds_outpacket */

		log_err("ERROR: Bad magic: %d,%d,%d,%d\n",
		ptr[0], ptr[1], ptr[2], ptr[3]);
		return false;
	}

	/* Set up to parse the options */

	ptr       += 4;
	remaining  = DHCPD_OPTIONS_SIZE - 4;
	overloaded = DHCPD_OPTION_FIELD;
	currfield  = DHCPD_OPTION_FIELD;

	/* Set all options to the default value */

	g_state.ds_optmsgtype   = 0;    /* Incoming DHCP message type */
	g_state.ds_optreqip     = 0;    /* Requested IP address (host order) */
	g_state.ds_optserverip  = 0;    /* Serverip IP address (host order) */
	g_state.ds_optleasetime = 0;    /* Requested lease time (host order) */
	g_state.ds_optend       = NULL;

	do {
		/* The form of an option is:
		 *   code   - 1 byte
		 *   length - 1 byte
		 *   data   - variable number of bytes
		 */

		switch (ptr[DHCPD_OPTION_CODE]) {
			/* Skip over any padding bytes */

		case DHCP_OPTION_PAD:
			optlen = 1;
			break;

			/* the Overload option is used to indicate that the
			 * DHCP 'sname' or 'file' fields are being overloaded by
			 * using them to carry DHCP options. A DHCP server
			 * inserts this option if the returned parameters will
			 * exceed the usual space allotted for options.
			 *
			 * If this option is present, the client interprets the
			 * specified additional fields after it concludes
			 * interpretation of the standard option fields.
			 *
			 * Legal values for this option are:
			 *
			 *   1     the 'file' field is used to hold options
			 *   2     the 'sname' field is used to hold options
			 *   3     both fields are used to hold options
			 */

		case DHCP_OPTION_OVERLOAD:
			optlen = ptr[DHCPD_OPTION_LENGTH] + 2;
			if (optlen >= 1 && optlen < remaining)
				overloaded = ptr[DHCPD_OPTION_DATA];
			break;

		case DHCP_OPTION_END:
			if (currfield == DHCPD_OPTION_FIELD &&
					(overloaded & DHCPD_FILE_FIELD) != 0) {
				ptr       = g_state.ds_inpacket.file;
				remaining = sizeof(g_state.ds_inpacket.file);
				currfield = DHCPD_FILE_FIELD;
			} else if (currfield == DHCPD_FILE_FIELD &&
			     (overloaded & DHCPD_SNAME_FIELD) != 0) {
				ptr       = g_state.ds_inpacket.sname;
				remaining = sizeof(g_state.ds_inpacket.sname);
				currfield = DHCPD_SNAME_FIELD;
			} else
				return true;
			break;

		case DHCP_OPTION_REQ_IPADDR: /* Requested IP Address */
			optlen = ptr[DHCPD_OPTION_LENGTH] + 2;
			if (optlen >= 4 && optlen < remaining) {
				memcpy(&tmp, &ptr[DHCPD_OPTION_DATA], 4);
				g_state.ds_optreqip = (in_addr_t)ntohl(tmp);
			}
			break;

		case DHCP_OPTION_LEASE_TIME: /* IP address lease time */
			optlen = ptr[DHCPD_OPTION_LENGTH] + 2;
			if (optlen >= 4 && optlen < remaining) {
				memcpy(&tmp, &ptr[DHCPD_OPTION_DATA], 4);
				g_state.ds_optleasetime = (time_t)ntohl(tmp);
			}
			break;

		case DHCP_OPTION_MSG_TYPE: /* DHCP message type */
			optlen = ptr[DHCPD_OPTION_LENGTH] + 2;
			if (optlen >= 1 && optlen < remaining)
				g_state.ds_optmsgtype = ptr[DHCPD_OPTION_DATA];
			break;

		case DHCP_OPTION_SERVER_ID: /* Server identifier */
			optlen = ptr[DHCPD_OPTION_LENGTH] + 2;
			if (optlen >= 4 && optlen < remaining) {
				memcpy(&tmp, &ptr[DHCPD_OPTION_DATA], 4);
				g_state.ds_optserverip = (in_addr_t)ntohl(tmp);
			}
			break;

		default:
			/* Skip over unsupported options */

			optlen = ptr[DHCPD_OPTION_LENGTH] + 2;
			break;
		}

		/* Advance to the next option */

		ptr       += optlen;
		remaining -= optlen;
	} while (remaining > 0);

	return false;
}

/****************************************************************************
 * Name: dhcpd_verifyreqip
 ****************************************************************************/

static inline bool dhcpd_verifyreqip(in_addr_t startip, in_addr_t endip)
{
	struct lease_s *lease;

	/* Verify that the requested IP address is within the supported lease
	 * range
	 */

	if (g_state.ds_optreqip >= startip && g_state.ds_optreqip <= endip) {
	/* And verify that the lease has not already been taken or offered
	 * (unless the lease/offer is expired, then the address is free game).
	 */

		lease = dhcpd_findbyipaddr(g_state.ds_optreqip, startip, endip);
		log_dbg("lease = %d", lease);
		if (!lease || dhcpd_leaseexpired(lease)) {
			log_dbg("can't use IP %081x", (long)
							g_state.ds_optreqip);
			return true;
		}
	}

	return false;
}

/****************************************************************************
 * Name: dhcpd_verifyreqleasetime
 ****************************************************************************/

static inline bool dhcpd_verifyreqleasetime(uint32_t *leasetime)
{
	uint32_t tmp = g_state.ds_optleasetime;

	/* Did the client request a specific lease time? */

	if (tmp != 0) {
		/* Yes..  Verify that the requested lease time is within a
		 * valid range
		 */

		if (tmp > CONFIG_NETUTILS_DHCPD_MAXLEASETIME)
			tmp = CONFIG_NETUTILS_DHCPD_MAXLEASETIME;
		else if (tmp < CONFIG_NETUTILS_DHCPD_MINLEASETIME)
			tmp = CONFIG_NETUTILS_DHCPD_MINLEASETIME;

		/* Return the clipped lease time */

		*leasetime = tmp;
		return true;
	}

	return false;
}

/****************************************************************************
 * Name: dhcpd_addoption
 ****************************************************************************/

static int dhcpd_addoption(uint8_t *option)
{
	int offset;
	int len = 4;

	if (g_state.ds_optend) {
		offset = g_state.ds_outpacket.options - g_state.ds_optend;
		len    = option[DHCPD_OPTION_LENGTH] + 2;

		/* Check if the option will fit into the options array */

		if (offset + len + 1 < DHCPD_OPTIONS_SIZE) {
			/* Copy the option into the option array */

			memcpy(g_state.ds_optend, option, len);
			g_state.ds_optend += len;
			*g_state.ds_optend  = DHCP_OPTION_END;
		}
	}

	return len;
}

/****************************************************************************
 * Name: dhcpd_addoption8
 ****************************************************************************/

static int dhcpd_addoption8(uint8_t code, uint8_t value)
{
	uint8_t option[3];

	/* Construct the option sequence */

	option[DHCPD_OPTION_CODE]   = code;
	option[DHCPD_OPTION_LENGTH] = 1;
	option[DHCPD_OPTION_DATA]   = value;

	/* Add the option sequence to the response */

	return dhcpd_addoption(option);
}

/****************************************************************************
 * Name: dhcpd_addoption32
 ****************************************************************************/

static int dhcpd_addoption32(uint8_t code, uint32_t value)
{
	uint8_t option[6];

	/* Construct the option sequence */

	option[DHCPD_OPTION_CODE]   = code;
	option[DHCPD_OPTION_LENGTH] = 4;
	memcpy(&option[DHCPD_OPTION_DATA], &value, 4);

	/* Add the option sequence to the response */

	return dhcpd_addoption(option);
}

/****************************************************************************
 * Name: dhcp_addoption32p
 ****************************************************************************/

static int dhcp_addoption32p(uint8_t code, uint8_t *value)
{
	uint8_t option[6];

	/* Construct the option sequence */

	option[DHCPD_OPTION_CODE]   = code;
	option[DHCPD_OPTION_LENGTH] = 4;
	memcpy(&option[DHCPD_OPTION_DATA], value, 4);

	/* Add the option sequence to the response */

	return dhcpd_addoption(option);
}

/****************************************************************************
 * Name: dhcpd_soclet
 ****************************************************************************/

static inline int dhcpd_socket(void)
{
	int sockfd;
	int optval;
	int ret;

	/* Create a socket to listen for requests from DHCP clients */

	sockfd = socket(PF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0) {
		log_err("ERROR: socket failed: %d\n", errno);
		return ERROR;
	}

	/* Configure the socket */

	optval = 1;
	ret = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (void *)&optval,
								sizeof(int));
	if (ret < 0) {
		log_err("ERROR: setsockopt SO_REUSEADDR failed: %d\n", errno);
		close(sockfd);
		return ERROR;
	}

	optval = 1;
	ret = setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, (void *)&optval,
								sizeof(int));
	if (ret < 0) {
		log_err("ERROR: setsockopt SO_BROADCAST failed: %d\n", errno);
		close(sockfd);
		return ERROR;
	}

	return sockfd;

}

/****************************************************************************
 * Name: dhcpd_openresponder
 ****************************************************************************/

static inline int dhcpd_openresponder(void)
{
	struct sockaddr_in addr;
	int sockfd;
	int ret;

	log_dbg("Responder: %08lx\n", ntohl(g_state.ds_serverip));

	/* Create a socket to listen for requests from DHCP clients */

	sockfd = dhcpd_socket();
	if (sockfd < 0) {
		log_err("ERROR: socket failed: %d\n", errno);
		return ERROR;
	}

	/* Bind the socket to a local port.*/

	addr.sin_family      = AF_INET;
	addr.sin_port        = htons(DHCP_SERVER_PORT);
	addr.sin_addr.s_addr = g_state.ds_serverip;

	ret = bind(sockfd, (struct sockaddr *)&addr, sizeof(
							struct sockaddr_in));
	if (ret < 0) {
		log_err("ERROR: bind failed, port=%d addr=%08lx: %d\n",
		addr.sin_port, (long)addr.sin_addr.s_addr, errno);
		close(sockfd);
		return ERROR;
	}

	return sockfd;
}

/****************************************************************************
 * Name: dhcpd_initpacket
 ****************************************************************************/

static void dhcpd_initpacket(uint8_t mtype)
{
	uint32_t nulladdr = 0;

	/* Set up the generic parts of the DHCP server message */

	memset(&g_state.ds_outpacket, 0, sizeof(struct dhcpmsg_s));

	g_state.ds_outpacket.op         = DHCP_REPLY;
	g_state.ds_outpacket.htype      = g_state.ds_inpacket.htype;
	g_state.ds_outpacket.hlen       = g_state.ds_inpacket.hlen;

	memcpy(&g_state.ds_outpacket.xid, &g_state.ds_inpacket.xid, 4);
	memcpy(g_state.ds_outpacket.chaddr, g_state.ds_inpacket.chaddr, 16);

	log_dbg("MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
		g_state.ds_inpacket.chaddr[0], g_state.ds_inpacket.chaddr[1],
		g_state.ds_inpacket.chaddr[2], g_state.ds_inpacket.chaddr[3],
		g_state.ds_inpacket.chaddr[4], g_state.ds_inpacket.chaddr[5]);

	if (memcmp(g_state.ds_outpacket.giaddr, &nulladdr, 4) != 0)
		g_state.ds_outpacket.flags  = g_state.ds_inpacket.flags;
	else
		g_state.ds_outpacket.flags  = 0;

	memset(g_state.ds_outpacket.giaddr, 0, 4);

	/* Add the generic options */

	memcpy(g_state.ds_outpacket.options, g_magiccookie, 4);
	g_state.ds_optend = &g_state.ds_outpacket.options[4];
	*g_state.ds_optend = DHCP_OPTION_END;
	dhcpd_addoption8(DHCP_OPTION_MSG_TYPE, mtype);
	dhcpd_addoption32(DHCP_OPTION_SERVER_ID, g_state.ds_serverip);
}

/****************************************************************************
 * Name: dhcpd_sendpacket
 ****************************************************************************/

static int dhcpd_sendpacket(int bbroadcast, const char *interface)
{
	struct sockaddr_in addr;
	in_addr_t ipaddr;
	int sockfd;
	int len;
	int ret = ERROR;

	/* Determine which address to respond to (or if we need to broadcast
	 * the response)
	 *
	 * (1) If he caller know that it needs to multicast the response,
	 * it will set bbroadcast.
	 * (2) Otherwise, if the client already has and address (ciaddr),
	 * then use that for uni-cast
	 * (3) Broadcast if the client says it can't handle uni-cast
	 * (BOOTP_BROADCAST set)
	 * (4) Otherwise, the client claims it can handle the uni-casst response
	 * and we will uni-cast to the offered address (yiaddr).
	 *
	 * NOTE: We really should also check the giaddr field.  If no zero, the
	 * server should send any return messages to the 'DHCP server' port on
	 * the BOOTP relay agent whose address appears in 'giaddr'.
	 */

	if (bbroadcast)
		ipaddr = INADDR_BROADCAST;
	else if (memcmp(g_state.ds_outpacket.ciaddr, g_anyipaddr, 4) != 0) {
		dhcpd_arpupdate(g_state.ds_outpacket.ciaddr,
					g_state.ds_outpacket.chaddr, interface);
		memcpy(&ipaddr, g_state.ds_outpacket.ciaddr, 4);
	} else if (g_state.ds_outpacket.flags & htons(BOOTP_BROADCAST))
		ipaddr = INADDR_BROADCAST;
	else {
		dhcpd_arpupdate(g_state.ds_outpacket.yiaddr,
					g_state.ds_outpacket.chaddr, interface);
		memcpy(&ipaddr, g_state.ds_outpacket.yiaddr, 4);
	}

	/* Create a socket to respond with a packet to the client.  We
	 * cannot re-use the listener socket because it is not bound correctly
	 */

	sockfd = dhcpd_openresponder();
	if (sockfd >= 0) {
		/* Then send the reponse to the DHCP client port at that
		 * address
		 */

		memset(&addr, 0, sizeof(struct sockaddr_in));
		addr.sin_family      = AF_INET;
		addr.sin_port        = htons(DHCP_CLIENT_PORT);
		addr.sin_addr.s_addr = ipaddr;

		/* Send the minimum sized packet that includes the END option */

		len = (g_state.ds_optend - (uint8_t *)&g_state.ds_outpacket)
									+ 1;
		log_dbg("sendto %08lx:%04x len=%d\n",
		    (long)ntohl(addr.sin_addr.s_addr), ntohs(addr.sin_port),
									len);

		ret = sendto(sockfd, &g_state.ds_outpacket, len, 0,
				(struct sockaddr *)&addr, sizeof(
				struct sockaddr_in));
		close(sockfd);
	}

	return ret;
}

/****************************************************************************
 * Name: dhcpd_sendoffer
 ****************************************************************************/

static inline int dhcpd_sendoffer(in_addr_t ipaddr, in_addr_t netmask,
				in_addr_t gw_addr, in_addr_t dns_addr[],
				uint32_t leasetime, const char *interface)
{

	int i;
	in_addr_t netaddr;

	uint32_t *dnsaddr = NULL;

	dnsaddr = (uint32_t *)malloc(MAX_DNS_ADDRESSES*sizeof(uint32_t));

	for (i = 0; i < MAX_DNS_ADDRESSES; i++)
		dnsaddr[i] = htonl(dns_addr[i]);

	/* IP address is in host order */

	log_dbg("Sending offer: %08lx\n", (long)ipaddr);

	/* Initialize the outgoing packet */

	dhcpd_initpacket(DHCPOFFER);

	/* Add the address offered to the client (converting to network order)
	 */

	netaddr = htonl(ipaddr);
	memcpy(g_state.ds_outpacket.yiaddr, &netaddr, 4);

	/* Add the leasetime to the response options */

	dhcpd_addoption32(DHCP_OPTION_LEASE_TIME, htonl(leasetime));

	dhcpd_addoption32(DHCP_OPTION_SUBNET_MASK, netmask);

	dhcpd_addoption32(DHCP_OPTION_ROUTER, gw_addr);

	for (i = 0; i < MAX_DNS_ADDRESSES; i++) {
		if (dnsaddr[i] != INADDR_ANY)
			dhcp_addoption32p(DHCP_OPTION_DNS_SERVER,
			(uint8_t *)&dnsaddr[i]);
	}

	if (dnsaddr)
		free(dnsaddr);

	/* Send the offer response */

	return dhcpd_sendpacket(true, interface);
}

/****************************************************************************
 * Name: dhcpd_sendnak
 ****************************************************************************/

static int dhcpd_sendnak(const char *interface)
{
	/* Initialize and send the NAK response */

	dhcpd_initpacket(DHCPNAK);
	memcpy(g_state.ds_outpacket.ciaddr, g_state.ds_inpacket.ciaddr, 4);
	return dhcpd_sendpacket(true, interface);
}

/****************************************************************************
 * Name: dhcpd_sendack
 ****************************************************************************/

int dhcpd_sendack(in_addr_t ipaddr, in_addr_t startip, in_addr_t netmask,
		in_addr_t gw_addr, in_addr_t dns_addr[],
		unsigned int num_leases, const char *interface)
{
	uint32_t leasetime = CONFIG_NETUTILS_DHCPD_LEASETIME;
	in_addr_t netaddr;
	int i;

	uint32_t *dnsaddr = NULL;

	dnsaddr = (uint32_t *)malloc(MAX_DNS_ADDRESSES*sizeof(uint32_t));

	for (i = 0; i < MAX_DNS_ADDRESSES; i++)
		dnsaddr[i] = dns_addr[i];

	/* Initialize the ACK response */

	dhcpd_initpacket(DHCPACK);
	memcpy(g_state.ds_outpacket.ciaddr, g_state.ds_inpacket.ciaddr, 4);

	/* Add the IP address assigned to the client */

	netaddr = htonl(ipaddr);
	memcpy(g_state.ds_outpacket.yiaddr, &netaddr, 4);

	/* Did the client request a specific lease time? */

	(void)dhcpd_verifyreqleasetime(&leasetime);

	/* Add the lease time to the response */

	dhcpd_addoption32(DHCP_OPTION_LEASE_TIME, htonl(leasetime));

	dhcpd_addoption32(DHCP_OPTION_SUBNET_MASK, netmask);

	dhcpd_addoption32(DHCP_OPTION_ROUTER, gw_addr);

	for (i = 0; i < MAX_DNS_ADDRESSES; i++) {
		if (dnsaddr[i] != INADDR_ANY)
			dhcp_addoption32p(DHCP_OPTION_DNS_SERVER,
							(uint8_t *)&dnsaddr[i]);
	}

	if (dnsaddr)
		free(dnsaddr);

	if (dhcpd_sendpacket(false, interface) < 0)
		return ERROR;

	dhcpd_setlease(g_state.ds_inpacket.chaddr, ipaddr, leasetime,
		num_leases, startip);


	return OK;
}

/****************************************************************************
 * Name: dhcpd_discover
 ****************************************************************************/

static inline int dhcpd_discover(unsigned int num_leases, in_addr_t startip,
	in_addr_t endip, in_addr_t netmask, in_addr_t gw_addr,
	in_addr_t dns_addr[], const char *interface)
{
	struct lease_s *lease;
	in_addr_t ipaddr;
	uint32_t leasetime = CONFIG_NETUTILS_DHCPD_LEASETIME;

	/* Check if the client is aleady in the lease table */

	lease = dhcpd_findbymac(g_state.ds_inpacket.chaddr, num_leases);
	if (lease) {
		/* Yes... get the remaining time on the lease */

		if (!dhcpd_leaseexpired(lease)) {
			leasetime = lease->expiry - dhcpd_time();
			if (leasetime < CONFIG_NETUTILS_DHCPD_MINLEASETIME)
				leasetime = CONFIG_NETUTILS_DHCPD_MINLEASETIME;
		}

		/* Get the IP address associated with the lease (host order) */

		ipaddr = dhcp_leaseipaddr(lease, startip);
		log_dbg("Already have lease for IP %08lx\n", (long)ipaddr);
	}

	/* Check if the client has requested a specific IP address */

	else if (dhcpd_verifyreqip(startip, endip)) {
		/* Use the requested IP address (host order) */

		ipaddr = g_state.ds_optreqip;
		log_dbg("User requested IP %08lx\n", (long)ipaddr);
	} else {
		/* No... allocate a new IP address (host order)*/

		ipaddr = dhcpd_allocipaddr(startip, endip);
		log_dbg("Allocated IP %08lx\n", (long)ipaddr);
	}

	/* Did we get any IP address? */

	if (!ipaddr) {
		/* Nope... return failure */

		log_err("ERROR: Failed to get IP address\n");
		return ERROR;
	}

	/* Reserve the leased IP for a shorter time for the offer */

	if (!dhcpd_setlease(g_state.ds_inpacket.chaddr, ipaddr,
		CONFIG_NETUTILS_DHCPD_OFFERTIME, num_leases, startip)) {
		log_err("ERROR: Failed to set lease\n");
		return ERROR;
	}

	/* Check if the client has requested a specific lease time */

	(void)dhcpd_verifyreqleasetime(&leasetime);

	/* Send the offer response */

	return dhcpd_sendoffer(ipaddr, netmask, gw_addr, dns_addr, leasetime,
				interface);
}

/****************************************************************************
 * Name: dhcpd_request
 ****************************************************************************/

static inline int dhcpd_request(unsigned int num_leases, in_addr_t startip,
	in_addr_t endip, in_addr_t netmask, in_addr_t gw_addr,
	in_addr_t dns_addr[], const char *interface)
{
	struct lease_s *lease;
	in_addr_t ipaddr = 0;
	uint8_t response = 0;

	/* Check if this client already holds a lease.  This can happen when the
	 * client (1) the IP is reserved for the client from a previous offer,
	 * or (2) the client is re-initializing or rebooting while the lease is
	 * still valid.
	 */

	lease = dhcpd_findbymac(g_state.ds_inpacket.chaddr, num_leases);
	if (lease) {
		/* Yes.. the client already holds a lease.
		 * Verify that the request is consistent
		 * with the existing lease (host order).
		 */

		ipaddr = dhcp_leaseipaddr(lease, startip);
		log_dbg("Lease ipaddr: %08x Server IP: %08x Requested IP: %08x",
		ipaddr, g_state.ds_optserverip, g_state.ds_optreqip);

		if (g_state.ds_optserverip) {
			/* ACK if the serverip is correct and the requested IP
			 * address is the one
			 * already offered to the client.
			 */

			if (g_state.ds_optserverip == ntohl(g_state.ds_serverip)
				&& (g_state.ds_optreqip != 0 ||
						g_state.ds_optreqip == ipaddr))
				response = DHCPACK;
			else
				response = DHCPNAK;
		}

		/* We have the lease and no server IP was requested.
		 * Was a specific IP address requested? (host order)
		 */

		else if (g_state.ds_optreqip) {
			/* Yes..ACK if the requested IP address is the one
			 * already leased. Both addresses are in host order.
			 */

			if (ipaddr == g_state.ds_optreqip)
				response = DHCPACK;
			else
				response = DHCPNAK;
		}

		/* The client has specified neither a server IP nor requested
		 * IP address
		 */

		else {
			/* ACK if the IP used by the client is the one already
			 * assigned to it. NOTE ipaddr is in host order; ciaddr
			 * is network order!
			 */

			uint32_t tmp = htonl(ipaddr);

			if (memcmp(&tmp, g_state.ds_inpacket.ciaddr, 4) == 0)
				response = DHCPACK;
			else
				response = DHCPNAK;
		}
	}

	/* The client does not hold a lease (referenced by its MAC address)
	 * and is requesting a specific IP address that was, apparently,
	 * never offered to the client.  Perform some sanity checks before
	 * sending the NAK.
	 */

	else if (g_state.ds_optreqip && !g_state.ds_optserverip) {
		log_dbg("Server IP: %08x Requested IP: %08x\n",
			g_state.ds_optserverip, g_state.ds_optreqip);

		/* Is this IP address already assigned? */

		lease = dhcpd_findbyipaddr(g_state.ds_optreqip, startip, endip);
		if (lease) {
			/* Yes.. Send NAK unless the lease has expired */

			if (!dhcpd_leaseexpired(lease))
				response = DHCPNAK;
		}

		/* No.. is the requested IP address in range? NAK if not */

		else if (g_state.ds_optreqip < startip ||
			g_state.ds_optreqip > endip)
			response = DHCPNAK;
	}

	/* Otherwise, the client does not hold a lease and is not requesting
	 * any specific IP address.
	 */

	/* Finally, either (1) send the ACK, (2) send a NAK, or (3) remain
	 * silent based on the checks above.
	 */

	if (response == DHCPACK) {
		log_dbg("ACK IP %08lx\n", (long)ipaddr);
		dhcpd_sendack(ipaddr, startip, netmask, gw_addr, dns_addr,
							num_leases, interface);
	} else if (response == DHCPNAK) {
		log_dbg("NAK IP %08lx\n", (long)ipaddr);
		dhcpd_sendnak(interface);
	} else
		log_dbg("Remaining silent IP %08lx\n", (long)ipaddr);

	return OK;
}

/****************************************************************************
 * Name: dhcpd_decline
 ****************************************************************************/

static inline int dhcpd_decline(unsigned int num_leases)
{
	struct lease_s *lease;

	/* Find the lease associated with this hardware address */

	lease = dhcpd_findbymac(g_state.ds_inpacket.chaddr, num_leases);
	if (lease) {
		/* Disassociate the IP from the MAC, but prevent re-used of this
		 * address for a period of time.
		 */

		memset(lease->mac, 0, DHCP_HLEN_ETHERNET);
		lease->expiry = dhcpd_time() +
			CONFIG_NETUTILS_DHCPD_DECLINETIME;
	}

	return OK;
}

static inline int dhcpd_release(unsigned int num_leases)
{
	struct lease_s *lease;

	/* Find the lease associated with this hardware address */

	lease = dhcpd_findbymac(g_state.ds_inpacket.chaddr, num_leases);
	if (lease)
		/* Release the IP address now */
		memset(lease, 0, sizeof(struct lease_s));

	return OK;
}

/****************************************************************************
 * Name: dhcpd_openlistener
 ****************************************************************************/

static inline int dhcpd_openlistener(const char *interface)
{
	struct sockaddr_in addr;
	struct ifreq req;
	int sockfd;
	int ret;

	/* Create a socket to listen for requests from DHCP clients */

	sockfd = dhcpd_socket();
	if (sockfd < 0) {
		log_err("ERROR: socket failed: %d\n", errno);
		return ERROR;
	}

	/* Get the IP address of the selected device */

	strncpy(req.ifr_name, interface, IFNAMSIZ);
	ret = ioctl(sockfd, SIOCGIFADDR, (unsigned long)&req);
	if (ret < 0) {
		log_err("ERROR: setsockopt SIOCGIFADDR failed: %d\n", errno);
		close(sockfd);
		return ERROR;
	}

	g_state.ds_serverip = ((struct sockaddr_in *)
						&req.ifr_addr)->sin_addr.s_addr;
	log_dbg("serverip: %08lx\n", ntohl(g_state.ds_serverip));

	/* Bind the socket to a local port. We have to bind to INADDRY_ANY to
	 * receive broadcast messages.
	 */

	addr.sin_family      = AF_INET;
	addr.sin_port        = htons(DHCP_SERVER_PORT);
	addr.sin_addr.s_addr = INADDR_ANY;

	ret = bind(sockfd, (struct sockaddr *)&addr,
						sizeof(struct sockaddr_in));
	if (ret < 0) {
		log_err("ERROR: bind failed, port=%d addr=%08lx: %d\n",
			addr.sin_port, (long)addr.sin_addr.s_addr, errno);
		close(sockfd);
		return ERROR;
	}

	return sockfd;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

static int loop_handler(int fd, enum watch_io io, void *arg)
{
	/* Read the next g_state.ds_outpacket */

	dhcp_server_handle *server = (dhcp_server_handle *)arg;
	int nbytes;

	nbytes = recv(fd, &g_state.ds_inpacket, sizeof(struct dhcpmsg_s), 0);
	if (nbytes < 0) {
		/* On errors (other EINTR), close the socket and try again */

		log_err("ERROR: recv failed: %s\n", strerror(errno));
		if (errno != EINTR) {
			close(fd);
			fd = -1;
		}
	}

	/* Parse the incoming message options */

	if (!dhcpd_parseoptions())
		/* Failed to parse the message options */

		log_err("ERROR: No msg type\n");

	/* Now process the incoming DHCP message by its message type */

	switch (g_state.ds_optmsgtype) {
	case DHCPDISCOVER:
		log_dbg("DHCPDISCOVER\n");
		dhcpd_discover(server->num_leases, server->startip,
			server->endip, server->netmask,
			server->gw_addr, server->dns_addr,
			server->interface);
		break;

	case DHCPREQUEST:
		log_dbg("DHCPREQUEST\n");
		dhcpd_request(server->num_leases, server->startip,
			server->endip, server->netmask,
			server->gw_addr, server->dns_addr,
			server->interface);
		break;

	case DHCPDECLINE:
		log_dbg("DHCPDECLINE\n");
		dhcpd_decline(server->num_leases);
		break;

	case DHCPRELEASE:
		log_dbg("DHCPRELEASE\n");
		dhcpd_release(server->num_leases);
		break;

	case DHCPINFORM: /* Not supported */
	default:
		log_err("ERROR: Unsupported message type: %d\n",
			g_state.ds_optmsgtype);
		break;
	}

	return 1;
}

/****************************************************************************
 * Name: dhcpd_run
 ****************************************************************************/

void *dhcpd_start(artik_network_dhcp_server_config *config)
{
	unsigned int num_leases = config->num_leases;
	dhcp_server_handle *server = NULL;

	server = malloc(sizeof(dhcp_server_handle));

	memset(server, 0, sizeof(dhcp_server_handle));
	server->interface = (config->interface == ARTIK_WIFI) ? "wlan0" : "eth0";
	server->num_leases = num_leases;
	server->startip = htonl(inet_addr(config->start_addr.address));
	server->endip = server->startip + server->num_leases - 1;
	server->netmask = inet_addr(config->netmask.address);
	server->gw_addr = inet_addr(config->gw_addr.address);
	server->dns_addr = (in_addr_t *)malloc(
			MAX_DNS_ADDRESSES * sizeof(in_addr_t));
	server->loop = (artik_loop_module *)artik_request_api_module("loop");

	for (int i = 0; i < MAX_DNS_ADDRESSES; i++) {
		if (strcmp(config->dns_addr[i].address, "") == 0)
			server->dns_addr[i] = INADDR_ANY;
		else
			server->dns_addr[i] = inet_addr(
						config->dns_addr[i].address);
	}

	log_dbg("Started\n");

	/* Initialize everything to zero */
	memset(&g_state, 0, sizeof(struct dhcpd_state_s));
	g_state.ds_leases = (struct lease_s *)malloc(
			num_leases * sizeof(struct lease_s));
	memset(g_state.ds_leases, 0, num_leases * sizeof(struct lease_s));

	/* Create a socket to listen for requests from DHCP clients */
	server->sockfd = dhcpd_openlistener(server->interface);
	if (server->sockfd < 0) {
		log_err("ERROR: Failed to create socket\n");
		artik_release_api_module(server->loop);
		free(g_state.ds_leases);
		free(server);
		return NULL;
	}

	server->loop->add_fd_watch(server->sockfd,
			WATCH_IO_IN | WATCH_IO_ERR | WATCH_IO_HUP | WATCH_IO_NVAL,
			loop_handler, server, &server->watch_id);

	return (void *)server;
}

void dhcpd_stop(void *handle)
{
	dhcp_server_handle *server = (dhcp_server_handle *)handle;

	if (!handle)
		return;

	close(server->sockfd);

	server->loop->remove_fd_watch(server->watch_id);

	artik_release_api_module(server->loop);
	free(g_state.ds_leases);
	free(server);
}
