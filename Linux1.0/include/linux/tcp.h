/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the TCP protocol.
 *
 * Version:	@(#)tcp.h	1.0.2	04/28/93
 *
 * Author:	Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _LINUX_TCP_H
#define _LINUX_TCP_H


#define HEADER_SIZE	64		/* maximum header size		*/


struct tcphdr {
  unsigned short	source;  /* ���ض˿ں� */
  unsigned short	dest;  /* Ŀ�Ķ˿ں� */
  unsigned long		seq; 	/* tcp�ֽڵ����кţ������Ķεĵ�һ���ֽڵ���� */
  unsigned long		ack_seq; /* ��Զ��ȷ�ϵ����к� */
  unsigned short	res1:4,    /* ����λ */
			doff:4,     /* ����ƫ�ƣ���ָ��tcp���Ķε�������ʼ����tcp���Ķε���ʼ���ж�Զ��
						 * ��Ϊtcp�ײ������ǲ��̶��ģ���λΪ4�ֽ� 
						 */
			fin:1,    /* ֪ͨԶ������Ҫ�ر����� */
			syn:1,	/* ���������� */
			rst:1,  /* ��λ��־��Ч�����ڸ�λ��Ӧ��tcp���� */
			psh:1,  /* �Ʊ�Ǹñ����λʱ������Զ�˾��콫�����е����ݶ��ߣ�
                  * ���ն˲��������ݽ��ж��д������Ǿ����ܿ콫
					        * ����ת��Ӧ�ô����ڴ���telnet�Ƚ���ģʽ������ʱ���ñ�־����λ��
					        */
			ack:1,	/* ��ʾ��ȷ�ϰ� */
			urg:1,    /* ���ͽ������ݱ�ǣ�����ñ��Ϊ1�����ʾ�����ݰ����н������� */
			res2:2;
  unsigned short	window;     /* ��ָ������������Է����͵������������շ��Ļ���ռ������޵�
   								 * ���ô���ֵ��Ϊ���շ��÷��ͷ������䷢�ʹ��ڵ����ݣ���λΪ�ֽ�
   								 */
  unsigned short	check;		/* У��� */
  unsigned short	urg_ptr;       /* ���ͽ������ݵ�ָ�� */
};


/* http://www.2cto.com/net/201209/157585.html */

enum {
  TCP_ESTABLISHED = 1, /* ����һ���򿪵����� */
  TCP_SYN_SENT, /* �ٷ������������ȴ�ƥ����������� */
  TCP_SYN_RECV, /* ���յ��ͷ���һ�����������ȴ��Է������������ȷ�� */
#if 0
  TCP_CLOSING, /* not a valid state, just a seperator so we can use
		  < tcp_closing or > tcp_closing for checks. */
#endif
  TCP_FIN_WAIT1, /* �ȴ�Զ��TCP�����ж����󣬻���ǰ�������ж������ȷ�� */
  TCP_FIN_WAIT2, /* ��Զ��TCP�ȴ������ж����� */
  TCP_TIME_WAIT, /* �ȴ��㹻��ʱ����ȷ��Զ��TCP���յ������ж������ȷ�� */
  TCP_CLOSE,	 /* û���κ�����״̬ */
  TCP_CLOSE_WAIT, /* �ȴ��ӱ����û������������ж����� */
  TCP_LAST_ACK,	 /* �ȴ�ԭ���ķ���Զ��TCP�������ж������ȷ�� */
  TCP_LISTEN  /* ��������Զ����TCP�˿ڵ��������� */
};

#endif	/* _LINUX_TCP_H */
