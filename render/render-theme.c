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

#include "render-theme.h"
#include "render-core.h"

/**
 * set_doc_theme - Set the theme for Emacs Reader
 * @foreground: The color to be set for the foreground property of the image.
 * @background: The color to be set for the background property of the image.
 *
 * Both of them should be strings like "white" or a hexadecimal value for the
 * color.
 */

emacs_value set_doc_theme(emacs_env *env, ptrdiff_t nargs, emacs_value *args,
                          void *data) {
  (void)data;
  (void)nargs;
  DocState *state = get_doc_state_ptr(env);

  size_t foreground_ln = 0;
  size_t background_ln = 0;

  if (state) {
    elisp_2_c_str(env, args[0], &state->svg_foreground, &foreground_ln);
    elisp_2_c_str(env, args[1], &state->svg_background, &background_ln);
  }

  emacs_value current_image_data = svg2elisp_image(
      env, state, state->current_svg_data, state->current_svg_size);

  render_pages(state, state->current_page_number);

  // Render the themed page on the buffer’s overlay
  emacs_value current_svg_overlay = get_current_svg_overlay(env);
  emacs_value overlay_put_args[3] = {
      current_svg_overlay, env->intern(env, "display"), current_image_data};
  env->funcall(env, env->intern(env, "overlay-put"), 3, overlay_put_args);

  return EMACS_T;
}
