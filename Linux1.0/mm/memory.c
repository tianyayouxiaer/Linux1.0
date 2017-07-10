/*
 *  linux/mm/memory.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * demand-loading started 01.12.91 - seems it is high on the list of
 * things wanted, and it should be easy to implement. - Linus
 */

/*
 * Ok, demand-loading was easy, shared pages a little bit tricker. Shared
 * pages started 02.12.91, seems to work. - Linus.
 *
 * Tested sharing by executing about 30 /bin/sh: under the old kernel it
 * would have taken more than the 6M I have free, but it worked well as
 * far as I could see.
 *
 * Also corrected some "invalidate()"s - I wasn't doing enough of them.
 */

/*
 * Real VM (paging to/from disk) started 18.12.91. Much more work and
 * thought has to go into this. Oh, well..
 * 19.12.91  -  works, somewhat. Sometimes I get faults, don't know why.
 *		Found it. Everything seems to work now.
 * 20.12.91  -  Ok, making the swap-device changeable like the root.
 */

#include <asm/system.h>
#include <linux/config.h>

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>


//mem_init�д��ݵ�end_mem�����ں�֧�ֵ����������ַ
unsigned long high_memory = 0;

/*pg0ӳ��������ַ��0-4MB��Χ*/
extern unsigned long pg0[1024];		/* page table for 0-4MB for everybody */

extern void sound_mem_init(void);
extern void die_if_kernel(char *,struct pt_regs *,long);

/* ���������е�ҳ������ */
int nr_swap_pages = 0;

/* �ں˿������������׺Ϳ��п������*/
int nr_free_pages = 0;
unsigned long free_page_list = 0;
/*
 * The secondary free_page_list is used for malloc() etc things that
 * may need pages during interrupts etc. Normal get_free_page() operations
 * don't touch it, so it stays as a kind of "panic-list", that can be
 * accessed when all other mm tricks have failed.
 */
int nr_secondary_pages = 0;
/* �ں�Ϊԭ���ڴ����Ԥ���Ŀ���ҳ */
unsigned long secondary_page_list = 0;

#define copy_page(from,to) \
__asm__("cld ; rep ; movsl": :"S" (from),"D" (to),"c" (1024):"cx","di","si")

unsigned short * mem_map = NULL;

#define CODE_SPACE(addr,p) ((addr) < (p)->end_code)

/*
 * oom() prints a message (so that the user knows why the process died),
 * and gives the process an untrappable SIGSEGV.
 */
void oom(struct task_struct * task)
{
	printk("\nout of memory\n");
	task->sigaction[SIGKILL-1].sa_handler = NULL;
	task->blocked &= ~(1<<(SIGKILL-1));
	send_sig(SIGKILL,task,1);
}

/* �ͷ�һ��ҳ�����ڴ棬Ҳ����4MB
 * page_dir��ҳ����ַ�ĵ�ַ
 **/
static void free_one_table(unsigned long * page_dir)
{
	int j;
	unsigned long pg_table = *page_dir;
	unsigned long * page_table;

	if (!pg_table)
		return;
	*page_dir = 0;
	if (pg_table >= high_memory || !(pg_table & PAGE_PRESENT)) {
		printk("Bad page table: [%p]=%08lx\n",page_dir,pg_table);
		return;
	}
	/*����Ǳ���ҳ����������*/
	if (mem_map[MAP_NR(pg_table)] & MAP_PAGE_RESERVED)
		return;
	/*��ȡҳ���׵�ַ��ѭ������ҳ���е�ÿһ��*/
	page_table = (unsigned long *) (pg_table & PAGE_MASK);
	for (j = 0 ; j < PTRS_PER_PAGE ; j++,page_table++) {
		unsigned long pg = *page_table;
		
		if (!pg)
			continue;
		/*���ҳ�����ֵ*/
		*page_table = 0;
		/* ע��˴��е�ǳ���Ҫ
		 * �����ʵ��ڴ治��������ʱ���ڽ������У�����Ҫ������Ĵ���
		 * PAGE_PRESENT��ʾ�ڴ���������
		 */
		if (pg & PAGE_PRESENT)
			free_page(PAGE_MASK & pg);
		else
			swap_free(pg);
	}
	/*�ͷ�ҳ����ռ�õĿռ�*/
	free_page(PAGE_MASK & pg_table);
}

/*
 * This function clears all user-level page tables of a process - this
 * is needed by execve(), so that old pages aren't in the way. Note that
 * unlike 'free_page_tables()', this function still leaves a valid
 * page-table-tree in memory: it just removes the user pages. The two
 * functions are similar, but there is a fundamental difference.
 */

/* ֻ�������̵��ں�̬ҳ�����ͷ������û�̬ҳ��
 */

void clear_page_tables(struct task_struct * tsk)
{
	int i;
	unsigned long pg_dir;
	unsigned long * page_dir;

	if (!tsk)
		return;
	if (tsk == task[0])
		panic("task[0] (swapper) doesn't support exec()\n");
	/*ÿ�����̵�ҳĿ¼�������tast_struct->tss.cr3����*/
	pg_dir = tsk->tss.cr3;
	page_dir = (unsigned long *) pg_dir;
	if (!page_dir || page_dir == swapper_pg_dir) {
		printk("Trying to clear kernel page-directory: not good\n");
		return;
	}
	if (mem_map[MAP_NR(pg_dir)] > 1) {
		unsigned long * new_pg;

		/* ����һ���µ�ҳĿ¼����
		 * �����̵��ں�̬��ַ�ռ俽�����µ�ҳ�浱�У�
		 * ���û�̬0-3GB�ĵ�ַ�ռ��ͷţ�Ȼ���µ�ҳĿ¼��
		 * ��ֵ��cr3,���ͷ�ԭ����ҳĿ¼�����˺�����Ҫ����exev�����嵱��
		 */
		if (!(new_pg = (unsigned long*) get_free_page(GFP_KERNEL))) {
			oom(tsk);
			return;
		}
		for (i = 768 ; i < 1024 ; i++)
			new_pg[i] = page_dir[i];
		free_page(pg_dir);
		tsk->tss.cr3 = (unsigned long) new_pg;
		return;
	}
	for (i = 0 ; i < 768 ; i++,page_dir++)
		free_one_table(page_dir);
	invalidate();
	return;
}

/*
 * This function frees up all page tables of a process when it exits.
 */
void free_page_tables(struct task_struct * tsk)
{
	int i;
	unsigned long pg_dir;
	unsigned long * page_dir;

	if (!tsk)
		return;
	if (tsk == task[0]) {
		printk("task[0] (swapper) killed: unable to recover\n");
		panic("Trying to free up swapper memory space");
	}
	pg_dir = tsk->tss.cr3;
	if (!pg_dir || pg_dir == (unsigned long) swapper_pg_dir) {
		printk("Trying to free kernel page-directory: not good\n");
		return;
	}
	tsk->tss.cr3 = (unsigned long) swapper_pg_dir;
	if (tsk == current)
		__asm__ __volatile__("movl %0,%%cr3": :"a" (tsk->tss.cr3));

	/* ���ҳĿ¼�������ü�������1��������ô���⣬����˵���������̶�ָ����һ��ҳĿ¼��
	 * �ͷ�һ�����̵�ҳ�������ҳ���ļ�����1���������κδ������أ����ֻ��һ����������
	 * ����Ҫ��ҳĿ¼����ӳ����ڴ涼�ͷţ����Կ�����clone_page_tables��ʵ��
	 */
	if (mem_map[MAP_NR(pg_dir)] > 1) {
		free_page(pg_dir);
		return;
	}
	page_dir = (unsigned long *) pg_dir;
	for (i = 0 ; i < PTRS_PER_PAGE ; i++,page_dir++)
		free_one_table(page_dir);
	free_page(pg_dir);
	invalidate();
}

/*
 * clone_page_tables() clones the page table for a process - both
 * processes will have the exact same pages in memory. There are
 * probably races in the memory management with cloning, but we'll
 * see..
 */


int clone_page_tables(struct task_struct * tsk)
{
	unsigned long pg_dir;

	pg_dir = current->tss.cr3;
	mem_map[MAP_NR(pg_dir)]++;
	tsk->tss.cr3 = pg_dir;
	return 0;
}

/*
 * copy_page_tables() just copies the whole process memory range:
 * note the special handling of RESERVED (ie kernel) pages, which
 * means that they are always shared by all processes.
 */
int copy_page_tables(struct task_struct * tsk)
{
	int i;
	unsigned long old_pg_dir, *old_page_dir;
	unsigned long new_pg_dir, *new_page_dir;

	/* ����һ���µ�ҳĿ¼��
	 */
	if (!(new_pg_dir = get_free_page(GFP_KERNEL)))
		return -ENOMEM;
	/* ��ȡ��ǰ���̵�ҳĿ¼���������½��̵�ҳĿ¼��
	 */
	old_pg_dir = current->tss.cr3;
	tsk->tss.cr3 = new_pg_dir;
	old_page_dir = (unsigned long *) old_pg_dir;
	new_page_dir = (unsigned long *) new_pg_dir;
	/* ��ʼѭ�����ƽ��̵�ҳĿ¼������Ϊÿҳ4kb,ÿ��ҳ����4byte���ܹ�1024��ҳ��
	 */
	for (i = 0 ; i < PTRS_PER_PAGE ; i++,old_page_dir++,new_page_dir++) {
		int j;
		unsigned long old_pg_table, *old_page_table;
		unsigned long new_pg_table, *new_page_table;

		old_pg_table = *old_page_dir;
		if (!old_pg_table)
			continue;
		if (old_pg_table >= high_memory || !(old_pg_table & PAGE_PRESENT)) {
			printk("copy_page_tables: bad page table: "
				"probable memory corruption");
			*old_page_dir = 0;
			continue;
		}
		/* ������ں˿ռ���ֱ�Ӹ�ֵ����
		 */
		if (mem_map[MAP_NR(old_pg_table)] & MAP_PAGE_RESERVED) {
			*new_page_dir = old_pg_table;
			continue;
		}
		/* ����һ���µ�ҳ��
		 */
		if (!(new_pg_table = get_free_page(GFP_KERNEL))) {
			free_page_tables(tsk);
			return -ENOMEM;
		}
		old_page_table = (unsigned long *) (PAGE_MASK & old_pg_table);
		new_page_table = (unsigned long *) (PAGE_MASK & new_pg_table);
		for (j = 0 ; j < PTRS_PER_PAGE ; j++,old_page_table++,new_page_table++) {
			unsigned long pg;
			pg = *old_page_table;
			/* ����ǵ���0����˵����ҳ�ڴ滹û�б�ӳ�䣬Ҳ�Ͳ����ڿ�����
			 * ����һ��if��������ڴ���ӳ���ˣ����ǲ����ڴ浱�У���˵����
			 * ���������У���ô����Ҫ�ڽ��������п���
			 */
			if (!pg)
				continue;
			if (!(pg & PAGE_PRESENT)) {
				*new_page_table = swap_duplicate(pg);
				continue;
			}
			if ((pg & (PAGE_RW | PAGE_COW)) == (PAGE_RW | PAGE_COW))
				pg &= ~PAGE_RW;
			/* �˴���û��ȥ����һ���µ��ڴ棬ʵ���Ǹ����̺��ӽ����ڹ����ڴ棬���Ҫд��ȥ����
			 * Ҳ����дʱ����(copy-on-write),ͬʱ����������ҳmem_map�����ü�������__verify_write����
			 * ����free_page��С���ü���
			 */
			*new_page_table = pg;
			if (mem_map[MAP_NR(pg)] & MAP_PAGE_RESERVED)
				continue;
			*old_page_table = pg;
			mem_map[MAP_NR(pg)]++;
		}
		*new_page_dir = new_pg_table | PAGE_TABLE;
	}
	invalidate();
	return 0;
}

/*
 * a more complete version of free_page_tables which performs with page
 * granularity.
 */

/* ����fromΪ��ʼ��ַ����СΪsize�ĵ�ַ�ռ�ӽ��̵�
 * ����ҳ����ɾ��
 */
int unmap_page_range(unsigned long from, unsigned long size)
{
	unsigned long page, page_dir;
	unsigned long *page_table, *dir;
	unsigned long poff, pcnt, pc;

	if (from & ~PAGE_MASK) {
		printk("unmap_page_range called with wrong alignment\n");
		return -EINVAL;
	}
	size = (size + ~PAGE_MASK) >> PAGE_SHIFT;
	dir = PAGE_DIR_OFFSET(current->tss.cr3,from);
	poff = (from >> PAGE_SHIFT) & (PTRS_PER_PAGE-1);
	if ((pcnt = PTRS_PER_PAGE - poff) > size)
		pcnt = size;

	for ( ; size > 0; ++dir, size -= pcnt,
	     pcnt = (size > PTRS_PER_PAGE ? PTRS_PER_PAGE : size)) {
		if (!(page_dir = *dir))	{
			poff = 0;
			continue;
		}
		if (!(page_dir & PAGE_PRESENT)) {
			printk("unmap_page_range: bad page directory.");
			continue;
		}
		page_table = (unsigned long *)(PAGE_MASK & page_dir);
		if (poff) {
			page_table += poff;
			poff = 0;
		}
		for (pc = pcnt; pc--; page_table++) {
			if ((page = *page_table) != 0) {
				*page_table = 0;
				if (1 & page) {
					if (!(mem_map[MAP_NR(page)] & MAP_PAGE_RESERVED))
						if (current->rss > 0)
							--current->rss;
					free_page(PAGE_MASK & page);
				} else
					swap_free(page);
			}
		}
		if (pcnt == PTRS_PER_PAGE) {
			*dir = 0;
			free_page(PAGE_MASK & page_dir);
		}
	}
	invalidate();
	return 0;
}

/* ע�������unmap_page_range�������
 * �ڽ�����ҳ���еĵ�ַӳ���ȡ��֮��
 * ����Ӧ��������Ϊmaskֵ��mask��ֵ������
 * ���̲�����ǰ���Ե�ַ��Ӧ������ҳʱ��Ӧ��
 * ��ȡզ���Ĳ����������Ե�ַ����
 */
int zeromap_page_range(unsigned long from, unsigned long size, int mask)
{
	unsigned long *page_table, *dir;
	unsigned long poff, pcnt;
	unsigned long page;

	if (mask) {
		if ((mask & (PAGE_MASK|PAGE_PRESENT)) != PAGE_PRESENT) {
			printk("zeromap_page_range: mask = %08x\n",mask);
			return -EINVAL;
		}
		mask |= ZERO_PAGE;
	}
	if (from & ~PAGE_MASK) {
		printk("zeromap_page_range: from = %08lx\n",from);
		return -EINVAL;
	}
	/*��ȡ��ҳĿ¼���е�λ��*/
	dir = PAGE_DIR_OFFSET(current->tss.cr3,from);
	size = (size + ~PAGE_MASK) >> PAGE_SHIFT;
	/*��ȡ��ҳ���е�λ��*/
	poff = (from >> PAGE_SHIFT) & (PTRS_PER_PAGE-1);
	if ((pcnt = PTRS_PER_PAGE - poff) > size)
		pcnt = size;

	while (size > 0) {
		/*�����ǰ�����ڴ浱��*/
		if (!(PAGE_PRESENT & *dir)) {
				/* clear page needed here?  SRB. */
			if (!(page_table = (unsigned long*) get_free_page(GFP_KERNEL))) {
				invalidate();
				return -ENOMEM;
			}
			if (PAGE_PRESENT & *dir) {
				free_page((unsigned long) page_table);
				page_table = (unsigned long *)(PAGE_MASK & *dir++);
			} else
				*dir++ = ((unsigned long) page_table) | PAGE_TABLE;
		} else {
			/*��ȡҳĿ¼����ҳ����λ��*/
			page_table = (unsigned long *)(PAGE_MASK & *dir++);
		}
		page_table += poff;
		poff = 0;
		for (size -= pcnt; pcnt-- ;) {
			if ((page = *page_table) != 0) {
				*page_table = 0;
				if (page & PAGE_PRESENT) {
					if (!(mem_map[MAP_NR(page)] & MAP_PAGE_RESERVED))
						if (current->rss > 0)
							--current->rss;
					free_page(PAGE_MASK & page);
				} else
					swap_free(page);
			}
			*page_table++ = mask;
		}
		pcnt = (size > PTRS_PER_PAGE ? PTRS_PER_PAGE : size);
	}
	invalidate();
	return 0;
}

/*
 * maps a range of physical memory into the requested pages. the old
 * mappings are removed. any references to nonexistent pages results
 * in null mappings (currently treated as "copy-on-access")
 */
int remap_page_range(unsigned long from, unsigned long to, unsigned long size, int mask)
{
	unsigned long *page_table, *dir;
	unsigned long poff, pcnt;
	unsigned long page;

	if (mask) {
		if ((mask & (PAGE_MASK|PAGE_PRESENT)) != PAGE_PRESENT) {
			printk("remap_page_range: mask = %08x\n",mask);
			return -EINVAL;
		}
	}
	if ((from & ~PAGE_MASK) || (to & ~PAGE_MASK)) {
		printk("remap_page_range: from = %08lx, to=%08lx\n",from,to);
		return -EINVAL;
	}
	dir = PAGE_DIR_OFFSET(current->tss.cr3,from);
	size = (size + ~PAGE_MASK) >> PAGE_SHIFT;
	poff = (from >> PAGE_SHIFT) & (PTRS_PER_PAGE-1);
	if ((pcnt = PTRS_PER_PAGE - poff) > size)
		pcnt = size;

	while (size > 0) {
		if (!(PAGE_PRESENT & *dir)) {
			/* clearing page here, needed?  SRB. */
			if (!(page_table = (unsigned long*) get_free_page(GFP_KERNEL))) {
				invalidate();
				return -1;
			}
			*dir++ = ((unsigned long) page_table) | PAGE_TABLE;
		}
		else
			page_table = (unsigned long *)(PAGE_MASK & *dir++);
		if (poff) {
			page_table += poff;
			poff = 0;
		}

		for (size -= pcnt; pcnt-- ;) {
			if ((page = *page_table) != 0) {
				*page_table = 0;
				if (PAGE_PRESENT & page) {
					if (!(mem_map[MAP_NR(page)] & MAP_PAGE_RESERVED))
						if (current->rss > 0)
							--current->rss;
					free_page(PAGE_MASK & page);
				} else
					swap_free(page);
			}

			/*
			 * the first condition should return an invalid access
			 * when the page is referenced. current assumptions
			 * cause it to be treated as demand allocation in some
			 * cases.
			 */
			if (!mask)
				*page_table++ = 0;	/* not present */
			else if (to >= high_memory)
				*page_table++ = (to | mask);
			else if (!mem_map[MAP_NR(to)])
				*page_table++ = 0;	/* not present */
			else {
				*page_table++ = (to | mask);
				if (!(mem_map[MAP_NR(to)] & MAP_PAGE_RESERVED)) {
					++current->rss;
					mem_map[MAP_NR(to)]++;
				}
			}
			to += PAGE_SIZE;
		}
		pcnt = (size > PTRS_PER_PAGE ? PTRS_PER_PAGE : size);
	}
	invalidate();
	return 0;
}

/*
 * This function puts a page in memory at the wanted address.
 * It returns the physical address of the page gotten, 0 if
 * out of memory (either when trying to access page-table or
 * page.)
 */
unsigned long put_page(struct task_struct * tsk,unsigned long page,
	unsigned long address,int prot)
{
	unsigned long *page_table;

	if ((prot & (PAGE_MASK|PAGE_PRESENT)) != PAGE_PRESENT)
		printk("put_page: prot = %08x\n",prot);
	if (page >= high_memory) {
		printk("put_page: trying to put page %08lx at %08lx\n",page,address);
		return 0;
	}
	/*  �˴���address�����Ե�ַ,�˴�һ��Ҫ�������
	 *  Linux���ڴ������õ��Ƕ�ҳʽ�ģ��߼���ַ---(�ֶ�)---���Ե�ַ----(��ҳ)-----������ַ��
	 *  Ȼ���ҵ�����ҳĿ¼�������ڵ��Ҳ����ҳ���ĵ�ַ
	 */
	page_table = PAGE_DIR_OFFSET(tsk->tss.cr3,address);
	if ((*page_table) & PAGE_PRESENT)
		page_table = (unsigned long *) (PAGE_MASK & *page_table);
	else {
		printk("put_page: bad page directory entry\n");
		oom(tsk);
		*page_table = BAD_PAGETABLE | PAGE_TABLE;
		return 0;
	}
	/*ȡ����ַ��ҳ���е�λ��*/
	page_table += (address >> PAGE_SHIFT) & (PTRS_PER_PAGE-1);
	/* ���ҳ���Ѿ���ӳ���ˣ���֮ǰ��ӳ��ȡ��
	 * �˴���һ�����⣬ӳ��ȡ��ʱ��Ϊɶû���ͷű�ӳ��������ڴ�?
	 * ���߼�Сmem_map���ü���
	 */
	if (*page_table) {
		printk("put_page: page already exists\n");
		*page_table = 0;
		invalidate();
	}
	*page_table = page | prot;
/* no need for invalidate */
	return page;
}

/*
 * The previous function doesn't work very well if you also want to mark
 * the page dirty: exec.c wants this, as it has earlier changed the page,
 * and we want the dirty-status to be correct (for VM). Thus the same
 * routine, but this time we mark it dirty too.
 */

/* page��ʾ������ַ
  * address��ʾ���Ե�ַ 
  * ��address���Ե�ַ��Ӧԭ����ҳ���ӳ�䣬������ӳ�䵽pageָ�������ҳ 
  * ��������ҳΪ�� 
  */
unsigned long put_dirty_page(struct task_struct * tsk, unsigned long page, unsigned long address)
{
	unsigned long tmp, *page_table;

	if (page >= high_memory)
		printk("put_dirty_page: trying to put page %08lx at %08lx\n",page,address);
	if (mem_map[MAP_NR(page)] != 1)
		printk("mem_map disagrees with %08lx at %08lx\n",page,address);
        /* �������Ե�ַ�������Ե�ַ��Ӧ��ҳĿ¼�����������ַ */
	page_table = PAGE_DIR_OFFSET(tsk->tss.cr3,address);
	if (PAGE_PRESENT & *page_table)
		page_table = (unsigned long *) (PAGE_MASK & *page_table);
	else {
		/*�����ǰ��ҳ������ڴ���,�����·���һ�����е��ڴ�ҳ*/
		if (!(tmp = get_free_page(GFP_KERNEL)))
			return 0;
		/*�˴�Ϊɶ��Ҫ�ж�һ��?*/
		if (PAGE_PRESENT & *page_table) {
			free_page(tmp);
			page_table = (unsigned long *) (PAGE_MASK & *page_table);
		} else {
			*page_table = tmp | PAGE_TABLE;
			page_table = (unsigned long *) tmp;
		}
	}
        /* ȡ���Ե�ַ��Ӧ��ҳ���е�ҳ���� */
	page_table += (address >> PAGE_SHIFT) & (PTRS_PER_PAGE-1);
	if (*page_table) {
		printk("put_dirty_page: page already exists\n");
                /* ����ҳ�����ӳ��Ϊ0��Ҳ���Ǹ�ҳ���ָ���κ�ҳ */
		*page_table = 0;
		invalidate();
	}
	/*����ҳ���Ϊ���˽��*/
	*page_table = page | (PAGE_DIRTY | PAGE_PRIVATE);
/* no need for invalidate */
	return page;
}

/*
 * This routine handles present pages, when users try to write
 * to a shared page. It is done by copying the page to a new address
 * and decrementing the shared-page counter for the old page.
 *
 * Note that we do many checks twice (look at do_wp_page()), as
 * we have to be careful about race-conditions.
 *
 * Goto-purists beware: the only reason for goto's here is that it results
 * in better assembly code.. The "default" path will see no jumps at all.
 */



/* �ڴ�д��������
 */
static void __do_wp_page(unsigned long error_code, unsigned long address,
	struct task_struct * tsk, unsigned long user_esp)
{
	unsigned long *pde, pte, old_page, prot;
	unsigned long new_page;

	new_page = __get_free_page(GFP_KERNEL);
	pde = PAGE_DIR_OFFSET(tsk->tss.cr3,address);
	pte = *pde;
	/*д�����ڴ��Ӧӳ���ҳ�������ڴ�ʱ�������κδ���*/
	if (!(pte & PAGE_PRESENT))
		goto end_wp_page;
	if ((pte & PAGE_TABLE) != PAGE_TABLE || pte >= high_memory)
		goto bad_wp_pagetable;
	pte &= PAGE_MASK;
	pte += PAGE_PTR(address);
	/*��Ҫд�������ڴ治���ڴ浱��ʱ�������κδ���*/
	old_page = *(unsigned long *) pte;
	if (!(old_page & PAGE_PRESENT))
		goto end_wp_page;
	if (old_page >= high_memory)
		goto bad_wp_page;
	if (old_page & PAGE_RW)
		goto end_wp_page;
	tsk->min_flt++;
	prot = (old_page & ~PAGE_MASK) | PAGE_RW;
	old_page &= PAGE_MASK;
	if (mem_map[MAP_NR(old_page)] != 1) {
		if (new_page) {
			if (mem_map[MAP_NR(old_page)] & MAP_PAGE_RESERVED)
				++tsk->rss;
			/*������ҳ���ݿ���*/
			copy_page(old_page,new_page);
			*(unsigned long *) pte = new_page | prot;
			/* ��Ϊ֮ǰ������ַ��ӳ�䵽ͬһ��������ַ
			 * ���ڽ�ͬһ������ҳ������һ�ݣ���ԭ������ҳ�����ü��������1
			 * Ҳ��������ִ��free_page��ԭ��
			 */
			free_page(old_page);
			invalidate();
			return;
		}
		free_page(old_page);
		oom(tsk);
		*(unsigned long *) pte = BAD_PAGE | prot;
		invalidate();
		return;
	}
	*(unsigned long *) pte |= PAGE_RW;
	invalidate();
	if (new_page)
		free_page(new_page);
	return;
	/*����ҳ�����Ӧ��ҳ�ǻ���ҳ��ͬʱ����ɱ�����̵��ź�*/
bad_wp_page:
	printk("do_wp_page: bogus page at address %08lx (%08lx)\n",address,old_page);
	*(unsigned long *) pte = BAD_PAGE | PAGE_SHARED;
	send_sig(SIGKILL, tsk, 1);
	goto end_wp_page;
	/*����ҳĿ¼���Ӧ��ҳ��ʱ���ģ����ҷ���ɱ�����̵��ź�*/
bad_wp_pagetable:
	printk("do_wp_page: bogus page-table at address %08lx (%08lx)\n",address,pte);
	*pde = BAD_PAGETABLE | PAGE_TABLE;
	send_sig(SIGKILL, tsk, 1);
end_wp_page:
	if (new_page)
		free_page(new_page);
	return;
}

/*
 * check that a page table change is actually needed, and call
 * the low-level function only in that case..
 */
void do_wp_page(unsigned long error_code, unsigned long address,
	struct task_struct * tsk, unsigned long user_esp)
{
	unsigned long page;
	unsigned long * pg_table;

	pg_table = PAGE_DIR_OFFSET(tsk->tss.cr3,address);
	page = *pg_table;
	if (!page)
		return;
	if ((page & PAGE_PRESENT) && page < high_memory) {
		/* PAGE_PTR(address)�ҵ����Ե�ַ��ҳ���е�ƫ���� */
		pg_table = (unsigned long *) ((page & PAGE_MASK) + PAGE_PTR(address));
		page = *pg_table;
		/*�����ǰҳ�����ڴ���߸�ҳ�������ǹ�����д�ģ���ֱ�ӷ���*/
		if (!(page & PAGE_PRESENT))
			return;
		if (page & PAGE_RW)
			return;
		/*�������дʱ����*/
		if (!(page & PAGE_COW)) {
			if (user_esp && tsk == current) {
				current->tss.cr2 = address;
				current->tss.error_code = error_code;
				current->tss.trap_no = 14;   /*14����Ҳ������*/
				send_sig(SIGSEGV, tsk, 1);
				return;
			}
		}
		if (mem_map[MAP_NR(page)] == 1) {
			*pg_table |= PAGE_RW | PAGE_DIRTY;
			invalidate();
			return;
		}
		__do_wp_page(error_code, address, tsk, user_esp);
		return;
	}
	printk("bad page directory entry %08lx\n",page);
	*pg_table = 0;
}


/* �˲�����Ҫ���COW��������fork��ʱ�򣬲�û�н������̵ģ������ڴ�ҳ������
 * ֻ����д��ʱ�򣬲Ż�ȥ������������һ��������Ҫдʱ���ͽ����ڵ�����ҳ����һ��*/
int __verify_write(unsigned long start, unsigned long size)
{
	size--;
	size += start & ~PAGE_MASK;
	size >>= PAGE_SHIFT;
	start &= PAGE_MASK;
	/*һ�δ�������ҳ*/
	do {
		do_wp_page(1,start,current,0);
		start += PAGE_SIZE;
	} while (size--);
	return 0;
}

/* ����һ���յ������ڴ�ҳ��Ȼ��Ѹ�����ҳӳ�䵽address��ʾ�����Ե�ַ���ڵ�ҳ*/
static inline void get_empty_page(struct task_struct * tsk, unsigned long address)
{
	unsigned long tmp;

	if (!(tmp = get_free_page(GFP_KERNEL))) {
		oom(tsk);
		tmp = BAD_PAGE;
	}
	if (!put_page(tsk,tmp,address,PAGE_PRIVATE))
		free_page(tmp);
}

/*
 * try_to_share() checks the page at address "address" in the task "p",
 * to see if it exists, and if it is clean. If so, share it with the current
 * task.
 *
 * NOTE! This assumes we have checked that p != current, and that they
 * share the same executable or library.
 *
 * We may want to fix this to allow page sharing for PIC pages at different
 * addresses so that ELF will really perform properly. As long as the vast
 * majority of sharable libraries load at fixed addresses this is not a
 * big concern. Any sharing of pages between the buffer cache and the
 * code space reduces the need for this as well.  - ERY
 */

/* ������p�����Ե�ַ����������tsk�����Ե�ַ,
 * ͬʱtsk��address���Ե�ַӳ�䵽������ַnewpage��
 * ���ҳʱ��PAGE_RW�ã�������������ü�������
 */
static int try_to_share(unsigned long address, struct task_struct * tsk,
	struct task_struct * p, unsigned long error_code, unsigned long newpage)
{
	unsigned long from;
	unsigned long to;
	unsigned long from_page;
	unsigned long to_page;

	/*��ȡͬһ�����Ե�ַ��������ͬ���̵��е�ҳĿ¼���ַ*/
	from_page = (unsigned long)PAGE_DIR_OFFSET(p->tss.cr3,address);
	to_page = (unsigned long)PAGE_DIR_OFFSET(tsk->tss.cr3,address);
/* is there a page-directory at from ? */
	from = *(unsigned long *) from_page;
	if (!(from & PAGE_PRESENT))
		return 0;
	from &= PAGE_MASK;
	/*�ҵ�Ҫ����ҳ��ҳ���еĵ�ַ*/
	from_page = from + PAGE_PTR(address);
	from = *(unsigned long *) from_page;
/* is the page clean and present? */
	if ((from & (PAGE_PRESENT | PAGE_DIRTY)) != PAGE_PRESENT)
		return 0;
	if (from >= high_memory)
		return 0;
	/*���Ҫ������ҳ�����ڴ�������*/
	if (mem_map[MAP_NR(from)] & MAP_PAGE_RESERVED)
		return 0;
/* is the destination ok? */
	to = *(unsigned long *) to_page;
	if (!(to & PAGE_PRESENT))
		return 0;
	to &= PAGE_MASK;
	to_page = to + PAGE_PTR(address);
	/* ���to_page�Ѿ���Ӧ������ҳ�����޷������������ͷŸ��ڴ���ӳ�䣬
	 * ��Ϊ��ʱ�����Ͳ�֪��������ҳ���ŵ���ʲô����*/
	if (*(unsigned long *) to_page)
		return 0;
/* share them if read - do COW immediately otherwise */
	if (error_code & PAGE_RW) {
		if(!newpage)	/* did the page exist?  SRB. */
			return 0;
		copy_page((from & PAGE_MASK),newpage);
		to = newpage | PAGE_PRIVATE;
	} else {
		mem_map[MAP_NR(from)]++;
		from &= ~PAGE_RW;
		to = from;
		if(newpage)	/* only if it existed. SRB. */
			free_page(newpage);
	}
	*(unsigned long *) from_page = from;
	*(unsigned long *) to_page = to;
	invalidate();
	return 1;
}

/*
 * share_page() tries to find a process that could share a page with
 * the current one. Address is the address of the wanted page relative
 * to the current data space.
 *
 * We first check if it is at all feasible by checking executable->i_count.
 * It should be >1 if there are other tasks sharing this inode.
 */

/* area����Ҫ�����������ַ
 * tsk����Ҫ�������Ľ���
 * address�����Ե�ַ
 * newpage��������ַ
 */
int share_page(struct vm_area_struct * area, struct task_struct * tsk,
	struct inode * inode,
	unsigned long address, unsigned long error_code, unsigned long newpage)
{
	struct task_struct ** p;

	if (!inode || inode->i_count < 2 || !area->vm_ops)
		return 0;
	/*����ɨ������б�*/
	for (p = &LAST_TASK ; p > &FIRST_TASK ; --p) {
		if (!*p)
			continue;
		if (tsk == *p)
			continue;
		/*����ͽ��̿�ִ���ļ���Ӧ��inode�ڵ㲻һ��*/
		if (inode != (*p)->executable) {
			if(!area) continue;
			/* Now see if there is something in the VMM that
			   we can share pages with */
			if(area){
			  struct vm_area_struct * mpnt;
			  /*ɨ����̵������ַ�ռ�*/
			  for (mpnt = (*p)->mmap; mpnt; mpnt = mpnt->vm_next) {
			  	/* �����ַ�Ĳ���������i�ڵ�ţ��豸����ͬ
				 */
			    if (mpnt->vm_ops == area->vm_ops &&
			       mpnt->vm_inode->i_ino == area->vm_inode->i_ino&&
			       mpnt->vm_inode->i_dev == area->vm_inode->i_dev){
			      if (mpnt->vm_ops->share(mpnt, area, address))
				break;
			    };
			  };
			  if (!mpnt) continue;  /* Nope.  Nuthin here */
			};
		}
		if (try_to_share(address,tsk,*p,error_code,newpage))
			return 1;
	}
	return 0;
}

/*
 * fill in an empty page-table if none exists.
 */

/* �����Ե�ַaddress��Ӧ��ҳĿ¼�����һ��ҳ����
 * ����Ѿ������ˣ���ֱ�ӷ��أ����û�з��䣬������
 * һҳ�µ��ڴ���Ϊҳ����������ҳ�ڴ�ĸ���ҳĿ¼��
 * ע��˴������Ƿ�����һ��ҳ������Ҳ�����Ӧ��������û��
 */
static inline unsigned long get_empty_pgtable(struct task_struct * tsk,unsigned long address)
{
	unsigned long page;
	unsigned long *p;

	p = PAGE_DIR_OFFSET(tsk->tss.cr3,address);
	if (PAGE_PRESENT & *p)
		return *p;
	if (*p) {
		printk("get_empty_pgtable: bad page-directory entry \n");
		*p = 0;
	}
	page = get_free_page(GFP_KERNEL);
	/*�ҵ�ҳĿ¼���λ��*/
	p = PAGE_DIR_OFFSET(tsk->tss.cr3,address);
	/*�����ǰҳĿ¼���Ѿ����ڴ浱�У�Ҳ����˵�Ѿ�����ӳ����
	 *��ôֱ�ӷ����Ѿ�ӳ���ҳ���ĵ�ַ
	 */
	if (PAGE_PRESENT & *p) {
		free_page(page);
		return *p;
	}
	if (*p) {
		printk("get_empty_pgtable: bad page-directory entry \n");
		*p = 0;
	}
	/*�����û��ӳ�����������һҳ�ڴ�(��Ϊҳ��)��ַ����ҳĿ¼��*/
	if (page) {
		*p = page | PAGE_TABLE;
		return *p;
	}
	oom(current);
	*p = BAD_PAGETABLE | PAGE_TABLE;
	return 0;
}

/* Ҫ���ʵĵ�ַ�����ڴ浱��
 */
void do_no_page(unsigned long error_code, unsigned long address,
	struct task_struct *tsk, unsigned long user_esp)
{
	unsigned long tmp;
	unsigned long page;
	struct vm_area_struct * mpnt;

	/* ��ȡӳ�������ҳ�����ʧ���ˣ���������
	 * ����ɹ��ˣ�����������ҳ�����Ӧ������ҳ
	 */
	page = get_empty_pgtable(tsk,address);
	if (!page)
		return;
	page &= PAGE_MASK;
	page += PAGE_PTR(address);
	tmp = *(unsigned long *) page;
	/*�������ҳ�Ѿ����ڴ浱�У������κδ���*/
	if (tmp & PAGE_PRESENT)
		return;
	/*���ӽ������ں���ռ�õ�����ҳ������*/
	++tsk->rss;
	/*���ȱҳ���ڴ��ڽ��������򽫽������е��ڴ潻�����ڴ�*/
	if (tmp) {
		++tsk->maj_flt;
		/*ע��˴���page��ҳ����ĵ�ַ*/
		swap_in((unsigned long *) page);
		return;
	}
	/* vm_area_struct�еĵ�ַ�Ǻ�4KB�����
	 */
	address &= 0xfffff000;
	tmp = 0;
	/* �˴�ע�������ַ�����ǰ��յ�ַ��С˳�������еģ�Ŀǰ�汾�ں��������ģ�
	 * �ڸ߰汾�ں����Ƕ������ṹ(AVL)��
	 */
	for (mpnt = tsk->mmap; mpnt != NULL; mpnt = mpnt->vm_next) {
		if (address < mpnt->vm_start)
			break;
		if (address >= mpnt->vm_end) {
			tmp = mpnt->vm_end;
			continue;
		}
		if (!mpnt->vm_ops || !mpnt->vm_ops->nopage) {
			++tsk->min_flt;
			get_empty_page(tsk,address);
			return;
		}
		mpnt->vm_ops->nopage(error_code, mpnt, address);
		return;
	}
	/*���ǵ�ǰ���̾ͺ�˵�ˣ�ֱ�Ӹ�tsk����һҳ�����ڴ�*/
	if (tsk != current)
		goto ok_no_page;
	if (address >= tsk->end_data && address < tsk->brk)
		goto ok_no_page;
	if (mpnt && mpnt == tsk->stk_vma &&
	    address - tmp > mpnt->vm_start - address &&
	    tsk->rlim[RLIMIT_STACK].rlim_cur > mpnt->vm_end - address) {
		mpnt->vm_start = address;
		goto ok_no_page;
	}
	/*cr2��¼ȱҳ��ַ*/
	tsk->tss.cr2 = address;
	current->tss.error_code = error_code;
	current->tss.trap_no = 14;
	/*���Ͷδ����źţ�ɱ������*/
	send_sig(SIGSEGV,tsk,1);
	if (error_code & 4)	/* user level access? */
		return;
ok_no_page:
	++tsk->min_flt;
	get_empty_page(tsk,address);
}

/*
 * This routine handles page faults.  It determines the address,
 * and the problem, and then passes it off to one of the appropriate
 * routines.
 */
 
asmlinkage void do_page_fault(struct pt_regs *regs, unsigned long error_code)
{
	unsigned long address;
	unsigned long user_esp = 0;
	unsigned int bit;

	/* get the address */
	/*��cr2�ж�ȡ����ҳ����ĵ�ַ*/
	__asm__("movl %%cr2,%0":"=r" (address));
	/*������û��ռ�*/
	if (address < TASK_SIZE) {
		if (error_code & 4) {	/* user mode access? */
			if (regs->eflags & VM_MASK) {
				bit = (address - 0xA0000) >> PAGE_SHIFT;
				if (bit < 32)
					current->screen_bitmap |= 1 << bit;
			} else 
				user_esp = regs->esp;
		}
		if (error_code & 1)
			do_wp_page(error_code, address, current, user_esp);
		else
			do_no_page(error_code, address, current, user_esp);
		return;
	}
	address -= TASK_SIZE;
	if (wp_works_ok < 0 && address == 0 && (error_code & PAGE_PRESENT)) {
		wp_works_ok = 1;
		pg0[0] = PAGE_SHARED;
		printk("This processor honours the WP bit even when in supervisor mode. Good.\n");
		return;
	}
	if (address < PAGE_SIZE) {
		printk("Unable to handle kernel NULL pointer dereference");
		pg0[0] = PAGE_SHARED;
	} else
		printk("Unable to handle kernel paging request");
	printk(" at address %08lx\n",address);
	die_if_kernel("Oops", regs, error_code);
	do_exit(SIGKILL);
}

/*
 * BAD_PAGE is the page that is used for page faults when linux
 * is out-of-memory. Older versions of linux just did a
 * do_exit(), but using this instead means there is less risk
 * for a process dying in kernel mode, possibly leaving a inode
 * unused etc..
 *
 * BAD_PAGETABLE is the accompanying page-table: it is initialized
 * to point to BAD_PAGE entries.
 *
 * ZERO_PAGE is a special page that is used for zero-initialized
 * data and COW.
 */
unsigned long __bad_pagetable(void)
{
	extern char empty_bad_page_table[PAGE_SIZE];

	__asm__ __volatile__("cld ; rep ; stosl":
		:"a" (BAD_PAGE + PAGE_TABLE),
		 "D" ((long) empty_bad_page_table),
		 "c" (PTRS_PER_PAGE)
		:"di","cx");
	return (unsigned long) empty_bad_page_table;
}

unsigned long __bad_page(void)
{
	extern char empty_bad_page[PAGE_SIZE];

	__asm__ __volatile__("cld ; rep ; stosl":
		:"a" (0),
		 "D" ((long) empty_bad_page),
		 "c" (PTRS_PER_PAGE)
		:"di","cx");
	return (unsigned long) empty_bad_page;
}

unsigned long __zero_page(void)
{
	extern char empty_zero_page[PAGE_SIZE];

	__asm__ __volatile__("cld ; rep ; stosl":
		:"a" (0),
		 "D" ((long) empty_zero_page),
		 "c" (PTRS_PER_PAGE)
		:"di","cx");
	return (unsigned long) empty_zero_page;
}

void show_mem(void)
{
	int i,free = 0,total = 0,reserved = 0;
	int shared = 0;

	printk("Mem-info:\n");
	printk("Free pages:      %6dkB\n",nr_free_pages<<(PAGE_SHIFT-10));
	printk("Secondary pages: %6dkB\n",nr_secondary_pages<<(PAGE_SHIFT-10));
	printk("Free swap:       %6dkB\n",nr_swap_pages<<(PAGE_SHIFT-10));
	i = high_memory >> PAGE_SHIFT;
	while (i-- > 0) {
		total++;
		if (mem_map[i] & MAP_PAGE_RESERVED)
			reserved++;
		else if (!mem_map[i])
			free++;
		else
			shared += mem_map[i]-1;
	}
	printk("%d pages of RAM\n",total);
	printk("%d free pages\n",free);
	printk("%d reserved pages\n",reserved);
	printk("%d pages shared\n",shared);
	show_buffers();
}

/*
 * paging_init() sets up the page tables - note that the first 4MB are
 * already mapped by head.S.
 *
 * This routines also unmaps the page at virtual kernel address 0, so
 * that we can trap those pesky NULL-reference errors in the kernel.
 */

/* ���ں��п��õĵ�ַ�ռ�ӳ�䵽swapper_pg_dirҳĿ¼����Ӧ��
  * 0��ַ����3g��ַ����Ҳ����swapper_pg_dir�ĵ�0��͵�768��� 
  * ���ɸ�ҳ����ӳ����ͬ��Ҳ���ǽ�ϵͳ�����п����ڴ�ӳ�䵽swapper_pg_dir 
  * ���У�����Ϊ�ں˵�ȫ��ҳĿ¼������ӳ���ʱ���������ɵ�һ��ҳ���� 
  * start_mem------һ��ҳ��-------------- end_mem 
  * ��󷵻صľ���һ��ҳ��֮����׵�ַ 
  */
unsigned long paging_init(unsigned long start_mem, unsigned long end_mem)
{
	unsigned long * pg_dir;
	unsigned long * pg_table;
	unsigned long tmp;
	unsigned long address;

/*
 * Physical page 0 is special; it's not touched by Linux since BIOS
 * and SMM (for laptops with [34]86/SL chips) may need it.  It is read
 * and write protected to detect null pointer references in the
 * kernel.
 */
#if 0
	memset((void *) 0, 0, PAGE_SIZE);
#endif
	start_mem = PAGE_ALIGN(start_mem);
	/*�˴���addressӦ����������ַ��ӳ����ǽ��̵��ں˿ռ�,��
	 *�ں˳����ǿ���������������ڴ���κ�λ�õ�*/
	address = 0;
	pg_dir = swapper_pg_dir;
	while (address < end_mem) {
		/*�ڶ���ҳ�����У�ҳĿ¼���е�768��������3GB��λ�ã���ÿ�����̵ĵ�ַ
		  *�ռ䵱�У�0-3GB�ǽ��̵��û���ַ�ռ䣬3-4GB�ǽ��̵��ں˿ռ�*/
		tmp = *(pg_dir + 768);		/* at virtual addr 0xC0000000 */
		/* ���ҳ����Ϊ�գ����start_memλ�ÿ�ʼ����һ��ҳ������һ��ҳ��
		 * ����1024�����һҳ��С��Ȼ����һҳ���ĵ�ַ����ҳĿ¼��
		 */
		if (!tmp) {
			tmp = start_mem | PAGE_TABLE;
			*(pg_dir + 768) = tmp;
                        /* Ϊ��ӳ��swapper_pg_dirʱ�õ���ҳ����
                          *  ����õ���ҳ�������һ����ַ������
                          */
			start_mem += PAGE_SIZE;
		}
                /* ��ӳ��0x00000�ĵ�ַʱ�������֮ǰ��ӳ�� */
		*pg_dir = tmp;			/* also map it in at 0x0000000 for init */
		pg_dir++;
		/*��ȡҳ���ĵ�ַ*/
		pg_table = (unsigned long *) (tmp & PAGE_MASK);
		/*ѭ������һ��ҳ��*/
		for (tmp = 0 ; tmp < PTRS_PER_PAGE ; tmp++,pg_table++) {
			if (address < end_mem)
				*pg_table = address | PAGE_SHARED;
			else
				*pg_table = 0;
			address += PAGE_SIZE;
		}
	}
	invalidate();
	return start_mem;
}


/* �ڴ��ʼ��
 * start_low_mem�Ǵ�640KB����ʼ��
 */
void mem_init(unsigned long start_low_mem,
	      unsigned long start_mem, unsigned long end_mem)
{
	int codepages = 0;
	int reservedpages = 0;
	int datapages = 0;
	unsigned long tmp;
	unsigned short * p;
	extern int etext;

	cli();
	end_mem &= PAGE_MASK;
	high_memory = end_mem;
	start_mem +=  0x0000000f;
	start_mem &= ~0x0000000f;
	tmp = MAP_NR(end_mem);
	/* mem_map����start_mem����ʼ����ռ�ÿռ��Ǹ���������ַ����ҳ��
	 * ��ȷ���ģ�mem_map��������¼ÿҳ�����ڴ��ʹ�����
	 **/
	mem_map = (unsigned short *) start_mem;
	p = mem_map + tmp;
	start_mem = (unsigned long) p;
	/*�����е��ڴ�ҳ״̬����ΪMAP_PAGE_RESERVED��Ӧ�ú�COW�й�*/
	while (p > mem_map)
		*--p = MAP_PAGE_RESERVED;
	start_low_mem = PAGE_ALIGN(start_low_mem);
	start_mem = PAGE_ALIGN(start_mem);
	/* ���start_low_memС��1M��640KB-1MB�����Դ��ˡ�0xA0000=640KB */
	while (start_low_mem < 0xA0000) {
		mem_map[MAP_NR(start_low_mem)] = 0;
		start_low_mem += PAGE_SIZE;
	}
        /* �������ڴ�����ҳ�����������Ϊ0����ʾΪ���� */
	while (start_mem < end_mem) {
		mem_map[MAP_NR(start_mem)] = 0;
		start_mem += PAGE_SIZE;
	}
	/* �����һ��ѭ�������е�����ҳ������ΪMAP_PAGE_RESERVED��
	 *  ���ں��������ѭ���У�����Ҫ������ҳ������Ϊ0��Ҳ����������
	 */
#ifdef CONFIG_SOUND
	sound_mem_init();
#endif
	/* �˶δ���ǳ���Ҫ���漰��kamlloc
	 * free_page_list��Ϊ��������ҳ��������
	 * nr_free_pages������������ҳ��������
	 **/

	free_page_list = 0;
	nr_free_pages = 0;
	for (tmp = 0 ; tmp < end_mem ; tmp += PAGE_SIZE) {
		/*���mem_map�ı�Ǵ���0�����ʾ��ҳ���ѱ�ʹ��*/
		if (mem_map[MAP_NR(tmp)]) {
			/*���ʵ���Դ���*/
			if (tmp >= 0xA0000 && tmp < 0x100000)
				reservedpages++;
			/*�˴�etextӦ�����ں˴����*/
			else if (tmp < (unsigned long) &etext)
				codepages++;
			else
				datapages++;
			continue;
		}
		/* �������ҳ�ǿ��еģ��򽫸ÿ���ҳ�����ӵ�free_page_list����
		  * �������ף�ͬʱ����nr_free_pages
		  */
		*(unsigned long *) tmp = free_page_list;
		free_page_list = tmp;
		nr_free_pages++;
	}
	tmp = nr_free_pages << PAGE_SHIFT;
	printk("Memory: %luk/%luk available (%dk kernel code, %dk reserved, %dk data)\n",
		tmp >> 10,
		end_mem >> 10,
		codepages << (PAGE_SHIFT-10),
		reservedpages << (PAGE_SHIFT-10),
		datapages << (PAGE_SHIFT-10));
/* test if the WP bit is honoured in supervisor mode */
	wp_works_ok = -1;
	pg0[0] = PAGE_READONLY;
	invalidate();
	__asm__ __volatile__("movb 0,%%al ; movb %%al,0": : :"ax", "memory");
	pg0[0] = 0;
	invalidate();
	/*��ʼ������ҳ����*/
	if (wp_works_ok < 0)
		wp_works_ok = 0;
	return;
}

void si_meminfo(struct sysinfo *val)
{
	int i;

	i = high_memory >> PAGE_SHIFT;
	val->totalram = 0;
	val->freeram = 0;
	val->sharedram = 0;
	val->bufferram = buffermem;
	while (i-- > 0)  {
		if (mem_map[i] & MAP_PAGE_RESERVED)
			continue;
		val->totalram++;
		if (!mem_map[i]) {
			val->freeram++;
			continue;
		}
		val->sharedram += mem_map[i]-1;
	}
	val->totalram <<= PAGE_SHIFT;
	val->freeram <<= PAGE_SHIFT;
	val->sharedram <<= PAGE_SHIFT;
	return;
}


/* This handles a generic mmap of a disk file */
void file_mmap_nopage(int error_code, struct vm_area_struct * area, unsigned long address)
{
	struct inode * inode = area->vm_inode;
	unsigned int block;
	unsigned long page;
	int nr[8];
	int i, j;
	int prot = area->vm_page_prot;

	address &= PAGE_MASK;
	/*��Ϊ�ļ�ӳ�䶼�Ǵ������ַ�εĿ�ʼ��ַ����ʼӳ��ģ�
	 *��ӳ����ļ����Դӵڶ��ٸ��ֽڴ���ʼ���������ļ����ܵ�ƫ����Ϊ
	 *һ�¼���
	 */
	block = address - area->vm_start + area->vm_offset;

	/*��ȡƫ�ƿ��
	 */
	block >>= inode->i_sb->s_blocksize_bits;

	page = get_free_page(GFP_KERNEL);
	if (share_page(area, area->vm_task, inode, address, error_code, page)) {
		++area->vm_task->min_flt;
		return;
	}

	++area->vm_task->maj_flt;
	if (!page) {
		oom(current);
		put_page(area->vm_task, BAD_PAGE, address, PAGE_PRIVATE);
		return;
	}
	for (i=0, j=0; i< PAGE_SIZE ; j++, block++, i += inode->i_sb->s_blocksize)
		nr[j] = bmap(inode,block);
	if (error_code & PAGE_RW)
		prot |= PAGE_RW | PAGE_DIRTY;
	/* ע��nr��8��Ԫ�أ����ж��������豸���߼����
	 */
	page = bread_page(page, inode->i_dev, nr, inode->i_sb->s_blocksize, prot);

	if (!(prot & PAGE_RW)) {
		if (share_page(area, area->vm_task, inode, address, error_code, page))
			return;
	}
	if (put_page(area->vm_task,page,address,prot))
		return;
	free_page(page);
	oom(current);
}


/* �������ַ�ռ��Ӧ���ļ�ӳ��д���豸
 * �ڽ��ļ�mmap�������ַ�ռ�ʱ��һ����ַ�ζ�Ӧ
 * һ���ļ���������ĳһ�μ����ͷ�ʱ��ֱ�ӽ������
 * ��Ӧ���ļ���д�ؼ��ɡ�
 */

void file_mmap_free(struct vm_area_struct * area)
{
	if (area->vm_inode)
		iput(area->vm_inode);
#if 0
	if (area->vm_inode)
		printk("Free inode %x:%d (%d)\n",area->vm_inode->i_dev, 
				 area->vm_inode->i_ino, area->vm_inode->i_count);
#endif
}

/*
 * Compare the contents of the mmap entries, and decide if we are allowed to
 * share the pages
 */

/* �ж������ַ�ռ��Ƿ���Թ���
 * ֻ�ǽ����������жϣ���û�����κεĴ���
 */
int file_mmap_share(struct vm_area_struct * area1, 
		    struct vm_area_struct * area2, 
		    unsigned long address)
{
	if (area1->vm_inode != area2->vm_inode)
		return 0;
	if (area1->vm_start != area2->vm_start)
		return 0;
	if (area1->vm_end != area2->vm_end)
		return 0;
	if (area1->vm_offset != area2->vm_offset)
		return 0;
	if (area1->vm_page_prot != area2->vm_page_prot)
		return 0;
	return 1;
}


/* �����ڴ�������� */
struct vm_operations_struct file_mmap = {
	NULL,			/* open */
	file_mmap_free,		/* close */
	file_mmap_nopage,	/* nopage */
	NULL,			/* wppage */
	file_mmap_share,	/* share */
	NULL,			/* unmap */
};