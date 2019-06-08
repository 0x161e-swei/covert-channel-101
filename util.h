#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>

#ifndef UTIL_H_
#define UTIL_H_

#ifdef DEBUG
#define debug(...) do{			\
	fprintf(stdout, ##__VA_ARGS__); \
} while(0)
#else
#define debug(...)
#endif

#ifdef MAP_HUGETLB
#define HUGEPAGES MAP_HUGETLB
#else
#define HUGEPAGES 0
#endif

#define ADDR_PTR uint64_t
#define CYCLES uint32_t

typedef enum _channel {
    PrimeProbe,
    FlushReload
} Channel;

struct Node {
    ADDR_PTR addr;
    struct Node *next;
};

/*
 * Execution state of the program, with the variables
 * that we need to pass around the various functions.
 */
struct state {
    char *buffer;
    struct Node *addr_set;
    int interval;
    uint64_t cache_region;
    bool benchmark_mode; // sender only
    int wait_period_between_measurements; // receiver only
    Channel channel;
};

inline CYCLES measure_one_block_access_time(ADDR_PTR addr)
{
    CYCLES cycles;

    asm volatile("mov %1, %%r8\n\t"
            "lfence\n\t"
            "rdtsc\n\t"
            "mov %%eax, %%edi\n\t"
            "mov (%%r8), %%r8\n\t"
            "lfence\n\t"
            "rdtsc\n\t"
            "sub %%edi, %%eax\n\t"
    : "=a"(cycles) /*output*/
    : "r"(addr)
    : "r8", "edi");

    return cycles;
}

inline void clflush(ADDR_PTR addr) {
    asm volatile ("clflush (%0)"::"r"(addr));
}

uint64_t printPID();

int ipow(int base, int exp);

char *string_to_binary(char *s);

char *conv_char(char *data, int size, char *msg);

uint64_t get_cache_set_index(ADDR_PTR phys_addr);
uint64_t get_hugepage_cache_set_index(ADDR_PTR virt_addr);
uint64_t get_L1_cache_set_index(ADDR_PTR virt_addr);
uint64_t get_L3_cache_set_index(ADDR_PTR virt_addr);
void *allocateBuffer(uint64_t size);

void append_string_to_linked_list(struct Node **head, ADDR_PTR addr);

// L1 properties
// static const int CACHE_SETS_L1 = 64;
// static const int CACHE_WAYS_L1 = 8;
//
// L3 properties
// static const int CACHE_SETS_L3 = 8192;
// static const int CACHE_WAYS_L3 = 16;
// static const int CACHE_SLICES_L3 = 8;


// =======================================
// Machine Configuration
// =======================================

// Hugepage
#define HUGEPAGE_BITS 21
#define HUGEPAGE_SIZE (1 << HUGEPAGE_BITS)
#define HUGEPAGE_MASK (HUGEPAGE_SIZE - 1)

// Cache
#define CACHE_LINESIZE      64
#define LOG_CACHE_LINESIZE  6

// L1
#define LOG_CACHE_SETS_L1   6
#define CACHE_SETS_L1       64
#define CACHE_SETS_L1_MASK  (CACHE_SETS_L1 - 1)
#define CACHE_WAYS_L1       8

// LLC

#define LOG_CACHE_SETS_L3   15
#define CACHE_SETS_L3       32768
#define CACHE_SETS_L3_MASK  (CACHE_SETS_L3 - 1)
#define CACHE_WAYS_L3       20
#define CACHE_SLICES_L3     8

// =======================================
// Covert Channel Default Configuration
// =======================================


#define CHANNEL_DEFAULT_INTERVAL    200
#define CHANNEL_DEFAULT_REGION      0x0
#define CHANNEL_DEFAULT_WAIT_PERIOD 80

#endif
