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

		/* carry flag will be set if starting x was >= PAGE_OFFSET */
		//VIRTUAL_BUG_ON((x > y) || !phys_addr_valid(x));
	}

	return x;
}

/* start and end virtual address of kernel text region */
#define KERN_TEXT_START 0xFFFFFFFF81000000ULL
#define KERN_TEXT_END   0xFFFFFFFF82400000ULL

/* offset of 'proc_dir_ops' in 'struct proc_dir_entry' */
#define OFFSET_HEAD_PROC_DIR_OPS 48
#define OFFSET_READ 16

uintptr_t find_proc_root_from_systemmap(char *map)
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
		if (strstr(buf, "D proc_root")) {
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
	uintptr_t v_proc_root;
	uint16_t busn, devn;
	char *map;

	memset(&nt, 0, sizeof(nt));
	v_proc_root = 0;
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

	v_proc_root = find_proc_root_from_systemmap(map);
	if (v_proc_root == 0) {
		fprintf(stderr, "proc_root not found on System.map %s\n", map);
		return -1;
	}

    /* Check read pointer */
    uintptr_t p_proc_root = __phys_addr(v_proc_root);
    printf("Physical address of proc_root is 0x%lx\n", p_proc_root);

    uintptr_t p_proc_dir_ops = p_proc_root + OFFSET_HEAD_PROC_DIR_OPS;
    uintptr_t proc_dir_ops_val_v = 0;
    ret = dma_read(&nt, p_proc_dir_ops, &proc_dir_ops_val_v, sizeof(proc_dir_ops_val_v));
	if (ret < sizeof(proc_dir_ops_val_v)) {
		return -1;
    }
    printf("Value of proc_dir_ops is: 0x%lx\n", proc_dir_ops_val_v);

    uintptr_t proc_dir_ops_val_p = __phys_addr(proc_dir_ops_val_v);
    uintptr_t proc_dir_ops_read_p = proc_dir_ops_val_p + OFFSET_READ;

    uintptr_t read_pointer = 0;
    ret = dma_read(&nt, proc_dir_ops_read_p, &read_pointer, sizeof(read_pointer));
	if (ret < sizeof(read_pointer)) {
		return -1;
    }
    printf("Value of .read is: 0x%lx\n", read_pointer);
    printf("Checking if .read pointer is in kernel text region: %s\n", 
            (KERN_TEXT_START <= read_pointer && read_pointer <= KERN_TEXT_END) ? "YES" : "NO");

	/* Get end time and calculate turnaround time */
	clock_gettime(CLOCK_MONOTONIC, &end);
	double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
	printf("Time elapsed: %f seconds\n", elapsed);

	return 0;
}
