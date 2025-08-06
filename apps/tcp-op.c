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

/* offsets */
#define OFFSET_SHOW 24
#define OFFSET_OPEN 8

/* start and end virtual address of kernel text region */
#define KERN_TEXT_START 0xFFFFFFFF81000000ULL
#define KERN_TEXT_END   0xFFFFFFFF82400000ULL

uintptr_t find_tcp4_seq_ops_from_systemmap(char *map)
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
		if (strstr(buf, "d tcp4_seq_ops")) {
			char *p;
			p = strchr(buf, ' ');
			*p = '\0';
			addr = strtoul(buf, NULL, 16);
		}
	}

	fclose(fp);

	return addr;
}

uintptr_t find_proc_net_seq_ops_from_systemmap(char *map)
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
		if (strstr(buf, "d proc_net_seq_ops")) {
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

    /* check 'show' operation in seq_ops */
	uintptr_t v_tcp4_seq_ops = find_tcp4_seq_ops_from_systemmap(map);
    if (!v_tcp4_seq_ops) {
        printf("[ERROR]: could not find tcp4_seq_ops in System.map, exiting.\n");
        return -1;
    }  
    uintptr_t p_tcp4_seq_ops = __phys_addr(v_tcp4_seq_ops);

    uintptr_t show_op;
    ret = dma_read(&nt, p_tcp4_seq_ops + OFFSET_SHOW, &show_op, sizeof(show_op));
    if (ret < sizeof(show_op)) {
        return -1;
    }
#ifdef PRINT_OUT
    printf("show_op: 0x%lx, within kernel text region ? %c\n", show_op,
           (KERN_TEXT_START <= show_op && show_op <= KERN_TEXT_END) ? 'Y' : 'N');
#endif

    /* check 'open' operation in proc_ops */
	uintptr_t v_proc_net_seq_ops = find_proc_net_seq_ops_from_systemmap(map);
    if (!v_proc_net_seq_ops) {
        printf("[ERROR]: could not find proc_net_seq_ops in System.map, exiting.\n");
        return -1;
    }  
    uintptr_t p_proc_net_seq_ops = __phys_addr(v_proc_net_seq_ops);

    uintptr_t open_op;
    ret = dma_read(&nt, p_proc_net_seq_ops + OFFSET_OPEN, &open_op, sizeof(open_op));
    if (ret < sizeof(open_op)) {
        return -1;
    }
#ifdef PRINT_OUT
    printf("open_op: 0x%lx, within kernel text region ? %c\n", open_op,
           (KERN_TEXT_START <= open_op && open_op <= KERN_TEXT_END) ? 'Y' : 'N');
#endif

	/* Get end time and calculate turnaround time */
	clock_gettime(CLOCK_MONOTONIC, &end);
	double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
	printf("Time elapsed: %f seconds\n", elapsed);

	return 0;
}
