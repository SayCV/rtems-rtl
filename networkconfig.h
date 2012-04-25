/*
 * Network configuration EXAMPLE!!! 
 * 
 ************************************************************
 * EDIT THIS FILE TO REFLECT YOUR NETWORK CONFIGURATION     *
 * BEFORE RUNNING ANY RTEMS PROGRAMS WHICH USE THE NETWORK! * 
 ************************************************************
 *
 *  $Id: networkconfig.h,v 1.9 2007/07/18 19:35:18 joel Exp $
 */

#ifndef _CONFIGURE_NETWORKCONFIG_H_
#define _CONFIGURE_NETWORKCONFIG_H_

/*
 *  The following will normally be set by the BSP if it supports
 *  a single network device driver.  In the event, it supports
 *  multiple network device drivers, then the user's default
 *  network device driver will have to be selected by a BSP
 *  specific mechanism.
 */

#if !defined(CONFIGURE_NETWORK_DRIVER_NAME)
#warning "CONFIGURE_NETWORK_DRIVER_NAME is not defined"
#define CONFIGURE_NETWORK_DRIVER_NAME "no_network1"
#endif

#if !defined(CONFIGURE_NETWORK_DRIVER_ATTACH)
#warning "CONFIGURE_NETWORK_DRIVER_ATTACH is not defined"
#define CONFIGURE_NETWORK_DRIVER_ATTACH 0
#endif

#if !defined(CONFIGURE_NETWORK_HOSTNAME)
#define CONFIGURE_NETWORK_HOSTNAME "rtemstst"
#endif

#if !defined(CONFIGURE_NETWORK_DOMAINNAME)
#define CONFIGURE_NETWORK_DOMAINNAME "nodomain.com"
#endif

#if !defined(CONFIGURE_NETWORK_IPADDR)
#define CONFIGURE_NETWORK_IPADDR "10.10.10.10"
#endif

#if !defined(CONFIGURE_NETWORK_NETMASK)
#define CONFIGURE_NETWORK_NETMASK "255.255.255.0"
#endif

#if !defined(CONFIGURE_NETWORK_GATEWAY)
#define CONFIGURE_NETWORK_GATEWAY "10.10.10.1"
#endif

#if !defined(CONFIGURE_NETWORK_LOGHOST)
#define CONFIGURE_NETWORK_LOGHOST CONFIGURE_NETWORK_GATEWAY
#endif

#if !defined(CONFIGURE_NETWORK_DNS)
#define CONFIGURE_NETWORK_DNS CONFIGURE_NETWORK_GATEWAY
#endif

#if !defined(CONFIGURE_NETWORK_NTP)
#define CONFIGURE_NETWORK_NTP CONFIGURE_NETWORK_GATEWAY
#endif

#include <bsp.h>

/*
 * Define RTEMS_SET_ETHERNET_ADDRESS if you want to specify the
 * Ethernet address here.  If RTEMS_SET_ETHERNET_ADDRESS is not
 * defined the driver will choose an address.
 */
#if defined(CONFIGURE_ETHERNET_ADDRESS)
static char ethernet_address[6] = { CONFIGURE_ETHERNET_ADDRESS };
#endif

#ifdef CONFIGURE_NETWORK_LOOPBACK 
/*
 * Loopback interface
 */
extern void rtems_bsdnet_loopattach();
static struct rtems_bsdnet_ifconfig loopback_config = {
  "lo0",                    /* name */
  rtems_bsdnet_loopattach,  /* attach function */
  NULL,                     /* link to next interface */
  "127.0.0.1",              /* IP address */
  "255.0.0.0",              /* IP net mask */
};
#endif

/*
 * Default network interface
 */
static struct rtems_bsdnet_ifconfig netdriver_config = {
  CONFIGURE_NETWORK_DRIVER_NAME,    /* name */
  CONFIGURE_NETWORK_DRIVER_ATTACH,  /* attach function */
#ifdef CONFIGURE_NETWORK_LOOPBACK 
  &loopback_config,                 /* link to next interface */
#else
  NULL,                             /* No more interfaces */
#endif

#if defined (CONFIGURE_NETWORK_BOOTP) || defined (CONFIGURE_NETWORK_DHCP) 
  NULL,                             /* BOOTP/DHCP supplies IP address */
  NULL,                             /* BOOTP/DHCP supplies IP net mask */
#else
  CONFIGURE_NETWORK_IPADDR,         /* IP address */
  CONFIGURE_NETWORK_NETMASK,        /* IP net mask */
#endif /* !CONFIGURE_NETWORK_BOOTP */

#if (defined (CONFIGURE_ETHERNET_ADDRESS))
  ethernet_address,                 /* Ethernet hardware address */
#else
  NULL,                             /* Driver supplies hardware address */
#endif
  0                                 /* Use default driver parameters */
};

/*
 * Network configuration
 */
struct rtems_bsdnet_config rtems_bsdnet_config = {
  &netdriver_config,

#if (defined (CONFIGURE_NETWORK_BOOTP))
  rtems_bsdnet_do_bootp,
#elif (defined (CONFIGURE_NETWORK_DHCP))
  rtems_bsdnet_do_dhcp,
#else
  NULL,
#endif

#if (defined (CONFIGURE_NETWORK_PRIORITY))
  CONFIGURE_NETWORK_PRIORITY,      /* Default network task priority */
#else
  0,                               /* Default network task priority */
#endif

#if (defined (CONFIGURE_NETWORK_MBUFS))
  CONFIGURE_NETWORK_MBUFS,         /* Default mbuf capacity */
#else
  0,                               /* Default mbuf capacity */
#endif

#if (defined (CONFIGURE_NETWORK_MCLUSTERS))
  CONFIGURE_NETWORK_MCLUSTERS,     /* Default mbuf cluster capacity */
#else
  0,                               /* Default mbuf cluster capacity */
#endif

#if !defined (CONFIGURE_NETWORK_BOOTP)
  CONFIGURE_NETWORK_HOSTNAME,      /* Host name */
#endif
#if !defined (CONFIGURE_NETWORK_BOOTP) && !defined(CONFIGURE_NETWORK_DHCP)
  CONFIGURE_NETWORK_DOMAINNAME,    /* Domain name */
  CONFIGURE_NETWORK_GATEWAY,       /* Gateway */
  CONFIGURE_NETWORK_LOGHOST,       /* Log host */
  { CONFIGURE_NETWORK_DNS },       /* Name server(s) */
  { CONFIGURE_NETWORK_NTP },       /* NTP server(s) */
#endif /* !CONFIGURE_NETWORK_BOOTP */
};

#endif /* _CONFIGURE_NETWORKCONFIG_H_ */
