/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the AF_INET socket handler.
 *
 * Version:	@(#)sock.h	1.0.4	05/13/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Corey Minyard <wf-rch!minyard@relay.EU.net>
 *		Florian La Roche <flla@stud.uni-sb.de>
 *
 * Fixes:
 *		Alan Cox	:	Volatiles in skbuff pointers. See
 *					skbuff comments. May be overdone,
 *					better to prove they can be removed
 *					than the reverse.
 *		Alan Cox	:	Added a zapped field for tcp to note
 *					a socket is reset and must stay shut up
 *		Alan Cox	:	New fields for options
 *	Pauline Middelink	:	identd support
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _SOCK_H
#define _SOCK_H

#include <linux/timer.h>
#include <linux/ip.h>		/* struct options */
#include <linux/tcp.h>		/* struct tcphdr */

#include "skbuff.h"		/* struct sk_buff */
#include "protocol.h"		/* struct inet_protocol */
#ifdef CONFIG_AX25
#include "ax25.h"
#endif
#ifdef CONFIG_IPX
#include "ipx.h"
#endif

#define SOCK_ARRAY_SIZE	64


/*
 * This structure really needs to be cleaned up.
 * Most of it is for TCP, and not used by any of
 * the other protocols.
 */
/* ��������ݽṹ 
 */
struct sock {
  struct options		*opt;
  /*wmem_alloc��һ���������������ۼ�Ϊ����ڷ����sk_buff�洢�ռ䣬
    * һ�㲻Ӧ�ó����޶�sndbuf
    */
  volatile unsigned long	wmem_alloc;
  /* ��ǰ����������С����ֵ���ɴ���ϵͳ�涨�����ֵ
    */
  volatile unsigned long	rmem_alloc;
  /* ��ʾӦ�ó�����һ��д����ʱ����Ӧ�ĵ�һ���ֽڵ����к�
    */
  unsigned long			write_seq;
  /* ��ʾ���ؽ�Ҫ���͵���һ�����ݰ��е�һ���ֽڶ�Ӧ�����к�
    */
  unsigned long			sent_seq;
  /* ��ʾ����ϣ����Զ�˽��յ���һ�����ݵ����к�
    */
  unsigned long			acked_seq;
  /* Ӧ�ó����д���ȡ(����δ��ȡ)���ݵĵ�һ�����кţ�
    * ���copied_seq=100�������к�100֮ǰ�����ݾ��ѱ��û���ȡ
    * ���к�100֮������ݶ�û�б���ȡ 
    */
  unsigned long			copied_seq;
  /* ��ʾĿǰ���ؽ��յ��ĶԱ��ط������ݵ�Ӧ�����к�
    */
  unsigned long			rcv_ack_seq;
  /* ���ڴ�С����һ������ֵ����ʾ���ؽ�Ҫ�������ݰ������������һ�����ݵ����кţ�
    * ���ɴ���window_seq
    */
  unsigned long			window_seq;
  /* ���ֶ��ڶԷ�����FIN���ݰ�ʱʹ�ã��ڽ��յ�Զ�˷��͵� FIN���ݰ���
    * fin_seq ����ʼ��Ϊ�Է��� FIN ���ݰ����һ���ֽڵ����кż� 1����ʾ���ضԴ� FIN ���ݰ�����Ӧ������к�
    */
  unsigned long			fin_seq;

  /*  ���������ֶ����ڽ������ݴ���urg_seq ��ʾ��������������кš�
    * urg_data ��һ����־λ��������Ϊ 1 ʱ����ʾ���յ��������ݡ�
    */
  unsigned long			urg_seq;
  unsigned long			urg_data;

  /*
   * Not all are volatile, but some are, so we
   * might as well say they all are.
   */
  /* inuse=1 ��ʾ������������ʹ�ø� sock �ṹ����������ȴ� */
  volatile char                 inuse,
				dead, /* dead=1 ��ʾ�� sock �ṹ�Ѵ����ͷ�״̬*/
				urginline,/* urginline=1 ��ʾ�������ݽ���������ͨ���ݴ���*/
				intr,
				blog,/* blog=1 ��ʾ��Ӧ�׽��ִ��ڽ���״̬����ʱ���յ����ݰ���������*/
				done,
				reuse,  /* ע���inuse������ */
				keepopen,/* keepopen=1 ��ʾʹ�ñ��ʱ�� */
				linger,/* linger=1 ��ʾ�ڹر��׽���ʱ��Ҫ�ȴ�һ��ʱ����ȷ�����ѹرա�*/
				delay_acks,/* delay_acks=1��ʾ�ӳ�Ӧ�𣬿�һ�ζԶ�����ݰ�����Ӧ�� */
				destroy,/* destroy=1 ��ʾ�� sock �ṹ�ȴ�����*/
				ack_timed,
				no_check,
				/* ���zapped=1�����ʾ���׽����ѱ�Զ�˸�λ��Ҫ�������ݰ����������½���
                                  * ���ӣ�Ҳ���Ǳ�ʾ���յ���һ��rst 
                                  */
				zapped,	/* In ax25 & ipx means not linked */
				broadcast,
				nonagle;/* noagle=1 ��ʾ��ʹ�� NAGLE �㷨*/
  unsigned long		        lingertime;/*��ʾ�ȴ��رղ�����ʱ�䣬ֻ�е� linger ��־λΪ 1 ʱ�����ֶβ������塣*/
  int				proc;/* �� sock �ṹ�������׽��֣������Ľ��̵Ľ��̺š�*/
  struct sock			*next;   /* �γ�struct sock��һ������ */
  /* ��RAW�׽��ִ����͹رյ�ʱ��������¼struct inet_protocolָ��
    * ��PACKET�׽��ִ����͹ر�ʱ������¼struct packet_typeָ�� 
    */
  struct sock			*pair;   

  /* send_head, send_tail ���� TCPЭ���ط����С�
    * send_head ָ��Ķ��� ��send_tail ָ��ö��е�β������ 
    * �ö��л����ѷ��ͳ�ȥ����δ�õ��Է�Ӧ������ݰ���
    */
  struct sk_buff		*volatile send_tail;
  struct sk_buff		*volatile send_head;

  /* back_logΪ���յ����ݰ�������У��ں���tcp_data�����struct sock��inuseΪ1��
    * �򽫽��յ���skb���뵽back_log��
    */
  struct sk_buff		*volatile back_log;

  /* partial �����л���ĵ������ݰ�Դ�� TCP Э�����ʽ���䣬���� TCP Э�飬Ϊ�˱�����������
    * ����С���ݰ��������������Ч�ʣ��ײ�����ջʵ�ֶ����û�Ӧ�ó����͵��������ݽ�����
    * �����棬�����۵�һ��������MSS�� ��������Ϊ���������ͳ�ȥ��partial ���������ݰ�������
    * �����ڴˣ������������ݣ�������ݲ����� OOB ���ݣ����������̷��͸�Զ�ˣ� ������ʱ����
    * һ�������������ݰ��������ݿ������ô����ݰ��У�֮�󽫸����ݰ����浽 partial �����У���
    * �´��û�������������ʱ���ں����ȼ�� partial �������Ƿ���֮ǰδ����������ݰ�������Щ
    * ���ݿ��Լ�����䵽�����ݰ���ֱ�������Ž��䷢�ͳ�ȥ����ȻΪ�˾������ٶ�Ӧ�ó���Ч��
    * ��Ӱ�죬����ȴ�������ʱ����һ���ģ���ʵ���ϣ��ں�����һ����ʱ��������ʱ����ʱʱ��
    * ��� partial �����л�����δ���������ݰ��� ��Ȼ���䷢�ͳ�ȥ�� ��ʱ���ͺ���Ϊ tcp_send_partial.
    * ���������������£�����Ҫ���� partial �����ݰ�ʱ���ں�Ҳֱ�ӵ��� tcp_send_partial ��������
    * ���͡�*/
  struct sk_buff		*partial;  /* ������󳤶ȵĴ��������ݰ���
  								     * ��ʹ�����MTUֵ���������ݰ�
  								     */
  struct timer_list		partial_timer;  /*��ʱ���� partial ָ��ָ������ݰ������⻺�棨�ȴ���ʱ�������*/
  long				retransmits; /* �ط�����*/
  struct sk_buff		*volatile wback,  /* wback,wfront��ʾд���е�ǰ�ͺ� */
				*volatile wfront,
				*volatile rqueue; /* socket���հ����У���ȡ��ʱ���Ǵ���������ж�ȡ�� */
  /* ��ͬЭ���Э�����������ע���struct proto_ops�ṹ����
    */
  struct proto			*prot;
  /* �����û���յ����ݣ����ڸõȴ������еȴ�
   */
  struct wait_queue		**sleep;/*���̵ȴ�sock�ĵ�λ*/
  unsigned long			daddr;   /*�׽��ֵ�Զ�˵�ַ*/
  /*�󶨵�ַ*/
  unsigned long			saddr;  /*�׽��ֵı��ص�ַ*/
  unsigned short		max_unacked;/* ���δ����������������Ӧ������ */
  unsigned short		window; /* Զ�˴��ڴ�С */
  unsigned short		bytes_rcv;  /* �ѽ����ֽ�����*/
/* mss is min(mtu, max_window) */
  unsigned short		mtu;  /*����䵥Ԫ*/     /* mss negotiated in the syn's */

  /* ����ĳ��ȣ�MSS=MTU-IP�ײ�����-TCP�ײ�����,MSS=TCP���Ķγ���-TCP�ײ����ȡ�
    * current eff. mss - can change  
    */
  volatile unsigned short	mss; 
  volatile unsigned short	user_mss; /*�û�ָ���� MSSֵ*/ /* mss requested by user in ioctl */
  volatile unsigned short	max_window;
  unsigned short		num;  		/*��Ӧ���ض˿ں�*/
  volatile unsigned short	cong_window;
  volatile unsigned short	cong_count;
  volatile unsigned short	ssthresh;
  volatile unsigned short	packets_out;
  volatile unsigned short	shutdown;
  volatile unsigned long	rtt;/* ����ʱ�����ֵ*/
  volatile unsigned long	mdev;/* mean deviation, ��RTTD,  ����ƫ��*/
  volatile unsigned long	rto;
/* currently backoff isn't used, but I'm maintaining it in case
 * we want to go back to a backoff formula that needs it
 */
  volatile unsigned short	backoff;/* �˱��㷨����ֵ */
  volatile short		err; /* �����־ֵ*/
  unsigned char			protocol; /* �����Э��ֵ ��ʾ��ǰ�����׽���������Э�� */
  /* �׽���״̬ */
  volatile unsigned char	state;

  /* ack_backlog�ֶμ�¼Ŀǰ�ۼƵ�Ӧ���Ͷ�δ���͵�
    * Ӧ�����ݰ��ĸ���
    */
  volatile unsigned char	ack_backlog; 
  unsigned char			max_ack_backlog;  /*��ʾ�����������*/
  unsigned char			priority;
  unsigned char			debug;
  unsigned short		rcvbuf;  /*��ʾ���ջ��������ֽڳ���*/
  unsigned short		sndbuf;  /*��ʾ���ͻ��������ֽڳ���*/
  unsigned short		type;  /* ��ʾ�׽��ֵ����� */
#ifdef CONFIG_IPX
  ipx_address			ipx_source_addr,ipx_dest_addr;
  unsigned short		ipx_type;
#endif
#ifdef CONFIG_AX25
/* Really we want to add a per protocol private area */
  ax25_address			ax25_source_addr,ax25_dest_addr;
  struct sk_buff *volatile	ax25_retxq[8];
  char				ax25_state,ax25_vs,ax25_vr,ax25_lastrxnr,ax25_lasttxnr;
  char				ax25_condition;
  char				ax25_retxcnt;
  char				ax25_xx;
  char				ax25_retxqi;
  char				ax25_rrtimer;
  char				ax25_timer;
  ax25_digi			*ax25_digipeat;
#endif  
/* IP 'private area' or will be eventually */
  int				ip_ttl;	 /* IP�ײ� TTL �ֶ�ֵ��ʵ���ϱ�ʾ·��������*/	/* TTL setting */
  int				ip_tos;	/* IP�ײ� TOS�ֶ�ֵ����������ֵ*/	/* TOS */
  struct tcphdr			dummy_th;/* ����� TCP�ײ����� TCPЭ���д���һ���������ݰ�ʱ�������ô��ֶο��ٴ��� TCP �ײ���*/

  /* This part is used for the timeout functions (timer.c). */
  /* ��ʾstruct sock��ʲô���͵�ʱ�ӣ���TIME_WRITE�ȵ� */
  int				timeout;	/* What are we waiting for? */
  struct timer_list		timer;

  /* identd */
  /* Э�����ݶ�Ӧ��socketָ�� */
  struct socket			*socket;
  
  /* Callbacks */
  /* ���ѵȴ�socket�Ľ��̣�Ҳ����sock��״̬�����ı� */
  void				(*state_change)(struct sock *sk);
  /* ��ʾsock�������Ѿ�׼���� */
  void				(*data_ready)(struct sock *sk,int bytes);
  void				(*write_space)(struct sock *sk);
  /* ���󱨸溯����ͨ��icmp_rcv�������� */
  void				(*error_report)(struct sock *sk);
  
};

/* ��������Э��Ĳ�������,��ʾ����㺯���Ĳ�������һ���ṹ
 * ���ڲ�ͬЭ�����ʹ����ͬ�Ķ˿ں�TCP��UDP����ͬʱʹ��1000�˿�
 */
struct proto {
  struct sk_buff *	(*wmalloc)(struct sock *sk,
				    unsigned long size, int force,
				    int priority);
  struct sk_buff *	(*rmalloc)(struct sock *sk,
				    unsigned long size, int force,
				    int priority);
  void			(*wfree)(struct sock *sk, void *mem,
				 unsigned long size);
  void			(*rfree)(struct sock *sk, void *mem,
				 unsigned long size);
  unsigned long		(*rspace)(struct sock *sk);
  unsigned long		(*wspace)(struct sock *sk);
  void			(*close)(struct sock *sk, int timeout);
  int			(*read)(struct sock *sk, unsigned char *to,
				int len, int nonblock, unsigned flags);
  int			(*write)(struct sock *sk, unsigned char *to,
				 int len, int nonblock, unsigned flags);
  int			(*sendto)(struct sock *sk,
				  unsigned char *from, int len, int noblock,
				  unsigned flags, struct sockaddr_in *usin,
				  int addr_len);
  int			(*recvfrom)(struct sock *sk,
				    unsigned char *from, int len, int noblock,
				    unsigned flags, struct sockaddr_in *usin,
				    int *addr_len);
  int			(*build_header)(struct sk_buff *skb,
					unsigned long saddr,
					unsigned long daddr,
					struct device **dev, int type,
					struct options *opt, int len, int tos, int ttl);
  int			(*connect)(struct sock *sk,
				  struct sockaddr_in *usin, int addr_len);
  struct sock *		(*accept) (struct sock *sk, int flags);
  void			(*queue_xmit)(struct sock *sk,
				      struct device *dev, struct sk_buff *skb,
				      int free);
  void			(*retransmit)(struct sock *sk, int all);
  void			(*write_wakeup)(struct sock *sk);
  void			(*read_wakeup)(struct sock *sk);
  int			(*rcv)(struct sk_buff *buff, struct device *dev,
			       struct options *opt, unsigned long daddr,
			       unsigned short len, unsigned long saddr,
			       int redo, struct inet_protocol *protocol);
  int			(*select)(struct sock *sk, int which,
				  select_table *wait);
  int			(*ioctl)(struct sock *sk, int cmd,
				 unsigned long arg);
  int			(*init)(struct sock *sk);
  void			(*shutdown)(struct sock *sk, int how);
  int			(*setsockopt)(struct sock *sk, int level, int optname,
  				 char *optval, int optlen);
  int			(*getsockopt)(struct sock *sk, int level, int optname,
  				char *optval, int *option);  	 
  unsigned short	max_header;
  unsigned long		retransmits;      /* ��ʾЭ�鳬ʱ�ش��Ĵ��� */
  /* ͨ���˿ںź�SOCK_ARRAY_SIZEȡ��õ����� */
  struct sock *		sock_array[SOCK_ARRAY_SIZE];
  char			name[80];   /* Э��������TCP,UDP�ȵ� */
};

#define TIME_WRITE	1  /* ��ʱ�ش� */
#define TIME_CLOSE	2   /* �ȴ��ر� */
#define TIME_KEEPOPEN	3  /* ���� */
#define TIME_DESTROY	4  /* �׽����ͷ� */
#define TIME_DONE	5	/* used to absorb those last few packets */
#define TIME_PROBE0	6   /* ��0����̽�� */
#define SOCK_DESTROY_TIME 1000	/* about 10 seconds			*/

#define PROT_SOCK	1024	/* Sockets 0-1023 can't be bound too unless you are superuser */

#define SHUTDOWN_MASK	3   /* ��ȫ�ر� */
#define RCV_SHUTDOWN	1     /* ����ͨ���ر� */
#define SEND_SHUTDOWN	2   /* ����ͨ���ر� */


extern void			destroy_sock(struct sock *sk);
extern unsigned short		get_new_socknum(struct proto *, unsigned short);
extern void			put_sock(unsigned short, struct sock *); 
extern void			release_sock(struct sock *sk);
extern struct sock		*get_sock(struct proto *, unsigned short,
					  unsigned long, unsigned short,
					  unsigned long);
extern void			print_sk(struct sock *);
extern struct sk_buff		*sock_wmalloc(struct sock *sk,
					      unsigned long size, int force,
					      int priority);
extern struct sk_buff		*sock_rmalloc(struct sock *sk,
					      unsigned long size, int force,
					      int priority);
extern void			sock_wfree(struct sock *sk, void *mem,
					   unsigned long size);
extern void			sock_rfree(struct sock *sk, void *mem,
					   unsigned long size);
extern unsigned long		sock_rspace(struct sock *sk);
extern unsigned long		sock_wspace(struct sock *sk);

extern int			sock_setsockopt(struct sock *sk,int level,int op,char *optval,int optlen);
extern int			sock_getsockopt(struct sock *sk,int level,int op,char *optval,int *optlen);

/* declarations from timer.c */
extern struct sock *timer_base;

void delete_timer (struct sock *);
void reset_timer (struct sock *, int, unsigned long);
void net_timer (unsigned long);


#endif	/* _SOCK_H */
