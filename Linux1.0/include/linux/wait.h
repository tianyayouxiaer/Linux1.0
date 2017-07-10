#ifndef _LINUX_WAIT_H
#define _LINUX_WAIT_H

#define WNOHANG		0x00000001
#define WUNTRACED	0x00000002

#define __WCLONE	0x80000000

struct wait_queue {
	struct task_struct * task;
	struct wait_queue * next;
};

struct semaphore {
	int count;
	struct wait_queue * wait;
};

#define MUTEX ((struct semaphore) { 1, NULL })

struct select_table_entry {
	struct wait_queue wait;			/* ʵ�����ӵ��ȴ������еı��� */
	struct wait_queue ** wait_address;  /* ָ��ʵ�ʵȴ��������ײ��������wait_address��ɾ��������wait */
};

typedef struct select_table_struct {
	int nr;
	struct select_table_entry * entry;
} select_table;

/* һ��select_table���ֻ����ռ��һҳ�ڴ��С��select_table_entry */
#define __MAX_SELECT_TABLE_ENTRIES (4096 / sizeof (struct select_table_entry))

#endif