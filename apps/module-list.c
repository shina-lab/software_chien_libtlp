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


/* offsets into struct task_struct */
#define OFFSET_ACTIVE_MM 2344

/* offsets into struct mm_struct */
#define OFFSET_PGD 72

/* offsets into struct module */
#define OFFSET_STATE 0
#define OFFSET_LIST 8
#define OFFSET_NAME 24

/* size of module name buffer */
#define SIZE_NAME_BUFFER 16

/* states of module */
#define MODULE_STATE_LIVE 0
#define MODULE_STATE_COMING 1
#define MODULE_STATE_GOING 2
#define MODULE_STATE_UNFORMED 3

#define VALID_PHYS_MEMADDR 0x100000000

#define LIST_TO_MOD(paddr) (paddr - 8)

const char *state_to_str(long state)
{
	switch(state & 0x00FF) {
        case MODULE_STATE_LIVE: 
            return "MODULE_STATE_LIVE";
        case MODULE_STATE_COMING:
            return "MODULE_STATE_COMING";
        case MODULE_STATE_GOING:
            return "MODULE_STATE_GOING";
        case MODULE_STATE_UNFORMED:
            return "MODULE_STATE_UNFORMED";
        default:
            return "UNKNOWN STATE!";
    }
}

uintptr_t find_modules_from_systemmap(char *map)
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
		if (strstr(buf, "D modules")) {
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

uintptr_t get_pnext(struct nettlp *net, uintptr_t pcurr, uintptr_t vpgd) {
	uintptr_t v_next;
	int ret = dma_read(net, pcurr, &v_next, sizeof(v_next));
	if (ret < sizeof(v_next)) {
		fprintf(stderr, "could not get next pointer.\n");
		return 0;
	}
	return vaddr_to_paddr(net, v_next, vpgd);
}

uintptr_t get_paddr_init_module(struct nettlp *net, char *map, uintptr_t vpgd) {
	uintptr_t v_modules = find_modules_from_systemmap(map);
	if (v_modules == 0) {
		fprintf(stderr, "modules not found on System.map %s\n", map);
		return 0;
	}
    uintptr_t p_modules = __phys_addr(v_modules);
	return get_pnext(net, p_modules, vpgd);
}

void dump_mod_list_head(struct nettlp *net, uintptr_t pmod_list_head) {
	uintptr_t pmod = LIST_TO_MOD(pmod_list_head);

	uintptr_t state;
	int ret = dma_read(net, pmod + OFFSET_STATE, &state, sizeof(state));
	if (ret < sizeof(state)) {
		printf("[ERROR]: could not read 'state' field of 'struct module'\n");
		return;
	}

	char name_buf[SIZE_NAME_BUFFER];
	ret = dma_read(net, pmod + OFFSET_NAME, &name_buf, sizeof(name_buf));
	if (ret < sizeof(name_buf)) {
		printf("[ERROR]: could not read 'name' field of 'struct module'\n");
		return;
	}
	name_buf[SIZE_NAME_BUFFER - 1] = '\0';  // to prevent overflow
#ifdef PRINT_OUT
	printf("%ld       %.23s         %s\n", state, state_to_str(state), name_buf);
#endif 
	return;
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

	ret = nettlp_init(&nt);
	if (ret < 0) {
		perror("nettlp_init");
		return ret;
	}

	/* Get start time */
	struct timespec start, end;
	clock_gettime(CLOCK_MONOTONIC, &start);

	uintptr_t vpgd = get_vpgd(&nt, map);
	if (!vpgd) {
		printf("[ERROR]: failed to get virtual address of pgd, exiting.\n");
		return -1;
	}

	uintptr_t p_init = get_paddr_init_module(&nt, map, vpgd);
	if (!p_init) {
		printf("[ERROR]: failed to get initial module, exiting.\n");
		return -1;
	}
#ifdef PRINT_OUT
	printf("state | state_name              | module_name\n");
#endif
	uintptr_t p_curr = p_init;
	do {
		dump_mod_list_head(&nt, p_curr);
		p_curr = get_pnext(&nt, p_curr, vpgd);
		if (p_curr < VALID_PHYS_MEMADDR) break;
	} while (1);

	/* Get end time and calculate turnaround time */
	clock_gettime(CLOCK_MONOTONIC, &end);
	double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
	printf("Time elapsed: %f seconds\n", elapsed);

	return 0;
}
