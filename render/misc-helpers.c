//
// Copyright (C) 2025  Divya Ranjan Pattanaik
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

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
