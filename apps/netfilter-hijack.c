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

/* start and end virtual address of kernel text region */
#define KERN_TEXT_START 0xFFFFFFFF81000000ULL
#define KERN_TEXT_END   0xFFFFFFFF82400000ULL

/* size of hooks array */
#define OFFSET_NF 2624
#define NF_INET_NUMHOOKS 5
#define OFFSET_HOOKS_IPV4 104
#define OFFSET_HOOKS_IPV6 144
#define OFFSET_HOOKS 8
#define SIZE_HOOK_STRUCT 16

uintptr_t find_init_net_from_systemmap(char *map)
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
		if (strstr(buf, "B init_net")) {
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

	uintptr_t v_init_net = find_init_net_from_systemmap(map);
	if (v_init_net == 0) {
		fprintf(stderr, "init_net not found on System.map %s\n", map);
		return -1;
	}

    uintptr_t p_init_net = __phys_addr(v_init_net);
    uintptr_t p_nf = p_init_net + OFFSET_NF;
    uintptr_t p_hooks_ipv4 = p_nf + OFFSET_HOOKS_IPV4;
    uintptr_t p_hooks_ipv6 = p_nf + OFFSET_HOOKS_IPV6;

    /* check ipv4 hooks */
    uintptr_t hooks_ipv4_arr[NF_INET_NUMHOOKS];
    ret = dma_read(&nt, p_hooks_ipv4, &hooks_ipv4_arr, sizeof(hooks_ipv4_arr));
    if (ret < sizeof(hooks_ipv4_arr)) return -1;

    for (int i = 0; i < NF_INET_NUMHOOKS; i++) {
        uintptr_t v = hooks_ipv4_arr[i];
        uintptr_t p = __phys_addr(v);
        u_int16_t num_entries = 0;
        ret = dma_read(&nt, p, &num_entries, sizeof(num_entries));
        if (ret < sizeof(num_entries)) return -1;

        void *hooks = calloc(num_entries, SIZE_HOOK_STRUCT);
        ret = dma_read(&nt, p + OFFSET_HOOKS, hooks, num_entries * SIZE_HOOK_STRUCT);
        if (ret < num_entries * SIZE_HOOK_STRUCT) return -1;
        for (int j = 0; j < num_entries; j++) {
            uintptr_t ent = *(uintptr_t *)((uintptr_t)hooks + j * SIZE_HOOK_STRUCT);
#ifdef PRINT_OUT
            printf("IPV4 nf_hook_entry is 0x%lx\n", ent);
#endif
        }
        free(hooks);
    }

    /* check ipv6 hooks */
    uintptr_t hooks_ipv6_arr[NF_INET_NUMHOOKS];
    ret = dma_read(&nt, p_hooks_ipv6, &hooks_ipv6_arr, sizeof(hooks_ipv6_arr));
    if (ret < sizeof(hooks_ipv6_arr)) return -1;

    for (int i = 0; i < NF_INET_NUMHOOKS; i++) {
        uintptr_t v = hooks_ipv6_arr[i];
        uintptr_t p = __phys_addr(v);
        u_int16_t num_entries = 0;
        ret = dma_read(&nt, p, &num_entries, sizeof(num_entries));
        if (ret < sizeof(num_entries)) return -1;

        void *hooks = calloc(num_entries, SIZE_HOOK_STRUCT);
        ret = dma_read(&nt, p + OFFSET_HOOKS, hooks, num_entries * SIZE_HOOK_STRUCT);
        if (ret < num_entries * SIZE_HOOK_STRUCT) return -1;
        for (int j = 0; j < num_entries; j++) {
            uintptr_t ent = *(uintptr_t *)((uintptr_t)hooks + j * SIZE_HOOK_STRUCT);
#ifdef PRINT_OUT
            printf("IPV6 nf_hook_entry is 0x%lx\n", ent);
#endif
        }
        free(hooks);
    }

	/* Get end time and calculate turnaround time */
	clock_gettime(CLOCK_MONOTONIC, &end);
	double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
	printf("Time elapsed: %f seconds\n", elapsed);

	return 0;
}
