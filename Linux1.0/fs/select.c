/*
 * This file contains the procedures for the handling of select
 *
 * Created for Linux based loosely upon Mathius Lattner's minix
 * patches by Peter MacDonald. Heavily edited by Linus.
 */

#include <linux/types.h>
#include <linux/time.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/signal.h>
#include <linux/errno.h>

#include <asm/segment.h>
#include <asm/system.h>

#define ROUND_UP(x,y) (((x)+(y)-1)/(y))

/*
 * Ok, Peter made a complicated, but straightforward multiple_wait() function.
 * I have rewritten this, taking some shortcuts: This code may not be easy to
 * follow, but it should be free of race-conditions, and it's practical. If you
 * understand what I'm doing here, then you understand how the linux
 * sleep/wakeup mechanism works.
 *
 * Two very simple procedures, select_wait() and free_wait() make all the work.
 * select_wait() is a inline-function defined in <linux/sched.h>, as all select
 * functions have to call it to add an entry to the select table.
 */

/*
 * I rewrote this again to make the select_table size variable, take some
 * more shortcuts, improve responsiveness, and remove another race that
 * Linus noticed.  -- jrs
 */

static void free_wait(select_table * p)
{
	struct select_table_entry * entry = p->entry + p->nr;

	while (p->nr > 0) {
		p->nr--;
		entry--;
		/* entry�е�wait��ʵ�ʵĵȴ������и�ɾ�� */
		remove_wait_queue(entry->wait_address,&entry->wait);
	}
}

/*
 * The check function checks the ready status of a file using the vfs layer.
 *
 * If the file was not ready we were added to its wait queue.  But in
 * case it became ready just after the check and just before it called
 * select_wait, we call it again, knowing we are already on its
 * wait queue this time.  The second call is not necessary if the
 * select_table is NULL indicating an earlier file check was ready
 * and we aren't going to sleep on the select_table.  -- jrs
 */

/* ����ļ���� 
 */
static int check(int flag, select_table * wait, struct file * file)
{
	struct inode * inode;
	struct file_operations *fops;
	int (*select) (struct inode *, struct file *, int, select_table *);

	inode = file->f_inode;
	if ((fops = file->f_op) && (select = fops->select))
		return select(inode, file, flag, wait)
		    || (wait && select(inode, file, flag, NULL));
	/* ��ͨ�ļ��϶��ǿ��Ա���д�� */
	if (S_ISREG(inode->i_mode))
		return 1;
	return 0;
}

/* �����ļ��������ķ�Χ��һ���������ļ���������1��
 * ��in,out��ex���������з��ص��Ǽ�⵽�䶯���ļ����
 * ��������ֵ�����⵽�䶯������ 
 */
int do_select(int n, fd_set *in, fd_set *out, fd_set *ex,
	fd_set *res_in, fd_set *res_out, fd_set *res_ex)
{
	int count;
	select_table wait_table, *wait;
	struct select_table_entry *entry;
	unsigned long set;
	int i,j;
	int max = -1;

	/* ѭ�����ÿ���ļ����ϵ�λ */
	for (j = 0 ; j < __FDSET_LONGS ; j++) {
		/* һ��unsigned long�ܹ���ʾ32���ļ���������32=2��5�η�*/
		i = j << 5;
		/* ��������ļ�����������ֹͣ */
		if (i >= n)
			break;
		set = in->fds_bits[j] | out->fds_bits[j] | ex->fds_bits[j];
		/* set�ƶ�8�ξ͵���0�� */
		for ( ; set ; i++,set >>= 1) {
			if (i >= n)
				goto end_check;
			/* ���Լ����е����һλ */
			if (!(set & 1))
				continue;
			if (!current->filp[i])
				return -EBADF;
			if (!current->filp[i]->f_inode)
				return -EBADF;
			/* ��¼�����ļ������� */
			max = i;
		}
	}
end_check:
	/* ��¼ʵ�ʼ��ӵ��ļ������������ֵ+1 */
	n = max + 1;

	/* ��ȡռ��һҳ��С��select_table_entry�ڴ� */
	if(!(entry = (struct select_table_entry*) __get_free_page(GFP_KERNEL)))
		return -ENOMEM;
	FD_ZERO(res_in);
	FD_ZERO(res_out);
	FD_ZERO(res_ex);
	count = 0;
	/* ��ʼ���ȴ��б� */
	wait_table.nr = 0;
	wait_table.entry = entry;
	wait = &wait_table;
repeat:
	current->state = TASK_INTERRUPTIBLE;
	/* ѭ��ɨ�����м��ӵ��ļ���������ע�������wait��һ�μ�⵽���ļ��䶯֮ǰ����check������ΪNULL,
	 * ֮��Ͷ�ΪNULL,��Ϊÿһ�������ļ�����������Ӧһ��Ψһ��struct sock����ǰ����ֻ��Ҫ��struct sock
	 * ��sleep�еȴ�һ�ξ͹��ˡ�
	 */
	for (i = 0 ; i < n ; i++) {
		if (FD_ISSET(i,in) && check(SEL_IN,wait,current->filp[i])) {
			FD_SET(i, res_in);
			count++;
			wait = NULL; /* �˴�����ΪNULL,�ʹ����Ѿ���⵽��һ���ļ��䶯�����Բ���Ҫ�ڵȴ��������ļ���
			                  * ��if(!count-----)�ж�ʱ�Ϳ��Կ��ٷ��� 
			                  */
		}
		if (FD_ISSET(i,out) && check(SEL_OUT,wait,current->filp[i])) {
			FD_SET(i, res_out);
			count++;
			wait = NULL;
		}
		if (FD_ISSET(i,ex) && check(SEL_EX,wait,current->filp[i])) {
			FD_SET(i, res_ex);
			count++;
			wait = NULL;
		}
	}
	/* ���е��ļ���ɨ��һ�ι��󣬾��Ѿ���ӵ�struct sock�е�sleep���У�
	 * ���wait��ΪNULL�����������һ�� 
	 */
	wait = NULL;
	/* ע�������������������һ��ɨ�����һ�����ӵ��ļ��б䶯������������
	 * �ڵ������ж��������У������ǰ���̽��յ����źţ�����û��
	 * ����������select����������������ˣ���Ϊ�ú��������ں�̬���У�
	 * ��Ϊ�źŵĴ����������ں�̬���ص��û�̬��ʱ��ִ�еģ�����Ϊ��
	 * ������Ӧ�źţ����ý��̴Ӹú������п����˳� 
	 */
	if (!count && current->timeout && !(current->signal & ~current->blocked)) {
		/* ��ʱ��ǰ�����Ѿ���ӵ�����strut sock�е�sleep�����У���������cpu */
		schedule();
		goto repeat;
	}
	/* ����ǰ���̴������Ѿ���ӵ�struct sock��sleep������ɾ�� */
	free_wait(&wait_table);
	free_page((unsigned long) entry);
	current->state = TASK_RUNNING;
	return count;
}

/*
 * We do a VERIFY_WRITE here even though we are only reading this time:
 * we'll write to it eventually..
 */

/* ��fs_pointer���ݿ�����fdsetΪ��ַ���ڴ浱��
 */
static int __get_fd_set(int nr, unsigned long * fs_pointer, unsigned long * fdset)
{
	int error;

	FD_ZERO(fdset);
	if (!fs_pointer)
		return 0;
	error = verify_area(VERIFY_WRITE,fs_pointer,sizeof(fd_set));
	if (error)
		return error;
	/* ע��һ��fdsetռ��32�ֽڣ�ÿ���ֽ�8λ��ͨ��λ����ʾ��
	 * ����Ա�ʾ32*8���ļ���������������㹻��ʾ��ǰϵͳ���ļ��������ķ�Χ
	 */
	while (nr > 0) {
		*fdset = get_fs_long(fs_pointer);
		fdset++;
		fs_pointer++;
		/* ��Ϊһ��unsigned long��32λ�����Ա�ʾ32���ļ������� */
		nr -= 32;
	}
	return 0;
}

/* ��fdset�е����ݸ��Ƶ�fs_pointer��ַ����ʼ��
 */
static void __set_fd_set(int nr, unsigned long * fs_pointer, unsigned long * fdset)
{
	if (!fs_pointer)
		return;
	while (nr > 0) {
		put_fs_long(*fdset, fs_pointer);
		fdset++;
		fs_pointer++;
		nr -= 32;
	}
}

#define get_fd_set(nr,fsp,fdp) \
__get_fd_set(nr, (unsigned long *) (fsp), (unsigned long *) (fdp))

#define set_fd_set(nr,fsp,fdp) \
__set_fd_set(nr, (unsigned long *) (fsp), (unsigned long *) (fdp))

/*
 * We can actually return ERESTARTSYS insetad of EINTR, but I'd
 * like to be certain this leads to no problems. So I return
 * EINTR just for safety.
 *
 * Update: ERESTARTSYS breaks at least the xview clock binary, so
 * I'm trying ERESTARTNOHAND which restart only when you want to.
 */
asmlinkage int sys_select( unsigned long *buffer )
{
/* Perform the select(nd, in, out, ex, tv) system call. */
	int i;
	fd_set res_in, in, *inp;
	fd_set res_out, out, *outp;
	fd_set res_ex, ex, *exp;
	int n;
	struct timeval *tvp;
	unsigned long timeout;

	i = verify_area(VERIFY_READ, buffer, 20);
	if (i)
		return i;
	n = get_fs_long(buffer++);
	if (n < 0)
		return -EINVAL;
	if (n > NR_OPEN)
		n = NR_OPEN;
	/* ��������ļ������������Ƿ�ɶ� */
	inp = (fd_set *) get_fs_long(buffer++);
	/* ��������ļ������������Ƿ��д */
	outp = (fd_set *) get_fs_long(buffer++);
	exp = (fd_set *) get_fs_long(buffer++);
	tvp = (struct timeval *) get_fs_long(buffer);
	if ((i = get_fd_set(n, inp, &in)) ||
	    (i = get_fd_set(n, outp, &out)) ||
	    (i = get_fd_set(n, exp, &ex))) return i;
	timeout = ~0UL;
	if (tvp) {
		i = verify_area(VERIFY_WRITE, tvp, sizeof(*tvp));
		if (i)
			return i;
		timeout = ROUND_UP(get_fs_long((unsigned long *)&tvp->tv_usec),(1000000/HZ));
		timeout += get_fs_long((unsigned long *)&tvp->tv_sec) * HZ;
		if (timeout)
			timeout += jiffies + 1;
	}
	/* ���õ�ǰ���̵ĵȴ�ʱ�� */
	current->timeout = timeout;
	i = do_select(n, &in, &out, &ex, &res_in, &res_out, &res_ex);
	/* ��¼ʵ�ʵȴ�ʱ�� */
	if (current->timeout > jiffies)
		timeout = current->timeout - jiffies;
	else
		timeout = 0;
	current->timeout = 0;
	if (tvp) {
		put_fs_long(timeout/HZ, (unsigned long *) &tvp->tv_sec);
		timeout %= HZ;
		timeout *= (1000000/HZ);
		put_fs_long(timeout, (unsigned long *) &tvp->tv_usec);
	}
	if (i < 0)
		return i;
	if (!i && (current->signal & ~current->blocked))
		return -ERESTARTNOHAND;
	/* ����⵽�Ľ����д��������Ӧ���ڴ浱�� */
	set_fd_set(n, inp, &res_in);
	set_fd_set(n, outp, &res_out);
	set_fd_set(n, exp, &res_ex);
	return i;
}
