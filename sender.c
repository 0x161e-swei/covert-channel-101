#include "util.h"

struct state {
    char *buffer;
    int step_set;
    int step_line;
    int interval;
    bool debug;
};

void init_state(struct state *state, int argc, char **argv) {
    int n = CACHE_WAYS_L3;
    int o = 6;                          // log_2(64), where 64 is the line size
    int s = 13;                         // log_2(8192), where 8192 is the number of cache sets
    int two_o = ipow(2, o);             // 64
    int two_o_s = ipow(2, s) * two_o;   // 524,288
    int b = n * two_o_s;                // size in bytes of the LLC

    state->step_set = two_o;
    state->step_line = two_o_s;
    state->buffer = malloc((size_t) b);

    // Set some default values; need to be tuned up
    state->interval = 9200;
    state->debug = false;

    int option;
    while ((option = getopt(argc, argv, "di:w:")) != -1) {
        switch (option) {
            case 'd':
                state->debug = true;
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

    if (!one) {
        while ((curr_t - start_t) < state->interval) {

            /* i is the set index */
            for (int i = 0; i < CACHE_SETS_L3 && (curr_t - start_t) < state->interval; i++) {

                /* j is the line index */
                for (int j = 0; j < CACHE_WAYS_L3 && (curr_t - start_t) < state->interval; j++) {

                    curr_t = clock();
                    clflush((ADDR_PTR) (state->buffer + i * state->step_set + j * state->step_line));
                }
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

        // Let the receiver detect that
        // I am about to send a string and sync
        send_bit(false, &state);
        send_bit(true, &state);

        send_bit(false, &state);
        send_bit(true, &state);

        send_bit(false, &state);
        send_bit(true, &state);

        send_bit(false, &state);
        send_bit(true, &state);

        send_bit(false, &state);
        send_bit(false, &state);

        // Send the string
        for (int ind = 0; ind < msg_len; ind++) {

            if (msg[ind] == '0') {
                send_bit(false, &state);

            } else {
                send_bit(true, &state);
            }
        }

    }

    printf("Sender finished\n");
    return 0;
}
