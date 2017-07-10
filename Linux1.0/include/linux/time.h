#ifndef _LINUX_TIME_H
#define _LINUX_TIME_H

struct timeval {
	long	tv_sec;		/* seconds */
	long	tv_usec;	/* microseconds */
};

struct timezone {
	int	tz_minuteswest;	/* minutes west of Greenwich */
	int	tz_dsttime;	/* type of dst correction */
};

#define NFDBITS			__NFDBITS

#define FD_SETSIZE		__FD_SETSIZE
#define FD_SET(fd,fdsetp)	__FD_SET(fd,fdsetp)  	/*��fd���õ�fdsetp�ļ����ϵ���*/
#define FD_CLR(fd,fdsetp)	__FD_CLR(fd,fdsetp)  	/*��fd�Ӽ��ϵ���ɾ��*/
#define FD_ISSET(fd,fdsetp)	__FD_ISSET(fd,fdsetp)   /*�ж��Ƿ�����*/
#define FD_ZERO(fdsetp)		__FD_ZERO(fdsetp)		/*��������0*/

/*
 * Names of the interval timers, and structure
 * defining a timer setting.
 */
#define	ITIMER_REAL	0
#define	ITIMER_VIRTUAL	1
#define	ITIMER_PROF	2

struct	itimerval {
	struct	timeval it_interval;	/* timer interval */
	struct	timeval it_value;	/* current value */
};

#endif
