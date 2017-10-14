
#include "util.h"
#include <string.h>

int cache_slices = 8;
int cache_sets = 8192;
int cache_ways = 16;
int channels = 6;

#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define GET_EVICTION_SET(addr, set) (void**)(((char**)(addr)) + cache_ways * (set))

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

uint32_t get_cache_set_index(uint64_t phys_addr) {
    uint64_t mask = ((uint64_t) 1 << 17) - 1;
    return (phys_addr & mask) >> 6;
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


void **get_cache_sets() {

    // REF: page 5 of:
    // https://www.blackhat.com/docs/asia-17/materials/
    //          asia-17-Schwarz-Hello-From-The-Other-Side-SSH-Over-Robust-Cache-Covert-Channels-In-The-Cloud-wp.pdf
    //
    // Compute the number of addresses per 2MB page usable as an eviction set
    const int n_addr_per_set = 16 / cache_slices;           // 2

    // To fully evict a cache set, we require as many addresses as there are cache
    // ways. Thus, the number of required 2 MB pages is
    const int n_pages = cache_ways / n_addr_per_set + 1;    // 9
    const int sub_set_offset = 64; // prevent interference from traffic on 4k aligned sets

    // cache-set is a table that contains the potential addresses in
    // a given slice and set for each of the allocated 2MB pages
    //
    // For each page, we consider only 32 cache sets, containing
    // addresses with a distance of 4 KB to each other.
    const int number_of_useful_sets_per_page = 32; 
    uint8_t *cache_set[n_pages][number_of_useful_sets_per_page][cache_slices][n_addr_per_set];
    memset(cache_set, 0, sizeof(cache_set));

    // This counter is used for when we have more than 1 address
    // in the same set and slice.
    uint8_t cache_set_counter[number_of_useful_sets_per_page][cache_slices];

    // Allocate a buffer of enough 2MB pages
    uint8_t *addr = malloc((size_t) (n_pages * 2 * 1024 * 1024));

    // Take each allocated page
    for (int page_index = 0; page_index < n_pages; ++page_index) {

        // Zeroize the counter for this page
        memset(cache_set_counter, 0, sizeof(cache_set_counter));

        // And iterate in 4 KB steps over it
        for (int j = 0; j < 512; ++j) {
            uint8_t *cur_addr = addr + page_index * (2 * 1024 * 1024) + j * 4096;

            // Get slice and set of each 4KB address
            int slice = get_cache_slice((uint64_t) cur_addr & ((1 << 21) - 1));
            int set = get_cache_set_index((uint64_t)(cur_addr)) >> 6;

            // Save the address in the cache_set table
            cache_set[page_index][set][slice][cache_set_counter[set][slice]++] = cur_addr + sub_set_offset;
        }
    }

    // --> At this point we have a bunch of addresses in multiple cache sets and cache slices

    // We arbitrarily choose (same for both sender and receiver)
    // the index of the cache set that we will use.
    const int set_offset = 0;
    const int probe_count = MIN(2, n_addr_per_set); // 2 seems to be enough, but won't just work for 16 cores

    // turns out, 2 probes are enough for pretty much any stress level
    volatile uint8_t *probe_set[probe_count];
    for (int i = 0; i < probe_count; ++i)
        probe_set[i] = cache_set[0][set_offset][0][i];

    uint8_t *final_cache_sets[number_of_useful_sets_per_page * cache_slices][cache_ways]; // [set][index]

    for (int set = 0; set < number_of_useful_sets_per_page; ++set) {

        for (int slice = 0; slice < cache_slices; ++slice) {

            for (int page = 0; page < n_pages - 1; ++page) {

                // Consider each address that we have on this same set, slice and page
                for (int i = 0; i < n_addr_per_set; ++i) {
                    int set_index = set * cache_slices + slice;
                    int way_index = page * n_addr_per_set + i;

                    final_cache_sets[set_index][way_index] = cache_set[page][set][slice][i] - sub_set_offset;
                }
            }
        }
    }

    void **cache_set_out = malloc(sizeof(final_cache_sets));
    memcpy(cache_set_out, final_cache_sets, sizeof(final_cache_sets));

    return cache_set_out;
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

void clflush(ADDR_PTR addr)
{
    asm volatile ("clflush (%0)" :: "r"(addr));
}

int main(int argc, char **argv)
{
    // Setup code
    int n = cache_ways;
    int o = 6;              // log_2(64), where 64 is the line size
    int s = 13;             // log_2(8192), where 8192 is the number of cache sets
    int c = 4;              // arbitrary constant

    int two_o = ipow(2, o);
    int two_o_s = ipow(2, s) + two_o;

    int b = n * two_o_s * c;
    char buffer[b];

    printf("Evicting the LLC.\n");

	int sending = 1;
	while (sending) {
//		char text_buf[128];
//		fgets(text_buf, sizeof(text_buf), stdin);

//        for (int i = 0; i < 16777216; i++) {
//            clflush((ADDR_PTR) &buffer[i]);
//        }

        for (int i = 0; i < cache_sets; i++) {
            clflush((ADDR_PTR) &buffer[i]);
//            for (int j = 0; j < n * c; j++) {
//                buffer[two_o * i + two_o_s * j] = 'c';       // 9 is arbitrary
//            }
        }

		// Put your covert channel code here
	}

	printf("Sender finished.\n");

	return 0;
}


