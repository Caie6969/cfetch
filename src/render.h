#ifndef CFETCH_RENDER_H
#define CFETCH_RENDER_H

#include "config.h"
#include "sysinfo.h"
#include <stdbool.h>

/* Render the full output: ASCII art side-by-side with info lines */
void render_output(const sysinfo_t *info, const cfetch_config_t *cfg,
                   bool use_color);

/* Render JSON output */
void render_json(const sysinfo_t *info, const cfetch_config_t *cfg);

#endif /* CFETCH_RENDER_H */
