#ifndef CFETCH_CONFIG_H
#define CFETCH_CONFIG_H

#include <stdbool.h>
#include <string.h>

/* Safe string copy: always null-terminates, no truncation warnings */
static inline void scopy(char *dst, size_t dstsz, const char *src)
{
    if (dstsz == 0) return;
    size_t slen = strlen(src);
    size_t n = slen < dstsz - 1 ? slen : dstsz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

#define CFETCH_MAX_MODULES      32
#define CFETCH_MAX_CUSTOM       16
#define CFETCH_MAX_LINE         512
#define CFETCH_MAX_PATH         256
#define CFETCH_MAX_VALUE        256
#define CFETCH_MAX_ASCII_LINES  64
#define CFETCH_MAX_ASCII_WIDTH  80

/* Info module identifiers */
typedef enum {
    MOD_OS = 0,
    MOD_KERNEL,
    MOD_HOSTNAME,
    MOD_SHELL,
    MOD_TERMINAL,
    MOD_UPTIME,
    MOD_CPU,
    MOD_GPU,
    MOD_MEMORY,
    MOD_DISK,
    MOD_DE_WM,
    MOD_PACKAGES,
    MOD_LOCAL_IP,
    MOD_PUBLIC_IP,
    MOD_RESOLUTION,
    MOD_BATTERY,
    MOD_COUNT
} cfetch_module_t;

/* Custom field: either static value or command output */
typedef struct {
    char label[64];
    char value[CFETCH_MAX_VALUE]; /* static value or command */
    bool is_command;              /* true = execute value as shell command */
} cfetch_custom_field_t;

/* Color configuration */
typedef struct {
    int logo_color;   /* ANSI color code (0-255, -1 = default) */
    int label_color;
    int value_color;
    int separator_color;
} cfetch_colors_t;

/* Layout configuration */
typedef struct {
    int ascii_width;    /* padded width of ascii column */
    int padding;        /* spaces between logo and info */
    bool ascii_right;   /* false=left (default), true=right */
} cfetch_layout_t;

/* Full configuration */
typedef struct {
    char config_path[CFETCH_MAX_PATH];
    char ascii_path[CFETCH_MAX_PATH];   /* path to ascii art file, empty = auto */
    char distro_id[64];                 /* auto-detected distro id */

    bool modules[MOD_COUNT];            /* enabled modules */
    int  module_order[MOD_COUNT];       /* display order */
    int  module_count;                  /* number of enabled modules in order */

    cfetch_custom_field_t custom_fields[CFETCH_MAX_CUSTOM];
    int custom_field_count;

    cfetch_colors_t colors;
    cfetch_layout_t layout;

    bool public_ip_enabled;
    int  public_ip_timeout_ms;          /* timeout for public IP fetch */

    char separator[8];                  /* separator between label and value */
    bool minimal_ascii;                 /* 1 = small pfetch-style logo + fewer modules */
} cfetch_config_t;

/* Initialize config with defaults */
void config_init(cfetch_config_t *cfg);

/* Load config from file; returns 0 on success, -1 if not found */
int config_load(cfetch_config_t *cfg);

/* Save config to file; returns 0 on success */
int config_save(const cfetch_config_t *cfg);

/* Get the config directory path (~/.config/cfetch/) */
void config_get_dir(char *buf, int buflen);

/* Get module name string */
const char *config_module_name(cfetch_module_t mod);

#endif /* CFETCH_CONFIG_H */
