#ifndef _LINUX_DIRENT_H
#define _LINUX_DIRENT_H

#include <linux/limits.h>


/* ���ڴ浱�е�Ŀ¼�ṹ����Ͳ�ͬ�ļ�ϵͳ��Ŀ¼�ṹ����ת�� */
struct dirent {
	long		d_ino;
	off_t		d_off;
	unsigned short	d_reclen;
	char		d_name[NAME_MAX+1];
};

#endif
