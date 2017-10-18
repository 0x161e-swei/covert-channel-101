
#include "util.h"

// L3 properties
int cache_slices = 8;
int cache_sets = 8192;
int cache_ways = 16;

int get_cache_slice(uint64_t phys_addr) {
    static const int h0[] = {6, 10, 12, 14, 16, 17, 18, 20, 22, 24, 25, 26, 27, 28, 30, 32, 33, 35, 36};
    static const int h1[] = {7, 11, 13, 15, 17, 19, 20, 21, 22, 23, 24, 26, 28, 29, 31, 33, 34, 35, 37};
    static const int h2[] = {8, 12, 13, 16, 19, 22, 23, 26, 27, 30, 31, 34, 35, 36, 37};
    int slice;

    int count = sizeof(h0) / sizeof(h0[0]);
    int hash0 = 0;
    for (int i = 0; i < count; i++) {
        hash0 ^= (phys_addr >> h0[i]) & 1;
    }
    slice = hash0;

    int hash1 = 0;
    count = sizeof(h1) / sizeof(h1[0]);
    if (cache_slices > 2) {
        for (int i = 0; i < count; i++) {
            hash1 ^= (phys_addr >> h1[i]) & 1;
        }
        slice = hash1 << 1 | hash0;
    }

    int hash2 = 0;
    count = sizeof(h2) / sizeof(h2[0]);
    if (cache_slices > 4) {
        for (int i = 0; i < count; i++) {
            hash2 ^= (phys_addr >> h2[i]) & 1;
        }
        slice = (hash2 << 2) | (hash1 << 1) | hash0;
    }
    return slice;
}

uint64_t get_cache_set_index(ADDR_PTR phys_addr) {
    uint64_t mask = ((uint64_t) 1 << 17) - 1;
    return (phys_addr & mask) >> 6;
}

void clflush(ADDR_PTR addr)
{
    asm volatile ("clflush (%0)" :: "r"(addr));
}

void evict_set(volatile uint8_t **addresses, int kill_count) {
    for (size_t i = 0; i < kill_count; ++i) {
        *addresses[i];
        *addresses[i + 1];
        *addresses[i];
        *addresses[i + 1];
    }
}

void access_set(volatile uint8_t **addresses, int probe_count) {
    for (size_t i = 0; i < probe_count; ++i) {
        *addresses[i];
        *addresses[i + 1];
        *addresses[i + 2];
        *addresses[i];
        *addresses[i + 1];
        *addresses[i + 2];
    }
}

int ipow(int base, int exp)
{
    int result = 1;
    while (exp)
    {
        if (exp & 1)
            result *= base;
        exp >>= 1;
        base *= base;
    }

    return result;
}

int main(int argc, char **argv)
{
    // Setup code
    int n = cache_ways;
    int o = 6;              // log_2(64), where 64 is the line size
    int s = 13;             // log_2(8192), where 8192 is the number of cache sets

    int two_o = ipow(2, o);             // 64
    int two_o_s = ipow(2, s) * two_o;   // 524,288

    int b = n * two_o_s;    // size in bytes of the LLC
    char *buffer = malloc((size_t) b);

    printf("Evicting the LLC.\n");

	int sending = 1;
	while (sending) {
        // char text_buf[128];
        // fgets(text_buf, sizeof(text_buf), stdin);

        // i is the set index
        for (int i = 0; i < cache_sets; i++) {

            // j is the line index
            for (int j = 0; j < n; j++) {

                clflush((ADDR_PTR) &buffer[i * two_o + j * two_o_s]);
            }
        }

		// Put your covert channel code here
	}

	printf("Sender finished.\n");

	return 0;
}


