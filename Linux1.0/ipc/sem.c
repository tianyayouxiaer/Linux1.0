/*
 * linux/ipc/sem.c
 * Copyright (C) 1992 Krishna Balasubramanian 
 */

#include <linux/errno.h>
#include <asm/segment.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/sem.h>
#include <linux/ipc.h>
#include <linux/stat.h>
#include <linux/malloc.h>

extern int ipcperms (struct ipc_perm *ipcp, short semflg);
static int newary (key_t, int, int);
static int findkey (key_t key);
static void freeary (int id);

/* �ź����������ΪSEMMNI */
static struct semid_ds *semary[SEMMNI];
/*  ע��newary�����ж�used_sems�Ĳ��� */
static int used_sems = 0, used_semids = 0;                    
static struct wait_queue *sem_lock = NULL; /* �ȴ�ʹ���ź�����һ������ */
/* ���ϵͳ����ʹ�õ�����ź���ID����������Ҳ���Լ�С�����ᳬ��SEMMNI */
static int max_semid = 0;				

static unsigned short sem_seq = 0;     /* �ź������к� */


/* �ź���ͨ�ų�ʼ�� */
void sem_init (void)
{
	int i=0;
	
	sem_lock = NULL;
	used_sems = used_semids = max_semid = sem_seq = 0;
	for (i=0; i < SEMMNI; i++)
		semary[i] = (struct semid_ds *) IPC_UNUSED;
	return;
}

/* Ѱ��Ϊkey���ź�����������������������
 * ע��û�б�ʹ�õ��ź������ڲ���֮��
 * ����ź���������IPC_NOID,����̽��벻���жϵ�˯��
 */
static int findkey (key_t key)
{
	int id;
	struct semid_ds *sma;
	
	for (id=0; id <= max_semid; id++) {
		while ((sma = semary[id]) == IPC_NOID) 
			interruptible_sleep_on (&sem_lock);
		if (sma == IPC_UNUSED)
			continue;
		if (key == sma->sem_perm.key)
			return id;
	}
	return -1;
}

/* �½�һ���ź��������ü��а���nsems���ź��� */
static int newary (key_t key, int nsems, int semflg)
{
	int id;
	struct semid_ds *sma;
	struct ipc_perm *ipcp;
	int size;

	if (!nsems)
		return -EINVAL;
	/*�ź������������ܳ���SEMMNS*/
	if (used_sems + nsems > SEMMNS)
		return -ENOSPC;
	/* �ҵ�һ����û�б�ʹ�õ��ź����ṹ */
	for (id=0; id < SEMMNI; id++) 
		if (semary[id] == IPC_UNUSED) {
			semary[id] = (struct semid_ds *) IPC_NOID;
			goto found;
		}
	return -ENOSPC;
found:
	/* ������Ҫʹ���ڴ�Ĵ�С */
	size = sizeof (*sma) + nsems * sizeof (struct sem);
	used_sems += nsems;
	sma = (struct semid_ds *) kmalloc (size, GFP_KERNEL);
	if (!sma) {
		semary[id] = (struct semid_ds *) IPC_UNUSED;
		used_sems -= nsems;
		if (sem_lock)
			wake_up (&sem_lock);
		return -ENOMEM;
	}
	memset (sma, 0, size);
	/* ע��ʹ���������ڴ����� 0������һ��struct semid_ds��1���Ǻ����ڴ�*/
	sma->sem_base = (struct sem *) &sma[1];
	ipcp = &sma->sem_perm;
	ipcp->mode = (semflg & S_IRWXUGO);
	ipcp->key = key;
	ipcp->cuid = ipcp->uid = current->euid;
	ipcp->gid = ipcp->cgid = current->egid;
	ipcp->seq = sem_seq;                /* ע��������кźͷ���ֵ��ϵ */
	sma->eventn = sma->eventz = NULL;
	sma->sem_nsems = nsems;
	sma->sem_ctime = CURRENT_TIME;
        if (id > max_semid)
		max_semid = id;
	used_semids++;
	semary[id] = sma;
	if (sem_lock)
		wake_up (&sem_lock);
	/* Ϊɶ��ô����? */
	return (int) sem_seq * SEMMNI + id;
}

/* ��ϵͳ�л�ȡһ���ź��� */
int sys_semget (key_t key, int nsems, int semflg)
{
	int id;
	struct semid_ds *sma;
	
	if (nsems < 0  || nsems > SEMMSL)
		return -EINVAL;
	if (key == IPC_PRIVATE) 
		return newary(key, nsems, semflg);
	if ((id = findkey (key)) == -1) {  /* key not used */
		/* ��Ӧkey���ź���û���ҵ����ֲ��Ǵ��������������򴴽�һ���µ� */
		if (!(semflg & IPC_CREAT))
			return -ENOENT;
		return newary(key, nsems, semflg);
	}
	if (semflg & IPC_CREAT && semflg & IPC_EXCL)
		return -EEXIST;
	sma = semary[id];
	if (nsems > sma->sem_nsems)
		return -EINVAL;
	if (ipcperms(&sma->sem_perm, semflg))
		return -EACCES;
	/* ��newary������ϵ */
	return sma->sem_perm.seq*SEMMNI + id;
} 

static void freeary (int id)
{
	struct semid_ds *sma = semary[id];
	struct sem_undo *un;

	sma->sem_perm.seq++;
	sem_seq++;
	used_sems -= sma->sem_nsems;
	/* ���id����������ź���id�������������ɨ�裬
	 * �������semargΪIPC_UNUSED����������С
	 */
	if (id == max_semid)
		while (max_semid && (semary[--max_semid] == IPC_UNUSED));
	semary[id] = (struct semid_ds *) IPC_UNUSED;
	used_semids--;
	/* �������������ʲô���? ,��sem_exit�����������ϵ */
	for (un=sma->undo; un; un=un->id_next)
	        un->semadj = 0;
	/*����ź������л��еȴ����У����ѵȴ����� */
	while (sma->eventz || sma->eventn) {
		if (sma->eventz)
			wake_up (&sma->eventz);
		if (sma->eventn)
			wake_up (&sma->eventn);
		schedule();
	}
	kfree_s (sma, sizeof (*sma) + sma->sem_nsems * sizeof (struct sem));
	return;
}

/* ���źż��Ĳ��� */
int sys_semctl (int semid, int semnum, int cmd, void *arg)
{
	int i, id, val = 0;
	struct semid_ds *sma, *buf = NULL, tbuf;
	struct ipc_perm *ipcp;
	struct sem *curr;
	struct sem_undo *un;
	ushort nsems, *array = NULL;
	ushort sem_io[SEMMSL];
	
	if (semid < 0 || semnum < 0 || cmd < 0)
		return -EINVAL;

	switch (cmd) {
	case IPC_INFO: 
	case SEM_INFO: 
	{
		struct seminfo seminfo, *tmp;
		if (!arg || ! (tmp = (struct seminfo *) get_fs_long((int *)arg)))
			return -EFAULT;
		seminfo.semmni = SEMMNI;
		seminfo.semmns = SEMMNS;
		seminfo.semmsl = SEMMSL;
		seminfo.semopm = SEMOPM;
		seminfo.semvmx = SEMVMX;
		seminfo.semmnu = SEMMNU; 
		seminfo.semmap = SEMMAP; 
		seminfo.semume = SEMUME;
		seminfo.semusz = SEMUSZ;
		seminfo.semaem = SEMAEM;
		if (cmd == SEM_INFO) {
			seminfo.semusz = used_semids;
			seminfo.semaem = used_sems;
		}
		i= verify_area(VERIFY_WRITE, tmp, sizeof(struct seminfo));
		if (i)
			return i;
		memcpy_tofs (tmp, &seminfo, sizeof(struct seminfo));
		return max_semid;
	}

	case SEM_STAT:
		/* ��ȡ�����źż�����Ϣ */
		if (!arg || ! (buf = (struct semid_ds *) get_fs_long((int *) arg)))
			return -EFAULT;
		i = verify_area (VERIFY_WRITE, buf, sizeof (*sma));
		if (i)
			return i;
		if (semid > max_semid)
			return -EINVAL;
		sma = semary[semid];
		if (sma == IPC_UNUSED || sma == IPC_NOID)
			return -EINVAL;
		if (ipcperms (&sma->sem_perm, S_IRUGO))
			return -EACCES;
		id = semid + sma->sem_perm.seq * SEMMNI; 
		memcpy_tofs (buf, sma, sizeof(*sma));
		return id;
	}

	id = semid % SEMMNI;
	sma = semary [id];
	if (sma == IPC_UNUSED || sma == IPC_NOID)
		return -EINVAL;
	ipcp = &sma->sem_perm;
	nsems = sma->sem_nsems;
	if (ipcp->seq != semid / SEMMNI)
		return -EIDRM;
	if (semnum >= nsems)
		return -EINVAL;
	curr = &sma->sem_base[semnum];

	switch (cmd) {
	case GETVAL:
	case GETPID:
	case GETNCNT:
	case GETZCNT:
	case GETALL:
		if (ipcperms (ipcp, S_IRUGO))
			return -EACCES;
		switch (cmd) {
		case GETVAL : return curr->semval; 
		case GETPID : return curr->sempid;
		case GETNCNT: return curr->semncnt;
		case GETZCNT: return curr->semzcnt;
		case GETALL:
			if (!arg || ! (array = (ushort *) get_fs_long((int *) arg)))
				return -EFAULT;
			i = verify_area (VERIFY_WRITE, array, nsems* sizeof(short));
			if (i)
				return i;
		}
		break;
	case SETVAL: 
		if (!arg)
			return -EFAULT;
		if ((val = (int) get_fs_long ((int *) arg))  > SEMVMX || val < 0) 
			return -ERANGE;
		break;
	case IPC_RMID:
		if (suser() || current->euid == ipcp->cuid || 
		    current->euid == ipcp->uid) {
			freeary (id); 
			return 0;
		}
		return -EPERM;
	case SETALL: /* arg is a pointer to an array of ushort */
		if (!arg || ! (array = (ushort *) get_fs_long ((int *) arg)) )
			return -EFAULT;
		if ((i = verify_area (VERIFY_READ, array, sizeof tbuf)))
			return i;
		memcpy_fromfs (sem_io, array, nsems*sizeof(ushort));
		for (i=0; i< nsems; i++)
			if (sem_io[i] > SEMVMX)
				return -ERANGE;
		break;
	case IPC_STAT:
		if (!arg || !(buf = (struct semid_ds *) get_fs_long((int *) arg))) 
			return -EFAULT;
		if ((i = verify_area (VERIFY_WRITE, arg, sizeof tbuf)))
			return i;
		break;
	case IPC_SET:
		if (!arg || !(buf = (struct semid_ds *) get_fs_long((int *) arg))) 
			return -EFAULT;
		if ((i = verify_area (VERIFY_READ, buf, sizeof tbuf)))
			return i;
		memcpy_fromfs (&tbuf, buf, sizeof tbuf);
		break;
	}
	
	if (semary[id] == IPC_UNUSED || semary[id] == IPC_NOID)
		return -EIDRM;
	if (ipcp->seq != semid / SEMMNI)
		return -EIDRM;
	
	switch (cmd) {
	case GETALL:
		if (ipcperms (ipcp, S_IRUGO))
			return -EACCES;
		for (i=0; i< sma->sem_nsems; i++)
			sem_io[i] = sma->sem_base[i].semval;
		memcpy_tofs (array, sem_io, nsems*sizeof(ushort));
		break;
	case SETVAL:
		if (ipcperms (ipcp, S_IWUGO))
			return -EACCES;
		for (un = sma->undo; un; un = un->id_next)
			if (semnum == un->sem_num)
				un->semadj = 0;
		sma->sem_ctime = CURRENT_TIME;
		curr->semval = val;
		if (sma->eventn)
			wake_up (&sma->eventn);
		if (sma->eventz)
			wake_up (&sma->eventz);
		break;
	case IPC_SET:
		if (suser() || current->euid == ipcp->cuid || 
		    current->euid == ipcp->uid) {
			ipcp->uid = tbuf.sem_perm.uid;
			ipcp->gid = tbuf.sem_perm.gid;
			ipcp->mode = (ipcp->mode & ~S_IRWXUGO)
				| (tbuf.sem_perm.mode & S_IRWXUGO);
			sma->sem_ctime = CURRENT_TIME;
			return 0;
		}
		return -EPERM;
	case IPC_STAT:
		if (ipcperms (ipcp, S_IRUGO))
			return -EACCES;
		memcpy_tofs (buf, sma, sizeof (*sma));
		break;
	case SETALL:
		if (ipcperms (ipcp, S_IWUGO))
			return -EACCES;
		for (i=0; i<nsems; i++) 
			sma->sem_base[i].semval = sem_io[i];
		for (un = sma->undo; un; un = un->id_next)
			un->semadj = 0;
		if (sma->eventn)
			wake_up (&sma->eventn);
		if (sma->eventz)
			wake_up (&sma->eventz);
		sma->sem_ctime = CURRENT_TIME;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/* ���������ź�����ֵ
 * semid�ź�������ʶ��
 * tsops���в������ź������ṹ��������׵�ַ
 * nsops���в����ź����ĸ�������sops�ṹ�����ĸ���������ڻ����1
 */
int sys_semop (int semid, struct sembuf *tsops, unsigned nsops)
{
	int i, id;
	struct semid_ds *sma;
	struct sem *curr = NULL;
	struct sembuf sops[SEMOPM], *sop;
	struct sem_undo *un;
	int undos = 0, alter = 0, semncnt = 0, semzcnt = 0;
	
	if (nsops < 1 || semid < 0)
		return -EINVAL;
	if (nsops > SEMOPM)
		return -E2BIG;
	if (!tsops) 
		return -EFAULT;
	/* �����ݴ�tsops�Ӹ��Ƶ�sops�� */
	memcpy_fromfs (sops, tsops, nsops * sizeof(*tsops));  
	/* ע���źż���id������SEMMNI��Χ��SEMMNIֻ���źż����鷶Χ */
	id = semid % SEMMNI;
	/* �����źż�Ҫ�ǿ��õ� */
	if ((sma = semary[id]) == IPC_UNUSED || sma == IPC_NOID)
		return -EINVAL;
	/* ��ɨ��һ�����е��źŲ�������һ������ͳ�ƣ������������ */
	for (i=0; i<nsops; i++) { 
		sop = &sops[i];
		/* ��������źż��е��ź��������򷵻�ʧ�� */
		if (sop->sem_num > sma->sem_nsems)
			return -EFBIG;
		if (sop->sem_flg & SEM_UNDO)
			undos++;
		if (sop->sem_op) {
			alter++;
			/* �����н����ͷ���Դ���ں������صĵط�Ҫ���ѵȴ���Դ�Ľ��̶��� */
			if (sop->sem_op > 0)
				semncnt ++;
		}
	}
	/* �жϸ��źż��Ƿ���Ա����� */
	if (ipcperms(&sma->sem_perm, alter ? S_IWUGO : S_IRUGO))
		return -EACCES;
	/* 
	 * ensure every sop with undo gets an undo structure 
	 */
	/* �����е�undo������ӵ����̵�semun�����У�
	 * ͬʱ��undo������ӵ��źż���undo������
	 */
	if (undos) {
		for (i=0; i<nsops; i++) {
			/* �������SEM_UNDO�����������һ�� */
			if (!(sops[i].sem_flg & SEM_UNDO))
				continue;
			for (un = current->semun; un; un = un->proc_next) 
				if ((un->semid == semid) && 
				    (un->sem_num == sops[i].sem_num))
					break;
			/* ����ýڵ��Ѿ��ڽ��̵�semun�����У�����Ҫ�������� */
			if (un)
				continue;
			un = (struct sem_undo *) 
				kmalloc (sizeof(*un), GFP_ATOMIC);
			if (!un)
				return -ENOMEM; /* freed on exit */
			un->semid = semid;
			un->semadj = 0;
			un->sem_num = sops[i].sem_num;
			un->proc_next = current->semun;
			current->semun = un;
			un->id_next = sma->undo;
			sma->undo = un;
		}
	}
	
 slept:
	if (sma->sem_perm.seq != semid / SEMMNI) 
		return -EIDRM;
	/* ���δ���Ҫ�������ź��� */
	for (i=0; i<nsops; i++) {
		sop = &sops[i];
		curr = &sma->sem_base[sop->sem_num];
		/* ��������ź�ֵ�����ֵ���򷵻ط�Χ���� */
		if (sop->sem_op + curr->semval > SEMVMX)
			return -ERANGE;
		/* ���sem_opΪ0,��ý���ϣ���ȴ����ź�ֵΪ0 */
		if (!sop->sem_op && curr->semval) { 
			if (sop->sem_flg & IPC_NOWAIT)
				return -EAGAIN;
			if (current->signal & ~current->blocked) 
				return -EINTR;
			curr->semzcnt++;
			interruptible_sleep_on (&sma->eventz);
			curr->semzcnt--;
			goto slept;
		}
		/* ���Ҫ���ٵ��ź�ֵС��0��Ҳ������Դ�����ã�����̵ȴ� */
		if ((sop->sem_op + curr->semval < 0) ) { 
			if (sop->sem_flg & IPC_NOWAIT)
				return -EAGAIN;
			if (current->signal & ~current->blocked)
				return -EINTR;
			curr->semncnt++;
			interruptible_sleep_on (&sma->eventn);
			curr->semncnt--;
			goto slept;
		}
	}
	
	for (i=0; i<nsops; i++) {
		sop = &sops[i];
		curr = &sma->sem_base[sop->sem_num];
		curr->sempid = current->pid;
		/* ��ʱ�ź�ֵ������0�����ѵȴ��ź�ֵΪ0�ĵȴ����� */
		if (!(curr->semval += sop->sem_op))
			semzcnt++;
		if (!(sop->sem_flg & SEM_UNDO))
			continue;
		/* �����տ�ʼ��forѭ�������ǽ�undo�ź���ӵ��˽��̵�semun
		 * ���е��У���û�ж�undo��semadj������
		 */
		for (un = current->semun; un; un = un->proc_next) 
			if ((un->semid == semid) && 
			    (un->sem_num == sop->sem_num))
				break;
		if (!un) {
			printk ("semop : no undo for op %d\n", i);
			continue;
		}
		/* �ҵ���undo�ڵ������semadj��ֵ��
		 * ������ͷ�����Դ�� sem_op > 0,
		 * ��semadj�Ǹ���
		 */
		un->semadj -= sop->sem_op;
	}
	sma->sem_otime = CURRENT_TIME; 
    if (semncnt && sma->eventn)
		wake_up(&sma->eventn);
	/* ����еȴ��ź�ֵΪ0�Ľ������ѵȴ����� */
	if (semzcnt && sma->eventz)
		wake_up(&sma->eventz);
	return curr->semval;
}

/*
 * add semadj values to semaphores, free undo structures.
 * undo structures are not freed when semaphore arrays are destroyed
 * so some of them may be out of date.
 */
/* �����˳�ʱ����Խ���ռ�е��ź�����Դ�����ͷţ�
 * ��ֹ���������޷�ʹ���Ѿ��˳��Ľ���û���ͷŵ���Դ
 */
void sem_exit (void)
{
	struct sem_undo *u, *un = NULL, **up, **unp;
	struct semid_ds *sma;
	struct sem *sem = NULL;

	/* ѭ������ǰ���̵�undo�ź������źſ����Ƕ���źż��еĲ�ͬ�ź� */
	for (up = &current->semun; (u = *up); *up = u->proc_next, kfree(u)) {
		/* ��ȡundo�ź����ڵ��źż� */
		sma = semary[u->semid % SEMMNI];
		if (sma == IPC_UNUSED || sma == IPC_NOID) 
			continue;
		if (sma->sem_perm.seq != u->semid / SEMMNI)
			continue;
		/* Ȼ�����źż��е�undo�����в��ҵ�ǰ���̵�semun���е�undo�ڵ� */
		for (unp = &sma->undo; (un = *unp); unp = &un->id_next) {
			if (u == un) 
				goto found;
		}
		/* û�ҵ��򱨴���Ϊÿ��undo���źű������źż��б��ҵ� */
		printk ("sem_exit undo list error id=%d\n", u->semid);
		break;
found:
		/* �����ź���undo����Ĺ�ϵ��Ҳ����ɾ��un���undo�ڵ� */
		*unp = un->id_next;
		/* ��Ϊ���һ���ź�����ֵΪ0�����Բ���Ҫ���� */
		if (!un->semadj)
			continue;
		while (1) {
			/* ע���newary�з���ֵ��ϵ */
			if (sma->sem_perm.seq != un->semid / SEMMNI)
				break;
			sem = &sma->sem_base[un->sem_num];
			/* �����˳�ʱ���ź�ռ�õ���Դ���ͷŵ� */
			if (sem->semval + un->semadj >= 0) {
				sem->semval += un->semadj;
				sem->sempid = current->pid;
				sma->sem_otime = CURRENT_TIME;
				/*����ͷ�����Դ�������еȴ���Դ�Ľ��̶��У����Ѷ���*/
				if (un->semadj > 0 && sma->eventn)
					wake_up (&sma->eventn);
				/* �����ǰ�������ź�����ֵ����0��
				 * �Ҵ��ڵȴ��ź�ֵΪ0�ĵȴ����У����ѵȴ�����
				 */
				if (!sem->semval && sma->eventz)
					wake_up (&sma->eventz);
				break;
			} 
			if (current->signal & ~current->blocked)
				break;
			sem->semncnt++;
			/* ��ζ�ŵȴ�������źŵĽ��̶���һ�� */
			interruptible_sleep_on (&sma->eventn);
			sem->semncnt--;
		}
	}
	/* ���ý��̵�undo�ź���Ϊ�� */
	current->semun = NULL;
	return;
}
