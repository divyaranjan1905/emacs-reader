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

// This is the entrypoint for the dynamic module. It registers all the
// C functions (`emacs_*') that are to be called through the dynamic module.
// It also declares certain variables that are used and defined in C.
// Finally, just before loading the module, it initializes the thread pool
// which remains alive for the rest of the Emacs session.

#include "elisp-helpers.h"
#include "emacs-module.h"
#include "render-core.h"
#include "render-theme.h"
#include "render-threads.h"

int plugin_is_GPL_compatible;

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
	provide(env, "reader-render");

	// Initialize the global thread pool
	threadpool_init(&g_thread_pool);
	fprintf(stderr, "reader: %d threads have been initialized.\n",
		MAX_POOL_SIZE);

	fprintf(stderr,
		"reader: dynamic module has been loaded successfully.\n");

	return 0;
}
