//
// Copyright (C) 2025  Divya Ranjan Pattanaik
// Copyright (C) 2025  Tushar
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

#include "render-core.h"
#include "elisp-helpers.h"
#include "emacs-module.h"
#include "mupdf-helpers.h"
#include "render-theme.h"
#include "render-threads.h"

int plugin_is_GPL_compatible;

/**
 * Load and cache the display list for a page.
 *
 * If the page is empty, sets its status to rendering, loads the page,
 * computes its bounding box, and generates a display list. Uses a
 * cloned MuPDF context for thread safety.
 *
 * @param state  Pointer to the current DocState.
 * @param cp     Pointer to the CachedPage to load.
 * Return:       EXIT_SUCCESS on success (errors are rethrown via MuPDF).
 */

int
load_page_dl(DocState *state, CachedPage *cp)
{
	fz_page *loaded_page = NULL;
	fz_context *ctx = fz_clone_context(state->ctx);

	fz_drop_display_list(ctx, cp->display_list);
	if (cp->status == PAGE_STATUS_EMPTY)
	{
		cp->status = PAGE_STATUS_RENDERING;
		fz_try(ctx)
		{
			loaded_page
			    = fz_load_page(ctx, state->doc, cp->page_num);
			state->page_bbox = fz_bound_page(ctx, loaded_page);
			cp->display_list = fz_new_display_list_from_page(
			    state->ctx, loaded_page);
		}
		fz_always(ctx)
		{
			fz_drop_page(state->ctx, loaded_page);
			loaded_page = NULL;
		}
		fz_catch(ctx) fz_rethrow(ctx);
	}

	fz_drop_context(ctx);
	return EXIT_SUCCESS;
}

/**
 * Thread function to render a page and encode it as a PNM image.
 *
 * Uses the page's display list to generate a pixmap, optionally inverts
 * and gamma-corrects it, then encodes it to PNM and stores it in
 * `cp->img_data'. Updates image dimensions and status, and signals
 * completion via a condition variable.
 *
 * @param arg  Pointer to DrawThreadArgs (contains DocState and CachedPage).
 * Return:     NULL (result is stored in CachedPage).
 */

void *
draw_page_thread(void *arg)
{
	fz_output *out = NULL;
	fz_buffer *buf = NULL;
	fz_matrix ctm;

	DrawThreadArgs *args = (DrawThreadArgs *)arg;
	DocState *doc_state = args->doc_state;
	EmacsWinState *win_state = args->win_state;
	CachedPage *cp = args->cp;

	fz_context *ctx = fz_clone_context(doc_state->ctx);

	ctm = fz_transform_page(doc_state->page_bbox, win_state->resolution,
				win_state->rotate);
	cp->imgh = 0;
	cp->imgw = 0;

	fz_try(ctx)
	{
		if (cp->display_list == NULL)
		{
			fprintf(stderr, "Display list of page %d is empty\n",
				cp->page_num);
		}
		cp->pixmap = fz_new_pixmap_from_display_list(
		    ctx, cp->display_list, ctm, fz_device_rgb(ctx), 0);
		if (doc_state->invert)
		{
			fz_invert_pixmap_luminance(ctx, cp->pixmap);
			fz_gamma_pixmap(ctx, cp->pixmap, 1 / 1.4f);
		}
		cp->imgh = fz_pixmap_height(ctx, cp->pixmap)
			   / doc_state->frame_scale;
		cp->imgw
		    = fz_pixmap_width(ctx, cp->pixmap) / doc_state->frame_scale;
	}
	fz_catch(ctx) cp->status = PAGE_STATUS_ERROR;

	fz_try(ctx)
	{
		buf = fz_new_buffer(ctx, 1024);
		out = fz_new_output_with_buffer(ctx, buf);
		fz_write_pixmap_as_pnm(ctx, out, cp->pixmap);
		fz_drop_pixmap(ctx,
			       cp->pixmap); // Drop the pixmap after using it
		cp->pixmap = NULL;
	}
	fz_catch(ctx)
	{
		fz_close_output(ctx, out);
		fz_drop_output(ctx, out);
		fz_drop_buffer(ctx, buf);
	}

	fz_close_output(ctx, out);

	// Reset the pre-existing memory that we were pointing to
	free(cp->img_data);
	cp->img_data = NULL;
	cp->img_size = 0;

	// Prepare for assigning data from fz_buffer
	cp->img_size = buf->len;
	cp->img_data = (char *)malloc(cp->img_size);

	if (cp->img_data == NULL)
	{
		fprintf(stderr, "Could not allocate memory for image data\n");
		fz_close_output(ctx, out);
		fz_drop_buffer(ctx, buf);
		return NULL;
	}

	// Copy the data and null-terminate
	memcpy(cp->img_data, buf->data, cp->img_size);

	fz_drop_output(ctx, out);
	fz_drop_buffer(ctx, buf);
	fz_drop_context(ctx);

	pthread_mutex_lock(&cp->mutex);
	cp->status = PAGE_STATUS_READY;
	pthread_cond_signal(&cp->cond);
	pthread_mutex_unlock(&cp->mutex);

	free(args);
	return NULL;
}

/**
 * Build a sliding cache window around the given page.
 *
 * Determines a window of pages centered around page `n', clears any
 * previously rendered pages, loads display lists for empty ones, and
 * submits rendering jobs. Waits for the current page to be fully cached.
 *
 * @param state  Pointer to the current DocState.
 * @param n      Page number to center the cache window around.
 */

void
build_cache_window(DocState *doc_state, EmacsWinState *win_state, int n)
{
	int start, end;
	int pagecount = doc_state->pagecount;

	if (n < MAX_CACHE_WINDOW_SIZE)
	{
		start = 0;
		end = n + MAX_CACHE_WINDOW_SIZE;
		if (end >= pagecount)
			end = pagecount - 1;
	}
	else if (n > (pagecount - 1) - MAX_CACHE_WINDOW_SIZE)
	{
		end = pagecount - 1;
		start = n - MAX_CACHE_WINDOW_SIZE;
		if (start < 0)
			start = 0;
	}
	else
	{
		start = n - MAX_CACHE_WINDOW_SIZE;
		end = n + MAX_CACHE_WINDOW_SIZE;
	}

	win_state->current_page_number = n;
	win_state->current_window_index = n - start;

	for (int i = 0; i < MAX_CACHE_SIZE; ++i)
	{
		int idx = start + i;
		if (idx < pagecount && idx <= end)
		{
			CachedPage *cp = doc_state->cached_pages_pool[idx];
			win_state->cache_window[i] = cp;

			if (cp->status == PAGE_STATUS_READY)
			{
				free_cached_page(doc_state,
						 win_state->cache_window[i]);
			}

			if (cp->status == PAGE_STATUS_EMPTY)
			{
				load_page_dl(doc_state, cp);
				DrawThreadArgs *draw_args
				    = malloc(sizeof(DrawThreadArgs));
				draw_args->doc_state = doc_state;
				draw_args->win_state = win_state;
				draw_args->cp = cp;
				submit_job(draw_page_thread, draw_args,
					   &g_thread_pool);
			}
		}
		else
		{
			win_state->cache_window[i] = NULL;
		}
	}

	// Wait for the current page's cache to be ready if it isn't
	while (win_state->cache_window[win_state->current_window_index]->status
	       != PAGE_STATUS_READY)
		fprintf(stderr, "Waiting for page %d to be ready\n",
			win_state->cache_window[win_state->current_window_index]
			    ->page_num);
	win_state->current_cached_page
	    = win_state->cache_window[win_state->current_window_index];
}

/**
 * Slide the cache window forward by one page.
 *
 * Advances to the next page, shifts the cache window if within bounds,
 * reuses or frees cached pages, and spawns rendering jobs as needed.
 * Falls back to full rebuild if near document edges. Waits until the
 * new current page is ready.
 *
 * @param state  Pointer to the current DocState.
 * Return:       true if sliding succeeded, false if at end of document.
 */

bool
slide_cache_window_forward(DocState *doc_state, EmacsWinState *win_state)
{
	int n = ++win_state->current_page_number;
	int pagecount = doc_state->pagecount;

	if (n >= pagecount)
	{
		fprintf(stderr,
			"slide_cache_window_right: cannot slide past end (page "
			"%d)\n",
			n);
		return false;
	}

	if (n - MAX_CACHE_WINDOW_SIZE > 0
	    && n + MAX_CACHE_WINDOW_SIZE < pagecount)
	{
		// Free the leftmost page in the cache window
		if (win_state->cache_window[0] != NULL
		    && win_state->cache_window[0]->status == PAGE_STATUS_READY)
		{
			free_cached_page(doc_state, win_state->cache_window[0]);
		}
		memmove(
		    &win_state->cache_window[0], &win_state->cache_window[1],
		    sizeof(win_state->cache_window[0]) * (MAX_CACHE_SIZE - 1));

		CachedPage *cp
		    = doc_state->cached_pages_pool[n + MAX_CACHE_WINDOW_SIZE];
		win_state->cache_window[MAX_CACHE_SIZE - 1] = cp;
		if (cp->status == PAGE_STATUS_EMPTY)
		{
			load_page_dl(doc_state, cp);
			DrawThreadArgs *draw_args
			    = malloc(sizeof(DrawThreadArgs));
			draw_args->doc_state = doc_state;
			draw_args->win_state = win_state;
			draw_args->cp = cp;
			submit_job(draw_page_thread, draw_args, &g_thread_pool);
		}
		win_state->current_window_index = MAX_CACHE_WINDOW_SIZE;
	}
	else
	{
		build_cache_window(doc_state, win_state, n);
	}

	// Wait for the current page's cache to be ready if it isn't
	while (win_state->cache_window[win_state->current_window_index]->status
	       != PAGE_STATUS_READY)
		fprintf(stderr, "Waiting for page %d to be ready\n",
			win_state->cache_window[win_state->current_window_index]
			    ->page_num);
	win_state->current_cached_page
	    = win_state->cache_window[win_state->current_window_index];

	return true;
}

/**
 * Slide the cache window backward by one page.
 *
 * Moves to the previous page, shifts the cache window if within bounds,
 * frees or reuses cached pages, and triggers rendering if needed.
 * Falls back to a full rebuild near document start. Waits until the
 * new current page is ready.
 *
 * @param state  Pointer to the current DocState.
 * Return:       true if sliding succeeded, false if at start of document.
 */

bool
slide_cache_window_backward(DocState *doc_state, EmacsWinState *win_state)
{
	int n = --win_state->current_page_number;
	int pagecount = doc_state->pagecount;

	if (n < 0)
	{
		fprintf(
		    stderr,
		    "slide_window_left: cannot slide past start (page %d)\n",
		    win_state->current_page_number);
		return false;
	}

	if (n - MAX_CACHE_WINDOW_SIZE > 0
	    && n + MAX_CACHE_WINDOW_SIZE < pagecount)
	{
		// Free the rightmost page in the cache window
		if (win_state->cache_window[MAX_CACHE_SIZE - 1] != NULL
		    && win_state->cache_window[MAX_CACHE_SIZE - 1]->status
			   == PAGE_STATUS_READY)
		{
			free_cached_page(
			    doc_state,
			    win_state->cache_window[MAX_CACHE_SIZE - 1]);
		}
		memmove(
		    &win_state->cache_window[1], &win_state->cache_window[0],
		    sizeof(win_state->cache_window[0]) * (MAX_CACHE_SIZE - 1));
		CachedPage *cp
		    = doc_state->cached_pages_pool[n - MAX_CACHE_WINDOW_SIZE];
		win_state->cache_window[0] = cp;
		if (cp->status == PAGE_STATUS_EMPTY)
		{
			load_page_dl(doc_state, cp);
			DrawThreadArgs *draw_args
			    = malloc(sizeof(DrawThreadArgs));
			draw_args->doc_state = doc_state;
			draw_args->win_state = win_state;
			draw_args->cp = cp;
			submit_job(draw_page_thread, draw_args, &g_thread_pool);
		}
		win_state->current_window_index = MAX_CACHE_WINDOW_SIZE;
	}
	else
	{
		build_cache_window(doc_state, win_state, n);
	}

	// Wait for the current page's cache to be ready if it isn't
	while (win_state->cache_window[win_state->current_window_index]->status
	       != PAGE_STATUS_READY)
		fprintf(stderr, "Waiting for page %d to be ready\n",
			win_state->cache_window[win_state->current_window_index]
			    ->page_num);
	win_state->current_cached_page
	    = win_state->cache_window[win_state->current_window_index];

	return true;
}

/**
 * emacs_load_doc - Load a document from Emacs, initialize state, and render
 * first page.
 * @env:   The Emacs environment pointer.
 * @nargs: Number of arguments passed from Elisp (should be 1).
 * @args:  Array of Elisp argument values; args[0] is the file path string.
 * @data:  User-supplied callback data (ignored).
 *
 * Return: Elisp `t` on completion (`nil` otherwise).
 */

emacs_value
emacs_load_doc(emacs_env *env, ptrdiff_t nargs, emacs_value *args, void *data)
{
	(void)nargs;
	(void)data;
	size_t str_length = 0;

	emacs_value curr_win = EMACS_CURR_WIN;
	emacs_value current_doc_overlay = init_overlay(env, curr_win);
	DocState *doc_state = init_doc_state_ptr(env);
	EmacsWinState *win_state = init_win_state_ptr(env, doc_state, curr_win);

	if (!doc_state || !win_state)
	{
		emacs_message(env, "Document cannot be loaded into memory.");
		return EMACS_NIL;
	}

	reset_doc_state(doc_state);
	reset_win_state(win_state);

	if (!elisp_2_c_str(env, args[0], &doc_state->path, &str_length))
	{
		emacs_message(env,
			      "Failed to convert Emacs string to C string.");
		return EMACS_NIL;
	}

	init_main_ctx(doc_state);	// Creates mupdf context with locks
	if (load_mupdf_doc(doc_state)
	    == EXIT_FAILURE) // Opens the doc and sets pagecount
	{
		emacs_message(env,
			      "The document could not be loaded by MuPDF.");
		return EMACS_NIL;
	}
	outline2plist(env, doc_state->outline);

	doc_state->cached_pages_pool = malloc(
	    doc_state->pagecount * sizeof(*doc_state->cached_pages_pool));
	CachedPage *block = calloc(doc_state->pagecount, sizeof *block);

	for (int i = 0; i < doc_state->pagecount; ++i)
	{
		doc_state->cached_pages_pool[i] = &block[i];
		doc_state->cached_pages_pool[i]->page_num = i;
		pthread_mutex_init(&doc_state->cached_pages_pool[i]->mutex,
				   NULL);
		pthread_cond_init(&doc_state->cached_pages_pool[i]->cond, NULL);
		doc_state->cached_pages_pool[i]->status = PAGE_STATUS_EMPTY;
	}

	build_cache_window(doc_state, win_state,
			   win_state->current_page_number);
	set_current_pagecount(env, doc_state);
	set_current_page_number(env, win_state->current_page_number);
	set_current_render_status(env);
	doc_state->frame_scale = get_frame_scale_factor(env);

	CachedPage *cp = win_state->current_cached_page;

	// Wait until getting a signal from render threads
	pthread_mutex_lock(&cp->mutex);
	while (cp->status != PAGE_STATUS_READY)
		pthread_cond_wait(&cp->cond, &cp->mutex);
	pthread_mutex_unlock(&cp->mutex);

	display_img_to_overlay(env, win_state, cp->img_data, cp->img_size,
			       current_doc_overlay);
	return EMACS_T;
}

/**
 * Redisplay the current document page in Emacs.
 *
 * Resets visual state (no rotation/inversion), re-renders the current
 * cached page in a background thread, waits for completion, and updates
 * the image in the associated Emacs overlay.
 *
 * @param env    Emacs module environment.
 * @param nargs  Argument count (unused).
 * @param args   Argument values (unused).
 * @param data   Additional data (unused).
 * Return:       EMACS_T on success, EMACS_NIL if state is not available.
 */

emacs_value
emacs_redisplay_doc(emacs_env *env, ptrdiff_t nargs, emacs_value *args,
		    void *data)
{
	(void)nargs;
	(void)args;
	(void)data;

	DocState *doc_state = get_doc_state_ptr(env);
	emacs_value current_doc_overlay = get_current_doc_overlay(env);
	EmacsWinState *win_state = get_win_state_ptr(env, current_doc_overlay);

	if (doc_state && win_state)
	{
		doc_state->invert = 0;
		win_state->rotate = 0;
		CachedPage *cp = win_state->current_cached_page;
		DrawThreadArgs *draw_args = malloc(sizeof(DrawThreadArgs));
		draw_args->doc_state = doc_state;
		draw_args->win_state = win_state;
		draw_args->cp = cp;
		submit_job(draw_page_thread, draw_args, &g_thread_pool);

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

/**
 * Close the currently loaded document and free all resources.
 *
 * Frees the cache window and cached pages pool, drops the outline,
 * document, and MuPDF context, resets the DocState, and frees it.
 *
 * @param env    Emacs module environment.
 * @param nargs  Argument count (unused).
 * @param args   Argument values (unused).
 * @param data   Additional data (unused).
 * Return:       EMACS_T after successful cleanup.
 */

emacs_value
emacs_close_doc(emacs_env *env, ptrdiff_t nargs, emacs_value *args, void *data)
{
	(void)nargs;
	(void)args;
	(void)data;

	DocState *state = get_doc_state_ptr(env);
	emacs_value overlay = get_current_doc_overlay(env);
	EmacsWinState *win_state = get_win_state_ptr(env, overlay);
	free_cache_window(state, win_state);
	free_cached_pages_pool(state);
	fz_drop_outline(state->ctx, state->outline);
	fz_drop_document(state->ctx, state->doc);
	fz_drop_context(state->ctx);
	reset_doc_state(state);
	free(state);
	return EMACS_T;
}

/**
 * emacs_next_page - Move to and display the next page in the overlay.
 * @env:   The Emacs environment pointer.
 * @nargs: (ignored).
 * @args:  (ignored).
 * @data:  (ignored).
 *
 * Return: Elisp `t` on completion, `nil` otherwise.
 */

emacs_value
emacs_next_page(emacs_env *env, ptrdiff_t nargs, emacs_value *args, void *data)
{
	(void)nargs;
	(void)args;
	(void)data;
	DocState *doc_state = get_doc_state_ptr(env);
	emacs_value current_doc_overlay = get_current_doc_overlay(env);
	EmacsWinState *win_state = get_win_state_ptr(env, current_doc_overlay);

	if (doc_state && win_state)
	{
		if (win_state->current_page_number
		    == (doc_state->pagecount - 1))
		{
			emacs_message(env, "Already last page!");
			return EMACS_NIL;
		}

		// Get the pointer for the next CachedPage in the window
		CachedPage *next_cp
		    = win_state
			  ->cache_window[win_state->current_window_index + 1];

		if (next_cp->status != PAGE_STATUS_READY)
			load_page_dl(doc_state, next_cp);

		DrawThreadArgs *draw_args = malloc(sizeof(DrawThreadArgs));
		draw_args->doc_state = doc_state;
		draw_args->win_state = win_state;
		draw_args->cp = next_cp;
		submit_job(draw_page_thread, draw_args, &g_thread_pool);

		// Wait for the thread to signal before displaying
		pthread_mutex_lock(&next_cp->mutex);
		pthread_cond_wait(&next_cp->cond, &next_cp->mutex);
		pthread_mutex_unlock(&next_cp->mutex);
		display_img_to_overlay(env, win_state, next_cp->img_data,
				       next_cp->img_size, current_doc_overlay);
		slide_cache_window_forward(doc_state, win_state);
		set_current_page_number(env, win_state->current_page_number);
		return EMACS_T;
	}
	else
	{
		return EMACS_NIL;
	}

	return EMACS_T;
}

/**
 * emacs_prev_page - Move to and display the previous page in the overlay.
 * @env:   The Emacs environment pointer.
 * @nargs: (ignored).
 * @args:  (ignored).
 * @data:  (ignored).
 *
 * Return: Elisp `t` if page moved, `nil` if already at first page (and warn).
 */

emacs_value
emacs_prev_page(emacs_env *env, ptrdiff_t nargs, emacs_value *args, void *data)
{
	(void)nargs;
	(void)args;
	(void)data;
	DocState *doc_state = get_doc_state_ptr(env);
	emacs_value current_doc_overlay = get_current_doc_overlay(env);
	EmacsWinState *win_state = get_win_state_ptr(env, current_doc_overlay);

	if (doc_state && win_state)
	{
		if (win_state->current_page_number == 0)
		{
			emacs_message(env, "Already first page!");
			return EMACS_NIL;
		}

		CachedPage *prev_cp
		    = win_state
			  ->cache_window[win_state->current_window_index - 1];

		if (prev_cp->status != PAGE_STATUS_READY)
			load_page_dl(doc_state, prev_cp);

		DrawThreadArgs *draw_args = malloc(sizeof(DrawThreadArgs));
		draw_args->doc_state = doc_state;
		draw_args->win_state = win_state;
		draw_args->cp = prev_cp;
		submit_job(draw_page_thread, draw_args, &g_thread_pool);

		pthread_mutex_lock(&prev_cp->mutex);
		pthread_cond_wait(&prev_cp->cond, &prev_cp->mutex);
		pthread_mutex_unlock(&prev_cp->mutex);
		display_img_to_overlay(env, win_state, prev_cp->img_data,
				       prev_cp->img_size, current_doc_overlay);
		slide_cache_window_backward(doc_state, win_state);
		set_current_page_number(env, win_state->current_page_number);
	}
	else
	{
		return EMACS_NIL;
	}

	return EMACS_T;
}

/**
 * emacs_first_page - Jump to and display the first page of the document.
 * @env:   The Emacs environment pointer.
 * @nargs: (ignored).
 * @args:  (ignored).
 * @data:  (ignored).
 *
 * Return: Elisp `t` on success, `nil` if already at the first page (and warn).
 */

emacs_value
emacs_first_page(emacs_env *env, ptrdiff_t nargs, emacs_value *args, void *data)
{
	(void)nargs;
	(void)args;
	(void)data;
	DocState *doc_state = get_doc_state_ptr(env);
	emacs_value current_doc_overlay = get_current_doc_overlay(env);
	EmacsWinState *win_state = get_win_state_ptr(env, current_doc_overlay);

	if (doc_state && win_state)
	{

		if (win_state->current_page_number == 0)
		{
			emacs_message(env, "Already first page!");
			return EMACS_NIL;
		}

		win_state->current_page_number = 0;
		set_current_page_number(env, win_state->current_page_number);
		build_cache_window(doc_state, win_state,
				   win_state->current_page_number);
		CachedPage *first_cp = win_state->current_cached_page;
		display_img_to_overlay(env, win_state, first_cp->img_data,
				       first_cp->img_size, current_doc_overlay);
	}
	else
	{
		return EMACS_NIL;
	}

	return EMACS_T;
}

/**
 * emacs_last_page - Jump to and display the last page of the document.
 * @env:   The Emacs environment pointer.
 * @nargs: (ignored).
 * @args:  (ignored).
 * @data:  (ignored).
 *
 * Return: Elisp `t` on success, `nil` if already at the last page (and warn).
 */

emacs_value
emacs_last_page(emacs_env *env, ptrdiff_t nargs, emacs_value *args, void *data)
{
	(void)nargs;
	(void)args;
	(void)data;
	DocState *doc_state = get_doc_state_ptr(env);
	emacs_value current_doc_overlay = get_current_doc_overlay(env);
	EmacsWinState *win_state = get_win_state_ptr(env, current_doc_overlay);

	if (doc_state && win_state)
	{
		if (win_state->current_page_number == doc_state->pagecount - 1)
		{
			emacs_message(env, "Already the last page!");
			return EMACS_NIL;
		}

		win_state->current_page_number = doc_state->pagecount - 1;
		set_current_page_number(env, win_state->current_page_number);
		build_cache_window(doc_state, win_state,
				   win_state->current_page_number);
		CachedPage *last_cp = win_state->current_cached_page;
		display_img_to_overlay(env, win_state, last_cp->img_data,
				       last_cp->img_size, current_doc_overlay);
	}
	else
	{
		return EMACS_NIL;
	}

	return EMACS_T;
}

/**
 * emacs_goto_page - Jump to a specific page number and display it.
 * @env:   The Emacs environment pointer.
 * @nargs: Number of Elisp args (should be 1).
 * @args:  Array of Elisp argument values; args[0] is desired page index.
 * @data:  (ignored).
 *
 * Return: Elisp `t` on success or out-of-bounds (always `t`), otherwise emits a
 * bounds warning.
 */

emacs_value
emacs_goto_page(emacs_env *env, ptrdiff_t nargs, emacs_value *args, void *data)
{
	(void)nargs;
	(void)data;
	int page_number = env->extract_integer(env, args[0]);
	DocState *doc_state = get_doc_state_ptr(env);
	emacs_value current_doc_overlay = get_current_doc_overlay(env);
	EmacsWinState *win_state = get_win_state_ptr(env, current_doc_overlay);

	if (doc_state && win_state)
	{
		if (page_number >= 0
		    && page_number <= (doc_state->pagecount - 1))
		{
			win_state->current_page_number = page_number;
			set_current_page_number(env,
						win_state->current_page_number);
			build_cache_window(doc_state, win_state,
					   win_state->current_page_number);
			CachedPage *cp = win_state->current_cached_page;
			display_img_to_overlay(env, win_state, cp->img_data,
					       cp->img_size,
					       current_doc_overlay);
		}
		else
		{
			emacs_message(env,
				      "Provided page number is out of bounds!");
			return EMACS_NIL;
		}
	}
	else
	{
		return EMACS_NIL;
	}

	return EMACS_T;
}

/**
 * emacs_doc_change_page_size - Scale the displayed image to a new size.
 * @env:   The Emacs environment pointer.
 * @nargs: Number of Elisp args (should be 1).
 * @args:  Array of Elisp argument values; args[0] is the float scale factor.
 * @data:  (ignored).
 *
 * Return: Elisp `t` on completion, `nil` otherwise.
 */

emacs_value
emacs_doc_scale_page(emacs_env *env, ptrdiff_t nargs, emacs_value *args,
		     void *data)
{
	(void)nargs;
	(void)data;
	DocState *doc_state = get_doc_state_ptr(env);
	emacs_value current_doc_overlay = get_current_doc_overlay(env);
	EmacsWinState *win_state = get_win_state_ptr(env, current_doc_overlay);
	float scale_factor = env->extract_float(env, args[0]);

	if (doc_state && win_state)
	{
		DrawThreadArgs *draw_args = malloc(sizeof(DrawThreadArgs));
		draw_args->doc_state = doc_state;
		draw_args->win_state = win_state;
		draw_args->cp = win_state->current_cached_page;
		double new_res = fz_clamp(scale_factor * 72, MINRES, MAXRES);
		win_state->resolution = new_res;

		if (draw_args->cp->status != PAGE_STATUS_READY)
			load_page_dl(doc_state, draw_args->cp);
		submit_job(draw_page_thread, draw_args, &g_thread_pool);
		pthread_mutex_lock(&win_state->current_cached_page->mutex);
		pthread_cond_wait(&win_state->current_cached_page->cond,
				  &win_state->current_cached_page->mutex);
		pthread_mutex_unlock(&win_state->current_cached_page->mutex);

		display_img_to_overlay(env, win_state,
				       win_state->current_cached_page->img_data,
				       win_state->current_cached_page->img_size,
				       current_doc_overlay);
	}
	else
	{
		emacs_message(env, "Not a valid document, or you are not in an "
				   "Emacs Reader buffer!");
		return EMACS_NIL;
	}

	return EMACS_T;
}

/**
 * emacs_doc_rotate - rotate the displayed image by rotation angle.
 * @env:   The Emacs environment pointer.
 * @nargs: Number of Elisp args (should be 1).
 * @args:  Array of Elisp argument values, args[0] is the angle (in degrees).
 * @data:  (ignored).
 *
 * Return: Elisp `t` on completion, `nil` otherwise.
 */

emacs_value
emacs_doc_rotate(emacs_env *env, ptrdiff_t nargs, emacs_value *args, void *data)
{
	(void)nargs;
	(void)data;
	DocState *doc_state = get_doc_state_ptr(env);
	emacs_value current_doc_overlay = get_current_doc_overlay(env);
	EmacsWinState *win_state = get_win_state_ptr(env, current_doc_overlay);
	int rotation_deg = env->extract_integer(env, args[0]);
	if (doc_state && win_state)
	{
		win_state->rotate += rotation_deg;
		DrawThreadArgs *draw_args = malloc(sizeof(DrawThreadArgs));
		draw_args->doc_state = doc_state;
		draw_args->win_state = win_state;
		draw_args->cp = win_state->current_cached_page;

		if (draw_args->cp->status != PAGE_STATUS_READY)
			load_page_dl(doc_state, draw_args->cp);

		submit_job(draw_page_thread, draw_args, &g_thread_pool);

		// Wait for the thread to signal before displaying
		pthread_mutex_lock(&win_state->current_cached_page->mutex);
		pthread_cond_wait(&win_state->current_cached_page->cond,
				  &win_state->current_cached_page->mutex);
		pthread_mutex_unlock(&win_state->current_cached_page->mutex);
		display_img_to_overlay(env, win_state,
				       win_state->current_cached_page->img_data,
				       win_state->current_cached_page->img_size,
				       current_doc_overlay);
	}
	else
	{
		return EMACS_NIL;
	}
	return EMACS_T;
}

emacs_value
emacs_doc_window_create(emacs_env *env, ptrdiff_t nargs, emacs_value *args,
			void *data)
{
	(void)nargs;
	(void)data;
	emacs_value win = args[0];
	DocState *doc_state = get_doc_state_ptr(env);
	init_overlay(env, win);
	init_win_state_ptr(env, doc_state, win);
	return EMACS_T;
}

emacs_value
emacs_doc_window_close(emacs_env *env, ptrdiff_t nargs, emacs_value *args,
		       void *data)
{
	(void)nargs;
	(void)data;
	emacs_value overlay = args[0];
	EmacsWinState *win_state = get_win_state_ptr(env, overlay);
	if (win_state)
		free(win_state);
	env->funcall(env, env->intern(env, "delete-overlay"), 1, &overlay);
	return EMACS_T;
}

// Entrypoint for the dynamic module
int
emacs_module_init(struct emacs_runtime *runtime)
{
	emacs_env *env = runtime->get_environment(runtime);
	if (!env)
	{
		fprintf(stderr, "Failed to get Emacs environment.\n");
		return 1;
	}

	// Registrations for the required functions and variables

	register_module_func(env, emacs_load_doc, "reader-dyn--load-doc", 1, 1,
			     "Loads a DOC to be rendered in Emacs.");

	register_module_func(
	    env, emacs_redisplay_doc, "reader-dyn--redisplay-doc", 0, 0,
	    "Redisplays the document at the current page and scale.");
	register_module_func(env, emacs_close_doc, "reader-dyn--close-doc", 0,
			     0,
			     "Frees the DocState in memory and all other "
			     "artifacts related to the current document.");
	register_module_func(
	    env, emacs_next_page, "reader-dyn--next-page", 0, 0,
	    "Loads and renders the next page of the document.");

	register_module_func(
	    env, emacs_prev_page, "reader-dyn--prev-page", 0, 0,
	    "Loads and renders the previous page of the document.");

	register_module_func(
	    env, emacs_first_page, "reader-dyn--first-page", 0, 0,
	    "Loads and renders the first page of the document.");

	register_module_func(
	    env, emacs_last_page, "reader-dyn--last-page", 0, 0,
	    "Loads and renders the last page of the document.");

	register_module_func(env, emacs_goto_page, "reader-dyn--goto-page", 1,
			     1, "Loads and renders the N page number.");

	register_module_func(
	    env, emacs_doc_scale_page, "reader-dyn--scale-page", 1, 1,
	    "Scales the current page of the document by a given FACTOR.");

	register_module_func(env, emacs_set_dark_theme,
			     "reader-dyn--set-dark-theme", 0, 0,
			     "Sets the current document to have a dark theme. "
			     "It simply inverts the colors.");

	register_module_func(env, emacs_doc_rotate, "reader-dyn--rotate-doc", 1,
			     1, "Rotates the page by the given DEGREE.");

	register_module_func(
	    env, emacs_doc_window_create, "reader-dyn--window-create", 1, 1,
	    "Initializes window parameters for EmacsWinState.");
	register_module_func(env, emacs_doc_window_close,
			     "reader-dyn--window-close", 1, 1,
			     "Frees EmacsWinState.");

	// Register buffer-local variables.
	permanent_buffer_local_var(env, "reader-current-doc-pagecount");
	permanent_buffer_local_var(env, "reader-current-doc-render-status");
	permanent_buffer_local_var(env, "reader-current-doc-state-ptr");
	permanent_buffer_local_var(env, "reader-current-doc-outline");
	permanent_buffer_local_var(env, "reader--recent-pagenumber-fallback");

	// Provide the current dynamic module as a feature to Emacs
	provide(env, "render-core");

	// Initialize the global thread pool
	threadpool_init(&g_thread_pool);
	fprintf(stderr, "reader: %d threads have been initialized.\n",
		MAX_POOL_SIZE);

	fprintf(stderr,
		"reader: dynamic module has been loaded successfully.\n");

	return 0;
}
