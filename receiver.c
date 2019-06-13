#include "util.h"

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
    uint64_t bsize = 256 * CACHE_WAYS_L1 * L1_way_stride; // 64 * 8 * 4k = 2M

    // Allocate a buffer twice the size of the L1 cache
    state->buffer = allocateBuffer(bsize);

    printf("buffer pointer addr %p\n", state->buffer);
    // Initialize the buffer to be be the non-zero page
    for (uint32_t i = 0; i < bsize; i += 64) {
        *(state->buffer + i) = pid;
    }

    // Set some default state values.
    state->addr_set = NULL;

    // These numbers may need to be tuned up to the specific machine in use
    // NOTE: Make sure that interval is the same in both sender and receiver
    state->cache_region = CHANNEL_DEFAULT_REGION;
    state->interval = CHANNEL_DEFAULT_INTERVAL;
    state->access_period = CHANNEL_DEFAULT_ACCESS_PERIOD;
    state->prime_period = 0; // default is half of (interval - access_period)

    // Parse the command line flags
    //      -d is used to enable the debug prints
    //      -i is used to specify a custom value for the time interval
    //      -w is used to specify a custom number of wait time between two probes
    int option;
    while ((option = getopt(argc, argv, "di:a:r:")) != -1) {
        switch (option) {
            case 'i':
                state->interval = atoi(optarg);
                break;
            case 'r':
                state->cache_region = atoi(optarg);
                break;
            case 'a':
                state->access_period = atoi(optarg);
                break;
            case '?':
                fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
                exit(1);
            default:
                exit(1);
        }
    }

    if (state->prime_period == 0) {
        state->prime_period = (state->interval - state->access_period) / 2;
        state->probe_period = (state->interval - state->access_period) / 2;
    }
    else {
        state->probe_period = (state->interval - state->prime_period - state->access_period);
    }
    // debug("prime %u access %u probe %u\n", state->prime_period, state->access_period, state->probe_period);

    // Construct the addr_set by taking the addresses that have cache set index 0
    // There will be at least one of such addresses in our buffer.
    uint32_t addr_set_size = 0;
    for (int i = 0; i < 256 * CACHE_WAYS_L1 * CACHE_SETS_L1; i++) {
        ADDR_PTR addr = (ADDR_PTR) (state->buffer + CACHE_LINESIZE * i);
        if (get_L3_cache_set_index(addr) == state->cache_region) {
            append_string_to_linked_list(&state->addr_set, addr);
            addr_set_size++;
        }
        // restrict the probing set to CACHE_WAYS_L1 to aviod self eviction
        if (addr_set_size >= CACHE_WAYS_L1) break;
    }

    printf("Found addr_set size of %u\n", addr_set_size);

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
    uint64_t start_t, curr_t;

    start_t = getTime();
    curr_t = start_t;

    int misses = 0;
    int hits = 0;
    int total_measurements = 0;

    // miss in L3
    int misses_time_threshold = 200;
    struct Node *current = NULL;

    if ((getTime() - start_t) < state->interval) {
        // prime
        uint64_t prime_count = 0;
        do {
            current = state->addr_set;
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
        } while ((getTime() - start_t) < state->prime_period);

        // wait for sender to access
        while (getTime() - start_t < (state->prime_period + state->access_period)) {}

        // probe
        current = state->addr_set;
        while (current != NULL && (getTime() - start_t) < state->interval) {
            ADDR_PTR addr = current->addr;
            CYCLES time = measure_one_block_access_time(addr);

            // When the access time is larger than 1000 cycles,
            // it is usually due to a long-latency page walk.
            // We exclude such misses
            // because they are not caused by accesses from the sender.
            total_measurements += time < 600;
            misses  += (time < 600) && (time > misses_time_threshold);
            hits    += (time < 600) && (time <= misses_time_threshold);

            current = current->next;
        }

        while (getTime() - start_t < state->interval) {}
    }

    if (misses != 0) {
        debug("Misses: %d out of %d \n", misses, total_measurements);
    }

    return (misses > (float) total_measurements / 2)? 1: 0;
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

        // sync on clock edge
        while((getTime() & 0x003fffff) > 20000) {}
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
            debug("Start sequence fully detected.\n\n");

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
                        debug("String finished\n");
                        break;
                    }
                }
            }

            msg_ch[binary_msg_len - 8] = '\0';
            debug("Binary string received %s\n", msg_ch);

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
