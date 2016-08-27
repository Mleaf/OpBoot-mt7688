/*
 * Copyright (c) 2001, Adam Dunkels.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Adam Dunkels.
 * 4. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * This file is part of the uIP TCP/IP stack.
 *
 * $Id: main.c,v 1.16 2006/06/11 21:55:03 adam Exp $
 *
 */
#include "uip.h"
#include "uip_arp.h"

#include "timer.h"

#include <common.h>
#include <command.h>
#include <net.h>
#include <asm/addrspace.h>

#define BUF ((struct uip_eth_hdr *)&uip_buf[0])
int NetUipLoop=0;

#ifndef NULL
#define NULL (void *)0
#endif /* NULL */

extern VALID_BUFFER_STRUCT  rt2880_free_buf_list;
extern BUFFER_ELEM *rt2880_free_buf_entry_dequeue(VALID_BUFFER_STRUCT *hdr);

/*---------------------------------------------------------------------------*/
static int
dev_init(void)
{
  BUFFER_ELEM *buf;
  int i;

  for(i = 0; i < NUM_RX_DESC; i++) {
    buf = rt2880_free_buf_entry_dequeue(&rt2880_free_buf_list); 
    if(buf == NULL) {
      printf("Packet Buffer is empty!\n");
      return (-1);
    }
    NetRxPackets[i] = buf->pbuf;
  }
  return eth_init(NULL);
}
/*---------------------------------------------------------------------------*/
static int
dev_send(void)
{
  static BUFFER_ELEM *buf = NULL;

  if(buf == NULL)
    buf = rt2880_free_buf_entry_dequeue(&rt2880_free_buf_list); 
  if(buf == NULL) {
    printf("Packet Buffer is empty!\n");
    return (-1);
  }
  NetTxPacket = buf->pbuf;
  NetTxPacket = KSEG1ADDR(NetTxPacket);

  memcpy((void *)NetTxPacket, uip_buf, uip_len);

  eth_send(NetTxPacket, uip_len);
  return 0;
}
void dev_received(volatile uchar * inpkt, int len)
{
	memcpy(uip_buf, (const void *)inpkt, len);
	uip_len = len;

	 if(uip_len > 0) {
      //printf("rx a pkt: length %d, type %x\n", uip_len, BUF->type);
      if (uip_len > UIP_BUFSIZE) {
        printf("UIP error: uip_len > UIP_BUFSIZE!!\n");
		udelay(10 * CLOCK_SECOND);
		//continue;
      }
      if(BUF->type == htons(UIP_ETHTYPE_IP)) {
		uip_arp_ipin();
		uip_input();
		/* If the above function invocation resulted in data that
		   should be sent out on the network, the global variable
		   uip_len is set to a value > 0. */
		if(uip_len > 0) {
		  uip_arp_out();
		  dev_send();
		}
      } else if(BUF->type == htons(UIP_ETHTYPE_ARP)) {
		uip_arp_arpin();
		/* If the above function invocation resulted in data that
		   should be sent out on the network, the global variable
		   uip_len is set to a value > 0. */
		if(uip_len > 0) {
	          dev_send();
		}
      }

    }

}

/*---------------------------------------------------------------------------*/
int
uip_main(void)
{
  int i;
  uip_ipaddr_t ipaddr;
  //struct uip_eth_addr ethaddr = {{0x03,0x06,0x05,0x19,0x11,0x11}};
  struct timer periodic_timer, arp_timer;

  timer_set(&periodic_timer, CLOCK_SECOND / 2);
  timer_set(&arp_timer, CLOCK_SECOND * 10);
  
  uip_init();

  uip_ipaddr(ipaddr, 192,168,1,1);//≈‰÷√IP
  uip_sethostaddr(ipaddr);
  uip_ipaddr(ipaddr, 192,168,1,1);//≈‰÷√Õ¯πÿ
  uip_setdraddr(ipaddr);
  uip_ipaddr(ipaddr, 255,255,255,0);//≈‰÷√◊”Õ¯—⁄¬Î
  uip_setnetmask(ipaddr);
  uip_gethostaddr(ipaddr); 
  //uip_setethaddr(ethaddr);
  dhcpd_init();
  httpd_init();
  dev_init();
  printf("dhcpd uboot by mleaf version V1.0.0\n");
  printf("mail:350983773@qq.com\n");
  printf("Enter http firmware upgrading mode ip:%s\n","192.168.1.1");

  for ( ; ; ) {
    uip_len=eth_rx();
	if(ctrlc()){
		
			eth_halt();
			NetUipLoop=0;
			printf("\nMleaf uip web failsafe mode aborted!\n\n");
			return(-1);
	}
	
   if(timer_expired(&periodic_timer)) {
      timer_reset(&periodic_timer);
      for(i = 0; i < UIP_CONNS; i++) {
		uip_periodic(i);
		/* If the above function invocation resulted in data that
		   should be sent out on the network, the global variable
		   uip_len is set to a value > 0. */
		if(uip_len > 0) {
		  uip_arp_out();
		  dev_send();
		}
      }

#if UIP_UDP
      for(i = 0; i < UIP_UDP_CONNS; i++) {
		uip_udp_periodic(i);
		/* If the above function invocation resulted in data that
		   should be sent out on the network, the global variable
		   uip_len is set to a value > 0. */
		if(uip_len > 0) {
		  uip_arp_out();
		  dev_send();
	}
      }
#endif /* UIP_UDP */
      
      /* Call the ARP timer function every 10 seconds. */
      if(timer_expired(&arp_timer)) {
		timer_reset(&arp_timer);
		uip_arp_timer();
      }
    }
  }
  return 0;
}
/*---------------------------------------------------------------------------*/
void
uip_log(char *m)
{
  printf("uIP log : %s\n", m);
}
/*---------------------------------------------------------------------------*/


int mleaf_uip_command(cmd_tbl_t *cmdtp, int flag, int argc, char *argv[])
{
  if(!strncmp(argv[1], "start", 6)) {
    NetUipLoop = 1;
    uip_main();
  }
  else
    printf("Usage:\n%s\n use\"help uip\"\n", cmdtp->usage);
  return 0;
}
//run_command("uip start", 0);//by mleaf
U_BOOT_CMD(
  uip,	2,	1, 	mleaf_uip_command,
  "mleaf uip	- uip command\n",
  "mleaf uip usage:\n"
  "  uip start\n"
);
