/*
 * Copyright (c) 2011, Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/**
 * \author
 *         Niclas Finne <nfi@sics.se>
 *         Joakim Eriksson <joakime@sics.se>
 */

#include "net/uip.h"
#include "net/uip-ds6.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>

#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/socket.h>


#define DEBUG DEBUG_NONE
#include "net/uip-debug.h"

#ifdef linux
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
//#include <net/if.h>  <- conflict with linux/if.h
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netinet/ether.h>
#include <linux/if_packet.h>
#include <linux/if.h>
#include <linux/if_tun.h>
struct ifreq if_idx;
#endif

#include <err.h>
#include "net/netstack.h"
#include "net/packetbuf.h"
#include "eth-drv.h"
#include "raw-tap-dev.h"
#include "packet-filter.h"

//Temporary, should be removed
#include "native-slip.h"

extern char slip_config_tundev[32];
extern uint16_t slip_config_basedelay;
extern uint8_t use_raw_ethernet;

#ifndef __CYGWIN__
static int tunfd;

static int set_fd(fd_set *rset, fd_set *wset);
static void handle_fd(fd_set *rset, fd_set *wset);
static const struct select_callback tun_select_callback = {
  set_fd,
  handle_fd
};
#endif /* __CYGWIN__ */

int ssystem(const char *fmt, ...)
     __attribute__((__format__ (__printf__, 1, 2)));
int
ssystem(const char *fmt, ...) __attribute__((__format__ (__printf__, 1, 2)));

int
ssystem(const char *fmt, ...)
{
  char cmd[128];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(cmd, sizeof(cmd), fmt, ap);
  va_end(ap);
  printf("%s\n", cmd);
  fflush(stdout);
  return system(cmd);
}

/*---------------------------------------------------------------------------*/
void
cleanup(void)
{
  //ssystem("ifconfig %s down", slip_config_tundev);
#ifndef linux
  ssystem("sysctl -w net.ipv6.conf.all.forwarding=1");
#endif
  ssystem("netstat -nr"
	  " | awk '{ if ($2 == \"%s\") print \"route delete -net \"$1; }'"
	  " | sh",
	  slip_config_tundev);
}

/*---------------------------------------------------------------------------*/
void
sigcleanup(int signo)
{
  printf("signal %d\n", signo);
  exit(1);			/* exit(0) will call cleanup() */
}

/*---------------------------------------------------------------------------*/
void
ifconf(const char *tundev)
{
#if defined(__APPLE__)
  ssystem("ifconfig %s link 02:a:b:c:d:e; ip6 -d %s; ip6 -u %s; ifconfig %s inet6 up ", tundev, tundev, tundev, tundev);
#else
  ssystem("ifconfig %s up", tundev );
#endif

  /* Print the configuration to the console. */
  ssystem("ifconfig %s\n", tundev);
}
/*---------------------------------------------------------------------------*/
int
devopen(const char *dev, int flags)
{
  char t[32];
  strcpy(t, "/dev/");
  strncat(t, dev, sizeof(t) - 5);
  return open(t, flags);
}
/*---------------------------------------------------------------------------*/

#ifdef linux
int
eth_alloc(const char *tundev)
{
	int sockfd;
	if ((sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) == -1) {
		    perror("socket");
	}
	memset(&if_idx, 0, sizeof(struct ifreq));
	strncpy(if_idx.ifr_name, tundev, IFNAMSIZ-1);
	if (ioctl(sockfd, SIOCGIFINDEX, &if_idx) < 0)
		    perror("SIOCGIFINDEX");
	struct sockaddr_ll sll;
	sll.sll_family = AF_PACKET;
	sll.sll_ifindex = if_idx.ifr_ifindex;
	sll.sll_protocol = htons (ETH_P_ALL);
	if (bind ( sockfd, (struct sockaddr *) &sll, sizeof (sll)) < 0)
			perror("bind");
	struct packet_mreq      mr;
	memset (&mr, 0, sizeof (mr));
	mr.mr_ifindex = if_idx.ifr_ifindex;
	mr.mr_type = PACKET_MR_PROMISC;
	if (setsockopt (sockfd, SOL_PACKET,PACKET_ADD_MEMBERSHIP, &mr, sizeof (mr)) < 0)
		perror("setsockopt");

	return sockfd;
}

int
tun_alloc(char *dev)
{
  struct ifreq ifr;
  int fd, err;

  if( (fd = open("/dev/net/tun", O_RDWR)) < 0 ) {
    return -1;
  }

  memset(&ifr, 0, sizeof(ifr));

  /* Flags: IFF_TUN   - TUN device (no Ethernet headers)
   *        IFF_NO_PI - Do not provide packet information
   */
  ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
  if(*dev != 0)
    strncpy(ifr.ifr_name, dev, IFNAMSIZ);

  if((err = ioctl(fd, TUNSETIFF, (void *) &ifr)) < 0 ) {
    close(fd);
    return err;
  }
  strcpy(dev, ifr.ifr_name);
  return fd;
}
#else
int
tun_alloc(char *dev)
{
  return devopen(dev, O_RDWR);
}
#endif

#ifdef __CYGWIN__
/*wpcap process is used to connect to host interface */
void
tun_init()
{
  setvbuf(stdout, NULL, _IOLBF, 0); /* Line buffered output. */

  slip_init();
}

#else

static uint16_t delaymsec=0;
static uint32_t delaystartsec,delaystartmsec;

/*---------------------------------------------------------------------------*/
void
tun_init()
{
  setvbuf(stdout, NULL, _IOLBF, 0); /* Line buffered output. */

  slip_init();

#ifdef linux
  if ( use_raw_ethernet ) {
    tunfd = eth_alloc(slip_config_tundev);
  } else {
    tunfd = tun_alloc(slip_config_tundev);
  }
#else
  tunfd = tun_alloc(slip_config_tundev);
#endif
  if(tunfd == -1) err(1, "main: open");

  select_set_callback(tunfd, &tun_select_callback);

  printf("opened device ``/dev/%s''\n", slip_config_tundev);

  atexit(cleanup);
  signal(SIGHUP, sigcleanup);
  signal(SIGTERM, sigcleanup);
  signal(SIGINT, sigcleanup);
  ifconf(slip_config_tundev);
}

/*---------------------------------------------------------------------------*/
void
tun_output(uint8_t *data, int len)
{
  /* printf("*** Writing to tun...%d\n", len); */
  if(write(tunfd, data, len) != len) {
    err(1, "serial_to_tun: write");
  }
}
/*---------------------------------------------------------------------------*/
int
tun_input(unsigned char *data, int maxlen)
{
  int size;
  if((size = read(tunfd, data, maxlen)) == -1)
    err(1, "tun_input: read");
  return size;
}

/*---------------------------------------------------------------------------*/
/* tun and slip select callback                                              */
/*---------------------------------------------------------------------------*/
static int
set_fd(fd_set *rset, fd_set *wset)
{
  FD_SET(tunfd, rset);
  return 1;
}

/*---------------------------------------------------------------------------*/

static unsigned char tmp_tap_buf[ETHERNET_LLH_LEN+UIP_BUFSIZE];

static void
handle_fd(fd_set *rset, fd_set *wset)
{
  /* Optional delay between outgoing packets */
  /* Base delay times number of 6lowpan fragments to be sent */
  /* delaymsec = 10; */
  if(delaymsec) {
    struct timeval tv;
    int dmsec;
    gettimeofday(&tv, NULL);
    dmsec=(tv.tv_sec-delaystartsec)*1000+tv.tv_usec/1000-delaystartmsec;
    if(dmsec<0) delaymsec=0;
    if(dmsec>delaymsec) delaymsec=0;
  }

  if(delaymsec==0) {
    int size;

    if(FD_ISSET(tunfd, rset)) {
      size = tun_input(tmp_tap_buf, sizeof(tmp_tap_buf));
      printf("TUN data incoming read:%d\n", size);
//#if CETIC_RAW_ETHERNET
      //Remove extra data from packet capture
      //uip_len = size - ETHERNET_LLH_LEN - 4;
//#else
      uip_len = size - ETHERNET_LLH_LEN;
//#endif
      memcpy(ll_header, tmp_tap_buf, ETHERNET_LLH_LEN);
      memcpy(uip_buf, tmp_tap_buf+ETHERNET_LLH_LEN, uip_len);
      eth_input();

      if(slip_config_basedelay) {
        struct timeval tv;
      	gettimeofday(&tv, NULL) ;
      	delaymsec=slip_config_basedelay;
      	delaystartsec =tv.tv_sec;
      	delaystartmsec=tv.tv_usec/1000;
      }
    }
  }
}
#endif /*  __CYGWIN_ */

/*---------------------------------------------------------------------------*/