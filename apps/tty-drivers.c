 #include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>

#include <time.h> /* for turnaround time */

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
#define OFFSET_NAME 24
#define OFFSET_TYPE 56
#define OFFSET_NUM 52
#define OFFSET_TTYS 128
#define OFFSET_LDISC 88
#define OFFSET_RECB 96
#define OFFSET_TTY_DRIVERS 168

#define SIZE_NAME_BUF 16

#define LIST_TO_DRIVERS(paddr) (paddr - 168)

/* offsets into struct task_struct */
#define OFFSET_ACTIVE_MM 2344

/* offsets into struct mm_struct */
#define OFFSET_PGD 72

/* start and end virtual address of kernel text region */
#define KERN_TEXT_START 0xFFFFFFFF81000000ULL
#define KERN_TEXT_END   0xFFFFFFFF82400000ULL

#define VALID_PHYS_MEMADDR 0x100000000

uintptr_t find_tty_drivers_from_systemmap(char *map)
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
		if (strstr(buf, "D tty_drivers")) {
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

uintptr_t get_pnext(struct nettlp *net, uintptr_t p_tty_driver) {
	uintptr_t vnext;
	int ret = dma_read(net, p_tty_driver + OFFSET_TTY_DRIVERS, &vnext, sizeof(vnext));
	if (ret < sizeof(vnext)) {
		return 0;
	}
	if (vnext == 0) {
		return 0;
	}
	uintptr_t pnext = LIST_TO_DRIVERS(__phys_addr(vnext));
	// printf("vnext: %lx, pnext: %lx\n", vnext, pnext);
	return pnext;
}

uintptr_t get_init_list(struct nettlp *net, uintptr_t p_tty_drivers) {
    uintptr_t vnext;
	int ret = dma_read(net, p_tty_drivers, &vnext, sizeof(vnext));
	if (ret < sizeof(vnext)) {
		return 0;
	}
	uintptr_t pnext = __phys_addr(vnext);
	return pnext;
}

void dump_tty_driver(struct nettlp *net, uintptr_t p_tty_driver) {
	int ret;

	u_int16_t type;
	ret = dma_read(net, p_tty_driver + OFFSET_TYPE, &type, sizeof(type));
	if (ret < sizeof(type)) {
		return;
	}

	unsigned int num;
	ret = dma_read(net, p_tty_driver + OFFSET_NUM, &num, sizeof(num));
	if (ret < sizeof(num)) {
		return;
	}

	/* read name */
	uintptr_t v_char_ptr;
	ret = dma_read(net, p_tty_driver + OFFSET_NAME, &v_char_ptr, sizeof(v_char_ptr));
	if (ret < sizeof(v_char_ptr)) {
		return -1;
	}
	uintptr_t p_char_ptr = __phys_addr(v_char_ptr);

	char name_buf[SIZE_NAME_BUF];
	ret = dma_read(net, p_char_ptr, &name_buf, sizeof(name_buf));
	if (ret < sizeof(name_buf)) {
		return;
	}
	name_buf[SIZE_NAME_BUF - 1] = '\0';

	printf("name: %16s, type: %u, max_array_size: %u\n", name_buf, type, num);
	
	uintptr_t v_ttys;
	ret = dma_read(net, p_tty_driver + OFFSET_TTYS, &v_ttys, sizeof(v_ttys));
	if (ret < sizeof(v_ttys)) {
		return;
	}
	uintptr_t p_ttys = __phys_addr(v_ttys);

	// /* Iterate through array */
	for (int i = 0; i < num; i++) {
		uintptr_t p_elem = p_ttys + (i << 3);

		uintptr_t v_tty;
		ret = dma_read(net, p_elem, &v_tty, sizeof(v_tty));
		if (ret < sizeof(v_tty)) {
			break;
		}
		if (!v_tty) {
			break;
		}
		
		uintptr_t p_tty = __phys_addr(v_tty);
		uintptr_t p_ldisc_field = p_tty + OFFSET_LDISC;
		uintptr_t v_ldisc;
		ret = dma_read(net, p_ldisc_field, &v_ldisc, sizeof(v_ldisc));
		if (ret < sizeof(v_ldisc)) {
			break;
		}
		uintptr_t p_ldisc = __phys_addr(v_ldisc);
		
		uintptr_t v_ops;
		ret = dma_read(net, p_ldisc, &v_ops, sizeof(v_ops));
		if (ret < sizeof(v_ops)) {
			break;
		}
		uintptr_t p_ops = __phys_addr(v_ops);

		uintptr_t receive_buf;
		ret = dma_read(net, p_ops + OFFSET_RECB, &receive_buf, sizeof(receive_buf));
		if (ret < sizeof(receive_buf)) {
			break;
		}
		printf("--- receive_buf: 0x%lx, within kernel text region? %c\n", receive_buf,
				(KERN_TEXT_START <= receive_buf && receive_buf <= KERN_TEXT_END) ? 'Y' : 'C');
	}
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

	uintptr_t v_tty_drivers = find_tty_drivers_from_systemmap(map);
    if (!v_tty_drivers) {
        printf("[ERROR]: could not find tty_drivers in System.map, exiting.\n");
        return -1;
    }
    uintptr_t p_tty_drivers = __phys_addr(v_tty_drivers);
	printf("v: %lx, p: %lx\n", v_tty_drivers, p_tty_drivers);

	uintptr_t p_init_list = get_init_list(&nt, p_tty_drivers);
	printf("p: 0x%lx\n", p_init_list);

    uintptr_t p_init_tty_driver = LIST_TO_DRIVERS(p_init_list);
    printf("p: 0x%lx\n", p_init_tty_driver);

	uintptr_t pcurr = p_init_tty_driver;
	int cnt = 10;
	do {
		dump_tty_driver(&nt, pcurr);
		pcurr = get_pnext(&nt, pcurr);
		if (pcurr < VALID_PHYS_MEMADDR) break;
	} while (1);

	/* Get end time and calculate turnaround time */
	clock_gettime(CLOCK_MONOTONIC, &end);
	double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
	printf("Time elapsed: %f seconds\n", elapsed);

    return 0;
}