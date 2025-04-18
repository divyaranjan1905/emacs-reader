#ifndef HELPERS_H
#define HELPERS_H

#include <emacs-module.h>
#include <mupdf/fitz.h>
#include <stdbool.h>
#include <stddef.h>

bool emacs_2_c_str(emacs_env *env, emacs_value value, char **buffer,
                   size_t *size);
bool create_buffers(fz_context *ctx, fz_buffer **curr_buf, fz_buffer **prev_buf,
                    fz_buffer **next_buf);
bool create_outputs(fz_context *ctx, fz_output **curr_out, fz_output **prev_out,
                    fz_output **next_out, fz_buffer *curr_buf,
                    fz_buffer *prev_buf, fz_buffer *next_buf);
void drop_all_buffers(fz_context *ctx, fz_buffer *curr, fz_buffer *prev,
                      fz_buffer *next);
void drop_all_outputs(fz_context *ctx, fz_output *curr, fz_output *prev,
                      fz_output *next);
void drop_all_devices(fz_context *ctx, fz_device *curr, fz_device *prev,
                      fz_device *next);
void close_all_outputs(fz_context *ctx, fz_output *curr, fz_output *prev,
                       fz_output *next);
void close_all_devices(fz_context *ctx, fz_device *curr, fz_device *prev,
                       fz_device *next);
void provide(emacs_env *env, const char *value);

#endif
