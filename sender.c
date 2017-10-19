#include "util.h"
#include "cache_utils.c"

/* Taken from 
https://stackoverflow.com/questions/41384262/convert-string-to-binary-in-c */
char* stringToBinary(char* s) {
  if(s == NULL) return 0; /* no input string */
  size_t len = strlen(s);
  /* each char is one byte (8 bits) and + 1 at the end for null terminator */
  char *binary = malloc(len*8 + 1); 
  binary[0] = '\0';
  for(size_t i = 0; i < len; ++i) {
    char ch = s[i];
    for(int j = 7; j >= 0; --j){
      if(ch & (1 << j)) {
	strcat(binary,"1");
      } else {
	strcat(binary,"0");
      }
    }
  }
  return binary;
}

int main(int argc, char **argv)
{
  /* Begin Setup code */
  int n = cache_ways;
  int o = 6;              // log_2(64), where 64 is the line size
  int s = 13;             // log_2(8192), where 8192 is the number of cache sets
  int two_o = ipow(2, o);             // 64
  int two_o_s = ipow(2, s) * two_o;   // 524,288
  int b = n * two_o_s;    // size in bytes of the LLC
  /* End Setup code */
  char *buffer = malloc((size_t) b);
  int sending = 1;
  while (sending) {
    printf("Please type a message.\n");
    char text_buf[128];
    fgets(text_buf, sizeof(text_buf), stdin);
    char* msg = stringToBinary(text_buf);
#ifdef DEBUG
    printf(msg);
#endif
    size_t msg_len = strlen(msg);
    for (int ind =0 ; ind < msg_len ; ind++){
      if (msg[ind] == '1'){
#ifdef DEBUG
	printf("flushing\n");
#endif
	/* i is the set index */
	for (int i = 0; i < cache_sets; i++) {
	  /* j is the line index */
	  for (int j = 0; j < n; j++) {
	    clflush((ADDR_PTR) &buffer[i * two_o + j * two_o_s]);
	  }
	}
      }else{
#ifdef DEBUG	
	printf("Not flushing\n");
#endif
      }
      /* hardcoding for now */
      for (int junk = 0; junk < 45000; junk++) {}      
    }
  }
  
  printf("Sender finished :) \n");
  return 0;
}


