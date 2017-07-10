#ifndef _LINUX_SCHED_H
#define _LINUX_SCHED_H

#define NEW_SWAP

/*
 * define DEBUG if you want the wait-queues to have some extra
 * debugging code. It's not normally used, but might catch some
 * wait-queue coding errors.
 *
 *  #define DEBUG
 */

#define HZ 100

/*
 * System setup flags..
 */
extern int hard_math;
extern int x86;
extern int ignore_irq13;
extern int wp_works_ok;

/*
 * Bus types (default is ISA, but people can check others with these..)
 * MCA_bus hardcoded to 0 for now.
 */
extern int EISA_bus;
#define MCA_bus 0

#include <linux/tasks.h>
#include <asm/system.h>

/*
 * User space process size: 3GB. This is hardcoded into a few places,
 * so don't change it unless you know what you are doing.
 */
#define TASK_SIZE	0xc0000000

/*
 * Size of io_bitmap in longwords: 32 is ports 0-0x3ff.
 */
#define IO_BITMAP_SIZE	32

/*
 * These are the constant used to fake the fixed-point load-average
 * counting. Some notes:
 *  - 11 bit fractions expand to 22 bits by the multiplies: this gives
 *    a load-average precision of 10 bits integer + 11 bits fractional
 *  - if you want to count load-averages more often, you need more
 *    precision, or rounding will get you. With 2-second counting freq,
 *    the EXP_n values would be 1981, 2034 and 2043 if still using only
 *    11 bit fractions.
 */
extern unsigned long avenrun[];		/* Load averages */

#define FSHIFT		11		/* nr of bits of precision */
#define FIXED_1		(1<<FSHIFT)	/* 1.0 as fixed-point */
#define LOAD_FREQ	(5*HZ)		/* 5 sec intervals */
#define EXP_1		1884		/* 1/exp(5sec/1min) as fixed-point */
#define EXP_5		2014		/* 1/exp(5sec/5min) */
#define EXP_15		2037		/* 1/exp(5sec/15min) */

#define CALC_LOAD(load,exp,n) \
	load *= exp; \
	load += n*(FIXED_1-exp); \
	load >>= FSHIFT;

#define CT_TO_SECS(x)	((x) / HZ)
#define CT_TO_USECS(x)	(((x) % HZ) * 1000000/HZ)

#define FIRST_TASK task[0]
#define LAST_TASK task[NR_TASKS-1]

#include <linux/head.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/signal.h>
#include <linux/time.h>
#include <linux/param.h>
#include <linux/resource.h>
#include <linux/vm86.h>
#include <linux/math_emu.h>

#define TASK_RUNNING		0
#define TASK_INTERRUPTIBLE	1
#define TASK_UNINTERRUPTIBLE	2	/*�ں�һЩ�ض����̣��ǲ��ɱ���ϵģ�Ҳ���ǿ��Ժ���ĳЩ�ź�*/
#define TASK_ZOMBIE		3           /* ���̵Ľ�ʬ״̬ */
#define TASK_STOPPED		4
#define TASK_SWAPPING		5

#ifndef NULL
#define NULL ((void *) 0)
#endif

#ifdef __KERNEL__

extern void sched_init(void);
extern void show_state(void);
extern void trap_init(void);

asmlinkage void schedule(void);

#endif /* __KERNEL__ */

struct i387_hard_struct {
	long	cwd;
	long	swd;
	long	twd;
	long	fip;
	long	fcs;
	long	foo;
	long	fos;
	long	st_space[20];	/* 8*10 bytes for each FP-reg = 80 bytes */
};

struct i387_soft_struct {
	long	cwd;
	long	swd;
	long	twd;
	long	fip;
	long	fcs;
	long	foo;
	long	fos;
	long    top;
	struct fpu_reg	regs[8];	/* 8*16 bytes for each FP-reg = 128 bytes */
	unsigned char	lookahead;
	struct info	*info;
	unsigned long	entry_eip;
};

union i387_union {
	struct i387_hard_struct hard;
	struct i387_soft_struct soft;
};

struct tss_struct {
	unsigned short	back_link,__blh;
	unsigned long	esp0;
	unsigned short	ss0,__ss0h;
	unsigned long	esp1;
	unsigned short	ss1,__ss1h;
	unsigned long	esp2;
	unsigned short	ss2,__ss2h;
	unsigned long	cr3;
	unsigned long	eip;
	unsigned long	eflags;
	unsigned long	eax,ecx,edx,ebx;
	unsigned long	esp;
	unsigned long	ebp;
	unsigned long	esi;
	unsigned long	edi;
	unsigned short	es, __esh;
	unsigned short	cs, __csh;
	unsigned short	ss, __ssh;
	unsigned short	ds, __dsh;
	unsigned short	fs, __fsh;
	unsigned short	gs, __gsh;
	unsigned short	ldt, __ldth;
	unsigned short	trace, bitmap;
	unsigned long	io_bitmap[IO_BITMAP_SIZE+1];
	unsigned long	tr;
	unsigned long	cr2, trap_no, error_code;
	union i387_union i387;
};

struct task_struct {
/* these are hardcoded - don't touch */
	volatile long state;	/* -1 unrunnable, 0 runnable, >0 stopped */
	long counter;           /* ��̬���ȼ� */
	long priority;
	/* ע��ܶ�ط�������д�� if (current->signal & ~current->blocked)
	 * signal��ʾ���͸����̵��ź�λͼ��blocked��ʾ�����������ź�λͼ
	 */
	unsigned long signal;   
	unsigned long blocked;	/* bitmap of masked signals */
	unsigned long flags;	/* per process flags, defined below */
	int errno;
	int debugreg[8];  /* Hardware debugging registers */
/* various fields */
	struct task_struct *next_task, *prev_task;
	struct sigaction sigaction[32];
	unsigned long saved_kernel_stack;
	unsigned long kernel_stack_page;
	/* exit_signal��ʾ�˳�ʱ�������̷��͵��ź�
	 */
	int exit_code, exit_signal;
        /* �Ƿ���elf��ִ���ļ���ʽ */
	int elf_executable:1;
	int dumpable:1;
	/* ��ʾ�ڴ�Խ�ʱ���ý����Ƿ���Ա����� */
	int swappable:1;
    /* ���ֽ�������ִ���ϳ�����룬������ϵͳ����execve()װ��һ���µĳ��� 
      */
	int did_exec:1;
	/* start_code,end_code��ʾ����εĵ�ַ�ռ�
	  * end_data��ʾ���ݶεĽ�����ַ 
	  * start_brk��ʾheap����ʼ�ռ䣬brk��ʾ��ǰ��heapָ�� 
	  * start_stack��ʾstack�ε���ʼ��ַ 
	  */ 
	unsigned long start_code,end_code,end_data,start_brk,brk,start_stack,start_mmap;
	unsigned long arg_start, arg_end, env_start, env_end;
	/* pgrp��ʾ������ţ�pid��ʾ���̺ţ����������һ��
	 * �������쵼���̣��쵼���̵�pid��Ϊ�������id 
	 * ����ʶ������飬��������黹���Թ���һ���Ự
	 * 
	 */
	int pid,pgrp,session,leader;
    /* ��û�и�����ID��Suplimentary ID���������һ���û�ֻ���������ѵ�ͬ���飬
      * ����chinsung���û�ID��1000����ô��ֻ����chinsung�飬��������IDҲ��1000��
      * ���Ҫ����һ������ftp���ļ�����ôӦ���Ƚ��Լ�����ID����ftp����ID���С�
      * ��Ȼ�����е��鷳��������BSD4.2�Ժ󣬳����˸�����ID�ĸ��
      * һ���û���������һ���飬�������������ɸ����飻�ڽ���Ȩ��У��ʱ��
      * ����������û����ڵ��飬��Ҫ�������û����ڵĸ����顣
      * �����������ʵ���ˣ��ñ�����ͬʱ���ںü�����Ŀ��
      */
	int	groups[NGROUPS];
	/* 
	 * pointers to (original) parent process, youngest child, younger sibling,
	 * older sibling, respectively.  (p->father can be replaced with 
	 * p->p_pptr->pid)
	 */

	/* p_opptr��¼�����ý��̵Ľ��̣�p_pptrΪ��ǰ�����̣�p_cptrΪ��С���ӽ���
	 * p_ysptr��¼������ֵܽ��̣�p_osptrΪ�ϵ��ֵܽ���
	 */
	struct task_struct *p_opptr,*p_pptr, *p_cptr, *p_ysptr, *p_osptr;
	/* �ȴ����ӽ��̵Ķ��У��ǽ��Լ���ӵ��ö��е���
	 */
	struct wait_queue *wait_chldexit;	/* for wait4() */
	/*
	 * For ease of programming... Normal sleeps don't need to
	 * keep track of a wait-queue: every task has an entry of its own
	 */
	/* �û�id����Чid һ���������û��SUID��SGIDλ����euid=uid egid=gid��
	 * �ֱ����������������û���uid��gid������kevin�û���uid��gid�ֱ�Ϊ204��202��
	 * foo�û���uid��gidΪ 200��201��kevin����myfile�����γɵĽ��̵�euid=uid=204��
	 * egid=gid=202���ں˸�����Щֵ���жϽ��̶���Դ���ʵ����ƣ�
	 * ��ʵ����kevin�û�����Դ���ʵ�Ȩ�ޣ���fooû��ϵ�� 
      * ���һ������������SUID����euid��egid��ɱ����еĳ���������ߵ�uid��gid��
      * ����kevin�û�����myfile��euid=200��egid=201��uid=204��gid=202��
      * ��������̾�����������foo����Դ����Ȩ�ޡ�
      * ʹ��euid��ȷ����Դ�ķ���Ȩ�� 
      */
	unsigned short uid,euid,suid;
	/* �û���id����Ч��id*/
	unsigned short gid,egid,sgid;
	/* �����ʱ��ָ�����̼����ñ����»��ѣ�����tickΪ��λ */
	unsigned long timeout;
	/* ÿ��tickʹit_real_value��1������0ʱ����̷����ź�SIGALRM�����������ó�ֵ��
	 * ��ֵ������it_real_cr����
	 * �������û�̬�����ں�̬ÿ��tickʹit_prof_value��1������0ʱ
	 * ����̷���SIGPROF�źţ����������ã���ֵ��it_prof_incr����
	 * �������û�ִ̬��ʱÿ��tickʹit_virt_value��1������0
	 * ʱ������̷���SIGVTALRM�źţ����������ó�ֵ����ֵ��it_virt_incr����
	 */
	unsigned long it_real_value, it_prof_value, it_virt_value;
	unsigned long it_real_incr, it_prof_incr, it_virt_incr;
	long utime,stime,cutime,cstime,start_time;
	/* ��ʾ�Խ�������������������ȱҳ�жϴ��� ��
	  * maj_flt��ʾ��Ҫ��д���̣��������ڴ�ҳ�ڴ�������Ҫload�������ڴ浱�� 
	  * Ҳ����������ҳ�ڴ治�㣬��Ҫ��̭��������ҳ�������� 
	  */
	unsigned long min_flt, maj_flt;
	unsigned long cmin_flt, cmaj_flt;
        /* ���̵ĸ�����Դ���ƣ���������ݶΣ���ջ�Σ����̵��������ռ�ȵ� */
	struct rlimit rlim[RLIM_NLIMITS]; 
	unsigned short used_math;
	unsigned short rss;	/* number of resident pages */ /*��ǰ�������е��ڴ�ҳ��*/
	char comm[16];
	struct vm86_struct * vm86_info;
	unsigned long screen_bitmap;
/* file system info */
	int link_count;
	/* ��ʾ���̵Ŀ����ն� */
	int tty;		/* -1 if no tty, so it must be signed */
	unsigned short umask;  /* ���̴����ļ�ʱĬ�ϵ�Ȩ�޷��� */
	struct inode * pwd;    /* ���̵ĵ�ǰ����Ŀ¼ */
	struct inode * root;   /* ��ǰ���̵ĸ�Ŀ¼ */
	struct inode * executable; /* ��ǰ���̿�ִ���ļ� */
	/* �����ַ�����ڵ�ַ�����У�
	 * ��ַ�ռ��ǰ��մ�С�����˳������ŵ�
	 */
	struct vm_area_struct * mmap;
	/* ���̵Ĺ����ڴ��б�
	 */
	struct shm_desc *shm;
	/* ���̵�undo�ź��б�Ҳ�����ڽ����˳���ʱ��
	 * �������ź���ռ�õ���Դ���ͷŵ�����Ȼ��Ҫ����Դ��
	 * ���������޷��õ���Դ��������Դ�ҷ�
	 */
	struct sem_undo *semun;
	struct file * filp[NR_OPEN];
	/* ���ӽ���ִ��exec�庯���滻�ӽ���ʱ����ʾ��Ҫ�رյ��ļ�������
	 * ��Ȼ�ļ���Զ���ڴ�״̬
	 */
	fd_set close_on_exec;
/* ldt for this task - used by Wine.  If NULL, default_ldt is used */
	struct desc_struct *ldt;
/* tss for this task */
	struct tss_struct tss;
#ifdef NEW_SWAP
	unsigned long old_maj_flt;	/* old value of maj_flt */
	unsigned long dec_flt;		/* page fault count of the last time */
	unsigned long swap_cnt;		/* number of pages to swap on next pass */ /*��ǰ��Ҫ������ҳ������*/
	short swap_table;		/* current page table */  /*��ǰ��Ҫ��������һ��ҳ�������*/
	short swap_page;		/* current page */ /* ��ǰ��Ҫ�������̶���ҳ������ */
#endif NEW_SWAP
	struct vm_area_struct *stk_vma;
};

/*
 * Per process flags
 */
#define PF_ALIGNWARN	0x00000001	/* Print alignment warning msgs */
					/* Not implemented yet, only for 486*/
#define PF_PTRACED	0x00000010	/* set if ptrace (0) has been called. */
#define PF_TRACESYS	0x00000020	/* tracing system calls */

/*
 * cloning flags:
 */
#define CSIGNAL		0x000000ff	/* signal mask to be sent at exit */
#define COPYVM		0x00000100	/* set if VM copy desired (like normal fork()) */
#define COPYFD		0x00000200	/* set if fd's should be copied, not shared (NI) */

/*
 *  INIT_TASK is used to set up the first task table, touch at
 * your own risk!. Base=0, limit=0x1fffff (=2MB)
 */
#define INIT_TASK \
/* state etc */	{ 0,15,15,0,0,0,0, \
/* debugregs */ { 0, },            \
/* schedlink */	&init_task,&init_task, \
/* signals */	{{ 0, },}, \
/* stack */	0,(unsigned long) &init_kernel_stack, \
/* ec,brk... */	0,0,0,0,0,0,0,0,0,0,0,0,0, \
/* argv.. */	0,0,0,0, \
/* pid etc.. */	0,0,0,0, \
/* suppl grps*/ {NOGROUP,}, \
/* proc links*/ &init_task,&init_task,NULL,NULL,NULL,NULL, \
/* uid etc */	0,0,0,0,0,0, \
/* timeout */	0,0,0,0,0,0,0,0,0,0,0,0, \
/* min_flt */	0,0,0,0, \
/* rlimits */   { {LONG_MAX, LONG_MAX}, {LONG_MAX, LONG_MAX},  \
		  {LONG_MAX, LONG_MAX}, {LONG_MAX, LONG_MAX},  \
		  {       0, LONG_MAX}, {LONG_MAX, LONG_MAX}}, \
/* math */	0, \
/* rss */	2, \
/* comm */	"swapper", \
/* vm86_info */	NULL, 0, \
/* fs info */	0,-1,0022,NULL,NULL,NULL,NULL, \
/* ipc */	NULL, NULL, \
/* filp */	{NULL,}, \
/* cloe */	{{ 0, }}, \
/* ldt */	NULL, \
/*tss*/	{0,0, \
	 sizeof(init_kernel_stack) + (long) &init_kernel_stack, KERNEL_DS, 0, \
	 0,0,0,0,0,0, \
	 (long) &swapper_pg_dir, \
	 0,0,0,0,0,0,0,0,0,0, \
	 USER_DS,0,USER_DS,0,USER_DS,0,USER_DS,0,USER_DS,0,USER_DS,0, \
	 _LDT(0),0, \
	 0, 0x8000, \
/* ioperm */ 	{~0, }, \
	 _TSS(0), 0, 0,0, \
/* 387 state */	{ { 0, }, } \
	} \
}

extern struct task_struct init_task;
extern struct task_struct *task[NR_TASKS];
extern struct task_struct *last_task_used_math;
extern struct task_struct *current;
extern unsigned long volatile jiffies;
extern unsigned long itimer_ticks;
extern unsigned long itimer_next;
extern struct timeval xtime;
extern int need_resched;

#define CURRENT_TIME (xtime.tv_sec)

extern void sleep_on(struct wait_queue ** p);
extern void interruptible_sleep_on(struct wait_queue ** p);
extern void wake_up(struct wait_queue ** p);
extern void wake_up_interruptible(struct wait_queue ** p);

extern void notify_parent(struct task_struct * tsk);
extern int send_sig(unsigned long sig,struct task_struct * p,int priv);
extern int in_group_p(gid_t grp);

extern int request_irq(unsigned int irq,void (*handler)(int));
extern void free_irq(unsigned int irq);
extern int irqaction(unsigned int irq,struct sigaction * sa);

/*
 * Entry into gdt where to find first TSS. GDT layout:
 *   0 - nul
 *   1 - kernel code segment
 *   2 - kernel data segment
 *   3 - user code segment
 *   4 - user data segment
 * ...
 *   8 - TSS #0
 *   9 - LDT #0
 *  10 - TSS #1
 *  11 - LDT #1
 */
/* ϵͳΪÿ�����̱���������ν��棬����nΪ����task_struct�����������е�����
 * ����ÿ��������ռ��8byte����ÿ������ռ���������n<<4,Ȼ��Ҫ����ǰ���
 * ��ʼλ�õ�ƫ������Ҳ����_TSS(n)��ȡ�����������ĵ�ַ
 */
#define FIRST_TSS_ENTRY 8
#define FIRST_LDT_ENTRY (FIRST_TSS_ENTRY+1)
#define _TSS(n) ((((unsigned long) n)<<4)+(FIRST_TSS_ENTRY<<3))
#define _LDT(n) ((((unsigned long) n)<<4)+(FIRST_LDT_ENTRY<<3))
#define load_TR(n) __asm__("ltr %%ax": /* no output */ :"a" (_TSS(n)))
#define load_ldt(n) __asm__("lldt %%ax": /* no output */ :"a" (_LDT(n)))
#define store_TR(n) \
__asm__("str %%ax\n\t" \
	"subl %2,%%eax\n\t" \
	"shrl $4,%%eax" \
	:"=a" (n) \
	:"0" (0),"i" (FIRST_TSS_ENTRY<<3))
/*
 *	switch_to(n) should switch tasks to task nr n, first
 * checking that n isn't the current task, in which case it does nothing.
 * This also clears the TS-flag if the task we switched to has used
 * tha math co-processor latest.
 */
/* current�����ʱ�򱻸ı䣬Ҳ�����л���tsk�������
 */
#define switch_to(tsk) \
__asm__("cmpl %%ecx,_current\n\t" \
	"je 1f\n\t" \
	"cli\n\t" \
	"xchgl %%ecx,_current\n\t" \
	"ljmp %0\n\t" \
	"sti\n\t" \
	"cmpl %%ecx,_last_task_used_math\n\t" \
	"jne 1f\n\t" \
	"clts\n" \
	"1:" \
	: /* no output */ \
	:"m" (*(((char *)&tsk->tss.tr)-4)), \
	 "c" (tsk) \
	:"cx")

#define _set_base(addr,base) \
__asm__("movw %%dx,%0\n\t" \
	"rorl $16,%%edx\n\t" \
	"movb %%dl,%1\n\t" \
	"movb %%dh,%2" \
	: /* no output */ \
	:"m" (*((addr)+2)), \
	 "m" (*((addr)+4)), \
	 "m" (*((addr)+7)), \
	 "d" (base) \
	:"dx")

#define _set_limit(addr,limit) \
__asm__("movw %%dx,%0\n\t" \
	"rorl $16,%%edx\n\t" \
	"movb %1,%%dh\n\t" \
	"andb $0xf0,%%dh\n\t" \
	"orb %%dh,%%dl\n\t" \
	"movb %%dl,%1" \
	: /* no output */ \
	:"m" (*(addr)), \
	 "m" (*((addr)+6)), \
	 "d" (limit) \
	:"dx")

#define set_base(ldt,base) _set_base( ((char *)&(ldt)) , base )
#define set_limit(ldt,limit) _set_limit( ((char *)&(ldt)) , (limit-1)>>12 )

/*
 * The wait-queues are circular lists, and you have to be *very* sure
 * to keep them correct. Use only these two functions to add/remove
 * entries in the queues.
 */

/* ��wait��ӵ���pָ��Ķ���Ϊ�ײ�����һ��λ�� */
extern inline void add_wait_queue(struct wait_queue ** p, struct wait_queue * wait)
{
	unsigned long flags;

#ifdef DEBUG
	if (wait->next) {
		unsigned long pc;
		__asm__ __volatile__("call 1f\n"
			"1:\tpopl %0":"=r" (pc));
		printk("add_wait_queue (%08x): wait->next = %08x\n",pc,(unsigned long) wait->next);
	}
#endif
	save_flags(flags);
	cli();
	/* �������Ϊ�գ�����waitָ����ף�nextָ���Լ�
	 * ���������������ӽڵ�ʱ���������о���һ���պϵ�Բ����
	 * ����wait��ӵ���*pΪ���׵���һ���ط�
	 */
	if (!*p) {
		wait->next = wait;
		*p = wait;
	} else {
		wait->next = (*p)->next;
		(*p)->next = wait;
	}
	restore_flags(flags);
}

/* ��wait�Ӷ���pָ��Ķ�����ɾ�� */
extern inline void remove_wait_queue(struct wait_queue ** p, struct wait_queue * wait)
{
	unsigned long flags;
	struct wait_queue * tmp;
#ifdef DEBUG
	unsigned long ok = 0;
#endif

	save_flags(flags);
	cli();
	/* ���������ֻ��һ�����򽫸ö���ָΪNULL
	 * ��֮ɨ��ȴ����У�������ɾ��wait
	 */
	if ((*p == wait) &&
#ifdef DEBUG
	    (ok = 1) &&
#endif
	    ((*p = wait->next) == wait)) {
		*p = NULL;
	} else {
		/* ��Ϊ������һ���պϵ�Բ��������tmp=wait�����ǿ����ҵ�wait�ģ�
		 * ����Ϊʲô����*p����ʼ����?
		 */
		tmp = wait;
		while (tmp->next != wait) {
			tmp = tmp->next;
#ifdef DEBUG
			if (tmp == *p)
				ok = 1;
#endif
		}
		tmp->next = wait->next;
	}
	wait->next = NULL;
	restore_flags(flags);
#ifdef DEBUG
	if (!ok) {
		printk("removed wait_queue not on list.\n");
		printk("list = %08x, queue = %08x\n",(unsigned long) p, (unsigned long) wait);
		__asm__("call 1f\n1:\tpopl %0":"=r" (ok));
		printk("eip = %08x\n",ok);
	}
#endif
}

extern inline void select_wait(struct wait_queue ** wait_address, select_table * p)
{
	struct select_table_entry * entry;

	/* ���������һ��ָ��ΪNULL,�򷵻أ��������� */
	if (!p || !wait_address)
		return;
	/* �������ܳ��� */
	if (p->nr >= __MAX_SELECT_TABLE_ENTRIES)
		return;
	/* ��ȡ��ǰ������entry�ĵ�ַ */
 	entry = p->entry + p->nr;
	/* ע������ط�����Ҫ��Ҳ����Ҫ��¼wait�����ǵȴ����ĸ������� */
	entry->wait_address = wait_address;
	entry->wait.task = current;
	entry->wait.next = NULL;
	add_wait_queue(wait_address,&entry->wait);
	/* ����nr������ */
	p->nr++;
}

extern void __down(struct semaphore * sem);

extern inline void down(struct semaphore * sem)
{
	if (sem->count <= 0)
		__down(sem);
	sem->count--;
}

extern inline void up(struct semaphore * sem)
{
	sem->count++;
	wake_up(&sem->wait);
}	

static inline unsigned long _get_base(char * addr)
{
	unsigned long __base;
	__asm__("movb %3,%%dh\n\t"
		"movb %2,%%dl\n\t"
		"shll $16,%%edx\n\t"
		"movw %1,%%dx"
		:"=&d" (__base)
		:"m" (*((addr)+2)),
		 "m" (*((addr)+4)),
		 "m" (*((addr)+7)));
	return __base;
}

#define get_base(ldt) _get_base( ((char *)&(ldt)) )

static inline unsigned long get_limit(unsigned long segment)
{
	unsigned long __limit;
	__asm__("lsll %1,%0"
		:"=r" (__limit):"r" (segment));
	return __limit+1;
}

/*  ������̴���ʧ�ܣ��򽫽����ڽ��̶����еĹ�ϵ�����
 *
 */

#define REMOVE_LINKS(p) do { unsigned long flags; \
	save_flags(flags) ; cli(); \
	(p)->next_task->prev_task = (p)->prev_task; \
	(p)->prev_task->next_task = (p)->next_task; \
	restore_flags(flags); \
	if ((p)->p_osptr) \
		(p)->p_osptr->p_ysptr = (p)->p_ysptr; \
	if ((p)->p_ysptr) \
		(p)->p_ysptr->p_osptr = (p)->p_osptr; \
	else \
		(p)->p_pptr->p_cptr = (p)->p_osptr; \
	} while (0)

/*  ���½���task_struct���뵽��init_taskΪ�׵�˫����еĶ�β
 *  
 */

#define SET_LINKS(p) do { unsigned long flags; \
	save_flags(flags); cli(); \
	(p)->next_task = &init_task; \
	(p)->prev_task = init_task.prev_task; \
	init_task.prev_task->next_task = (p); \
	init_task.prev_task = (p); \
	restore_flags(flags); \
	(p)->p_ysptr = NULL; \
	if (((p)->p_osptr = (p)->p_pptr->p_cptr) != NULL) \
		(p)->p_osptr->p_ysptr = p; \
	(p)->p_pptr->p_cptr = p; \
	} while (0)

#define for_each_task(p) \
	for (p = &init_task ; (p = p->next_task) != &init_task ; )

/*
 * This is the ldt that every process will get unless we need
 * something other than this.
 */
extern struct desc_struct default_ldt;

/* This special macro can be used to load a debugging register */

#define loaddebug(register) \
		__asm__("movl %0,%%edx\n\t" \
			"movl %%edx,%%db" #register "\n\t" \
			: /* no output */ \
			:"m" (current->debugreg[register]) \
			:"dx");

#endif
