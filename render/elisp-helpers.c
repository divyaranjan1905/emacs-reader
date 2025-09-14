// Helper functions that complement the C API of the Emacs Module

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

#include "elisp-helpers.h"
#include "emacs-module.h"
#include "render-core.h"
#include <stddef.h>
#include <string.h>

/**
 * elisp_2_c_str - Convert an Elisp string to a C string.
 * @env: Pointer (of type `emacs_env') to the environment of the Emacs runtime
 * @value: The value of the Elisp string that is to be converted.  It will only
 * the value (in Elisp, (symbol-value str)) of the string, not the symbol.
 * @buffer: The C character array that is going to store the converted string.
 * @size: Pointer to the length of the string after being converted, as
 * type size_t.
 *
 * Return: true on success, false on error
 */

bool
elisp_2_c_str(emacs_env *env, emacs_value value, char **buffer, size_t *size)
{
	ptrdiff_t buffer_size;
	if (!env->copy_string_contents(env, value, NULL, &buffer_size))
		return false;
	assert(env->non_local_exit_check(env) == emacs_funcall_exit_return);
	assert(buffer_size > 0);
	*buffer = malloc((size_t)buffer_size);
	if (*buffer == NULL)
	{
		env->non_local_exit_signal(env, env->intern(env, "memory-full"),
					   env->intern(env, "nil"));
		return false;
	}
	ptrdiff_t old_buffer_size = buffer_size;
	if (!env->copy_string_contents(env, value, *buffer, &buffer_size))
	{
		free(*buffer);
		*buffer = NULL;
		return false;
	}
	assert(env->non_local_exit_check(env) == emacs_funcall_exit_return);
	assert(buffer_size == old_buffer_size);
	*size = (size_t)(buffer_size - 1);
	return true;
}

/**
 * provide - Calls the Elisp (provide) function to provide a feature to the
 * module.
 * @env: Pointer (of type `emacs_env') to the environment of the Emacs runtime
 * @value: String of the symbol that is to be provid-ed.
 *
 * It doesn’t store the symbol or value of the feature anywhere, only calls the
 * provide function with the symbol from the string.
 *
 * It’s equivalent to doing the following in Elisp:
 * (provide 'feature)
 */

void
provide(emacs_env *env, const char *value)
{
	emacs_value Qvalue = env->intern(env, value);
	emacs_value Qprovide = env->intern(env, "provide");
	emacs_value args[] = { Qvalue };

	env->funcall(env, Qprovide, 1, args);
}

/**
 * register_module_func - Register a function from the dynamic module into the
 * Emacs environment.  It makes the function available to be directly usable
 * from Emacs as any other Elisp function.
 * @env: Pointer to the Emacs environment for the runtime (emacs_env).
 * @module_func: Pointer to the function (emacs_value) that needs to be
registered.
 * @symbol: The symbol for which the function is to be registered.
 * @min_args: The minimum number of arguments this function must be provided
 * with.
 * @max_args: The maximum number of arguments this function can take.
 * @docstring: The string which would act as the docstring for the module
 * function. Equivalent to the docstring in Elisp functions, and visible from
 * C-h f.
 *
 * The @module_func must be not only compile-able but also it must respect the
 * Elisp runtime of Emacs, any sort of undefined or unpredictable behavior would
 * not be found during compile-time, but rather would result in a segfault of
 * Emacs.
 *
 * Returns: nothing
 */

void
register_module_func(emacs_env *env,
		     emacs_value (*module_func)(emacs_env *env, ptrdiff_t nargs,
						emacs_value *args, void *data),
		     char *symbol, int min_args, int max_args, char *docstring)
{

	emacs_value elisp_func_symbol = env->intern(env, symbol);
	emacs_value elisp_func = env->make_function(
	    env, min_args, max_args, module_func, docstring, NULL);

	emacs_value elisp_func_args[2] = { elisp_func_symbol, elisp_func };
	env->funcall(env, env->intern(env, "fset"), 2, elisp_func_args);
}

/**
 * data2elisp_image - Create an Elisp image object from raw image data
 * @env: Emacs environment pointer
 * @img_data: Image data that is to be rendered as image, should usually be from
 * the DocState.
 * @img_size: The size of the image data that is going to be rendered, should
 * usually be from the DocState.
 *
 * One needs to be extremely about handling the image data, and make sure it
 * later gets dropped and freed accordingly.
 *
 * Returns: The Elisp image object, which will be a specific plist, and can be
 * rendered in Emacs with `insert-image' or other means.

 */

emacs_value
data2elisp_image(emacs_env *env, EmacsWinState *win_state, char *img_data,
		 size_t img_size)
{
	emacs_value image_width
	    = env->make_integer(env, doc_page_width(win_state));
	emacs_value image_height
	    = env->make_integer(env, doc_page_height(win_state));
	emacs_value img_data_string
	    = env->make_unibyte_string(env, img_data, img_size);
	emacs_value image_args[9]
	    = { img_data_string,
		env->intern(env, "pbm"),
		env->intern(env, "t"),
		env->intern(env, ":width"),
		image_width,
		env->intern(env, ":height"),
		image_height,
		env->intern(env,
			    ":scale"), // Never really used, only a placeholder
		env->make_integer(env, 1) };

	emacs_value image_data = env->funcall(
	    env, env->intern(env, "create-image"), 9, image_args);

	return image_data;
}

/**
 * init_doc_state_ptr - Initialize the DocState and expose it to Elisp.
 * @env:    The Emacs environment pointer.
 *
 * Return: A pointer to the current `docstate` (or null if the symbol is unbound
 * or not a valid user pointer.).
 */

DocState *
init_doc_state_ptr(emacs_env *env)
{
	DocState *state = malloc(sizeof(DocState));
	// Create a user pointer and expose it to Emacs in a buffer-local
	emacs_value user_ptr = env->make_user_ptr(env, NULL, state);
	emacs_value doc_state_ptr_sym
	    = env->intern(env, "reader-current-doc-state-ptr");
	env->funcall(env, env->intern(env, "set"), 2,
		     (emacs_value[]){ doc_state_ptr_sym, user_ptr });
	return state;
}

/**
 * get_doc_state_ptr - Retrieve the DocState pointer stored in Elisp.
 * @env:    The Emacs environment pointer.
 *
 * Return: A pointer to the current `DocState` (or NULL if the symbol is unbound
 * or not a valid user pointer.).
 */

DocState *
get_doc_state_ptr(emacs_env *env)
{
	emacs_value ptr_sym = env->intern(env, "reader-current-doc-state-ptr");
	emacs_value ptr
	    = env->funcall(env, env->intern(env, "symbol-value"), 1, &ptr_sym);
	DocState *state = env->get_user_ptr(env, ptr);
	return state;
}

EmacsWinState *
init_win_state_ptr(emacs_env *env, DocState *doc_state, emacs_value window)
{
	EmacsWinState *win_state = malloc((sizeof(EmacsWinState)));
	reset_win_state(win_state);
	win_state->doc_state = doc_state;
	// Create a user pointer
	emacs_value win_state_usr_ptr
	    = env->make_user_ptr(env, NULL, win_state);

	// Get the overlay window parameter
	emacs_value overlay = env->funcall(
	    env, env->intern(env, "window-parameter"), 2,
	    (emacs_value[]){ window, env->intern(env, "overlay") });

	// Set the overlay property for the EmacsWinState user pointer
	env->funcall(env, env->intern(env, "overlay-put"), 3,
		     (emacs_value[]){ overlay, env->intern(env, "win-state"),
				      win_state_usr_ptr });
	return win_state;
}

EmacsWinState *
get_win_state_ptr(emacs_env *env, emacs_value overlay)
{
	emacs_value ptr = env->funcall(
	    env, env->intern(env, "overlay-get"), 2,
	    (emacs_value[]){ overlay, env->intern(env, "win-state") });
	if (ptr == NULL)
		emacs_message(
		    env,
		    "Selected window doesn't have a pointer to EmacsWinState.");
	EmacsWinState *win_state = env->get_user_ptr(env, ptr);
	return win_state;
}

void
reset_win_state(EmacsWinState *win_state)
{
	*win_state = (EmacsWinState){
		.resolution = 72.0,
		.rotate = 0,
		.current_page_number = 0,
		.current_window_index = 0,
		.current_cached_page = NULL,
	};
}

void
set_current_page_number(emacs_env *env, int page)
{
	page += 1; // Exposed number shouldn't be 0-indexed
	emacs_value curr_win = EMACS_CURR_WIN;
	env->funcall(env, env->intern(env, "set-window-parameter"), 3,
		     (emacs_value[]){ curr_win, env->intern(env, "page"),
				      env->make_integer(env, page) });
	env->funcall(env, env->intern(env, "set"), 2,
		     (emacs_value[]){
			 env->intern(env, "reader--recent-pagenumber-fallback"),
			 env->make_integer(env, page) });
}

/**
 * set_current_render_status - Mark the buffer’s render status as true.
 * @env:    The Emacs environment pointer.
 *
 * Sets the Elisp variable `doc-render-status` to `t` in the current buffer,
 * indicating that the image rendering is up to date.
 */

void
set_current_render_status(emacs_env *env)
{
	emacs_value render_status_var
	    = env->intern(env, "reader-current-doc-render-status");
	env->funcall(
	    env, env->intern(env, "set"), 2,
	    (emacs_value[]){ render_status_var, env->intern(env, "t") });
}

/**
 * set_current_pagecount - Expose the document’s total page count to Elisp.
 * @env:    The Emacs environment pointer.
 * @state:  Pointer to the `DocState` containing the pagecount.
 *
 * Sets the Elisp variable `current-doc-pagecount` in the current buffer to the
 * number of pages the document has.
 */

void
set_current_pagecount(emacs_env *env, DocState *state)
{
	emacs_value pagecount_args[2]
	    = { env->intern(env, "reader-current-doc-pagecount"),
		env->make_integer(env, state->pagecount) };
	env->funcall(env, env->intern(env, "set"), 2, pagecount_args);
}

/**
 * init_overlay - Create and register an overlay covering the whole buffer.
 * @env:    The Emacs environment pointer.
 *
 * Makes an overlay spanning the entire buffer. Stores the overlay object in the
 * Elisp variable `reader-current-doc-overlay` for later use.
 */

emacs_value
init_overlay(emacs_env *env, emacs_value window)
{
	emacs_value start
	    = env->funcall(env, env->intern(env, "point-min"), 0, NULL);
	emacs_value end
	    = env->funcall(env, env->intern(env, "point-max"), 0, NULL);
	emacs_value overlay
	    = env->funcall(env, env->intern(env, "make-overlay"), 2,
			   (emacs_value[]){ start, end });
	env->funcall(
	    env, env->intern(env, "overlay-put"), 3,
	    (emacs_value[]){ overlay, env->intern(env, "window"), window });
	env->funcall(
	    env, env->intern(env, "set-window-parameter"), 3,
	    (emacs_value[]){ window, env->intern(env, "overlay"), overlay });

	return overlay;
}

/**
 * get_current_doc_overlay - Retrieve the stored overlay object for the
 * document as a window parameter.
 * @env:    The Emacs environment pointer.
 *
 * Return: The Elisp overlay object, or an unbound value if not set.
 */

emacs_value
get_current_doc_overlay(emacs_env *env)
{

	emacs_value curr_win = EMACS_CURR_WIN;
	emacs_value current_overlay = env->funcall(
	    env, env->intern(env, "window-parameter"), 2,
	    (emacs_value[]){ curr_win, env->intern(env, "overlay") });
	return current_overlay;
}

/**
 * emacs_message - Print a message in Emacs with the given string
 * @env:    The Emacs environment pointer.
 * @str:    Null-terminated C string containing the message.
 */

void
emacs_message(emacs_env *env, char *str)
{
	emacs_value el_string = env->make_string(env, str, strlen(str));
	env->funcall(env, env->intern(env, "message"), 1, &el_string);
}

/**
 * permanent_buffer_local_var: Sets a buffer local variable and makes it
 * permanently local so that it doesn’t get overridden
 * @env: The Emacs environment pointer.
 * @symbol: The symbol of the variable to be set
 */

void
permanent_buffer_local_var(emacs_env *env, char *symbol)
{
	emacs_value el_symbol = env->intern(env, symbol);
	env->funcall(env, env->intern(env, "make-variable-buffer-local"), 1,
		     &el_symbol);
	env->funcall(env, env->intern(env, "set"), 2,
		     (emacs_value[]){ el_symbol, EMACS_NIL });

	env->funcall(env, env->intern(env, "put"), 3,
		     (emacs_value[]){ el_symbol,
				      env->intern(env, "permanent-local"),
				      EMACS_T });
}

/**
 * Display image data in an Emacs overlay.
 *
 * Converts raw image data into an Emacs image spec and sets it as the
 * `display` property of the given overlay. Clears the image cache to
 * ensure the image is rendered fresh.
 *
 * @param env            Emacs module environment.
 * @param state          Document state (used by image conversion).
 * @param img_data       Pointer to raw image bytes (e.g., PNG).
 * @param img_size       Size of the image data.
 * @param buffer_overlay Emacs overlay to attach the image to.
 */

void
display_img_to_overlay(emacs_env *env, EmacsWinState *win_state, char *img_data,
		       size_t img_size, emacs_value buffer_overlay)
{
	emacs_value elisp_img
	    = data2elisp_image(env, win_state, img_data, img_size);
	emacs_value overlay_put_args[3]
	    = { buffer_overlay, env->intern(env, "display"), elisp_img };
	env->funcall(env, env->intern(env, "overlay-put"), 3, overlay_put_args);
	env->funcall(env, env->intern(env, "clear-image-cache"), 0, NULL);
}

bool
is_valid_utf8(char *s, size_t len)
{
	size_t i = 0;
	while (i < len)
	{
		unsigned char c = s[i];
		size_t remainder = 0;

		if (c <= 0x7f)
		{
			i++;
			continue;
		}
		else if ((c & 0xE0) == 0xC0)
		{
			remainder = 1;
			if (c < 0xC2)
				return false;
		}
		else if ((c & 0xF0) == 0xE0)
		{
			remainder = 2;
		}
		else if ((c & 0xF8) == 0xF0)
		{
			remainder = 3;
			if (c > 0xF4)
				return false;
		}
		else
		{
			return false;
		}

		if (i + remainder >= len)
			return false;
		for (size_t j = 1; j <= remainder; j++)
		{
			if ((s[i + j] & 0xC0) != 0x80)
				return false;
		}
		i += remainder + 1;
	}
	return true;
}

/**
 * Convert a fz_outline tree to an Emacs plist structure.
 *
 * Recursively traverses the outline linked list and builds a nested
 * plist representation for Emacs: each node becomes a plist with
 * `:title`, `:page`, and optionally `:children` keys.
 *
 * Stores the result in `reader-current-doc-outline` and returns it.
 *
 * @param env    Emacs module environment.
 * @param node   Root of the fz_outline tree (from MuPDF).
 * Return:       Emacs Lisp list of plists representing the outline.
 */

emacs_value
outline2plist(emacs_env *env, fz_outline *node)
{
	emacs_value outline_plist = EMACS_NIL;
	if (!node)
	{
		return outline_plist;
	}

	while (node)
	{
		if (!node->title)
		{
			node = node->next;
			continue;
		}

		if (!is_valid_utf8(node->title, strlen(node->title)))
		{
			return outline_plist;
			emacs_message(env, "String of Outline is not UTF-8");
			continue;
		}

		/* Build the base plist: (:title TITLE :page PAGE) */
		emacs_value node_title
		    = env->make_string(env, node->title, strlen(node->title));
		emacs_value node_page = env->make_integer(env, node->page.page);
		emacs_value base_args[]
		    = { env->intern(env, ":title"), node_title,
			env->intern(env, ":page"), node_page };
		emacs_value node_plist
		    = env->funcall(env, env->intern(env, "list"), 4, base_args);

		/* If there are children, recurse and append as :children key */
		if (node->down)
		{
			emacs_value children = outline2plist(env, node->down);
			emacs_value child_pair_args[]
			    = { env->intern(env, ":children"), children };
			emacs_value children_pair = env->funcall(
			    env, env->intern(env, "list"), 2, child_pair_args);
			/* Append the (:children CHILDREN) onto this node's
			 * plist */
			node_plist = env->funcall(
			    env, env->intern(env, "append"), 2,
			    (emacs_value[]){ node_plist, children_pair });
		}

		/* Cons this node_plist onto our accumulator */
		outline_plist = env->funcall(
		    env, env->intern(env, "cons"), 2,
		    (emacs_value[]){ node_plist, outline_plist });
		node = node->next;
	}

	/* Reverse to preserve original document order */
	outline_plist = env->funcall(env, env->intern(env, "nreverse"), 1,
				     &outline_plist);

	/* Store in reader-current-doc-outline and return */
	env->funcall(
	    env, env->intern(env, "set"), 2,
	    (emacs_value[]){ env->intern(env, "reader-current-doc-outline"),
			     outline_plist });
	return outline_plist;
}

float
get_frame_scale_factor(emacs_env *env)
{
	emacs_value factor = env->funcall(
	    env, env->intern(env, "frame-scale-factor"), 0, NULL);
	return env->extract_float(env, factor);
}
