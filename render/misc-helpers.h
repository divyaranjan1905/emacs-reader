#ifndef MISC_HELPERS_H
#define MISC_HELPERS_H

#include <ctype.h>

unsigned char
clamp(int v, int max);

int
parse_hex_byte(const char *s);

#endif
