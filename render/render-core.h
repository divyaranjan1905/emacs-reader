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

#ifndef RENDER_CORE_H
#define RENDER_CORE_H

#include "emacs-module.h"
#include <mupdf/fitz.h>
#include <pthread.h>

#define MINRES 18
#define MAXRES 850
#define MAX_CACHE_SIZE 11
#define MAX_CACHE_WINDOW_SIZE (MAX_CACHE_SIZE / 2)

typedef enum
{
	PAGE_STATUS_EMPTY,
	PAGE_STATUS_RENDERING,
	PAGE_STATUS_READY,
	PAGE_STATUS_ERROR
} PageStatus;

typedef struct
{
	int page_num;
	fz_display_list *display_list;
	fz_pixmap *pixmap;
	int imgw, imgh;
	char *img_data;
	size_t img_size;
	PageStatus status;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
} CachedPage;

typedef struct
{
	fz_context *ctx;
	fz_locks_context locks;
	fz_document *doc;
	CachedPage **cached_pages_pool;
	char *path;
	int pagecount;
	fz_rect page_bbox;
	fz_outline *outline;
	float frame_scale; // Emacs' frame-scale-factor
	int invert;
} DocState;

typedef struct
{
	DocState *doc_state;
	float resolution; // In terms of DPI (72, 144 etc.)
	CachedPage *cache_window[MAX_CACHE_SIZE];
	int current_window_index;
	CachedPage *current_cached_page;
	int current_page_number;
	int rotate;
} EmacsWinState;

int
load_page_dl(DocState *state, CachedPage *cp);
void *
draw_page_thread(void *arg);
void
build_cache_window(DocState *doc_state, EmacsWinState *win_state, int n);
bool
slide_cache_window_forward(DocState *state, EmacsWinState *win_state);
bool
slide_cache_window_backward(DocState *state, EmacsWinState *win_state);

// Emacs module functions
emacs_value
emacs_load_doc(emacs_env *env, ptrdiff_t nargs, emacs_value *args, void *data);
emacs_value
emacs_doc_window_create(emacs_env *env, ptrdiff_t nargs, emacs_value *args,
			void *data);
emacs_value
emacs_doc_window_close(emacs_env *env, ptrdiff_t nargs, emacs_value *args,
		       void *data);
emacs_value
emacs_next_page(emacs_env *env, ptrdiff_t nargs, emacs_value *args, void *data);
emacs_value
emacs_prev_page(emacs_env *env, ptrdiff_t nargs, emacs_value *args, void *data);
emacs_value
emacs_first_page(emacs_env *env, ptrdiff_t nargs, emacs_value *args,
		 void *data);
emacs_value
emacs_last_page(emacs_env *env, ptrdiff_t nargs, emacs_value *args, void *data);
emacs_value
emacs_goto_page(emacs_env *env, ptrdiff_t nargs, emacs_value *args, void *data);
emacs_value
emacs_doc_scale_page(emacs_env *env, ptrdiff_t nargs, emacs_value *args,
		     void *data);
emacs_value
emacs_doc_rotate_doc(emacs_env *env, ptrdiff_t nargs, emacs_value *args,
		     void *data);

#endif // RENDER_CORE_H
