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

#include "elisp-helpers.h"
#include "misc-helpers.h"

/*
Draws a highlight on the PPM.
Takes a plist as input.
 */

emacs_value
emacs_draw_highlight(emacs_env *env, ptrdiff_t nargs, emacs_value args[],
		     void *data)
{
	// unpack args
	emacs_value plist = args[0];
	int rx = env->extract_integer(env, args[1]);
	int ry = env->extract_integer(env, args[2]);
	int rw = env->extract_integer(env, args[3]);
	int rh = env->extract_integer(env, args[4]);
	double alpha = env->extract_float(env, args[5]);
	char *hex;
	ptrdiff_t hex_size;
	if (!elisp_2_c_str(env, args[6], &hex, &hex_size))
		fprintf(stderr, "failed to convert hex string\n");
	if (*hex == '#')
		memmove(hex, hex + 1, 7);

	// extract image data
	int width = env->extract_integer(
	    env,
	    env->funcall(env, env->intern(env, "plist-get"), 2,
			 (emacs_value[]){ plist, env->intern(env, ":width") }));
	int height = env->extract_integer(
	    env, env->funcall(
		     env, env->intern(env, "plist-get"), 2,
		     (emacs_value[]){ plist, env->intern(env, ":height") }));
	int maxval = env->extract_integer(
	    env, env->funcall(
		     env, env->intern(env, "plist-get"), 2,
		     (emacs_value[]){ plist, env->intern(env, ":maxval") }));
	emacs_value data_str
	    = env->funcall(env, env->intern(env, "plist-get"), 2,
			   (emacs_value[]){ plist, env->intern(env, ":data") });
	ptrdiff_t nbytes;
	char *bytes;
	if (!elisp_2_c_str(env, data_str, &bytes, &nbytes))
		fprintf(stderr, "failed to convert ppm binary data\n");

	// compute overlay color
	int hr = parse_hex_byte(hex + 0);
	int hg = parse_hex_byte(hex + 2);
	int hb = parse_hex_byte(hex + 4);
	int ov_r = (hr * maxval + 127) / 255;
	int ov_g = (hg * maxval + 127) / 255;
	int ov_b = (hb * maxval + 127) / 255;

	// bounds
	int x0 = rx < 0 ? 0 : rx, y0 = ry < 0 ? 0 : ry;
	int x1 = rx + rw > width ? width : rx + rw,
	    y1 = ry + rh > height ? height : ry + rh;

	for (int y = y0; y < y1; y++)
	{
		for (int x = x0; x < x1; x++)
		{
			size_t idx = (size_t)y * width + x;
			unsigned char *px = (unsigned char *)bytes + 3 * idx;
			int orr = px[0], org = px[1], orb = px[2];
			int nr
			    = (int)((1.0 - alpha) * orr + alpha * ov_r + 0.5);
			int ng
			    = (int)((1.0 - alpha) * org + alpha * ov_g + 0.5);
			int nb
			    = (int)((1.0 - alpha) * orb + alpha * ov_b + 0.5);
			px[0] = clamp(nr, maxval);
			px[1] = clamp(ng, maxval);
			px[2] = clamp(nb, maxval);
		}
	}

	fprintf(stderr, "Selection:\nx: %d, y: %d\n", rx, ry);
	// return updated plist: replace :data
	emacs_value new_data = env->make_unibyte_string(env, bytes, nbytes);
	env->funcall(
	    env, env->intern(env, "plist-put"), 3,
	    (emacs_value[]){ plist, env->intern(env, ":data"), new_data });
	return plist;
}
