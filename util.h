
// You may only use fgets() to pull input from stdin
// You may use any print function to stdout to print 
// out chat messages
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <getopt.h>

// You may use memory allocators and helper functions 
// (e.g., rand()).  You may not use system().
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>

#ifndef UTIL_H_
#define UTIL_H_

#define ADDR_PTR uint64_t
#define CYCLES uint32_t

CYCLES measure_one_block_access_time(ADDR_PTR addr);
void clflush(ADDR_PTR addr);
int ipow(int base, int exp);
char *string_to_binary(char *s);
char *conv_char(char *data, int size, char *msg);

// L1 properties
static const int CACHE_SETS_L1 = 64;
static const int CACHE_WAYS_L1 = 8;

// L3 properties
static const int CACHE_SETS_L3 = 8192;
static const int CACHE_WAYS_L3 = 16;
static const int CACHE_SLICES_L3 = 8;

#endif
