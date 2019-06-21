#include "util.h"

/*
 * Parses the arguments and flags of the program and initializes the struct config
* with those parameters (or the default ones if no custom flags are given).
 */
void init_config(struct config *config, int argc, char **argv) {
    uint64_t pid = print_pid();

    init_default(config, argc, argv);

    if (config->channel == PrimeProbe || config->channel == L1DPrimeProbe) {
        // 4096 L1 stride
        int L1_way_stride = ipow(2, LOG_CACHE_SETS_L1 + LOG_CACHE_LINESIZE);
        // (4 * 64) * 8 * 4k = 8M
        uint64_t bsize = 512 * CACHE_WAYS_L1 * L1_way_stride;

        // Allocate a buffer twice the size of the L1 cache
        config->buffer = allocate_buffer(bsize);

        printf("buffer pointer addr %p\n", config->buffer);
        // Initialize the buffer to be be the non-zero page
        for (uint32_t i = 0; i < bsize; i += 64) {
            *(config->buffer + i) = pid;
        }
        // Construct the addr_set by taking the addresses that have cache set index 0
        // There will be at least one of such addresses in our buffer.
        uint32_t addr_set_size = 0;
        for (int i = 0; i < 512 * CACHE_WAYS_L1 * CACHE_SETS_L1; i++) {
            ADDR_PTR addr = (ADDR_PTR) (config->buffer + CACHE_LINESIZE * i);
            // both of following function should work...L3 is a more restrict set
            if (get_cache_slice_set_index(addr) == config->cache_region) {
            // if (get_L3_cache_set_index(addr) == config->cache_region) {
                append_string_to_linked_list(&config->addr_set, addr);
                addr_set_size++;
            }
            // restrict the probing set to CACHE_WAYS_L1 to aviod self eviction
            if (config->channel == L1DPrimeProbe && addr_set_size >= CACHE_WAYS_L1) {
                break;
            }
            // more lines than private cache ways helps to put more lines
            // into llc slices, increasing chance of to conflict with sender
            else if (addr_set_size >= 5 * (CACHE_WAYS_L1 + CACHE_WAYS_L2)) {
                break;
            }
        }

        printf("Found addr_set size of %u\n", addr_set_size);
    }

    if (config->channel == FlushReload) {
        int inFile = open(config->shared_filename, O_RDONLY);
        if (inFile == -1) {
            fprintf(stderr, "ERROR: Failed to Open File\n");
            exit(-1);
        }

        size_t size = 4096;
        config->buffer = mmap(NULL, size, PROT_READ, MAP_SHARED, inFile, 0);
        if (config->buffer == (void*) -1 ) {
            fprintf(stderr, "ERROR: Failed to Map Address\n");
            exit(-1);
        }

        ADDR_PTR addr = (ADDR_PTR) config->buffer + config->cache_region * 64;
        append_string_to_linked_list(&config->addr_set, addr);
        printf("File mapped at %p and monitoring line %lx\n", config->buffer, addr);

    }
}

// receiver function pointer
bool (*detect_bit)(const struct config*);

bool detect_bit_fr(const struct config *config) {
    int misses = 0;
    int hits = 0;
    int total_measurements = 0;

    // This is high because the misses caused by clflush
    // usually cause an access time larger than 150 cycles

    uint64_t start_t = rdtsc();
    while ((rdtsc() - start_t) < config->interval) {
        uint64_t time = measure_one_block_access_time(config->addr_set->addr);

        // When the access time is larger than 1000 cycles,
        // it is usually due to a disk miss. We exclude such misses
        // because they are not caused by clflush.
        if (time < 1000) {
            total_measurements++;
            if (time > config->miss_threshold) {
                misses++;
            } else {
                hits++;
            }
        }

        // Busy loop to give time to the sender to flush the cache
        uint64_t wait_t = rdtsc();
        while((rdtsc() - wait_t) < config->access_period &&
                   (rdtsc() - start_t) < config->interval);
    }

    if (misses != 0) {
        debug("Misses: %d out of %d\n", misses, total_measurements);
    }

    bool ret =  misses > (float) total_measurements / 2.0;
    return ret;

}
/*
 * Detects a bit by repeatedly measuring the access time of the addresses in the
 * probing set and counting the number of misses for the clock length of config->interval.
 *
 * If the the first_bit argument is true, relax the strict definition of "one" and try to
 * cc_sync with the sender.
 */
// bool detect_bit(const struct config *config, uint64_t start_t)
bool detect_bit_pp(const struct config *config)
{
    uint64_t start_t = get_time();
    // debug("time %lx\n", start_t);

    int misses = 0;
    int hits = 0;
    int total_measurements = 0;

    // miss in L3
    struct Node *current = NULL;

    // prime
    uint64_t prime_count = 0;
    do {
        current = config->addr_set;
        while (current != NULL && current->next != NULL) {
            volatile uint64_t* addr1 = (uint64_t*) current->addr;
            volatile uint64_t* addr2 = (uint64_t*) current->next->addr;
            *addr1;
            *addr2;
            *addr1;
            *addr2;
            current = current->next;
            prime_count++;
        }
    } while ((get_time() - start_t) < config->prime_period);
    // debug("prime count%lu\n", prime_count);

    // wait for sender to access
    while (get_time() - start_t < (config->prime_period + config->access_period)) {}

    // probe
    current = config->addr_set;
    while (current != NULL && (get_time() - start_t) < config->interval) {
        ADDR_PTR addr = current->addr;
        uint64_t time = measure_one_block_access_time(addr);

        // When the access time is larger than 1000 cycles,
        // it is usually due to a long-latency page walk.
        // We exclude such misses
        // because they are not caused by accesses from the sender.
        total_measurements += time < 800;
        misses  += (time < 800) && (time > config->miss_threshold);
        hits    += (time < 800) && (time <= config->miss_threshold);

        current = current->next;
        // debug("access time %lu\n", time);
    }

    if (misses != 0) {
        debug("Misses: %d out of %d\n", misses, total_measurements);
    }

    bool ret = (misses > CACHE_WAYS_L1 / 2 - 1)? true: false;
    // FIXME: If only one set region used in a L1D, the channel is really not
    // reliable as too much noise even from stack reads and writes.
    // Mulitple regions for each channel is recommended.
    // The hardcoded 1 miss count threshold can be used for a noisy l1d-PP
    // bool ret = (misses > 1)? true: false;

    while (get_time() - start_t < config->interval) {}

    return ret;
}

// This is the only hardcoded variable which defines the max size of a message
// to be the same as the max size of the message in the starter code of the sender.
// static const int MAX_BUFFER_LEN = 128 * 8;

void benchmark_receive(struct config *config_p) {
    FILE *receiverSave = fopen("data/receiverSave", "w+");
    if (receiverSave == NULL) {
        fprintf(stderr, "ERROR: cannot open file to save.\n"
                "Check if data/ folder is created\n");
        exit(-1);
    }

    uint32_t benchmarkSize = 8192;
    uint8_t *msg = (uint8_t *)malloc(sizeof(uint8_t) * benchmarkSize);
    uint64_t start_t;
    struct timespec beg_t, end_t;
    for (uint32_t i = 0; i < benchmarkSize; i++) {
        // sync every 1024 bits, detecting pilot signal again
        if ((i & 0x3ff) == 0) {
            bool curr = true, prev = true;
            int flip_sequence = 4;
            while (true) {
                start_t = cc_sync();
                curr = detect_bit(config_p);

                if (flip_sequence == 0 && curr == 1 && prev == 1) {
                    debug("pilot signal detected for round %u\r", i / 1024);
                    start_t = cc_sync();
                    if (i == 0) clock_gettime(CLOCK_MONOTONIC, &beg_t);
                    break;
                }
                else if (flip_sequence > 0 && curr != prev) {
                    flip_sequence--;
                }
                else if (curr == prev) {
                    flip_sequence = 4;
                }
                prev = curr;
            }
        }

        msg[i] = detect_bit(config_p);

    }

    clock_gettime(CLOCK_MONOTONIC, &end_t);
    printf("total cycles to receive %u bits is %lu\n", benchmarkSize,
    (end_t.tv_sec - beg_t.tv_sec) * (long)1e9 + (end_t.tv_nsec -
    beg_t.tv_nsec));

    if (msg) {
        for (uint32_t i = 0; i < benchmarkSize; i++) {
            fprintf(receiverSave, "%u %u\n", i, msg[i]);
        }
        fclose(receiverSave);

        free(msg);
    }
}

int main(int argc, char **argv)
{
    // Initialize config and local variables
    struct config config;

    init_config(&config, argc, argv);
    if (config.channel == PrimeProbe || config.channel == L1DPrimeProbe) {
        detect_bit = detect_bit_pp;
    }
    else if (config.channel == FlushReload) {
        detect_bit = detect_bit_fr;
    }

    char msg_ch[MAX_BUFFER_LEN + 1];
    int flip_sequence = 4;
    bool current;
    bool previous = true;

    if (config.benchmark_mode) {
        benchmark_receive(&config);
        exit(0);
    }

    printf("Press enter to begin listening ");
    getchar();
    while (1) {

        // cc_sync on clock edge
        uint64_t start_t = cc_sync();
        // current = detect_bit(&config, start_t);
        current = detect_bit(&config);

        // This receiving loop is a sort of finite config machine.
        // Once again, it would be easier to explain how it works
        // in a whiteboard, but here is an attempt to give an idea:
        //
        // Starting from the base config, it first looks for a sequence
        // of bits of the form "1010" (ref: flip_sequence variable).
        //
        // The first 1 is used to cc_synchronize, the following ones are
        // used to make sure that the cc_synchronization was right.
        //
        // Once these bits have been detected, if there are other bit
        // flips, the receiver ignores them.
        //
        // In fact, as of now the sender sends more than 4 bit flips.
        // This is because sometimes the receiver may miss the first 2.
        // Thus having more still works.
        //
        // After the 1010 bits, when two consecutive 11 bits are detected,
        // the receiver will know that what follows is a message and go
        // into message receiving mode.
        //
        // Finally, when a NULL byte is received the receiver exits the
        // message receiving mode and restarts from the base config.
        if (flip_sequence == 0 && current == 1 && previous == 1) {
            debug("Start sequence fully detected.\n\n");

            uint32_t msg_len = 0, strike_zeros = 0;
            start_t = cc_sync();
            for (msg_len = 0; msg_len < MAX_BUFFER_LEN; msg_len++) {
#if 1
                // uint32_t bit = detect_bit(&config, start_t);
                uint32_t bit = detect_bit(&config);
                msg_ch[msg_len] = '0' + bit;
                strike_zeros = (strike_zeros + (1-bit)) & (bit-1);
                if (strike_zeros >= 8 && ((msg_len & 0x7) == 0)) {
                    debug("String finished\n");
                    break;
                }

#else
                if (detect_bit(&config)) {
                    msg_ch[msg_len] = '1';
                    strike_zeros = 0;
                } else {
                    msg_ch[msg_len] = '0';
                    if (++strike_zeros >= 8 && msg_len % 8 == 0) {
                        debug("String finished\n");
                        break;
                    }
                }
#endif
                start_t += config.interval;
            }

            msg_ch[msg_len - 8] = '\0';
            printf("message %s received\n", msg_ch);

            uint32_t ascii_msg_len = msg_len / 8;
            char msg[ascii_msg_len];
            printf("> %s\n", conv_msg(msg_ch, ascii_msg_len, msg));
            if (strcmp(msg, "exit") == 0) {
                break;
            }

        } else if (flip_sequence > 0 && current != previous) {
            flip_sequence--;

        } else if (current == previous) {
            flip_sequence = 4;
        }

        previous = current;
    }

    printf("Receiver finished\n");
    return 0;
}
