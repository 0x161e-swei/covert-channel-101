
#include "util.h"

/* Measure the time it takes to access a block with virtual address addr. */
extern inline CYCLES measure_one_block_access_time(ADDR_PTR addr);

/*
 * CLFlushes the given address.
 */
extern inline void clflush(ADDR_PTR addr);

/*
 * Computes base to the exp.
 */
int ipow(int base, int exp)
{
    int result = 1;
    while (exp) {
        if (exp & 1)
            result *= base;
        exp >>= 1;
        base *= base;
    }

    return result;
}

/*
 * Returns the 6 bits used index L1 cache sets of a given address.
 */
uint64_t get_L1_cache_set_index(ADDR_PTR virt_addr)
{
    return (virt_addr >> LOG_CACHE_LINESIZE) & CACHE_SETS_L1_MASK;
}

uint64_t get_L3_cache_set_index(ADDR_PTR virt_addr)
{
    return (virt_addr >> LOG_CACHE_LINESIZE) & CACHE_SETS_L3_MASK;
}

/*
 * Returns the 15 bits used cache index of a given virtual address,
 */
uint64_t get_hugepage_cache_set_index(ADDR_PTR virt_addr)
{
    return (virt_addr & HUGEPAGE_MASK) >> LOG_CACHE_LINESIZE;
}

/*
 * Returns the 10 bits cache set index of a given address.
 */
uint64_t get_cache_set_index(ADDR_PTR phys_addr)
{
    uint64_t mask = ((uint64_t) 1 << 16) - 1;
    return (phys_addr & mask) >> LOG_CACHE_LINESIZE;
}

/*
 * Allocate a buffer of the size as passed-in
 * returns the pointer to the buffer
 */
void *allocateBuffer(uint64_t size) {
    void *buffer = MAP_FAILED;
#ifdef HUGEPAGES
    buffer = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE|HUGEPAGES, -1, 0);
#endif

    if (buffer == MAP_FAILED) {
        fprintf(stderr, "allocating non-hugepages\n");
        buffer = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE, -1, 0);
    }
    if (buffer == MAP_FAILED) {
        fprintf(stderr, "Failed to allocate buffer!\n");
        exit(-1);
    }

    return buffer;
}

/*
 * Convert a given ASCII string to a binary string.
 * From:
 * https://stackoverflow.com/questions/41384262/convert-string-to-binary-in-c
 */
char *string_to_binary(char *s)
{
    if (s == NULL) return 0; /* no input string */

    size_t len = strlen(s) - 1;

    // Each char is one byte (8 bits) and + 1 at the end for null terminator
    char *binary = malloc(len * 8 + 1);
    binary[0] = '\0';

    for (size_t i = 0; i < len; ++i) {
        char ch = s[i];
        for (int j = 7; j >= 0; --j) {
            if (ch & (1 << j)) {
                strcat(binary, "1");
            } else {
                strcat(binary, "0");
            }
        }
    }

    return binary;
}

/*
 * Convert 8 bit data stream into character and return
 */
char *conv_char(char *data, int size, char *msg)
{
    for (int i = 0; i < size; i++) {
        char tmp[8];
        int k = 0;

        for (int j = i * 8; j < ((i + 1) * 8); j++) {
            tmp[k++] = data[j];
        }

        char tm = strtol(tmp, 0, 2);
        msg[i] = tm;
    }

    msg[size] = '\0';
    return msg;
}

/*
 * Appends the given string to the linked list which is pointed to by the given head
 */
void append_string_to_linked_list(struct Node **head, ADDR_PTR addr)
{
    struct Node *current = *head;

    // Create the new node to append to the linked list
    struct Node *new_node = malloc(sizeof(*new_node));
    new_node->addr = addr;
    new_node->next = NULL;

    // If the linked list is empty, just make the head to be this new node
    if (current == NULL)
        *head = new_node;

        // Otherwise, go till the last node and append the new node after it
    else {
        while (current->next != NULL)
            current = current->next;

        current->next = new_node;
    }
}

uint64_t printPID() {
    uint64_t pid = getpid();
    printf("Process ID: %lu\n", pid);
    return pid;
}

uint64_t getTime() {
    // can be a choice of channel?
    return rdtsc();
}

