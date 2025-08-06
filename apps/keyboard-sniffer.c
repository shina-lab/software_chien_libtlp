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

/* offsets */
#define OFFSET_HEAD 8
#define OFFSET_NEXT 8
#define OFFSET_PRIORITY 16

/* offsets into struct task_struct */
#define OFFSET_ACTIVE_MM 2344

/* offsets into struct mm_struct */
#define OFFSET_PGD 72

/* start and end virtual address of kernel text region */
#define KERN_TEXT_START 0xFFFFFFFF81000000ULL
#define KERN_TEXT_END   0xFFFFFFFF82400000ULL

uintptr_t find_keyboard_notifier_list_from_systemmap(char *map)
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
		if (strstr(buf, "b keyboard_notifier_list")) {
			char *p;
			p = strchr(buf, ' ');
			*p = '\0';
			addr = strtoul(buf, NULL, 16);
		}
	}

	fclose(fp);

	return addr;
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

uintptr_t get_vpgd(struct nettlp *net, char *map) {
	uintptr_t v_init_task = find_init_task_from_systemmap(map);
	if (!v_init_task) {
		printf("[ERROR]: failed to find init_task from System.map, aborting...\n");
		return 0;
	}

	uintptr_t p_init_task = __phys_addr(v_init_task);
	uintptr_t v_mm;
	int ret = dma_read(net, p_init_task + OFFSET_ACTIVE_MM, &v_mm, sizeof(v_mm));
	if (ret < sizeof(v_mm)) {
		printf("[ERROR]: failed to read 'mm' field of init_task, aborting...\n");
		return 0;
	}
	// printf("p_init_task: 0x%lx, v_mm: 0x%lx\n", p_init_task, v_mm);
	
	uintptr_t p_mm = __phys_addr(v_mm);
	uintptr_t vpgd;
	ret = dma_read(net, p_mm + OFFSET_PGD, &vpgd, sizeof(vpgd));
	if (ret < sizeof(vpgd)) {
		printf("[ERROR]: failed to read 'pgd' field of mm_struct, aborting...\n");
		return 0;
	}
	return vpgd;
}

void dump_nb(struct nettlp *nt, uintptr_t pcurr) {
    int ret;
    
    uintptr_t notifier_call;
    ret = dma_read(nt, pcurr, &notifier_call, sizeof(notifier_call));
    if (ret < sizeof(notifier_call)) {
        return;
    }
   
    uintptr_t prio;
    ret = dma_read(nt, pcurr + OFFSET_PRIORITY, &prio, sizeof(prio));
    if (ret < sizeof(prio)) {
        return;
    }

    printf("notifier call: 0x%lx, priority: %ld\n", notifier_call, prio);
}

uintptr_t get_pnext(struct nettlp *nt, uintptr_t vpgd, uintptr_t pcurr) {
    uintptr_t vnext;
    int ret = dma_read(nt, pcurr + OFFSET_NEXT, &vnext, sizeof(vnext));
    if (ret < sizeof(vnext)) {
        return 0;
    }
    if (vnext == 0) return 0;
    uintptr_t pnext = vaddr_to_paddr(nt, vnext, vpgd);
    return pnext;
}


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

	/* Get start time */
	struct timespec start, end;
	clock_gettime(CLOCK_MONOTONIC, &start);

	ret = nettlp_init(&nt);
	if (ret < 0) {
		perror("nettlp_init");
		return ret;
	}

    uintptr_t vpgd = get_vpgd(&nt, map);

	uintptr_t v_keyboard_notifier_list = find_keyboard_notifier_list_from_systemmap(map);
    if (!v_keyboard_notifier_list) {
        printf("[ERROR]: could not find keyboard_notifier_list in System.map, exiting.\n");
        return -1;
    }  
    uintptr_t p_keyboard_notifier_list = __phys_addr(v_keyboard_notifier_list);
    uintptr_t p_head = p_keyboard_notifier_list + OFFSET_HEAD;

    uintptr_t v_head_ptr;
    ret = dma_read(&nt, p_head, &v_head_ptr, sizeof(v_head_ptr));
    if (ret < sizeof(v_head_ptr)) {
        return -1;
    }
    if (!v_head_ptr) {
        printf("keyboard_notifier_list is empty.\n");
        return 0;
    }
    uintptr_t p_head_ptr = vaddr_to_paddr(&nt, v_head_ptr, vpgd);

    /* iterate through every notifier_block */
    uintptr_t pcurr = p_head_ptr;
    do {
        dump_nb(&nt, pcurr);
        pcurr = get_pnext(&nt, vpgd, pcurr);
    } while (pcurr);

	/* Get end time and calculate turnaround time */
	clock_gettime(CLOCK_MONOTONIC, &end);
	double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
	printf("Time elapsed: %f seconds\n", elapsed);
	
    return 0;
}