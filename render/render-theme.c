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
#include "elisp-helpers.h"
#include "render-core.h"
#include "render-threads.h"

/**
 * Toggle dark theme rendering for the current document page.
 *
 * Inverts the `invert` flag in the DocState, re-renders the current
 * page in a background thread, waits for it to finish, and updates
 * the Emacs overlay with the new image.
 *
 * @param env    Emacs module environment.
 * @param nargs  Argument count (unused).
 * @param args   Argument values (unused).
 * @param data   Additional data (unused).
 * Return:       EMACS_T on success, EMACS_NIL if no DocState is available.
 */

emacs_value
emacs_set_dark_theme(emacs_env *env, ptrdiff_t nargs, emacs_value *args,
		     void *data)
{
	(void)data;
	(void)nargs;
	(void)args;
	DocState *doc_state = get_doc_state_ptr(env);
	emacs_value current_doc_overlay = get_current_doc_overlay(env);
	EmacsWinState *win_state = get_win_state_ptr(env, current_doc_overlay);
	if (doc_state && win_state)
	{
		doc_state->invert ^= 1;
		CachedPage *cp = win_state->current_cached_page;
		DrawThreadArgs *draw_args = malloc(sizeof(DrawThreadArgs));
		draw_args->doc_state = doc_state;
		draw_args->win_state = win_state;
		draw_args->cp = cp;
		submit_job(draw_page_thread, draw_args, &g_thread_pool);

		// Wait for the thread to signal before displaying
		pthread_mutex_lock(&cp->mutex);
		pthread_cond_wait(&cp->cond, &cp->mutex);
		pthread_mutex_unlock(&cp->mutex);

		display_img_to_overlay(env, win_state, cp->img_data,
				       cp->img_size, current_doc_overlay);
	}
	else
	{
		return EMACS_NIL;
	}

	return EMACS_T;
}
