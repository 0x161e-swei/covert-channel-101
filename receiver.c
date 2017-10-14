
#include "util.h"

// L1 properties
int cache_sets = 64;
int cache_ways = 8;

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
	// Put your covert channel setup code here

    int n = cache_ways;
    int o = 6;              // log_2(64), where 64 is the line size
    int s = 6;              // log_2(64), where 64 is the number of cache sets

    int two_o_s = ipow(2, o + s);

    int b = n * two_o_s;
    char buffer[b];

	printf("Press enter to measure the time to read an address.\n");

	int sending = 1;
	while (sending) {
        char text_buf[128];
        fgets(text_buf, sizeof(text_buf), stdin);

        unsigned long long misses = 0;
        unsigned long long hits = 0;

        // These numbers need to be tuned up and optimized
        for (int k = 0; k < 100; k++) {
            for (int i = 0; i < n; i++) {

                // printf("%" PRIx64 "\n", (uint64_t) (buffer + two_o_s * i));
                CYCLES time = measure_one_block_access_time((ADDR_PTR) &buffer[two_o_s * i]);
                // printf("Time %d: %d\n", i, time);

                if (time > 80) {
                    misses++;
                } else {
                    hits++;
                }
            }

            // Busy loop to give time to the sender
            // The number of cycles of this loop are currently
            // decided by the program first argument.
            for (int junk = 0; junk < atoi(argv[1]); junk++) {}
        }

        printf("Hits: %lld\n", hits);
        printf("Misses: %lld\n", misses);

		// Put your covert channel code here
	}

	printf("Sender finished.\n");

	return 0;
}
