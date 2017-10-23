#include "util.h"

struct state {
    char *buffer;
    struct Node *eviction_set;
    int interval;
    bool debug;
    bool benchmark_mode;
};

void init_state(struct state *state, int argc, char **argv) {

    // The following calculations are based on the paper:
    // C5: Cross-Cores Cache Covert Channel (dimva 2015)
    int n = CACHE_WAYS_L3;
    int o = 6;                          // log_2(64), where 64 is the line size
    int s = 13;                         // log_2(8192), where 8192 is the number of cache sets
    int two_o = ipow(2, o);             // 64
    int two_o_s = ipow(2, s) * two_o;   // 524,288
    int b = n * two_o_s;                // size in bytes of the LLC

    state->buffer = malloc((size_t) b);
    state->eviction_set = NULL;

    // i is the set index
    for (int i = 0; i < CACHE_SETS_L3; i++) {

        // j is the line index
        for (int j = 0; j < CACHE_WAYS_L3; j++) {

            ADDR_PTR addr = (ADDR_PTR) (state->buffer + i * two_o + j * two_o_s);
            if (get_cache_set_index(addr) == 0x0) {
                append_string_to_linked_list(&state->eviction_set, addr);
            }
        }
    }

    // Set some default values; need to be tuned up
    state->interval = 160;
    state->debug = false;
    state->benchmark_mode = false;

    int option;
    while ((option = getopt(argc, argv, "di:w:b")) != -1) {
        switch (option) {
            case 'd':
                state->debug = true;
                break;
            case 'b':
                state->benchmark_mode = true;
                break;
            case 'i':
                state->interval = atoi(optarg);
                break;
            case '?':
                fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
                exit(1);
            default:
                exit(1);
        }
    }
}

void send_bit(bool one, struct state *state) {
    clock_t start_t, curr_t;

    start_t = clock();
    curr_t = start_t;

    if (one) {
        while ((curr_t - start_t) < state->interval) {
            struct Node *current = state->eviction_set;
            while (current != NULL && (curr_t - start_t) < state->interval) {
                ADDR_PTR addr = current->addr;
                clflush(addr);

                curr_t = clock();
                current = current->next;
            }
        }

    } else {
        start_t = clock();
        while (clock() - start_t < state->interval) {}
    }
}

int main(int argc, char **argv) {
    // Setup code
    struct state state;
    init_state(&state, argc, argv);

    int sending = 1;
    clock_t start_t, end_t;

    printf("Please type a message.\n");
    while (sending) {
        printf("< ");
        char text_buf[128];

        fgets(text_buf, sizeof(text_buf), stdin);

        char *msg = string_to_binary(text_buf);
        if (state.debug) {
            printf("%s\n", msg);
        }

        size_t msg_len = strlen(msg);

        if (state.benchmark_mode == true) {
            start_t = clock();
        }

        // Let the receiver detect that
        // I am about to send a start string and sync
        for (int i = 0; i < 8; i++) {
            send_bit(i % 2 == 0, &state);
        }
        send_bit(true, &state);
        send_bit(true, &state);

        // Send the message
        for (int ind = 0; ind < msg_len; ind++) {
            if (msg[ind] == '0') {
                send_bit(false, &state);
            } else {
                send_bit(true, &state);
            }
        }

        if (state.benchmark_mode == true) {
            end_t = clock();
            printf("Bitrate: %.2f Bytes/second\n", ((double) strlen(text_buf)) / ((double) (end_t - start_t) / CLOCKS_PER_SEC));
        }
    }

    printf("Sender finished\n");
    return 0;
}
