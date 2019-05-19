
// You may only use fgets() to pull input from stdin
// You may use any print function to stdout to print
// out chat messages
//
// You may use memory allocators and helper functions
// (e.g., rand()).  You may not use system().
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

#ifndef UTIL_H_
#define UTIL_H_

#define ADDR_PTR uint64_t
#define CYCLES uint32_t

struct Node {
    ADDR_PTR addr;
    struct Node *next;
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

void printPID();

int ipow(int base, int exp);

char *string_to_binary(char *s);

char *conv_char(char *data, int size, char *msg);

uint64_t get_cache_set_index(ADDR_PTR phys_addr);
uint64_t get_L1_cache_set_index(ADDR_PTR phys_addr);

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
// Machine configuration
// =======================================

// Cache
#define CACHE_LINESIZE      64
#define LOG_CACHE_LINESIZE  6

// L1
#define CACHE_SETS_L1       64
#define LOG_CACHE_SETS_L1   6
#define CACHE_WAYS_L1       8

// LLC
#define CACHE_SETS_L3       8192
#define LOG_CACHE_SETS_L3   13
#define CACHE_WAYS_L3       16
#define CACHE_SLICES_L3     8


#endif
