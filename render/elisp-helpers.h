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

#ifndef EMACS_HELPERS_H
#define EMACS_HELPERS_H

#include "emacs-module.h"
#include "mupdf-helpers.h"
#include "render-core.h"
#include <stdbool.h>
#include <stddef.h>

#define EMACS_NIL env->intern(env, "nil")
#define EMACS_T env->intern(env, "t")
#define EMACS_CURR_WIN                                                         \
	env->funcall(env, env->intern(env, "selected-window"), 0, NULL);

bool
elisp_2_c_str(emacs_env *env, emacs_value value, char **buffer, size_t *size);
void
provide(emacs_env *env, const char *value);
void
register_module_func(emacs_env *env,
		     emacs_value (*module_func)(emacs_env *env, ptrdiff_t nargs,
						emacs_value *args, void *data),
		     char *symbol, int min_args, int max_args, char *docstring);
emacs_value
data2elisp_image(emacs_env *env, EmacsWinState *win_state, char *img_data,
		 size_t img_size);

DocState *
init_doc_state_ptr(emacs_env *env);
DocState *
get_doc_state_ptr(emacs_env *env);
EmacsWinState *
init_win_state_ptr(emacs_env *env, DocState *doc_state, emacs_value window);
EmacsWinState *
get_win_state_ptr(emacs_env *env, emacs_value overlay);
void
reset_win_state(EmacsWinState *win_state);
void
set_current_page_number(emacs_env *env, int page);
void
set_current_render_status(emacs_env *env);
void
set_current_pagecount(emacs_env *env, DocState *state);
emacs_value
init_overlay(emacs_env *env, emacs_value window);
bool
check_current_overlay(emacs_env *env, emacs_value overlay);
emacs_value
get_current_doc_overlay(emacs_env *env);
float
get_frame_scale_factor(emacs_env *env);
void
emacs_message(emacs_env *env, char *str);
void
permanent_buffer_local_var(emacs_env *env, char *symbol);
void
display_img_to_overlay(emacs_env *env, EmacsWinState *win_state, char *img_data,
		       size_t img_size, emacs_value buffer_overlay);
emacs_value
outline2plist(emacs_env *env, fz_outline *node);

#endif
