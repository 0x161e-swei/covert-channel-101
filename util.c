
#include "util.h"

/* Measure the time it takes to access a block with virtual address addr. */
CYCLES measure_one_block_access_time(ADDR_PTR addr)
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

int ipow(int base, int exp)
{
	int result = 1;
	while (exp)
	{
		if (exp & 1)
			result *= base;
		exp >>= 1;
		base *= base;
	}

	return result;
}

void clflush(ADDR_PTR addr)
{
	asm volatile ("clflush (%0)" :: "r"(addr));
}

/*
 * Convert a given ASCII string to a binary string.
 * From:
 * https://stackoverflow.com/questions/41384262/convert-string-to-binary-in-c
 */
char *string_to_binary(char *s) {
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
char *conv_char(char *data, int size, char *msg) {
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