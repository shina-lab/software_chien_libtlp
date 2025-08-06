 #include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>  /* for turnaround time */

#ifdef __APPLE__
#define _AC(X,Y)        X
#else
#include <linux/const.h>
#endif

#include <libtlp.h>

// #define PRINT_OUT


/* from arch_x86/include/asm/page_64_types.h */
#define KERNEL_IMAGE_SIZE	(512 * 1024 * 1024)
//#define __PAGE_OFFSET_BASE      _AC(0xffff880000000000, UL)
#define __PAGE_OFFSET_BASE      _AC(0xffff888000000000, UL)
#define __PAGE_OFFSET           __PAGE_OFFSET_BASE
#define __START_KERNEL_map      _AC(0xffffffff80000000, UL)


/* from arch/x86/include/asm/page_types.h */
#define PAGE_OFFSET	((unsigned long)__PAGE_OFFSET)

// #define phys_base	0x1000000	/* x86 */
#define phys_base	0x4A00000	/* x86 */

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

		/* carry flag  will be set if starting x was >= PAGE_OFFSET */
		//VIRTUAL_BUG_ON((x > y) || !phys_addr_valid(x));
	}

	return x;
}

/* task_struct offsets for Linux 6.2.0-33-generic */
#define OFFSET_HEAD_STATE	    24
#define OFFSET_HEAD_PID		    2456
#define OFFSET_HEAD_CHILDREN    2488
#define OFFSET_HEAD_SIBLING	    2504
#define OFFSET_HEAD_COMM	    2960
#define OFFSET_HEAD_REAL_PARENT	2472
#define OFFSET_HEAD_PID		    2456
#define OFFSET_ACTIVE_MM        2344
#define OFFSET_FILES            3032

/* file-related offsets */
#define OFFSET_FDT          32
#define OFFSET_FD           8
#define OFFSET_F_PATH       16
#define OFFSET_DENTRY       8
#define OFFSET_D_NAME       32
#define OFFSET_NAME         8

/* offsets into struct mm_struct */
#define OFFSET_PGD 72

#define SIZE_NAME_BUF 32

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

uintptr_t vaddr_to_paddr(struct nettlp *nt, uintptr_t vaddr, uintptr_t pgd)
{
	int ret;
	uintptr_t ptr, ptr_next, paddr;
	uint16_t pm, dp, pd, pt, off;
	
	pm = 0;
	dp = 0;
	pd = 0;
	pt = 0;
	off = 0;

	off = (vaddr & 0x0FFF);
	pt = (vaddr >> 12) & 0x01FF;
	pd = (vaddr >> (9 + 12)) & 0x01FF;
	dp = (vaddr >> (9 + 9 + 12)) & 0x01FF;
	pm = (vaddr >> (9 + 9 + 9 + 12)) & 0x01FF;

	// printf("pm %u, dp %u, pd %u, pt %u, off %u\n", pm, dp, pd, pt, off);
	// printf("pm %x, dp %x, pd %x, pt %x, off %x\n", pm, dp, pd, pt, off);

	// printf("pgd %#lx\n", pgd);
	// printf("phy addr pgd is %#lx\n", __phys_addr(pgd));
	// printf("phy addr pgd read is is %#lx\n", __phys_addr(pgd) + pm);

	/* read Page Directory Pointer from PML4 */
	ret = dma_read(nt, __phys_addr(pgd) + (pm << 3),
		       &ptr_next, sizeof(ptr_next));
	if (ret < sizeof(ptr_next)) {
		fprintf(stderr, "failed to read page dir ptr from %#lx\n",
			__phys_addr(pgd) + (pm << 3));
		return 0;
	}
	ptr = ptr_next;
	// printf("pdp %#lx\n", ptr);

#define target_ptr(ptr, offset) \
	((ptr & 0x000FFFFFFFFFF000) + (offset << 3))

	/* read Page Directory from Page Directory Pointer */
	ret = dma_read(nt, target_ptr(ptr, dp), &ptr_next, sizeof(ptr_next));
	if (ret < sizeof(ptr)) {
		fprintf(stderr, "failed to read page directory from %#lx\n",
			target_ptr(ptr, dp));
		return 0;
	}
	ptr = ptr_next;
	// printf("pd %#lx\n", ptr);
	
	/* read page table from page directory */
	ret = dma_read(nt, target_ptr(ptr, pd), &ptr_next, sizeof(ptr_next));
	if (ret < sizeof(ptr)) {
		fprintf(stderr, "failed to read page directory from %#lx\n",
			target_ptr(ptr, pd));
		return 0;
	}
	ptr = ptr_next;
	// printf("page table %#lx\n", ptr);

	/* read page entry from page table */
	ret = dma_read(nt, target_ptr(ptr, pt), &ptr_next, sizeof(ptr_next));
	if (ret < sizeof(ptr)) {
		fprintf(stderr, "failed to read page directory from %#lx\n",
			target_ptr(ptr, pt));
		return 0;
	}
	ptr = ptr_next;
	// printf("page entry %#lx\n", ptr);

	/* ok, now ptr is actually the page entry */
	paddr = ((ptr & 0x000FFFFFFFFFF000) | off);
	return paddr;
}

uintptr_t get_vpgd(struct nettlp *net, uintptr_t v_init_task) {
	uintptr_t p_init_task = __phys_addr(v_init_task);
	uintptr_t v_mm;
	int ret = dma_read(net, p_init_task + OFFSET_ACTIVE_MM, &v_mm, sizeof(v_mm));
	if (ret < sizeof(v_mm)) {
		printf("[ERROR]: failed to read 'mm' field of init_task, aborting...\n");
		return 0;
	}
	
	uintptr_t p_mm = __phys_addr(v_mm);
	uintptr_t vpgd;
	ret = dma_read(net, p_mm + OFFSET_PGD, &vpgd, sizeof(vpgd));
	if (ret < sizeof(vpgd)) {
		printf("[ERROR]: failed to read 'pgd' field of mm_struct, aborting...\n");
		return 0;
	}
	return vpgd;
}


struct list_head {
	struct list_head *next, *prev;
};


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

    /* file-related */
    uintptr_t vfiles, pfiles;
};

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

	int pid;
	ret = dma_read(nt, t->ppid, &pid, sizeof(pid));
	if (ret < sizeof(pid)) {
		fprintf(stderr, "failed to read pid from %#lx\n", t->ppid);
		return -1;
	}
#ifdef PRINT_OUT
    printf("pid: %d\n", pid);
#endif 

    /* get list of fd names */
    t->vfiles = vhead + OFFSET_FILES;
    t->pfiles = __phys_addr(t->vfiles);
    
    uintptr_t v_files_struct = 0;
    ret = dma_read(nt, t->pfiles, &v_files_struct, sizeof(v_files_struct));
    if (ret < sizeof(v_files_struct)) {
        return -1;
    }
    uintptr_t p_files_struct = __phys_addr(v_files_struct);
    // printf("p_files_struct: 0x%lx\n", p_files_struct);

    uintptr_t v_fdtable;
    ret = dma_read(nt, p_files_struct + OFFSET_FDT, &v_fdtable, sizeof(v_fdtable));
    if (ret < sizeof(v_fdtable)) {
        return -1;
    }
    uintptr_t p_fdtable = __phys_addr(v_fdtable);
    // printf("v_fdtable: 0x%lx, p_fdtable: 0x%lx\n", v_fdtable, p_fdtable);

	uintptr_t num_fds = 0;
	ret = dma_read(nt, p_fdtable, &num_fds, sizeof(num_fds));
	if (ret < sizeof(num_fds)) {
		return -1;
	}
#ifdef PRINT_OUT
	printf("--- max number of entries: %ld\n", num_fds);
#endif

	uintptr_t v_fd;
	ret = dma_read(nt, p_fdtable + OFFSET_FD, &v_fd, sizeof(v_fd));
	if (ret < sizeof(v_fd)) {
		return -1;
	}
	uintptr_t p_fd = __phys_addr(v_fd);
	
	for (int i = 0; i < num_fds; i++) {
		uintptr_t p_entry = p_fd + (i << 3);
		uintptr_t v_file;
		ret = dma_read(nt, p_entry, &v_file, sizeof(v_file));
		if (ret < sizeof(v_file)) {
			return -1;
		}
		if (!v_file) {
			break;
		}

		uintptr_t p_file = __phys_addr(v_file);
		uintptr_t p_f_path = p_file + OFFSET_F_PATH;
		uintptr_t p_dentry = p_f_path + OFFSET_DENTRY;

		uintptr_t v_dentry;
		ret = dma_read(nt, p_dentry, &v_dentry, sizeof(v_dentry));
		if (ret < sizeof(v_dentry)) {
			return -1;
		}
		p_dentry = __phys_addr(v_dentry);

		uintptr_t p_dname = p_dentry + OFFSET_D_NAME;
		uintptr_t p_name = p_dname + OFFSET_NAME;

		uintptr_t v_str;
		ret = dma_read(nt, p_name, &v_str, sizeof(v_str));
		if (ret < sizeof(v_str)) {
			return -1;
		}
		uintptr_t p_str = __phys_addr(v_str);

		char name_buf[SIZE_NAME_BUF];
		ret = dma_read(nt, p_str, &name_buf, sizeof(name_buf));
		if (ret < sizeof(name_buf)) {
			return -1;
		}
		name_buf[SIZE_NAME_BUF - 1] = '\0';
#ifdef PRINT_OUT
		printf("--- file name: %s\n", name_buf);
#endif
	}

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


int main(int argc, char **argv)
{
	int ret, ch;
	struct nettlp nt;
	struct in_addr remote_host;
	uint16_t busn, devn;
	struct task_struct t;
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

	/* Get start time */
	struct timespec start, end;
	clock_gettime(CLOCK_MONOTONIC, &start);

	uintptr_t v_init_task = find_init_task_from_systemmap(map);
	if (v_init_task == 0) {
		fprintf(stderr, "init_task not found on System.map %s\n", map);
		return -1;
	}

    uintptr_t vpgd = get_vpgd(&nt, v_init_task);
	if (!vpgd) {
		printf("[ERROR]: failed to get virtual address of pgd, exiting.\n");
		return -1;
	}

	fill_task_struct(&nt, v_init_task, &t);
	task(&nt, t.vhead, t.vhead);

	/* Get end time and calculate turnaround time */
	clock_gettime(CLOCK_MONOTONIC, &end);
	double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
	printf("Time elapsed: %f seconds\n", elapsed);

	return 0;
}
