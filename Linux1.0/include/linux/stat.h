#ifndef _LINUX_STAT_H
#define _LINUX_STAT_H

#ifdef __KERNEL__

struct old_stat {
	unsigned short st_dev;
	unsigned short st_ino;
	unsigned short st_mode;
	unsigned short st_nlink;
	unsigned short st_uid;
	unsigned short st_gid;
	unsigned short st_rdev;
	unsigned long  st_size;
	unsigned long  st_atime;
	unsigned long  st_mtime;
	unsigned long  st_ctime;
};

struct new_stat {
	unsigned short st_dev;
	unsigned short __pad1;
	unsigned long st_ino;
	unsigned short st_mode;
	unsigned short st_nlink;
	unsigned short st_uid;
	unsigned short st_gid;
	unsigned short st_rdev;
	unsigned short __pad2;
	unsigned long  st_size;
	unsigned long  st_blksize;
	unsigned long  st_blocks;
	unsigned long  st_atime;
	unsigned long  __unused1;
	unsigned long  st_mtime;
	unsigned long  __unused2;
	unsigned long  st_ctime;
	unsigned long  __unused3;
	unsigned long  __unused4;
	unsigned long  __unused5;
};

#endif

#define S_IFMT  00170000
#define S_IFSOCK 0140000
#define S_IFLNK	 0120000
#define S_IFREG  0100000
#define S_IFBLK  0060000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFIFO  0010000                         /* �����ܵ��ļ� */
#define S_ISUID  0004000		        /* u+s�������û���ִ����������Ƴ����ʱ��
								  * effective id��Ϊ����ļ���owner user
								  */
#define S_ISGID  0002000
#define S_ISVTX  0001000

#define S_ISLNK(m)	(((m) & S_IFMT) == S_IFLNK)
#define S_ISREG(m)	(((m) & S_IFMT) == S_IFREG)  /* �Ƿ�һ�������ļ� */
#define S_ISDIR(m)	(((m) & S_IFMT) == S_IFDIR)    /* �Ƿ�һ��Ŀ¼ */
#define S_ISCHR(m)	(((m) & S_IFMT) == S_IFCHR)  /* �Ƿ�һ���ַ��ļ� */
#define S_ISBLK(m)	(((m) & S_IFMT) == S_IFBLK)   /* �Ƿ�һ�����ļ� */
/* �ж��Ƿ������ܵ��ļ��������ܵ������ִ������ļ�ϵͳ���У������ݴ������ڴ浱�� */
#define S_ISFIFO(m)	(((m) & S_IFMT) == S_IFIFO)    
#define S_ISSOCK(m)	(((m) & S_IFMT) == S_IFSOCK)

#define S_IRWXU 00700     /* �û��Լ���rwxȨ�� */
#define S_IRUSR 00400     /* �û��Ƿ�ɶ� */
#define S_IWUSR 00200     /* �û��Ƿ��д */
#define S_IXUSR 00100     /* �û��Ƿ��ִ�� */

#define S_IRWXG 00070     /* �û��������rwxȨ�� */
#define S_IRGRP 00040     /* �û��������rȨ�� */
#define S_IWGRP 00020     /* �û��������wȨ�� */
#define S_IXGRP 00010     /* �û��������xȨ�� */

#define S_IRWXO 00007     /* �����û���rwxȨ�� */
#define S_IROTH 00004     /* �����û���rȨ�� */
#define S_IWOTH 00002     /* �����û���wȨ�� */
#define S_IXOTH 00001     /* �����û���xȨ�� */

#ifdef __KERNEL__
#define S_IRWXUGO	(S_IRWXU|S_IRWXG|S_IRWXO)
#define S_IALLUGO	(S_ISUID|S_ISGID|S_ISVTX|S_IRWXUGO)
#define S_IRUGO		(S_IRUSR|S_IRGRP|S_IROTH)
#define S_IWUGO		(S_IWUSR|S_IWGRP|S_IWOTH)
#define S_IXUGO		(S_IXUSR|S_IXGRP|S_IXOTH)
#endif

#endif