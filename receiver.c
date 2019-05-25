#include "util.h"

/*
 * Execution state of the program, with the variables
 * that we need to pass around the various functions.
 */
struct state {
    char *buffer;
    struct Node *probing_set;
    int interval;
    uint64_t cache_region;
    int wait_period_between_measurements;
    bool debug;
};

/*
 * Parses the arguments and flags of the program and initializes the struct state
 * with those parameters (or the default ones if no custom flags are given).
 */
void init_state(struct state *state, int argc, char **argv)
{
    uint64_t pid = printPID();
    // The following calculations are based on the paper:
    //      C5: Cross-Cores Cache Covert Channel (dimva 2015)
    int L1_way_stride = ipow(2, LOG_CACHE_SETS_L1 + LOG_CACHE_LINESIZE); // 4096
    int bsize = 64 * CACHE_WAYS_L1 * L1_way_stride; // 64 * 8 * 4k = 2M

    // Allocate a buffer twice the size of the L1 cache
    char *buffer = MAP_FAILED;
#ifdef HUGEPAGES
    buffer = mmap(NULL, bsize, PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE|HUGEPAGES, -1, 0);
#endif

    if (buffer == MAP_FAILED) {
        fprintf(stderr, "allocating non-hugepages\n");
        buffer = mmap(NULL, bsize, PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE, -1, 0);
    }
    if (buffer == MAP_FAILED) {
        fprintf(stderr, "Failed to allocate buffer!\n");
        exit(-1);
    }
    state->buffer = buffer;
    printf("buffer pointer addr %p\n", state->buffer);
    // Initialize the buffer to be be the non-zero page
    for (uint32_t i = 0; i < bsize; i += 64) {
        *(state->buffer + i) = pid;
    }

    // Set some default state values.
    state->debug = false;
    state->probing_set = NULL;

    // These numbers may need to be tuned up to the specific machine in use
    // NOTE: Make sure that interval is the same in both sender and receiver
    state->interval = CHANNEL_DEFAULT_INTERVAL;
    state->cache_region = CHANNEL_DEFAULT_REGION;
    state->wait_period_between_measurements = CHANNEL_DEFAULT_WAIT_PERIOD;

    // Parse the command line flags
    //      -d is used to enable the debug prints
    //      -i is used to specify a custom value for the time interval
    //      -w is used to specify a custom number of wait time between two probes
    int option;
    while ((option = getopt(argc, argv, "di:w:r:")) != -1) {
        switch (option) {
            case 'd':
                state->debug = true;
                break;
            case 'i':
                state->interval = atoi(optarg);
                break;
            case 'r':
                state->cache_region = atoi(optarg);
                break;
            case 'w':
                state->wait_period_between_measurements = atoi(optarg);
                break;
            case '?':
                fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
                exit(1);
            default:
                exit(1);
        }
    }
    // Construct the probing_set by taking the addresses that have cache set index 0
    // There will be at least one of such addresses in our buffer.
    uint32_t probing_set_size = 0;
    for (int i = 0; i < 64 * CACHE_WAYS_L1 * CACHE_SETS_L1; i++) {
        ADDR_PTR addr = (ADDR_PTR) (state->buffer + CACHE_LINESIZE * i);
        if (get_L3_cache_set_index(addr) == state->cache_region) {
            append_string_to_linked_list(&state->probing_set, addr);
            probing_set_size++;
        }
        // restrict the probing set to CACHE_WAYS_L1 to aviod self eviction
        if (probing_set_size >= CACHE_WAYS_L1) break;
    }
    printf("Found probing_set size of %u\n", probing_set_size);

}

/*
 * Detects a bit by repeatedly measuring the access time of the addresses in the
 * probing set and counting the number of misses for the clock length of state->interval.
 *
 * If the the first_bit argument is true, relax the strict definition of "one" and try to
 * sync with the sender.
 */
bool detect_bit(const struct state *state, bool first_bit)
{
    clock_t start_t, curr_t;
    start_t = clock();
    curr_t = start_t;

    int misses = 0;
    int hits = 0;
    int total_measurements = 0;

    // This is high because the misses caused by clflush
    // usually cause an access time larger than 150 cycles
    int misses_time_threshold = 150;

    while ((curr_t - start_t) < state->interval) {
        struct Node *current = state->probing_set;
        while (current != NULL && (curr_t - start_t) < state->interval) {
            ADDR_PTR addr = current->addr;
            CYCLES time = measure_one_block_access_time(addr);

            // When the access time is larger than 1000 cycles,
            // it is usually due to a disk miss. We exclude such misses
            // because they are not caused by clflush.
            // if (time > misses_time_threshold && time < 1000)
            //     printf("measure time %u\n", time);
            if (time < 1000) {
                total_measurements++;
                if (time > misses_time_threshold) {
                    misses++;
                } else {
                    hits++;
                }
            }

            curr_t = clock();
            current = current->next;

            // Busy loop to give time to the sender to flush the cache
            for (int junk = 0; junk < state->wait_period_between_measurements &&
                               (curr_t - start_t) < state->interval; junk++) {
                curr_t = clock();
            }
        }
    }

    // This is what allows the receiver to synchronize with the sender.
    // It only happens if the first_bit flag is true.
    //
    // We start by computing a relaxed miss_threshold.
    // Heuristically, this is about 13% of the measurements.
    double miss_threshold = (float) total_measurements / 8.0;

    // Then, if the number of misses is higher than this miss_threshold
    if (first_bit && misses > miss_threshold) {
        start_t = clock();

        // We compute the number of misses that we instead would expect
        // to count when the sender is flushing.
        // Heuristically, this is about 83% of the measurements.
        double expected_for_a_one = (float) total_measurements / 1.2;

        // Then we compute of what fraction of 1 interval we are
        // out of sync compared to the sender
        double ratio = (1 - misses / expected_for_a_one);

        // Finally, we wait for that fraction of interval so that we
        // know that we will be synced at the start of the next interval.
        //
        // This is easier to understand on a whiteboard, but here is a
        // drawing attempt:
        //
        // Sender clock:
        //     |           |           |           |
        //
        // Receiver clock:
        // |           |   .       |   |           |
        //                   ⌙ here the receiver is out of sync
        //
        // However, he can count the ratio of misses got versus misses expected,
        // and then wait at the end of the cycle for an amount of time equal to the
        // one between the dot '.' and the '|' that precedes it in the drawing.
        double waiting_time = state->interval * ratio;
        while (clock() - start_t < waiting_time) {}
        if (state->debug) {
            printf("\n[Synchronized forward of %.2f]\n", ratio);
        }
    }

    // If first_bit is true, use the relaxed threshold of misses to identify a 1.
    // Otherwise, consider a one only when number of misses is more than half of the
    // total measurements.
    bool ret = ((first_bit && misses > miss_threshold) ||
                (!first_bit && misses > (float) total_measurements / 2.0));

    if (state->debug) {
        printf("Misses: %d out of %d --> %d\n", misses, total_measurements, ret);
    }

    return ret;
}

// This is the only hardcoded variable which defines the max size of a message
// to be the same as the max size of the message in the starter code of the sender.
static const int max_buffer_len = 128 * 8;

int main(int argc, char **argv)
{
    // Initialize state and local variables
    struct state state;
    init_state(&state, argc, argv);
    char msg_ch[max_buffer_len + 1];
    int flip_sequence = 4;
    bool first_time = true;
    bool current;
    bool previous = true;

    printf("Press enter to begin listening ");
    getchar();
    while (1) {
        current = detect_bit(&state, first_time);

        // This receiving loop is a sort of finite state machine.
        // Once again, it would be easier to explain how it works
        // in a whiteboard, but here is an attempt to give an idea:
        //
        // Starting from the base state, it first looks for a sequence
        // of bits of the form "1010" (ref: flip_sequence variable).
        //
        // The first 1 is used to synchronize, the following ones are
        // used to make sure that the synchronization was right.
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
        // message receiving mode and restarts from the base state.
        if (flip_sequence == 0 && current == 1 && previous == 1) {
            if (state.debug) {
                printf("Start sequence fully detected.\n\n");
            }

            int binary_msg_len = 0;
            int strike_zeros = 0;
            for (int i = 0; i < max_buffer_len; i++) {
                binary_msg_len++;

                if (detect_bit(&state, first_time)) {
                    msg_ch[i] = '1';
                    strike_zeros = 0;

                } else {
                    msg_ch[i] = '0';
                    if (++strike_zeros >= 8 && i % 8 == 0) {
                        if (state.debug) {
                            printf("String finished\n");
                        }

                        break;
                    }
                }
            }

            msg_ch[binary_msg_len - 8] = '\0';
            if (state.debug) {
                printf("Binary string received %s\n", msg_ch);
            }

            int ascii_msg_len = binary_msg_len / 8;
            char msg[ascii_msg_len];
            printf("> %s\n", conv_char(msg_ch, ascii_msg_len, msg));
            if (strcmp(msg, "exit") == 0) {
                break;
            }

        } else if (flip_sequence > 0 && current != previous) {
            flip_sequence--;
            first_time = false;

        } else if (current == previous) {
            flip_sequence = 4;
            first_time = true;
        }

        previous = current;
    }

    printf("Receiver finished\n");
    return 0;
}
