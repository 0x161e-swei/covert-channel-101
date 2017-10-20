#include "util.h"
#include "cache_utils.c"

struct state {
    char *buffer;
    int step;
    int iterations;
    int wait_cycles_between_measurements;
    bool debug;
};

bool detect_bit(struct state *state) {
  clock_t start_t, end_t, total_t;
  start_t = clock();
  unsigned long long misses = 0;
  unsigned long long hits = 0;

  for (int k = 0; k < state->iterations; k++) {
    for (int i = 0; i < CACHE_WAYS; i++) {
      CYCLES time = measure_one_block_access_time((ADDR_PTR) (state->buffer + state->step * i));
      // printf("%" PRIx64 "\n", (uint64_t) (buffer + two_o_s * i));
      // printf("Time %d: %d\n", i, time);
      if (time > 80) {
	misses++;
      } else {
	hits++;
      }
    }
    /* Busy loop to give time to the sender */
    /* The number of cycles of this loop are currently */
    /* decided by the program first argument. */
    for (int junk = 0; junk < state->wait_cycles_between_measurements; junk++) {}
  }
  if (state->debug) {
    printf("Hits: %lld\n", hits);
    printf("Misses: %lld\n", misses);
  }
  
  if (state->wait_cycles_between_measurements > 3000 &&  state->debug){
    end_t = clock();
    total_t = (end_t - start_t);
    printf("\n Total time taken by CPU: %ld\n", total_t);
  }
  // Consider a 1 when more than 1/20 of the accesses were cache misses
    // Fine tune divisor
  return (misses < (state->iterations * CACHE_WAYS) / 40);
}

/* convert 8 bit datastream into character and return */
char* conv_char(char *data, int size,char *msg){
  for (int i = 0; i<size;i++){
    char tmp[8];
    int k=0;
    for (int j = i*8; j< ((i+1)*8) ; j++){
      tmp[k++] = data[j];
    }
    char tm=strtol(tmp,0,2);
    /* printf("wajih %s = %c \n",tmp,tm); */
    msg[i] = tm;
  }
  msg[size]='\0';
  return msg;
}

void init_state(struct state *state, int argc, char **argv) {
    int n = CACHE_WAYS;
    int o = 6;              // log_2(64), where 64 is the line size
    int s = 6;              // log_2(64), where 64 is the number of cache sets

    int two_o_s = ipow(2, o + s);       // 4096
    int b = n * two_o_s;                // 32,768

    state->step = two_o_s;
    state->buffer = malloc((size_t) b);

    // Set some default values; need to be tuned up
    state->iterations = 100;
    state->wait_cycles_between_measurements = 45000;
    state->debug = false;
      
    int option;
    while ((option = getopt(argc, argv, "di:w:")) != -1) {
        switch (option) {
            case 'd':
                state->debug = true;
                break;
            case 'i':
                state->iterations = atoi(optarg);
                break;
            case 'w':
                state->wait_cycles_between_measurements = atoi(optarg);
                break;
            case '?':
                fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
                exit(1);
            default:
                exit(1);
        }
    }
}

int main(int argc, char **argv) {
  // TODO: Hard coding some stuff for now
  int max_buffer_len=1000;
  int binary_msg_len=0;
  
  // Setup code
  struct state state;
  init_state(&state, argc, argv);

  printf("Press enter to begin listening.\n");
  char text_buf[128];
  fgets(text_buf, sizeof(text_buf), stdin);
  char  msg_ch[max_buffer_len + 1];
  while (1) {
    /* char text_buf[128]; */
    /* fgets(text_buf, sizeof(text_buf), stdin); */
    // Small start bit detection small enough such
    state.wait_cycles_between_measurements = 1000;
    if (detect_bit(&state)){
      /* printf("One detected.\n"); */
    }else{
      if (state.debug) {	
	printf("Start bit detected.\n");
      }
      state.wait_cycles_between_measurements = 45000;
      for (int i = 0; i < max_buffer_len ; i++){
	if (detect_bit(&state)){
	  /* printf(" One "); */
	  msg_ch[i] = '1';
	  if (i%8==0){
	    printf("\n Stop bit stopped\n");
	    break;
	  }
	  binary_msg_len++;
	}else{
	  /* printf(" Zero "); */
	  msg_ch[i] = '0';
	  binary_msg_len++;
	}
      }
      break;
    }
  }
  msg_ch[binary_msg_len] = '\0';
  int ascii_msg_len = binary_msg_len/8;
  char msg[ascii_msg_len+1];
  printf("Binary string received %s\n", msg_ch);
  printf("Message received = %s \n", conv_char(msg_ch, ascii_msg_len, msg));
  return 0;
}
