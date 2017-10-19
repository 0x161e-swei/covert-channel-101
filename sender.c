#include "util.h"
#include "cache_utils.c"

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


