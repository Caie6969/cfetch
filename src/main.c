#include "config.h"
#include "sysinfo.h"
#include "setup.h"
#include "render.h"
#include "color_tui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

static void print_usage(void)
{
    printf("Usage: cfetch [OPTIONS]\n\n");
    printf("A fast system information tool written in C.\n\n");
    printf("Options:\n");
    printf("  --setup              Run the interactive setup wizard\n");
    printf("  --colors             Launch the interactive color editor (TUI)\n");
    printf("  --color-editor       Alias for --colors\n");
    printf("  --no-color           Disable colored output\n");
    printf("  --json               Output system info as JSON\n");
    printf("  -h, --help           Show this help message\n");
    printf("  -v, --version        Show version information\n");
}

static void print_version(void) { printf("cfetch 1.0.0\n"); }

int main(int argc, char *argv[])
{
    bool force_setup  = false;
    bool no_color     = false;
    bool json_output  = false;
    bool run_colors   = false;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--setup"))                 force_setup = true;
        else if (!strcmp(argv[i], "--no-color"))         no_color = true;
        else if (!strcmp(argv[i], "--json"))             json_output = true;
        else if (!strcmp(argv[i], "--colors") ||
            !strcmp(argv[i], "--color-editor"))     run_colors = true;
        else if (!strcmp(argv[i], "--help") ||
            !strcmp(argv[i], "-h"))                 { print_usage();   return 0; }
            else if (!strcmp(argv[i], "--version") ||
                !strcmp(argv[i], "-v"))                 { print_version(); return 0; }
                else {
                    fprintf(stderr, "cfetch: unknown option '%s'\n", argv[i]);
                    print_usage();
                    return 1;
                }
    }

    if (run_colors) {
        cfetch_config_t cfg;
        config_init(&cfg);
        if (config_load(&cfg) != 0) {
            fprintf(stderr, "cfetch: no config found — run 'cfetch --setup' first\n");
            return 1;
        }
        int rc = color_tui_run(&cfg);
        if (rc < 0) {
            fprintf(stderr, "cfetch: color editor failed (rc=%d)\n", rc);
            return 1;
        }
        return 0;
    }

    if (!isatty(STDOUT_FILENO)) no_color = true;

    cfetch_config_t cfg;
    config_init(&cfg);

    int loaded = config_load(&cfg);
    if (loaded != 0 || force_setup) {
        config_init(&cfg);
        sysinfo_detect_distro(cfg.distro_id, (int)sizeof(cfg.distro_id));
        int ret = setup_wizard(&cfg);
        if (ret != 0) { fprintf(stderr, "cfetch: setup failed\n"); return 1; }
        if (force_setup) return 0;
    }

    sysinfo_t info;
    sysinfo_gather(&info, &cfg);
    if (!cfg.distro_id[0] && info.distro_id[0])
        scopy(cfg.distro_id, sizeof(cfg.distro_id), info.distro_id);

    if (json_output) render_json(&info, &cfg);
    else             render_output(&info, &cfg, !no_color);

    return 0;
}
