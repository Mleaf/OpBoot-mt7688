#Copyright (c) 2015, mleaf mleaf90@gmail.com or 350983773@qq.com
#All rights reserved.
# include ../config.mk
include $(TOPDIR)/config.mk

CFLAGS += -Iunix/ -Iuip/ -Iapps/webserver -Iapps/dhcpc -Iapps/dhcpd

LIB	= libuip.a
APPS	= webserver

OS_OBJS  = unix/main.o unix/clock-arch.o

APP_OBJS = apps/webserver/httpd.o apps/webserver/http-strings.o \
	   apps/webserver/httpd-fs.o apps/webserver/httpd-cgi.o
DHCPC_OBJS = apps/dhcpc/dhcpc.o
DHCPD_OBJS = apps/dhcpd/dhcpd.o
UIP_OBJS = uip/uip.o uip/uip_arp.o uip/uiplib.o uip/psock.o uip/timer.o \
	   uip/uip-neighbor.o

OBJS	= $(UIP_OBJS) $(APP_OBJS) $(DHCPD_OBJS) $(OS_OBJS)

all: $(LIB)

$(LIB):	$(OBJS)
	$(AR) crv $@ $(OBJS)

#########################################################################

.depend:	Makefile $(OBJS:.o=.c)
		$(CC) -M $(CFLAGS) $(OBJS:.o=.c) > $@

sinclude .depend

#########################################################################


