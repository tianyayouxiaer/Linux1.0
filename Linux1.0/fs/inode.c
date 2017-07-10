/*
 *  linux/fs/inode.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/stat.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>

#include <asm/system.h>

static struct inode_hash_entry {
	struct inode * inode;
	int updating;       /* ��ֹ�������ͬʱ����һ��hash�ڵ��е�����ʱ�����ִ��� */
} hash_table[NR_IHASH];

static struct inode * first_inode;   /* inode����������ײ� */
static struct wait_queue * inode_wait = NULL;
static int nr_inodes = 0, nr_free_inodes = 0;

/* ͨ���豸�ţ�inode����ӳ��hash����
 */
static inline int const hashfn(dev_t dev, unsigned int i)
{
	return (dev ^ i) % NR_IHASH;
}

static inline struct inode_hash_entry * const hash(dev_t dev, int i)
{
	return hash_table + hashfn(dev, i);
}


/* ��inode�ڵ���뵽first_inode�ڵ����һ����
 * Ȼ��first_inodeָ��inode�ڵ㣬first_inode��һ��˫��
 */
static void insert_inode_free(struct inode *inode)
{
	inode->i_next = first_inode;
	inode->i_prev = first_inode->i_prev;
	inode->i_next->i_prev = inode;
	inode->i_prev->i_next = inode;
	first_inode = inode;
}

/* ��inode�ڵ��˫���������Ƴ������ǲ�û���ͷ�inode���ڴ�
 * ֻ��ն����inode��ָ��ָ��
 */
static void remove_inode_free(struct inode *inode)
{
	if (first_inode == inode)
		first_inode = first_inode->i_next;
	if (inode->i_next)
		inode->i_next->i_prev = inode->i_prev;
	if (inode->i_prev)
		inode->i_prev->i_next = inode->i_next;
	inode->i_next = inode->i_prev = NULL;
}

/* ��inode����hash˫���������һ��������h->inodeָ��inode
 */
void insert_inode_hash(struct inode *inode)
{
	struct inode_hash_entry *h;
	h = hash(inode->i_dev, inode->i_ino);

	inode->i_hash_next = h->inode;
	inode->i_hash_prev = NULL;
	if (inode->i_hash_next)
		inode->i_hash_next->i_hash_prev = inode;
	h->inode = inode;
}

/* inode�ڵ���һ��hash�ṹ hash_table[NR_IHASH],
 * ���е�ÿһ���Ӧһ��hash��˫��������˫��������
 * �Ľڵ㶼��ͨ���豸�ź�inode��ӳ������ġ�����ж��inode
 * ӳ�䵽hash_table�е�һ��������ڵ���˫���������������
 * �ú������ǽ�inode��˫��������ɾ����inode���ж��˫������ṹ��
 * i_hash_prev,i_hash_next��һ��˫������i_next��i_prev��һ��
 */

static void remove_inode_hash(struct inode *inode)
{
	struct inode_hash_entry *h;
	h = hash(inode->i_dev, inode->i_ino);

	/* �����hash�еĵ�һ������h->inodeָ����һ��
	 */
	if (h->inode == inode)
		h->inode = inode->i_hash_next;
	/* ��inode��hash˫�������и�ɾ������ն��ָ������
	 */
	if (inode->i_hash_next)
		inode->i_hash_next->i_hash_prev = inode->i_hash_prev;
	if (inode->i_hash_prev)
		inode->i_hash_prev->i_hash_next = inode->i_hash_next;
	inode->i_hash_prev = inode->i_hash_next = NULL;
}

/* ��inode��first_inode��ɾ����Ȼ��inode��ӵ�
 * ��first_inodeΪ�׵�˫�������ĩ�ˣ��൱�ڽ�inode
 * ��˫�������и��ƶ�һ��λ��
 */
static void put_last_free(struct inode *inode)
{
	remove_inode_free(inode);
	inode->i_prev = first_inode->i_prev;
	inode->i_prev->i_next = inode;
	inode->i_next = first_inode;
	inode->i_next->i_prev = inode;
}

/* ���·���һҳ���ڴ������inode��
 * ����ʼ����inode֮������ӹ�ϵ,
 * �����struct file����һ���������˾Ͳ����ͷţ�
 * ���ǻ��������������
 */
void grow_inodes(void)
{
	struct inode * inode;
	int i;

	if (!(inode = (struct inode*) get_free_page(GFP_KERNEL)))
		return;

	i=PAGE_SIZE / sizeof(struct inode);
	/* nr_inodes��¼���е�inode�ڵ�����ֻ�����ӣ������С
	 * nr_free_inodes��¼��ǰ����inode������
	 */
	nr_inodes += i;
	nr_free_inodes += i;

	if (!first_inode)
		inode->i_next = inode->i_prev = first_inode = inode++, i--;

	for ( ; i ; i-- )
		insert_inode_free(inode++);
}

unsigned long inode_init(unsigned long start, unsigned long end)
{
	memset(hash_table, 0, sizeof(hash_table));
	first_inode = NULL;
	return start;
}

static void __wait_on_inode(struct inode *);

static inline void wait_on_inode(struct inode * inode)
{
	if (inode->i_lock)
		__wait_on_inode(inode);
}

static inline void lock_inode(struct inode * inode)
{
	wait_on_inode(inode);
	inode->i_lock = 1;
}

static inline void unlock_inode(struct inode * inode)
{
	inode->i_lock = 0;
	wake_up(&inode->i_wait);
}

/*
 * Note that we don't want to disturb any wait-queues when we discard
 * an inode.
 *
 * Argghh. Got bitten by a gcc problem with inlining: no way to tell
 * the compiler that the inline asm function 'memset' changes 'inode'.
 * I've been searching for the bug for days, and was getting desperate.
 * Finally looked at the assembler output... Grrr.
 *
 * The solution is the weird use of 'volatile'. Ho humm. Have to report
 * it to the gcc lists, and hope we can do this more cleanly some day..
 */

/* �����豸���ε�����£��豸�ļ���inode��Ȼ���ڴ�
 * ��inode������������ɾ������������ݣ������ı�inode�ĵȴ�����
 */
void clear_inode(struct inode * inode)
{
	struct wait_queue * wait;

	wait_on_inode(inode);
	remove_inode_hash(inode);
	remove_inode_free(inode);
	wait = ((volatile struct inode *) inode)->i_wait;
	if (inode->i_count)
		nr_free_inodes++;   /*???????*/
	memset(inode,0,sizeof(*inode));
	((volatile struct inode *) inode)->i_wait = wait;
	insert_inode_free(inode);
}

/*  �ж��ļ�ϵͳ�Ƿ���Թ��� */
int fs_may_mount(dev_t dev)
{
	struct inode * inode, * next;
	int i;

	next = first_inode;
	for (i = nr_inodes ; i > 0 ; i--) {
		inode = next;
		next = inode->i_next;	/* clear_inode() changes the queues.. */
		if (inode->i_dev != dev)
			continue;
                /* ���е�������ǶԵ�ǰ���ٻ��浱������inode�豸��Ϊdev��inode�����ж�
                  * ���ifΪtrue����������豸��Ӧ���ļ�ϵͳ�ѱ����أ����߳����쳣 
                  */
		if (inode->i_count || inode->i_dirt || inode->i_lock)
			return 0;
		clear_inode(inode);
	}
	return 1;
}

/* �ж��Ƿ����ж�� */
int fs_may_umount(dev_t dev, struct inode * mount_root)
{
	struct inode * inode;
	int i;

	inode = first_inode;
        /* �ж��豸��Ӧ������inode�����ü����Ƿ�Ϊ0��
          * Ҳ�����豸���ļ��Ƿ񻹱��õ� 
          */
	for (i=0 ; i < nr_inodes ; i++, inode = inode->i_next) {
		if (inode->i_dev != dev || !inode->i_count)
			continue;
		if (inode == mount_root && inode->i_count == 1)
			continue;
		return 0;
	}
	return 1;
}

/* �жϳ������Ƿ�������¹��� */
int fs_may_remount_ro(dev_t dev)
{
	struct file * file;
	int i;

	/* Check that no files are currently opened for writing. */
	for (file = first_file, i=0; i<nr_files; i++, file=file->f_next) {
		if (!file->f_count || !file->f_inode ||
		    file->f_inode->i_dev != dev)
			continue;
                /* �ж��豸��Ӧ���ļ��Ƿ����� */
		if (S_ISREG(file->f_inode->i_mode) && (file->f_mode & 2))
			return 0;
	}
	return 1;
}

/* �����ǽ�inodeд�����ٻ��� */
static void write_inode(struct inode * inode)
{
	if (!inode->i_dirt)
		return;
	wait_on_inode(inode);
	if (!inode->i_dirt)
		return;
	if (!inode->i_sb || !inode->i_sb->s_op || !inode->i_sb->s_op->write_inode) {
		inode->i_dirt = 0;
		return;
	}
	inode->i_lock = 1;	
	inode->i_sb->s_op->write_inode(inode);
	unlock_inode(inode);
}

/* ����ʱinode�м�¼��dev��block��size�Ŀ������ݶ�ȡ�����ٻ��� */
static void read_inode(struct inode * inode)
{
	lock_inode(inode);
	if (inode->i_sb && inode->i_sb->s_op && inode->i_sb->s_op->read_inode)
		inode->i_sb->s_op->read_inode(inode);
	unlock_inode(inode);
}

/*
 * notify_change is called for inode-changing operations such as
 * chown, chmod, utime, and truncate.  It is guaranteed (unlike
 * write_inode) to be called from the context of the user requesting
 * the change.  It is not called for ordinary access-time updates.
 * NFS uses this to get the authentication correct.  -- jrs
 */

/* ���ļ����޸�ʱ����Ҫ֪ͨһ���Լ���������,��ext,ext2����notify_change
 * ������ΪNULL��ֻ����NFS���вŻᷢ��֪ͨ
 */
int notify_change(int flags, struct inode * inode)
{
	if (inode->i_sb && inode->i_sb->s_op  &&
	    inode->i_sb->s_op->notify_change)
		return inode->i_sb->s_op->notify_change(flags, inode);
	return 0;
}

/*
 * bmap is needed for demand-loading and paging: if this function
 * doesn't exist for a filesystem, then those things are impossible:
 * executables cannot be run from the filesystem etc...
 *
 * This isn't as bad as it sounds: the read-routines might still work,
 * so the filesystem would be otherwise ok (for example, you might have
 * a DOS filesystem, which doesn't lend itself to bmap very well, but
 * you could still transfer files to/from the filesystem)
 */

/* ʵ���ļ������ݿ�ŵ������豸���߼���ŵ�ӳ��
 */
int bmap(struct inode * inode, int block)
{
	if (inode->i_op && inode->i_op->bmap)
		return inode->i_op->bmap(inode,block);
	return 0;
}

void invalidate_inodes(dev_t dev)
{
	struct inode * inode, * next;
	int i;

	next = first_inode;
	for(i = nr_inodes ; i > 0 ; i--) {
		inode = next;
		next = inode->i_next;		/* clear_inode() changes the queues.. */
		if (inode->i_dev != dev)
			continue;
		/* ���inode����ʹ�õ��У��������� ��ע�������printk��ӡ */
		if (inode->i_count || inode->i_dirt || inode->i_lock) {
			printk("VFS: inode busy on removed device %d/%d\n", MAJOR(dev), MINOR(dev));
			continue;
		}
		clear_inode(inode);
	}
}

/* ͬ�����ٻ������豸dev������inode*/
void sync_inodes(dev_t dev)
{
	int i;
	struct inode * inode;

	inode = first_inode;
	for(i = 0; i < nr_inodes*2; i++, inode = inode->i_next) {
		if (dev && inode->i_dev != dev)
			continue;
		wait_on_inode(inode);
		if (inode->i_dirt)
			write_inode(inode);
	}
}

/* ����ɹ�������һ�����е�inode */
void iput(struct inode * inode)
{
	if (!inode)
		return;
	wait_on_inode(inode);
	if (!inode->i_count) {
		printk("VFS: iput: trying to free free inode\n");
		printk("VFS: device %d/%d, inode %lu, mode=0%07o\n",
			MAJOR(inode->i_rdev), MINOR(inode->i_rdev),
					inode->i_ino, inode->i_mode);
		return;
	}
	/* ����ǹܵ��ļ�����ֻ�ܴ�һ�߶�����һ��д��
	 * ��д���֮���Ӧ�ÿ�ʼ���ѵȴ����Ľ���
	 */
	if (inode->i_pipe)
		wake_up_interruptible(&PIPE_WAIT(*inode));
repeat:
	if (inode->i_count>1) {
		inode->i_count--;
		return;
	}
	/* ���ѵȴ�ʹ��inode����
	 */
	wake_up(&inode_wait);
	if (inode->i_pipe) {
		unsigned long page = (unsigned long) PIPE_BASE(*inode);
		PIPE_BASE(*inode) = NULL;
		free_page(page);
	}
	if (inode->i_sb && inode->i_sb->s_op && inode->i_sb->s_op->put_inode) {
		inode->i_sb->s_op->put_inode(inode);
		if (!inode->i_nlink)
			return;
	}
	if (inode->i_dirt) {
		write_inode(inode);	/* we can sleep - so do again */
		wait_on_inode(inode);
		goto repeat;
	}
	inode->i_count--;
	nr_free_inodes++;
	return;
}

/* �ӿ��������л�ȡһ������inode */
struct inode * get_empty_inode(void)
{
	struct inode * inode, * best;
	int i;
	/*�������inode��������inode��1/4���������inode*/
	if (nr_inodes < NR_INODE && nr_free_inodes < (nr_inodes >> 2))
		grow_inodes();
repeat:
	inode = first_inode;
	best = NULL;
	for (i = 0; i<nr_inodes; inode = inode->i_next, i++) {
		if (!inode->i_count) { /*����inodeû�б��õ�*/
			if (!best)
				best = inode;
			/*�ҵ�����dirt,lock�Ľڵ�*/
			if (!inode->i_dirt && !inode->i_lock) {
				best = inode;
				break;
			}
		}
	}
	if (!best || best->i_dirt || best->i_lock)
		if (nr_inodes < NR_INODE) {
			grow_inodes();
			goto repeat;
		}
	inode = best;
	/* ���е�����ط������ҵ��ĺ��ʵ�inode�Ѿ������inode���У�
	 * ���inodeΪNULL�����ʾ��û���ҵ�
	 */ 
	if (!inode) {
		printk("VFS: No free inodes - contact Linus\n");
		/* ����inodeʱ�������������Ҫ�����ý��̵ȴ���
		 * ��iput��ʱ������Ҫinode�Ľ���
		 */
		sleep_on(&inode_wait);
		goto repeat;
	}
	if (inode->i_lock) {
		wait_on_inode(inode);
		goto repeat;
	}
	if (inode->i_dirt) {
		write_inode(inode);
		goto repeat;
	}
	if (inode->i_count)
		goto repeat;
	clear_inode(inode);
	/* ��ʼ�����нڵ������
	 */ 
	inode->i_count = 1;
	inode->i_nlink = 1;
	inode->i_sem.count = 1;
	nr_free_inodes--;
	if (nr_free_inodes < 0) {
		printk ("VFS: get_empty_inode: bad free inode count.\n");
		nr_free_inodes = 0;
	}
	return inode;
}

/* ��ȡ�����ܵ���inode
 */
struct inode * get_pipe_inode(void)
{
	struct inode * inode;
	extern struct inode_operations pipe_inode_operations;

	if (!(inode = get_empty_inode()))
		return NULL;
	/* �������ܵ�����һҳ�����ݣ���������Կ��������ܵ����ʺϴ������ݴ���*/
	if (!(PIPE_BASE(*inode) = (char*) __get_free_page(GFP_USER))) {
		iput(inode);
		return NULL;
	}
	inode->i_op = &pipe_inode_operations;
	/*�����ܵ���һͷ����һͷд*/
	inode->i_count = 2;	/* sum of readers/writers */
	PIPE_WAIT(*inode) = NULL;
	PIPE_START(*inode) = PIPE_LEN(*inode) = 0;
	PIPE_RD_OPENERS(*inode) = PIPE_WR_OPENERS(*inode) = 0;
        /* ���ö�д����Ϊ1 */
	PIPE_READERS(*inode) = PIPE_WRITERS(*inode) = 1;
	PIPE_LOCK(*inode) = 0;
	inode->i_pipe = 1;
	inode->i_mode |= S_IFIFO | S_IRUSR | S_IWUSR;
	inode->i_uid = current->euid;
	inode->i_gid = current->egid;
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	return inode;
}

struct inode * iget(struct super_block * sb,int nr)
{
	return __iget(sb,nr,1);
}

/* ��ȡ�������Ӧi�ڵ�ŵ����ݣ�����ڸ��ٻ����п����ҵ����򷵻��ҵ���inode��
 * ���û���ҵ��������ǻ�ȡһ���յ�inode��
 * Ȼ�����inode����read_inode������Ӳ���ж�ȡext2_inode������
 */
struct inode * __iget(struct super_block * sb, int nr, int crossmntp)
{
	static struct wait_queue * update_wait = NULL;
	struct inode_hash_entry * h;
	struct inode * inode;
	struct inode * empty = NULL;

	/* ע���������ݶ����ڽ��̵��ں˶�ջ�У�
	 * ���������ͬʱִ�е���δ���ʱ,���Ƕ��ǲ�ֵͬ��
	 * ����update_wait���ǣ������ں˾�̬����
	 */
	
	if (!sb)
		panic("VFS: iget with sb==NULL");
	h = hash(sb->s_dev, nr);
repeat:
	for (inode = h->inode; inode ; inode = inode->i_hash_next)
		if (inode->i_dev == sb->s_dev && inode->i_ino == nr)
			goto found_it;
	if (!empty) {
		h->updating++;
        /* ��Ϊget_empty_inode��Ҫ����inode�������Դ�ʱ��Ҫ�����Բ��� */
		empty = get_empty_inode();
		if (!--h->updating)
			wake_up(&update_wait);
		if (empty)
			goto repeat;
		return (NULL);
	}
	/*��������һ����nr��s_dev��Ӧ��inode*/
	inode = empty;
	inode->i_sb = sb;
	inode->i_dev = sb->s_dev;
	inode->i_ino = nr;
	inode->i_flags = sb->s_flags;
	put_last_free(inode);
	insert_inode_hash(inode);
	/* inode��ʱ���е���Ϣֻ��s_dev,nr,s_flags
	 * read_inode���Ǹ���������Ϣ�Ӵ����ж�ȡinode
	 * ��Ӧ�ļ�����Ϣ��inode�����ݵ��У���ʱ��
	 * �����ļ�����������inode�е�i_op�ṹ
	 */
	read_inode(inode);
	goto return_it;

found_it:
	if (!inode->i_count)
		nr_free_inodes--;
	inode->i_count++;
	wait_on_inode(inode);
	if (inode->i_dev != sb->s_dev || inode->i_ino != nr) {
		printk("Whee.. inode changed from under us. Tell Linus\n");
		iput(inode);
		goto repeat;
	}
	if (crossmntp && inode->i_mount) {
		struct inode * tmp = inode->i_mount;
		tmp->i_count++;
		iput(inode);
		inode = tmp;
		wait_on_inode(inode);
	}
	if (empty)
		iput(empty);

return_it:
	while (h->updating)
		sleep_on(&update_wait);
	return inode;
}

/*
 * The "new" scheduling primitives (new as of 0.97 or so) allow this to
 * be done without disabling interrupts (other than in the actual queue
 * updating things: only a couple of 386 instructions). This should be
 * much better for interrupt latency.
 */
static void __wait_on_inode(struct inode * inode)
{
	struct wait_queue wait = { current, NULL };

	add_wait_queue(&inode->i_wait, &wait);
repeat:
	current->state = TASK_UNINTERRUPTIBLE;
	/*�����i�ڵ㱻����������Ҫ���ȣ�����������ִ��*/
	if (inode->i_lock) {
		schedule();
		goto repeat;
	}
	/*���Լ���i�ڵ�ĵȴ��������Ƴ�*/
	remove_wait_queue(&inode->i_wait, &wait);
	current->state = TASK_RUNNING;
}
