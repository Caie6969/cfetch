#ifndef CFETCH_COLOR_TUI_H
#define CFETCH_COLOR_TUI_H

#include "config.h"

/* Launches the interactive color editor for the ASCII logo associated
 * with `cfg` (resolved from cfg->ascii_path or distro_id + minimal_ascii).
 * Writes a sidecar "<ascii_path>.colors" file on save.
 * Returns 0 on save, 1 on quit-without-save, -1 on error. */
int color_tui_run(const cfetch_config_t *cfg);

/* Loads color overlay (if any) for the given ascii file path.
 * `cell_colors_out` is filled with ANSI escape strings (or empty) per cell,
 * indexed [row * max_cols + col]. Caller frees with color_overlay_free(). */
typedef struct {
    int rows, cols;
    char **cells;    /* rows*cols, each is malloc'd ANSI prefix or NULL */
} color_overlay_t;

int  color_overlay_load(const char *ascii_path, color_overlay_t *out);
void color_overlay_free(color_overlay_t *o);

#endif
