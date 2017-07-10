/*
 *  linux/mm/vmalloc.c
 *
 *  Copyright (C) 1993  Linus Torvalds
 */

#include <asm/system.h>
#include <linux/config.h>

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/malloc.h>
#include <asm/segment.h>

struct vm_struct {
	unsigned long flags;
	void * addr;		//ӳ��ĵ�ַ
	unsigned long size;
	struct vm_struct * next;
};

static struct vm_struct * vmlist = NULL;

/* Just any arbitrary offset to the start of the vmalloc VM area: the
 * current 8MB value just means that there will be a 8MB "hole" after the
 * physical memory until the kernel virtual memory starts.  That means that
 * any out-of-bounds memory accesses will hopefully be caught.
 * The vmalloc() routines leaves a hole of 4kB between each vmalloced
 * area for the same reason. ;)
 */
#define VMALLOC_OFFSET	(8*1024*1024)

/* ��Ҫ����ͳһ�޸Ľ��̵��ں˿ռ��ӳ��
 */

static inline void set_pgdir(unsigned long dindex, unsigned long value)
{
	struct task_struct * p;
	/*�����н��̵�ҳĿ¼����dindex������Ϊvalue*/
	p = &init_task;
	do {
		((unsigned long *) p->tss.cr3)[dindex] = value;
		p = p->next_task;
	} while (p != &init_task);
}

/* ��vmallocӳ����ڴ�����ӳ�䣬���ͷŵ�֮ǰӳ�������ҳ
 */
static int free_area_pages(unsigned long dindex, unsigned long index, unsigned long nr)
{
	unsigned long page, *pte;

	if (!(PAGE_PRESENT & (page = swapper_pg_dir[dindex])))
		return 0;
	page &= PAGE_MASK;
	pte = index + (unsigned long *) page;
	/*��ҳ���е�ҳ���ӳ��,���ͷŵ�����ҳ*/
	do {
		unsigned long pg = *pte;
		*pte = 0;
		if (pg & PAGE_PRESENT)
			free_page(pg);
		pte++;
	} while (--nr);
	/* �˴��жϸ�ҳ���Ƿ���ӳ�䣬Ҳ�������ҳ���Ƿ����ã�
	 * �����ҳ��û��ӳ���κ��ڴ棬����Ҫ����ҳ��ռ�õ�һҳ�ڴ�
	 * ���ͷŵ�����Ϊ��alloc_area_pages�����У������ӳ���ʱ��
	 * ���ҳ��Ϊ�գ���������һ���µ�ҳ�����ҳ��ֻʹ����һ�
	 * ���ͷŵ�ʱ��ҳ���н��е�һ��ӳ�������ˣ���ô��ҳ���
	 * û���κ������ˣ�Ϊ�˽�Լ�ڴ���ý���ҳ���ͷ�
	 */
	pte = (unsigned long *) page;
	for (nr = 0 ; nr < 1024 ; nr++, pte++)
		if (*pte)
			return 0;
	/* ��Ϊӳ������ں˿ռ䣬�����еĽ��̹����ں˿ռ�ģ�������Ҫ
	 * �����н��̵��ں˿ռ�ӳ�����
	 */
	set_pgdir(dindex,0);
	mem_map[MAP_NR(page)] = 1;
	free_page(page);
	invalidate();
	return 0;
}


/*  ע��linux���ڴ���䣬���ö���ҳ��dindex��4M����������ҳĿ¼���е�����
  *  index��ʹҳ���е�������nr�Ǵ�index��������ʼӳ�����ҳ���ڴ棬Ҳ��������nr
  *  ҳ�����ڴ�,��ӳ�䵽dindex,index...index+nr��λ���ϣ��˴�ע��swapper_pg_dir
  *  ���ں�ҳĿ¼����¼�����ڴ��ʹ�����
  */

static int alloc_area_pages(unsigned long dindex, unsigned long index, unsigned long nr)
{
	unsigned long page, *pte;

	page = swapper_pg_dir[dindex];
	/*�����ҳ��Ϊ�գ�������һ������ҳ��Ϊҳ��*/
	if (!page) {
		page = get_free_page(GFP_KERNEL);
		if (!page)
			return -ENOMEM;
		if (swapper_pg_dir[dindex]) {
			free_page(page);
			page = swapper_pg_dir[dindex];
		} else {
			mem_map[MAP_NR(page)] = MAP_PAGE_RESERVED;
			set_pgdir(dindex, page | PAGE_SHARED);
		}
	}
	page &= PAGE_MASK;
	pte = index + (unsigned long *) page;
	*pte = PAGE_SHARED;		/* remove a race with vfree() */
	do {
		unsigned long pg = get_free_page(GFP_KERNEL);

		if (!pg)
			return -ENOMEM;
		*pte = pg | PAGE_SHARED;
		pte++;
	} while (--nr);
	invalidate();
	return 0;
}

static int do_area(void * addr, unsigned long size,
	int (*area_fn)(unsigned long,unsigned long,unsigned long))
{
	unsigned long nr, dindex, index;
	//��ȡ�ڴ��ҳ��
	nr = size >> PAGE_SHIFT;
	//ӳ������ں˿ռ�     Ϊʲô+TASK_SIZE ??????
	dindex = (TASK_SIZE + (unsigned long) addr) >> 22;
	//index�ķ�Χ��0-1024��Χ
	index = (((unsigned long) addr) >> PAGE_SHIFT) & (PTRS_PER_PAGE-1);
	while (nr > 0) {
		/* �жϵ�ǰҳ���ж�������Ա�ӳ��   */
		unsigned long i = PTRS_PER_PAGE - index;

		if (i > nr)
			i = nr;
		nr -= i;
		if (area_fn(dindex, index, i))
			return -1;
		/*��һ��ӳ��ҳ���������0��ʼ*/
		index = 0;
		/*����ҳĿ¼���е�����*/
		dindex++;
	}
	return 0;
}

void vfree(void * addr)
{
	struct vm_struct **p, *tmp;

	if (!addr)
		return;
	//���ͷŵĵ�ַ��ҳ����
	if ((PAGE_SIZE-1) & (unsigned long) addr) {
		printk("Trying to vfree() bad address (%p)\n", addr);
		return;
	}
	//��vmalloc�����ҳ����ɨ��,�ҵ�Ҫ�ͷŵĽڵ㣬���ͷ��ҵ��Ľڵ�
	for (p = &vmlist ; (tmp = *p) ; p = &tmp->next) {
		if (tmp->addr == addr) {
			*p = tmp->next;
			do_area(tmp->addr, tmp->size, free_area_pages);
			kfree(tmp);
			return;
		}
	}
	printk("Trying to vfree() nonexistent vm area (%p)\n", addr);
}

/* ��vamlloc���η���ĵ�ַ�ǲ����������ģ���Ϊ�м�����һ��PAGE_SIZE��С��hole
 * ����vamlloc����ĵ�ַ��������ҳ��(����ڴ�)��ʵ�������õ������ڴ��ǲ������ġ�kmalloc
 * ��������С�ڴ� ,do_area��ʱ��+TASK_SIZE���ں˿ռ� ????????
 */
void * vmalloc(unsigned long size)
{
	void * addr;
	struct vm_struct **p, *tmp, *area;
	//size��ַ���룬����ҳ����Ϊһ��ҳ������Ҳ����һҳ��������
	size = PAGE_ALIGN(size);
	if (!size || size > high_memory)
		return NULL;
	//���ں��з���һ���ṹ���area
	area = (struct vm_struct *) kmalloc(sizeof(*area), GFP_KERNEL);
	if (!area)
		return NULL;

	/* addrָ�����8MB��ǰһ��λ�ã�vmalloc����ĵ�ַ�Ǵ�8MB��ʼ��*/
	addr = (void *) ((high_memory + VMALLOC_OFFSET) & ~(VMALLOC_OFFSET-1));
	/*�˴�ΪɶҪ��һ��PAGE_SIZE   ????   */
	area->size = size + PAGE_SIZE;
	area->next = NULL;
	
	/* ע��˴�����ȽϾ��ɣ���һ�����е�ʱ��forѭ��������ִ�У�ֱ����һ�μ�
	 * *p = areaִ�к󣬼�vmlist=area��tmp->addr�е�ַ��˳���ŵģ����������������
	 * 8MB��ʼ     |----(�ѱ�ʹ��)---|------(���д�СΪsize+PAGE_SIZE)-----|----(��ʹ��)----|....�ȵ�
	 */
	for (p = &vmlist; (tmp = *p) ; p = &tmp->next) {
		/*�ҵ�һ�����Դ��size��С�����䣬������area���뵽vmlist����*/
		if (size + (unsigned long) addr < (unsigned long) tmp->addr)
			break;
		//addr��ַ�������һ���ƶ�
		addr = (void *) (tmp->size + (unsigned long) tmp->addr);
	}
	//�������area���ӵ�p��ָ�ڵ���棬
	area->addr = addr;
	area->next = *p;
	*p = area;
	if (do_area(addr, size, alloc_area_pages)) {
		vfree(addr);
		return NULL;
	}
	return addr;
}


/* ����ʵ�ʶ�ȡ����
 * �ӵ�ַaddr������ȡcount�ֽڴ�ŵ�buf����
 */
int vread(char *buf, char *addr, int count)
{
	struct vm_struct **p, *tmp;
	char *vaddr, *buf_start = buf;
	int n;
	//ɨ��vmlist
	for (p = &vmlist; (tmp = *p) ; p = &tmp->next) {
		vaddr = (char *) tmp->addr;
		/*�˴�ѭ��һ����������ѭ������ִ�У���Ϊ�����е�ַ�Ǵ�С�������е�*/
		while (addr < vaddr) {
			if (count == 0)
				goto finished;
			put_fs_byte('\0', buf++), addr++, count--;
		}
		/*��vmalloc�е�+PAGE_SIZE��Ӧ*/
		n = tmp->size - PAGE_SIZE;
		if (addr > vaddr)
			n -= addr - vaddr;
		/* �������ʾ��ͼ�����û���ҵ���ȷ�Ľڵ㣬���ִ�������if,��ʱ
		 * n�ͻ�С��0,�Ͳ���ִ�������while��ֻ���ҵ���ȷ�Ľڵ�Ż�ִ��
		 * whileִ�к�Ὣʣ�µ������ֽڶ��꣬ǰ����count������0���������֮��
		 * count��Ȼ����0����������е����ݶ����'\0',Ҳ����ִ�������while
		 */
		while (--n >= 0) {
			if (count == 0)
				goto finished;
			put_fs_byte(*addr++, buf++), count--;
		}
	}
finished:
	return buf - buf_start;
}
