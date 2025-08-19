#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <time.h>  /* for turnaround time */

#ifdef __APPLE__
#define _AC(X,Y)        X
#else
#include <linux/const.h>
#endif

#include <libtlp.h>

#define PRINT_OUT  /* uncomment to disable print statements */
// #define POLL   /* uncomment to poll on shared memory region */
#define CHECK_ROOTKIT  /* comment out to revert to default behavior - check process-list once */
#define ROOTKIT_PID_BASE 80000  /* base of fake PID */
#define NUMBER_MODS 1000        /* number of modifications; fake PID ranges from [80,000, 81,000) */
#define GET_CLOCK_GRAN 1000     /* check clock every 1000 introspection tasks */
#define MAX_RUNTIME_SECS 10.0   /* number of seconds to run this test for */

/* from arch_x86/include/asm/page_64_types.h */
#define KERNEL_IMAGE_SIZE	(512 * 1024 * 1024)
//#define __PAGE_OFFSET_BASE      _AC(0xffff880000000000, UL)
#define __PAGE_OFFSET_BASE      _AC(0xffff888000000000, UL)
#define __PAGE_OFFSET           __PAGE_OFFSET_BASE
#define __START_KERNEL_map      _AC(0xffffffff80000000, UL)



/* from arch/x86/include/asm/page_types.h */
#define PAGE_OFFSET	((unsigned long)__PAGE_OFFSET)

// #define phys_base	0x1000000	/* x86 */
// #define phys_base	0x4A00000	/* x86 */
#define phys_base	0x0	/* x86 */

/* from arch/x86/mm/physaddr.c */
unsigned long __phys_addr(unsigned long x)
{
	unsigned long y = x - __START_KERNEL_map;

	/* use the carry flag to determine if x was < __START_KERNEL_map */
	if (x > y) {
		x = y + phys_base;
		//return 0;

		//if (y >= KERNEL_IMAGE_SIZE)
		//return 0;
	} else {
		x = y + (__START_KERNEL_map - PAGE_OFFSET);

		/* carry flag will be set if starting x was >= PAGE_OFFSET */
		//VIRTUAL_BUG_ON((x > y) || !phys_addr_valid(x));
	}

	return x;
}



/* from include/linux/sched.h*/
/* Used in tsk->state: */
#define TASK_RUNNING                    0x0000
#define TASK_INTERRUPTIBLE              0x0001
#define TASK_UNINTERRUPTIBLE            0x0002
#define __TASK_STOPPED                  0x0004
#define __TASK_TRACED                   0x0008
/* Used in tsk->exit_state: */
#define EXIT_DEAD                       0x0010
#define EXIT_ZOMBIE                     0x0020
#define EXIT_TRACE                      (EXIT_ZOMBIE | EXIT_DEAD)
/* Used in tsk->state again: */
#define TASK_PARKED                     0x0040
#define TASK_DEAD                       0x0080
#define TASK_WAKEKILL                   0x0100
#define TASK_WAKING                     0x0200
#define TASK_NOLOAD                     0x0400
#define TASK_NEW                        0x0800
#define TASK_STATE_MAX                  0x1000

char state_to_char(long state)
{
	switch(state & 0x00FF) {
	case TASK_RUNNING:
		return 'R';
	case TASK_INTERRUPTIBLE:
		return 'S';
	case TASK_UNINTERRUPTIBLE:
		return 'D';
	case __TASK_STOPPED:
	case __TASK_TRACED:
		return 'T';
	case TASK_DEAD:
		return 'X';
	default:
		return 'u';
	}
}


struct list_head {
	struct list_head *next, *prev;
};


/* task struct offset */
// #define OFFSET_HEAD_STATE	16
// #define OFFSET_HEAD_PID		2216
// #define OFFSET_HEAD_CHILDREN	2240
// #define OFFSET_HEAD_SIBLING	2256
// #define OFFSET_HEAD_COMM	2632
// #define OFFSET_HEAD_REAL_PARENT	2224
// new offsets for Linux 6.11.0-33-generic
#define OFFSET_HEAD_STATE	24
#define OFFSET_HEAD_PID		2496
#define OFFSET_HEAD_CHILDREN	2528
#define OFFSET_HEAD_SIBLING	2544
#define OFFSET_HEAD_COMM	3040
#define OFFSET_HEAD_REAL_PARENT	2512
#define OFFSET_HEAD_TASKS 2288

#define TASK_COMM_LEN		16

struct task_struct {
	uintptr_t	vhead, phead;
	uintptr_t	vstate, pstate;
	uintptr_t	vpid, ppid;
	uintptr_t	vchildren, pchildren;
	uintptr_t	vsibling, psibling;
	uintptr_t	vcomm, pcomm;
	uintptr_t	vreal_parent, preal_parent;

	struct list_head children;
	struct list_head sibling;

	uintptr_t	children_next;
	uintptr_t	children_prev;
	uintptr_t	sibling_next;
	uintptr_t	sibling_prev;

	uintptr_t	real_parent;
};

#define print_task_value(t, name) \
	printf(#name "  %#lx %#lx\n", t->v##name, t->p##name)

void dump_task_struct(struct task_struct *t)
{
	print_task_value(t, head);
	print_task_value(t, state);
	print_task_value(t, pid);
	print_task_value(t, children);
	print_task_value(t, sibling);
	print_task_value(t, comm);
	print_task_value(t, real_parent);

	printf("children_next %#lx %#lx\n",
	       t->children_next, __phys_addr(t->children_next));
	printf("children_prev %#lx %#lx\n",
	       t->children_prev, __phys_addr(t->children_prev));
	printf("sibling_next %#lx %#lx\n",
	       t->sibling_next, __phys_addr(t->sibling_next));
	printf("sibling_prev %#lx %#lx\n",
	       t->sibling_prev, __phys_addr(t->sibling_prev));
}

#define check_task_value(t, name)					\
	do {								\
		if(t->v##name == 0) {					\
			fprintf(stderr,					\
				"failed to get address of v" #name	\
				" %#lx\n", t->v##name);			\
			return -1;					\
		}							\
		if(t->p##name == 0) {					\
			fprintf(stderr,					\
				"failed to get physical address for p"	\
				#name					\
				" from %#lx\n", t->v##name);		\
			return -1;					\
		}							\
	} while(0)							\


int fill_task_struct(struct nettlp *nt, uintptr_t vhead,
		     struct task_struct *t)
{
	int ret;

	t->vhead = vhead;
	t->phead = __phys_addr(t->vhead);
	t->vstate = vhead + OFFSET_HEAD_STATE;
	t->pstate = __phys_addr(t->vstate);
	t->vpid = vhead + OFFSET_HEAD_PID;
	t->ppid = __phys_addr(t->vpid);
	t->vchildren = vhead + OFFSET_HEAD_CHILDREN;
	t->pchildren = __phys_addr(t->vchildren);
	t->vsibling = vhead + OFFSET_HEAD_SIBLING;
	t->psibling = __phys_addr(t->vsibling);
	t->vcomm = vhead + OFFSET_HEAD_COMM;
	t->pcomm = __phys_addr(t->vcomm);
	t->vreal_parent = vhead + OFFSET_HEAD_REAL_PARENT;
	t->preal_parent = __phys_addr(t->vreal_parent);

	ret = dma_read(nt, t->pchildren, &t->children, sizeof(t->children));
	if (ret < sizeof(t->children))
		return -1;

	ret = dma_read(nt, t->psibling, &t->sibling, sizeof(t->children));
	if (ret < sizeof(t->sibling))
		return -1;

	ret = dma_read(nt, t->preal_parent, &t->real_parent,
		       sizeof(t->real_parent));
	if (ret < sizeof(t->real_parent))
		return -1;

	t->children_next = (uintptr_t)t->children.next;
	t->children_prev = (uintptr_t)t->children.prev;
	t->sibling_next = (uintptr_t)t->sibling.next;
	t->sibling_prev = (uintptr_t)t->sibling.prev;

	check_task_value(t, head);
	check_task_value(t, pid);
	check_task_value(t, children);
	check_task_value(t, sibling);
	check_task_value(t, comm);

	return 0;
}


void print_task_struct_column(void)
{
#ifdef PRINT_OUT
	printf("PhyAddr             PID STAT COMMAND\n");
#endif
}
	

int print_task_struct(struct nettlp *nt, struct task_struct t)
{
	int ret, pid;
	long state;
	char comm[TASK_COMM_LEN];

	ret = dma_read(nt, t.ppid, &pid, sizeof(pid));
	if (ret < sizeof(pid)) {
		fprintf(stderr, "failed to read pid from %#lx\n", t.ppid);
		return -1;
	}

	ret = dma_read(nt, t.pcomm, &comm, sizeof(comm));
	if (ret < sizeof(comm)) {
		fprintf(stderr, "failed to read comm from %#lx\n", t.pcomm);
		return -1;
	}

	ret = dma_read(nt, t.pstate, &state, sizeof(state));
	if (ret < sizeof(state)) {
		fprintf(stderr, "failed to read state from %#lx\n", t.pstate);
		return -1;
	}

	comm[TASK_COMM_LEN - 1] = '\0';	/* preventing overflow */

#ifdef PRINT_OUT
	printf("%#016lx %6d %c    %s\n",
	       t.phead, pid, state_to_char(state), comm);
#endif
	return 0;
}



int task(struct nettlp *nt, uintptr_t vhead, uintptr_t parent)
{
	/*
	 * vhead is kernel virtual address of task_struct.
	 * parent is the vaddr of the parent's task_struct.
	 */

	int ret;
	struct task_struct t;
	
	ret = fill_task_struct(nt, vhead, &t);
	if (ret < 0) {
		fprintf(stderr, "failed to fill task_struct from %#lx\n",
			vhead);
		return ret;
	}
	
	/* print myself */
	ret = print_task_struct(nt, t);
	if (ret < 0)
		return ret;

	if (t.children_next != t.vchildren) {
		/* this task_struct has children. walk them */
		ret = task(nt, t.children_next - OFFSET_HEAD_SIBLING,
			   t.vhead);
		if (ret < 0)
			return ret;
	}
	
	if (t.sibling_next - OFFSET_HEAD_SIBLING == parent ||
	    t.sibling_next - OFFSET_HEAD_CHILDREN == parent) {
		/* walk done of the siblings spawned from the parent */
		return 0;
	}

	/* goto the next sibling */
	return task(nt, t.sibling_next - OFFSET_HEAD_SIBLING, parent);
}

uintptr_t find_init_task_from_systemmap(char *map)
{
	FILE *fp;
	char buf[4096];
	uintptr_t addr = 0;

	fp = fopen(map, "r");
	if (!fp) {
		perror("fopen");
		return 0;
	}

	while(fgets(buf, sizeof(buf), fp) != NULL) {
		if (strstr(buf, "D init_task")) {
			char *p;
			p = strchr(buf, ' ');
			*p = '\0';
			addr = strtoul(buf, NULL, 16);
		}
	}

	fclose(fp);

	return addr;
}

static bool *found_pid_arr = NULL;

void check_task(struct nettlp *nt, uintptr_t paddr_task) {
	int ret;
	int pid;
	ret = dma_read(nt, paddr_task + OFFSET_HEAD_PID, &pid, sizeof(pid));
	if (ret < 0) {
		return;
	}
	if (pid >= ROOTKIT_PID_BASE) {
		found_pid_arr[pid - ROOTKIT_PID_BASE] = true;
	}
}

uintptr_t get_paddr_next_task(struct nettlp *nt, uintptr_t paddr_curr_task) {
	int ret;
	uintptr_t vaddr_next_task;
	ret = dma_read(nt, paddr_curr_task + OFFSET_HEAD_TASKS, &vaddr_next_task, sizeof(vaddr_next_task));
	if (ret < 0) {
		return 0;
	}
	if (vaddr_next_task == 0) {
		return 0;
	}
	return __phys_addr(vaddr_next_task - OFFSET_HEAD_TASKS);
}

void get_detection_results(void) {
	unsigned int num_detected = 0;
	for (size_t i = 0; i < NUMBER_MODS; i++) {
		if (found_pid_arr[i]) num_detected++;
	}
	printf("number of fake PIDs detected: %u/%d\n", num_detected, NUMBER_MODS);
	printf("capture rate: %f\n", (float)num_detected / NUMBER_MODS);
	return;
}

void usage(void)
{
	printf("usage\n"
	       "    -r remote addr\n"
	       "    -l local addr\n"
	       "    -R remote host addr to get requester id\n"
	       "    -b bus number, XX:XX (exclusive with -R)\n"
	       "    -t tag\n"
	       "    -s path to System.map\n"
	);
}


#ifndef CHECK_ROOTKIT
int main(int argc, char **argv)
{
	int ret, ch;
	struct nettlp nt;
	struct in_addr remote_host;
	uintptr_t addr;
	uint16_t busn, devn;
	struct task_struct t;
	char *map;

	memset(&nt, 0, sizeof(nt));
	addr = 0;
	busn = 0;
	devn = 0;
	map = NULL;

	while ((ch = getopt(argc, argv, "r:l:R:b:t:s:")) != -1) {
		switch (ch) {
		case 'r':
			ret = inet_pton(AF_INET, optarg, &nt.remote_addr);
			if (ret < 1) {
				perror("inet_pton");
				return -1;
			}
			break;

		case 'l':
			ret = inet_pton(AF_INET, optarg, &nt.local_addr);
			if (ret < 1) {
				perror("inet_pton");
				return -1;
			}
			break;

		case 'R':
			ret = inet_pton(AF_INET, optarg, &remote_host);
			if (ret < 1) {
				perror("inet_pton");
				return -1;
			}

			nt.requester = nettlp_msg_get_dev_id(remote_host);
			break;

		case 'b':
			ret = sscanf(optarg, "%hx:%hx", &busn, &devn);
			nt.requester = (busn << 8 | devn);
			break;

		case 't':
			nt.tag = atoi(optarg);
			break;

		case 's':
			map = optarg;
			break;

		default :
			usage();
			return -1;
		}
	}

	ret = nettlp_init(&nt);
	if (ret < 0) {
		perror("nettlp_init");
		return ret;
	}

	/* Get start time */
	struct timespec start, end;
	clock_gettime(CLOCK_MONOTONIC, &start);

	addr = find_init_task_from_systemmap(map);
	if (addr == 0) {
		fprintf(stderr, "init_task not found on System.map %s\n", map);
		return -1;
	}

	fill_task_struct(&nt, addr, &t);
	print_task_struct_column();
	task(&nt, t.vhead, t.vhead);

	/* Get end time and calculate turnaround time */
	clock_gettime(CLOCK_MONOTONIC, &end);
	double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
	printf("Time elapsed: %f seconds\n", elapsed);

	return 0;
}
#else
int main(int argc, char **argv)
{
	int ret, ch;
	struct nettlp nt;
	struct in_addr remote_host;
	uint16_t busn, devn;
	char *map;

	memset(&nt, 0, sizeof(nt));
	busn = 0;
	devn = 0;
	map = NULL;

	while ((ch = getopt(argc, argv, "r:l:R:b:t:s:")) != -1) {
		switch (ch) {
		case 'r':
			ret = inet_pton(AF_INET, optarg, &nt.remote_addr);
			if (ret < 1) {
				perror("inet_pton");
				return -1;
			}
			break;

		case 'l':
			ret = inet_pton(AF_INET, optarg, &nt.local_addr);
			if (ret < 1) {
				perror("inet_pton");
				return -1;
			}
			break;

		case 'R':
			ret = inet_pton(AF_INET, optarg, &remote_host);
			if (ret < 1) {
				perror("inet_pton");
				return -1;
			}

			nt.requester = nettlp_msg_get_dev_id(remote_host);
			break;

		case 'b':
			ret = sscanf(optarg, "%hx:%hx", &busn, &devn);
			nt.requester = (busn << 8 | devn);
			break;

		case 't':
			nt.tag = atoi(optarg);
			break;

		case 's':
			map = optarg;
			break;

		default :
			usage();
			return -1;
		}
	}

	ret = nettlp_init(&nt);
	if (ret < 0) {
		perror("nettlp_init");
		return ret;
	}

	uintptr_t init_task_vaddr = find_init_task_from_systemmap(map);
	if (init_task_vaddr == 0) {
		return -1;
	}
	uintptr_t init_task_paddr = __phys_addr(init_task_vaddr);

	/* Allocate array to track which fake PIDs have been detected */
	found_pid_arr = calloc(NUMBER_MODS, sizeof(bool));
	if (!found_pid_arr) {
		return -1;
	}

	unsigned int cycles = 0;
	double elapsed;

	/* dbg_start and dbg_end are used to measure time required for one introspection. 
	*  remove these when doing actual experiments. */
	struct timespec start, curr, dbg_start, dbg_end;
	clock_gettime(CLOCK_MONOTONIC, &start);

	double sum = 0.0;

#ifndef POLL
	while (1) {
		clock_gettime(CLOCK_MONOTONIC, &dbg_start);
		uintptr_t curr_task_paddr = init_task_paddr;
		do {
			check_task(&nt, curr_task_paddr);
			curr_task_paddr = get_paddr_next_task(&nt, curr_task_paddr);
		} while (curr_task_paddr != init_task_paddr);
		clock_gettime(CLOCK_MONOTONIC, &dbg_end);
		elapsed = (dbg_end.tv_sec - dbg_start.tv_sec) + (dbg_end.tv_nsec - dbg_start.tv_nsec) / 1e9;
		printf("introspection time: %f [s]\n", elapsed);
		sum += elapsed;

		cycles++;
		if (cycles % GET_CLOCK_GRAN == 0) {
			clock_gettime(CLOCK_MONOTONIC, &curr);
			elapsed = (curr.tv_sec - start.tv_sec) + (curr.tv_nsec - start.tv_nsec) / 1e9;
			if (elapsed >= MAX_RUNTIME_SECS) break;
		}
	}
#else
	const char *NETTLP_RESUME_CPU = "2odified_by_dma_write\0";
	const unsigned long long shared_mem_phys_addr = 0x317fb000ULL;
	while (1) {
		char c;
		ret = dma_read(&nt, shared_mem_phys_addr, &c, sizeof(c));
		if (c == '3') {
			printf("HERE\n");
			uintptr_t curr_task_paddr = init_task_paddr;
			do {
				check_task(&nt, curr_task_paddr);
				curr_task_paddr = get_paddr_next_task(&nt, curr_task_paddr);
			} while (curr_task_paddr != init_task_paddr);
			cycles++;

			dma_write(&nt, shared_mem_phys_addr, NETTLP_RESUME_CPU, 22);
			if (cycles == NUMBER_MODS) break;
		}
	}
#endif

	double avg_elapsed = sum / cycles;  //! remove later
	printf("average elapsed time: %f [s]\n", avg_elapsed);  //! remove later
	get_detection_results();
	return 0;
}
#endif
