#include "misc-helpers.h"

unsigned char
clamp(int v, int max)
{
	if (v < 0)
		return 0;
	if (v > max)
		return max;
	return (unsigned char)v;
}

// parse two hex digits
int
parse_hex_byte(const char *s)
{
	int hi = isdigit(s[0]) ? s[0] - '0' : tolower(s[0]) - 'a' + 10;
	int lo = isdigit(s[1]) ? s[1] - '0' : tolower(s[1]) - 'a' + 10;
	return (hi << 4) | lo;
}
