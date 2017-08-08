/*
 * linux/ipc/msg.c
 * Copyright (C) 1992 Krishna Balasubramanian 
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/msg.h>
#include <linux/stat.h>
#include <linux/malloc.h>

#include <asm/segment.h>

extern int ipcperms (struct ipc_perm *ipcp, short msgflg);

static void freeque (int id);
static int newque (key_t key, int msgflg);
static int findkey (key_t key);

//定义了一个消息队列全局数组，消息队列最大个数MSGMNI
static struct msqid_ds *msgque[MSGMNI];
//所有消息队列上的字节数
static int msgbytes = 0;
//所有消息队列消息个数
static int msghdrs = 0;
//消息队列序列号
static unsigned short msg_seq = 0;
//已使用消息队列个数
static int used_queues = 0;
/* 标记系统中已使用的消息队列ID，可以增大也可以减小但不会超过MSGMNI */
static int max_msqid = 0;
/* 消息的全局等待队列 */
static struct wait_queue *msg_lock = NULL;

/* 消息初始化 */
void msg_init (void)
{
	int id;

	//初始化消息队列，1.0版本采用静态创建的方式
	for (id=0; id < MSGMNI; id++) 
		msgque[id] = (struct msqid_ds *) IPC_UNUSED;
		
	msgbytes = msghdrs = msg_seq = max_msqid = used_queues = 0;
	msg_lock = NULL;
	return;
}

//发送消息
int sys_msgsnd (int msqid, struct msgbuf *msgp, int msgsz, int msgflg)
{
	int id, err;
	struct msqid_ds *msq;
	struct ipc_perm *ipcp;
	struct msg *msgh;
	long mtype;

	//参数检查，消息的大小不能超过MSGMAX
	if (msgsz > MSGMAX || msgsz < 0 || msqid < 0)
		return -EINVAL;
	if (!msgp) 
		return -EFAULT;
		
	//内存检查
	err = verify_area (VERIFY_READ, msgp->mtext, msgsz);
	if (err) 
		return err;

	//获取消息类型
	if ((mtype = get_fs_long (&msgp->mtype)) < 1)
		return -EINVAL;

	//根据msqid获取消息队列
	id = msqid % MSGMNI;
	msq = msgque [id];
	if (msq == IPC_UNUSED || msq == IPC_NOID)
		return -EINVAL;
	//获取ipcp参数
	ipcp = &msq->msg_perm; 

 slept:
 	//seq参数检查
	if (ipcp->seq != (msqid / MSGMNI)) 
		return -EIDRM;
	//参数检查
	if (ipcperms(ipcp, S_IWUGO)) 
		return -EACCES;

	//如果要发送的数据内容和消息队列上现有数据大于消息队列上最大的字节数,则消息队列上没有空间了，如果用户没有设置
	//可以等待，则会返回错误，再次尝试
	if (msgsz + msq->msg_cbytes > msq->msg_qbytes) { 
		/* no space in queue */
		if (msgflg & IPC_NOWAIT)
			return -EAGAIN;
		if (current->signal & ~current->blocked)
			return -EINTR;
		//如果用户运行等待，则进程会睡眠消息队列上，并让出cpu
		interruptible_sleep_on (&msq->wwait);
		//被唤醒后再次检查
		goto slept;
	}
	
	/* allocate message header and text space*/ 
	//消息队列未满或者被唤醒，则继续执行，分配空间，msgsz为消息体
	msgh = (struct msg *) kmalloc (sizeof(*msgh) + msgsz, GFP_USER);
	if (!msgh)
		return -ENOMEM;
	msgh->msg_spot = (char *) (msgh + 1);//消息体的地址
	memcpy_fromfs (msgh->msg_spot, msgp->mtext, msgsz); //把用户要发送的消息拷贝到消息体中

	//参数检查
	if (msgque[id] == IPC_UNUSED || msgque[id] == IPC_NOID
		|| ipcp->seq != msqid / MSGMNI) {
		kfree_s (msgh, sizeof(*msgh) + msgsz);
		return -EIDRM;
	}
	//把该消息节点插入到消息队列的的消息链表的尾部，msg_last总是执行最后一个消息
	msgh->msg_next = NULL;
	if (!msq->msg_first)
		msq->msg_first = msq->msg_last = msgh;
	else {
		msq->msg_last->msg_next = msgh;
		msq->msg_last = msgh;
	}
	//设置消息的大小和类型
	msgh->msg_ts = msgsz;
	msgh->msg_type = mtype;
	//更新消息队列的消息字节数
	msq->msg_cbytes += msgsz;
	msgbytes  += msgsz;
	msghdrs++;
	msq->msg_qnum++;//消息队列中消息个数增加一
	msq->msg_lspid = current->pid;//发送最后一个消息的进程pid
	msq->msg_stime = CURRENT_TIME;
	if (msq->rwait)//唤醒等待消息的进程
		wake_up (&msq->rwait);
	//返回成功发送消息的字节数
	return msgsz;
}

//接收消息
int sys_msgrcv (int msqid, struct msgbuf *msgp, int msgsz, long msgtyp, 
		int msgflg)
{
	struct msqid_ds *msq;
	struct ipc_perm *ipcp;
	struct msg *tmsg, *leastp = NULL;
	struct msg *nmsg = NULL;
	int id, err;

	//参数判断
	if (msqid < 0 || msgsz < 0)
		return -EINVAL;
	if (!msgp || !msgp->mtext)
	    return -EFAULT;
	//内存验证
	err = verify_area (VERIFY_WRITE, msgp->mtext, msgsz);
	if (err)
		return err;

	//根据msqid获取消息队列
	id = msqid % MSGMNI;
	msq = msgque [id];
	if (msq == IPC_NOID || msq == IPC_UNUSED)
		return -EINVAL;
	ipcp = &msq->msg_perm; 

	/* 
	 *  find message of correct type.
	 *  msgtyp = 0 => get first.
	 *  msgtyp > 0 => get first message of matching type.
	 *  msgtyp < 0 => get message with least type must be < abs(msgtype).  
	 */
	//根据类型，获取参数
	while (!nmsg) {
		if(ipcp->seq != msqid / MSGMNI)
			return -EIDRM;
		//参数检查
		if (ipcperms (ipcp, S_IRUGO))
			return -EACCES;
		//如果msgtype为0，则获取第一个消息
		if (msgtyp == 0) 
			nmsg = msq->msg_first;
		else if (msgtyp > 0) {
			//如果msgtyp大于0，则根据消息具体类型返回消息
			if (msgflg & MSG_EXCEPT) { 
				//遍历消息队列，MSG_EXCEPT，当消息类型大于0时，读与消息类型不同的第一条消息 
				for (tmsg = msq->msg_first; tmsg; 
				     tmsg = tmsg->msg_next)
					if (tmsg->msg_type != msgtyp)
						break;
				nmsg = tmsg;//找到了
			} else {
				//遍历消息队列，MSG_EXCEPT，当消息类型大于0时，读与消息类型相同的第一条消息 
				for (tmsg = msq->msg_first; tmsg; 
				     tmsg = tmsg->msg_next)
					if (tmsg->msg_type == msgtyp)
						break;
				nmsg = tmsg;
			}
		} else {
			//小于0，则获取绝对值小于msgtyp的消息
			//从链表头开始遍历，寻找类型最小的消息
			for (leastp = tmsg = msq->msg_first; tmsg; 
			     tmsg = tmsg->msg_next) 
				if (tmsg->msg_type < leastp->msg_type) 
					leastp = tmsg;
					
			if (leastp && leastp->msg_type <= - msgtyp)
				nmsg = leastp;
		}

		//找到了消息
		if (nmsg) { /* done finding a message */
			//读取的大小不足一个消息大小，
			if ((msgsz < nmsg->msg_ts) && !(msgflg & MSG_NOERROR))
				return -E2BIG;
			//实际读取消息大小
			msgsz = (msgsz > nmsg->msg_ts)? nmsg->msg_ts : msgsz;
			//如果是头结点，则消息头指针指向下一个节点
			if (nmsg ==  msq->msg_first)
				msq->msg_first = nmsg->msg_next;
			else {
				//不是头结点，则遍历消息队列，删除该消息
				for (tmsg= msq->msg_first; tmsg; 
				     tmsg = tmsg->msg_next)
					if (tmsg->msg_next == nmsg) 
						break;
				tmsg->msg_next = nmsg->msg_next;
				//尾节点
				if (nmsg == msq->msg_last)
					msq->msg_last = tmsg;
			}
			//消息队列消息个数减1
			if (!(--msq->msg_qnum))
				//如果队列上无消息，则链表置空
				msq->msg_last = msq->msg_first = NULL;
			
			msq->msg_rtime = CURRENT_TIME;
			msq->msg_lrpid = current->pid;//保存读取消息的进程pid

			//维护所有消息队列的参数
			msgbytes -= nmsg->msg_ts; 
			msghdrs--; 

			//消息队列参数维护
			msq->msg_cbytes -= nmsg->msg_ts;
			//唤醒等待队列，因为可能因为队列满，所以阻塞了消息发送
			if (msq->wwait)
				wake_up (&msq->wwait);
			//返回数据
			put_fs_long (nmsg->msg_type, &msgp->mtype);
			memcpy_tofs (msgp->mtext, nmsg->msg_spot, msgsz);
			kfree_s (nmsg, sizeof(*nmsg) + msgsz); //释放该消息数据空间
			return msgsz;
		} else {  /* did not find a message */
			//如果没有消息，如果用户设置了阻塞标志，则阻塞，如果没有阻塞睡眠，则返回
			if (msgflg & IPC_NOWAIT)
				return -ENOMSG;
			if (current->signal & ~current->blocked)
				return -EINTR; 
			interruptible_sleep_on (&msq->rwait);
		}
	} /* end while */
	return -1;
}

/* 找到为key的消息 */
static int findkey (key_t key)
{
	int id;
	struct msqid_ds *msq;
	
	for (id=0; id <= max_msqid; id++) {
		//如果所有消息队列都被分配了，则可中断的睡眠在msg_lock上
		while ((msq = msgque[id]) == IPC_NOID) 
			interruptible_sleep_on (&msg_lock);
		if (msq == IPC_UNUSED)
			continue;
		//找到key对应的消息队列
		if (key == msq->msg_perm.key)
			return id;
	}
	
	return -1;
}

//创建消息队列
static int newque (key_t key, int msgflg)
{
	int id;
	struct msqid_ds *msq;
	struct ipc_perm *ipcp;

	//轮询消息队列数组，找到一个可用的消息队列
	for (id=0; id < MSGMNI; id++) 
		/* 找到一个可用的 */
		if (msgque[id] == IPC_UNUSED) {
			msgque[id] = (struct msqid_ds *) IPC_NOID;//置上被分配标志
			goto found;
		}
	return -ENOSPC;//所有的消息队列都被占用了

found:
	//为消息队列结构分配空间
	msq = (struct msqid_ds *) kmalloc (sizeof (*msq), GFP_KERNEL);
	if (!msq) {
		//分配失败，则取消占位标志，然后唤醒获取消息队列的队列
		msgque[id] = (struct msqid_ds *) IPC_UNUSED;
		if (msg_lock)
			wake_up (&msg_lock);
		return -ENOMEM;
	}
	
	//初始化消息队列参数
	ipcp = &msq->msg_perm;
	ipcp->mode = (msgflg & S_IRWXUGO);
	ipcp->key = key;
	ipcp->cuid = ipcp->uid = current->euid;
	ipcp->gid = ipcp->cgid = current->egid;
	ipcp->seq = msg_seq;

	//初始化消息
	msq->msg_first = msq->msg_last = NULL;
	msq->rwait = msq->wwait = NULL;//消息队列读写队列
	//消息队列中字节数和消息队列中消息个数置0
	msq->msg_cbytes = msq->msg_qnum = 0;
	//
	msq->msg_lspid = msq->msg_lrpid = 0;
	msq->msg_stime = msq->msg_rtime = 0;
	msq->msg_qbytes = MSGMNB;//消息队列上最大字节数位16KB
	msq->msg_ctime = CURRENT_TIME;
	//更新max_msqid
	if (id > max_msqid)
		max_msqid = id;
	//该消息队列放到消息队列数组中
	msgque[id] = msq;
	used_queues++;
	
	/* 和共享内存里面道理差不多 */
	if (msg_lock)
		wake_up (&msg_lock);

	//返回消息队列的id
	return (int) msg_seq * MSGMNI + id;
}

int sys_msgget (key_t key, int msgflg)
{
	int id;
	struct msqid_ds *msq;

	//创建一个消息队列，或者根据key找到消息队列
	if (key == IPC_PRIVATE) 
		return newque(key, msgflg);
	if ((id = findkey (key)) == -1) { /* key not used */
		//如果用户要根据key找到对应的消息队列，没有找到，并且用户没有指定创建标志，则返回No such file or directory
		if (!(msgflg & IPC_CREAT))
			return -ENOENT;
		//如果用户指定了创建标志，则创建一个消息队列
		return newque(key, msgflg);
	}
	
	if (msgflg & IPC_CREAT && msgflg & IPC_EXCL)
		return -EEXIST;

	//如果消息队列数组中找到该消息队列，则取出该消息队列
	msq = msgque[id];
	if (msq == IPC_UNUSED || msq == IPC_NOID)
		return -EIDRM;
		
	if (ipcperms(&msq->msg_perm, msgflg))
		return -EACCES;

	
	return msq->msg_perm.seq * MSGMNI +id;
} 

//释放消息队列
static void freeque (int id)
{
	struct msqid_ds *msq = msgque[id];
	struct msg *msgp, *msgh;

	//恢复参数
	msq->msg_perm.seq++;
	msg_seq++;
	msgbytes -= msq->msg_cbytes;
	if (id == max_msqid)
		while (max_msqid && (msgque[--max_msqid] == IPC_UNUSED));
	msgque[id] = (struct msqid_ds *) IPC_UNUSED;
	used_queues--;
	//唤醒读写队列的进程
	while (msq->rwait || msq->wwait) {
		if (msq->rwait)
			wake_up (&msq->rwait); 
		if (msq->wwait)
			wake_up (&msq->wwait);
		schedule(); 
	}
	//释放消息队列上所有消息的空间
	for (msgp = msq->msg_first; msgp; msgp = msgh ) {
		msgh = msgp->msg_next;
		msghdrs--;
		kfree_s (msgp, sizeof(*msgp) + msgp->msg_ts);
	}
	//释放该消息队列
	kfree_s (msq, sizeof (*msq));
}

//消息队列控制
int sys_msgctl (int msqid, int cmd, struct msqid_ds *buf)
{
	int id, err;
	struct msqid_ds *msq, tbuf;
	struct ipc_perm *ipcp;
	
	if (msqid < 0 || cmd < 0)
		return -EINVAL;
	switch (cmd) {
	case IPC_INFO: 
	case MSG_INFO: 
		if (!buf)
			return -EFAULT;
	{ 
		struct msginfo msginfo;
		msginfo.msgmni = MSGMNI;
		msginfo.msgmax = MSGMAX;
		msginfo.msgmnb = MSGMNB;
		msginfo.msgmap = MSGMAP;
		msginfo.msgpool = MSGPOOL;
		msginfo.msgtql = MSGTQL;
		msginfo.msgssz = MSGSSZ;
		msginfo.msgseg = MSGSEG;
		if (cmd == MSG_INFO) {
			msginfo.msgpool = used_queues;
			msginfo.msgmap = msghdrs;
			msginfo.msgtql = msgbytes;
		}
		err = verify_area (VERIFY_WRITE, buf, sizeof (struct msginfo));
		if (err)
			return err;
		memcpy_tofs (buf, &msginfo, sizeof(struct msginfo));
		return max_msqid;
	}
	case MSG_STAT:
		if (!buf)
			return -EFAULT;
		err = verify_area (VERIFY_WRITE, buf, sizeof (*msq));
		if (err)
			return err;
		if (msqid > max_msqid)
			return -EINVAL;
		msq = msgque[msqid];
		if (msq == IPC_UNUSED || msq == IPC_NOID)
			return -EINVAL;
		if (ipcperms (&msq->msg_perm, S_IRUGO))
			return -EACCES;
		id = msqid + msq->msg_perm.seq * MSGMNI; 
		memcpy_tofs (buf, msq, sizeof(*msq));
		return id;
	case IPC_SET:
		if (!buf)
			return -EFAULT;
		memcpy_fromfs (&tbuf, buf, sizeof (*buf));
		break;
	case IPC_STAT:
		if (!buf)
			return -EFAULT;
		err = verify_area (VERIFY_WRITE, buf, sizeof(*msq));
		if (err)
			return err;
		break;
	}

	id = msqid % MSGMNI;
	msq = msgque [id];
	if (msq == IPC_UNUSED || msq == IPC_NOID)
		return -EINVAL;
	ipcp = &msq->msg_perm;
	if (ipcp->seq != msqid / MSGMNI)
		return -EIDRM;

	switch (cmd) {
	case IPC_STAT:
		if (ipcperms (ipcp, S_IRUGO))
			return -EACCES;
		memcpy_tofs (buf, msq, sizeof (*msq));
		return 0;
		break;
	case IPC_RMID: case IPC_SET:
		if (!suser() && current->euid != ipcp->cuid && 
		    current->euid != ipcp->uid)
			return -EPERM;
		if (cmd == IPC_RMID) {
			freeque (id); 
			return 0;
		}
		if (tbuf.msg_qbytes > MSGMNB && !suser())
			return -EPERM;
		msq->msg_qbytes = tbuf.msg_qbytes;
		ipcp->uid = tbuf.msg_perm.uid;
		ipcp->gid =  tbuf.msg_perm.gid;
		ipcp->mode = (ipcp->mode & ~S_IRWXUGO) | 
			(S_IRWXUGO & tbuf.msg_perm.mode);
		msq->msg_ctime = CURRENT_TIME;
		break;
	default:
		return -EINVAL;
		break;
	}
	return 0;
}
