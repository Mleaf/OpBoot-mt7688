/**
 * \addtogroup apps
 * @{
 */

/**
 * \defgroup httpd Web server
 * @{
 * The uIP web server is a very simplistic implementation of an HTTP
 * server. It can serve web pages and files from a read-only ROM
 * filesystem, and provides a very small scripting language.

 */

/**
 * \file
 *         Web server
 * \author
 *         Adam Dunkels <adam@sics.se>
 */


/*
 * Copyright (c) 2004, Adam Dunkels.
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
 *
 * This file is part of the uIP TCP/IP stack.
 *
 * Author: Adam Dunkels <adam@sics.se>
 *
 * $Id: httpd.c,v 1.5 2008-12-30 07:24:24 winfred Exp $
 */

#include "uip.h"
#include "httpd.h"
#include "httpd-fs.h"
#include "httpd-cgi.h"
#include "http-strings.h"

#include <common.h>
#include <configs/rt2880.h>
#include <command.h>

#define STATE_WAITING 0
#define STATE_OUTPUT  1

#define ISO_nl      0x0a  //"\n"
#define ISO_cr      0x0d
#define ISO_space   0x20  //" "
#define ISO_bang    0x21  //"!"
#define ISO_percent 0x25  //"%"
#define ISO_period  0x2e  //"."	
#define ISO_slash   0x2f  //"/"
#define ISO_colon   0x3a  //":"


char content_boundary[80]; /* winfred: length should be the same as inputbuf */
// U-Boot partition size
#define WEBFAILSAFE_UPLOAD_UBOOT_SIZE_IN_BYTES		(128 * 1024)
// ART partition size
#define WEBFAILSAFE_UPLOAD_ART_SIZE_IN_BYTES		(64 * 1024)

/*---------------------------------------------------------------------------*/
static unsigned short
generate_part_of_file(void *state)
{
  struct httpd_state *s = (struct httpd_state *)state;

  if(s->file.len > uip_mss()) {
    s->len = uip_mss();
  } else {
    s->len = s->file.len;
  }
  memcpy(uip_appdata, s->file.data, s->len);
  
  return s->len;
}
/*---------------------------------------------------------------------------*/
static
PT_THREAD(send_file(struct httpd_state *s))
{
  PSOCK_BEGIN(&s->sout);
  
  do {
    PSOCK_GENERATOR_SEND(&s->sout, generate_part_of_file, s);
    s->file.len -= s->len;
    s->file.data += s->len;
  } while(s->file.len > 0);
      
  PSOCK_END(&s->sout);
}
/*---------------------------------------------------------------------------*/
static
PT_THREAD(send_part_of_file(struct httpd_state *s))
{
  PSOCK_BEGIN(&s->sout);

  PSOCK_SEND(&s->sout, s->file.data, s->len);
  
  PSOCK_END(&s->sout);
}
/*---------------------------------------------------------------------------*/
static void
next_scriptstate(struct httpd_state *s)
{
  char *p;
  p = strchr(s->scriptptr, ISO_nl) + 1;
  s->scriptlen -= (unsigned short)(p - s->scriptptr);
  s->scriptptr = p;
}
/*---------------------------------------------------------------------------*/
static
PT_THREAD(handle_script(struct httpd_state *s))
{
  char *ptr;
  
  PT_BEGIN(&s->scriptpt);


  while(s->file.len > 0) {

    /* Check if we should start executing a script. */
    if(*s->file.data == ISO_percent &&
       *(s->file.data + 1) == ISO_bang) {
      s->scriptptr = s->file.data + 3;
      s->scriptlen = s->file.len - 3;
      if(*(s->scriptptr - 1) == ISO_colon) {
	httpd_fs_open(s->scriptptr + 1, &s->file);
	PT_WAIT_THREAD(&s->scriptpt, send_file(s));
      } else {
	PT_WAIT_THREAD(&s->scriptpt,
		       httpd_cgi(s->scriptptr)(s, s->scriptptr));
      }
      next_scriptstate(s);
      
      /* The script is over, so we reset the pointers and continue
	 sending the rest of the file. */
      s->file.data = s->scriptptr;
      s->file.len = s->scriptlen;
    } else {
      /* See if we find the start of script marker in the block of HTML
	 to be sent. */

      if(s->file.len > uip_mss()) {
	s->len = uip_mss();
      } else {
	s->len = s->file.len;
      }

      if(*s->file.data == ISO_percent) {
	ptr = strchr(s->file.data + 1, ISO_percent);
      } else {
	ptr = strchr(s->file.data, ISO_percent);
      }
      if(ptr != NULL &&
	 ptr != s->file.data) {
	s->len = (int)(ptr - s->file.data);
	if(s->len >= uip_mss()) {
	  s->len = uip_mss();
	}
      }
      PT_WAIT_THREAD(&s->scriptpt, send_part_of_file(s));
      s->file.data += s->len;
      s->file.len -= s->len;
      
    }
  }
  
  PT_END(&s->scriptpt);
}
/*---------------------------------------------------------------------------*/
static
PT_THREAD(send_headers(struct httpd_state *s, const char *statushdr))
{
  char *ptr;

  PSOCK_BEGIN(&s->sout);

  PSOCK_SEND_STR(&s->sout, statushdr);

  ptr = strrchr(s->filename, ISO_period);
  if(ptr == NULL) {
    PSOCK_SEND_STR(&s->sout, http_content_type_binary);
  } else if(strncmp(http_html, ptr, 5) == 0 ||
	    strncmp(http_shtml, ptr, 6) == 0) {
    PSOCK_SEND_STR(&s->sout, http_content_type_html);
  } else if(strncmp(http_css, ptr, 4) == 0) {
    PSOCK_SEND_STR(&s->sout, http_content_type_css);
  } else if(strncmp(http_png, ptr, 4) == 0) {
    PSOCK_SEND_STR(&s->sout, http_content_type_png);
  } else if(strncmp(http_gif, ptr, 4) == 0) {
    PSOCK_SEND_STR(&s->sout, http_content_type_gif);
  } else if(strncmp(http_jpg, ptr, 4) == 0) {
    PSOCK_SEND_STR(&s->sout, http_content_type_jpg);
  } else {
    PSOCK_SEND_STR(&s->sout, http_content_type_plain);
  }
  PSOCK_END(&s->sout);
}
/*---------------------------------------------------------------------------*/
static
PT_THREAD(handle_output(struct httpd_state *s))
{
  char *ptr, *argv[2], addr[11];
  static unsigned long length = 0;
  unsigned long e_end;
  int get_addr_boundary(ulong *);
  int do_bootm(cmd_tbl_t *, int, int, char *argv[]);
  
  PT_BEGIN(&s->outputpt);
  if(strncmp(s->filename, http_firmware_cgi, sizeof(http_firmware_cgi)) == 0)
  {

	  length = simple_strtoul(content_boundary, NULL, 16);
	  length -= CFG_HTTP_DL_ADDR;
	  printf( "\n\n****************************\n*FIRMWARE UPGRADING*\n* DO NOT POWER OFF YOUR ROUTER! *\n****************************\n\n" );
	  raspi_erase_write((u8 *)CFG_HTTP_DL_ADDR, CFG_KERN_ADDR-CFG_FLASH_BASE, length);
	  printf("HTTP ugrade firmware done! Rebooting...\n\n");
	  /* reset the board */
	  do_reset(NULL, 0, 0, NULL);

  }
  else if(strncmp(s->filename, http_uboot_cgi, sizeof(http_uboot_cgi)) == 0)
  {

	  printf( "\n\n****************************\n*U-BOOT UPGRADING*\n* DO NOT POWER OFF YOUR ROUTER! *\n****************************\n\n" );
	  raspi_erase_write((u8 *)CFG_HTTP_DL_ADDR, CFG_FLASH_BASE - CFG_FLASH_BASE, WEBFAILSAFE_UPLOAD_UBOOT_SIZE_IN_BYTES);
	  printf("HTTP ugrade uboot done! Rebooting...\n\n");
	  /* reset the board */
	  do_reset(NULL, 0, 0, NULL);

  }
  else if(strncmp(s->filename, http_art_cgi, sizeof(http_art_cgi)) == 0)
  {

	  printf( "\n\n****************************\n*Factory  UPGRADING*\n* DO NOT POWER OFF YOUR ROUTER! *\n****************************\n\n" );
	  raspi_erase_write((u8 *)CFG_HTTP_DL_ADDR, CFG_FACTORY_ADDR - CFG_FLASH_BASE, WEBFAILSAFE_UPLOAD_ART_SIZE_IN_BYTES);
	  printf("HTTP ugrade Factory done! Rebooting...\n\n");
	  /* reset the board */
	  do_reset(NULL, 0, 0, NULL);

  }
  else if(!httpd_fs_open(s->filename, &s->file)) {//没有找到对应的页面  
//	printf("Did not find the corresponding page.\n");
	httpd_fs_open(http_404_html, &s->file);
    strcpy(s->filename, http_404_html);
    PT_WAIT_THREAD(&s->outputpt,
		   send_headers(s,
		   http_header_404));
    PT_WAIT_THREAD(&s->outputpt,
		   send_file(s));//等待线程完成(404错误头和内容)打印404页面  
  }
  
  else {
    PT_WAIT_THREAD(&s->outputpt,
		   send_headers(s,
		   http_header_200));//等待输出200成功  
    ptr = strchr(s->filename, ISO_period);
    if(ptr != NULL && strncmp(ptr, http_shtml, 6) == 0) {//判断为shtml  
      PT_INIT(&s->scriptpt);
      PT_WAIT_THREAD(&s->outputpt, handle_script(s));//等待CGI处理动态页面  
    } else {
      PT_WAIT_THREAD(&s->outputpt,//不是的话输出页面 
		     send_file(s));
    }
  }
  PSOCK_CLOSE(&s->sout);
  PT_END(&s->outputpt);
}
/*---------------------------------------------------------------------------*/
static
PT_THREAD(handle_input(struct httpd_state *s))
{
  PSOCK_BEGIN(&s->sin);

  PSOCK_READTO(&s->sin, ISO_space);//等待空格符  
  
  if(strncmp(s->inputbuf, http_get, 4) != 0 && //不是GET和POST就退出结束
      strncmp(s->inputbuf, http_post, 5) != 0) {
    PSOCK_CLOSE_EXIT(&s->sin);
  }
  PSOCK_READTO(&s->sin, ISO_space); //等待空格符

  if(s->inputbuf[0] != ISO_slash) {//不是'/'就退出结束  
    PSOCK_CLOSE_EXIT(&s->sin);
  }

  if(s->inputbuf[1] == ISO_space) {//如果是空格符就把index_html作为输出页  
    strncpy(s->filename, http_index_html, sizeof(s->filename));
  } else {//否则将后面的字符作为输出页  
    s->inputbuf[PSOCK_DATALEN(&s->sin) - 1] = 0;
    strncpy(s->filename, &s->inputbuf[0], sizeof(s->filename));
  }

  /*  httpd_log_file(uip_conn->ripaddr, s->filename);*/
  
  if(strncmp(s->inputbuf, http_firmware_cgi, sizeof(http_firmware_cgi)) == 0) {
    while(1) {
      PSOCK_READTO(&s->sin, ISO_nl);
      if(PSOCK_DATALEN(&s->sin) == 2 && s->inputbuf[0] == ISO_cr &&
                                        s->inputbuf[1] == ISO_nl)
        break;
    }

    PSOCK_READTO(&s->sin, ISO_nl);
    s->inputbuf[PSOCK_DATALEN(&s->sin) - 2] = 0;
    strcpy(content_boundary, s->inputbuf);

    while(1) {
      PSOCK_READTO(&s->sin, ISO_nl);
      if(PSOCK_DATALEN(&s->sin) == 2 && s->inputbuf[0] == ISO_cr &&
                                        s->inputbuf[1] == ISO_nl)
        break;
    }

    PSOCK_READBUF(&s->sin);
    s->state = STATE_OUTPUT;//s->state = STATE_OUTPUT之后就到handle_output(s)函数，用做输出文件的。
  }
  else if(strncmp(s->inputbuf, http_uboot_cgi, sizeof(http_uboot_cgi)) == 0) {
    while(1) {
      PSOCK_READTO(&s->sin, ISO_nl);
      if(PSOCK_DATALEN(&s->sin) == 2 && s->inputbuf[0] == ISO_cr &&
                                        s->inputbuf[1] == ISO_nl)
        break;
    }

    PSOCK_READTO(&s->sin, ISO_nl);
    s->inputbuf[PSOCK_DATALEN(&s->sin) - 2] = 0;
    strcpy(content_boundary, s->inputbuf);

    while(1) {
      PSOCK_READTO(&s->sin, ISO_nl);
      if(PSOCK_DATALEN(&s->sin) == 2 && s->inputbuf[0] == ISO_cr &&
                                        s->inputbuf[1] == ISO_nl)
        break;
    }

    PSOCK_READBUF(&s->sin);
    s->state = STATE_OUTPUT;//s->state = STATE_OUTPUT之后就到handle_output(s)函数，用做输出文件的。
  }
  else if(strncmp(s->inputbuf, http_art_cgi, sizeof(http_art_cgi)) == 0) {
    while(1) {
      PSOCK_READTO(&s->sin, ISO_nl);
      if(PSOCK_DATALEN(&s->sin) == 2 && s->inputbuf[0] == ISO_cr &&
                                        s->inputbuf[1] == ISO_nl)
        break;
    }

    PSOCK_READTO(&s->sin, ISO_nl);
    s->inputbuf[PSOCK_DATALEN(&s->sin) - 2] = 0;
    strcpy(content_boundary, s->inputbuf);

    while(1) {
      PSOCK_READTO(&s->sin, ISO_nl);
      if(PSOCK_DATALEN(&s->sin) == 2 && s->inputbuf[0] == ISO_cr &&
                                        s->inputbuf[1] == ISO_nl)
        break;
    }

    PSOCK_READBUF(&s->sin);
    s->state = STATE_OUTPUT;//s->state = STATE_OUTPUT之后就到handle_output(s)函数，用做输出文件的。
  }
  else{
    s->state = STATE_OUTPUT;//s->state = STATE_OUTPUT之后就到handle_output(s)函数，用做输出文件的。

    while(1) {
      PSOCK_READTO(&s->sin, ISO_nl);
      if(strncmp(s->inputbuf, http_referer, 8) == 0) {
        s->inputbuf[PSOCK_DATALEN(&s->sin) - 2] = 0;
        /*      httpd_log(&s->inputbuf[9]);*/
      }
    }
  }
  
  PSOCK_END(&s->sin);
}
/*---------------------------------------------------------------------------*/

static void
handle_connection(struct httpd_state *s)
{
  handle_input(s);
  //s->state = STATE_OUTPUT handle_output(s)输出文件
  if(s->state == STATE_OUTPUT) {
    handle_output(s);
	/*static PT_THREAD(handle_output(struct httpd_state *s))*/
  }
}
/*---------------------------------------------------------------------------*/

void
httpd_appcall(void)
{
  struct httpd_state *s = (struct httpd_state *)&(uip_conn->appstate);

  if(uip_closed() || uip_aborted() || uip_timedout()) {
  } else if(uip_connected()) {
    PSOCK_INIT(&s->sin, s->inputbuf, sizeof(s->inputbuf) - 1);
    PSOCK_INIT(&s->sout, s->inputbuf, sizeof(s->inputbuf) - 1);
    PT_INIT(&s->outputpt);
    s->state = STATE_WAITING;
    /*    timer_set(&s->timer, CLOCK_SECOND * 100);*/
    s->timer = 0;
    handle_connection(s);
  } else if(s != NULL) {
    if(uip_poll()) {
      ++s->timer;
      if(s->timer >= 20) {
	uip_abort();
      }
    } else {
      s->timer = 0;
    }
    handle_connection(s);
  } else {
    uip_abort();
  }
}
/*---------------------------------------------------------------------------*/
/**
 * \brief      Initialize the web server
 *
 *             This function initializes the web server and should be
 *             called at system boot-up.
 */
void
httpd_init(void)
{
  uip_listen(HTONS(80));
}
/*---------------------------------------------------------------------------*/
/** @} */
