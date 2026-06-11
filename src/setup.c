/* src/setup.c — cfetch interactive setup wizard */
#include "setup.h"
#include "config.h"
#include "sysinfo.h"
#include "ascii_extract.h"
#include "color_tui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>

/* ─── Helpers ─────────────────────────────────────────────── */

static void mkdir_p(const char *path)
{
    char tmp[CFETCH_MAX_PATH];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (len && tmp[len - 1] == '/') tmp[len - 1] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

static void rstrip(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n-1] == '\n' || s[n-1] == '\r' || s[n-1] == ' ' || s[n-1] == '\t'))
        s[--n] = '\0';
}

static int prompt_int(const char *msg, int dflt)
{
    char buf[64];
    printf("%s [%d]: ", msg, dflt);
    fflush(stdout);
    if (!fgets(buf, sizeof(buf), stdin)) return dflt;
    rstrip(buf);
    if (!buf[0]) return dflt;
    char *end;
    long v = strtol(buf, &end, 10);
    if (end == buf) return dflt;
    return (int)v;
}

static int prompt_yn(const char *msg, int dflt_yes)
{
    char buf[16];
    printf("%s [%s]: ", msg, dflt_yes ? "Y/n" : "y/N");
    fflush(stdout);
    if (!fgets(buf, sizeof(buf), stdin)) return dflt_yes;
    rstrip(buf);
    if (!buf[0]) return dflt_yes;
    char c = (char)tolower((unsigned char)buf[0]);
    return c == 'y';
}

static void prompt_str(const char *msg, char *out, size_t n, const char *dflt)
{
    char buf[512];
    printf("%s [%s]: ", msg, dflt ? dflt : "");
    fflush(stdout);
    if (!fgets(buf, sizeof(buf), stdin)) { scopy(out, n, dflt ? dflt : ""); return; }
    rstrip(buf);
    scopy(out, n, buf[0] ? buf : (dflt ? dflt : ""));
}

/* ─── Module defaults ─────────────────────────────────────── */

/* Minimal mode: only the essentials */
static const cfetch_module_t minimal_set[] = {
    MOD_OS, MOD_KERNEL, MOD_UPTIME, MOD_SHELL, MOD_CPU, MOD_MEMORY, MOD_DISK
};
static const int minimal_set_n = (int)(sizeof(minimal_set)/sizeof(minimal_set[0]));

/* Full mode default modules */
static const cfetch_module_t full_set[] = {
    MOD_OS, MOD_HOSTNAME, MOD_KERNEL, MOD_UPTIME, MOD_PACKAGES, MOD_SHELL,
    MOD_DE_WM, MOD_TERMINAL, MOD_CPU, MOD_GPU, MOD_MEMORY, MOD_DISK,
    MOD_LOCAL_IP
};
static const int full_set_n = (int)(sizeof(full_set)/sizeof(full_set[0]));

static void load_defaults(cfetch_config_t *cfg, int minimal)
{
    for (int i = 0; i < MOD_COUNT; i++) cfg->modules[i] = false;
    cfg->module_count = 0;

    const cfetch_module_t *src = minimal ? minimal_set : full_set;
    int n                       = minimal ? minimal_set_n : full_set_n;
    for (int i = 0; i < n; i++) {
        cfg->modules[src[i]] = true;
        cfg->module_order[cfg->module_count++] = (int)src[i];
    }
}

static void print_module_list(const cfetch_config_t *cfg)
{
    printf("\n\033[1;36m═══ Modules ═══\033[0m\n");
    printf("  (\033[32m✓\033[0m = enabled, \033[31m✗\033[0m = disabled)\n\n");
    for (int i = 0; i < MOD_COUNT; i++) {
        int on = cfg->modules[i];
        printf("  %2d) %s %s\n", i + 1,
               on ? "\033[32m✓\033[0m" : "\033[31m✗\033[0m",
               config_module_name((cfetch_module_t)i));
    }
    printf("\n   0) Done\n");
}

static void module_toggle(cfetch_config_t *cfg, cfetch_module_t m)
{
    cfg->modules[m] = !cfg->modules[m];
    /* Rebuild module_order to match modules[] in enum order */
    cfg->module_count = 0;
    for (int i = 0; i < MOD_COUNT; i++) {
        if (cfg->modules[i])
            cfg->module_order[cfg->module_count++] = i;
    }
}

/* ─── ASCII art ───────────────────────────────────────────── */

static void setup_ascii(cfetch_config_t *cfg, int *out_custom_chosen)
{
    *out_custom_chosen = 0;

    printf("\n\033[1;36m═══ ASCII Art ═══\033[0m\n");
    printf("Detected distro: %s\n\n", cfg->distro_id[0] ? cfg->distro_id : "(unknown)");

    printf("  1) Your distro (%s)\n", cfg->distro_id[0] ? cfg->distro_id : "linux");
    printf("  2) Custom (paste your own)\n");
    printf("  3) Default Tux penguin\n");

    int choice = prompt_int("\nSelect ASCII art", 1);

    if (choice == 2) {
        char dir[CFETCH_MAX_PATH];
        config_get_dir(dir, CFETCH_MAX_PATH);
        mkdir_p(dir);

        char custom_path[CFETCH_MAX_PATH + 32];
        snprintf(custom_path, sizeof(custom_path), "%s/custom_art.txt", dir);

        printf("\nPaste your ASCII art below (enter a blank line when done):\n");
        FILE *fp = fopen(custom_path, "w");
        if (!fp) {
            printf("Error: cannot create %s (%s)\n", custom_path, strerror(errno));
            scopy(cfg->ascii_path, sizeof(cfg->ascii_path), "");
            return;
        }
        char line[512];
        while (fgets(line, sizeof(line), stdin)) {
            if (line[0] == '\n') break;
            fputs(line, fp);
        }
        fclose(fp);
        scopy(cfg->ascii_path, sizeof(cfg->ascii_path), custom_path);
        printf("Custom art saved to %s\n", custom_path);
        *out_custom_chosen = 1;
        return;
    }

    if (choice == 3) {
        scopy(cfg->distro_id, sizeof(cfg->distro_id), "tux");
        scopy(cfg->ascii_path, sizeof(cfg->ascii_path), "");
        return;
    }

    char resolved[CFETCH_MAX_PATH];
    if (ascii_extract_resolve(cfg->distro_id, cfg->minimal_ascii,
        resolved, sizeof(resolved)) == 0)
        scopy(cfg->ascii_path, sizeof(cfg->ascii_path), resolved);
    else
        scopy(cfg->ascii_path, sizeof(cfg->ascii_path), "");
}

/* ─── Size mode ───────────────────────────────────────────── */

static void setup_size(cfetch_config_t *cfg, int custom_chosen)
{
    if (custom_chosen) {
        cfg->minimal_ascii = false;
        return;
    }

    printf("\n\033[1;36m═══ ASCII Size ═══\033[0m\n");
    printf("  1) Full (large, detailed logo)\n");
    printf("  2) Minimal (small, compact logo + fewer modules)\n");

    int s = prompt_int("\nSelect size", 1);
    cfg->minimal_ascii = (s == 2);
}

/* ─── Colors ──────────────────────────────────────────────── */

static void print_color_table(void)
{
    printf("\nANSI 256-color palette (use -1 for auto):\n");
    for (int i = 0; i < 16; i++) {
        printf("  \033[48;5;%dm  %3d  \033[0m", i, i);
        if ((i + 1) % 8 == 0) printf("\n");
    }
    printf("\n");
}

static void setup_colors(cfetch_config_t *cfg)
{
    if (!prompt_yn("\nConfigure colors?", 0)) return;
    printf("\n\033[1;36m═══ Colors ═══\033[0m");
    print_color_table();
    cfg->colors.logo_color      = prompt_int("Logo color",      -1);
    cfg->colors.label_color     = prompt_int("Label color",     -1);
    cfg->colors.value_color     = prompt_int("Value color",     -1);
    cfg->colors.separator_color = prompt_int("Separator color", -1);
}

/* ─── Modules ─────────────────────────────────────────────── */

static void setup_modules(cfetch_config_t *cfg)
{
    if (!prompt_yn("\nCustomize displayed modules?", 0)) return;

    for (;;) {
        print_module_list(cfg);
        int n = prompt_int("\nToggle module #", 0);
        if (n == 0) break;
        if (n < 1 || n > MOD_COUNT) {
            printf("Invalid selection.\n");
            continue;
        }
        module_toggle(cfg, (cfetch_module_t)(n - 1));
    }
}

static void setup_custom_modules(cfetch_config_t *cfg)
{
    if (!prompt_yn("\nAdd custom modules (shell commands)?", 0)) return;

    for (;;) {
        printf("\n\033[1;36m═══ Custom Modules ═══\033[0m\n");
        for (int i = 0; i < cfg->custom_field_count; i++) {
            const cfetch_custom_field_t *f = &cfg->custom_fields[i];
            printf("  %2d) %-16s %s %s\n", i+1, f->label,
                   f->is_command ? "$" : "=", f->value);
        }
        printf("  a) Add new\n");
        printf("  d) Delete by number\n");
        printf("  0) Done\n");
        printf("Choice: ");
        fflush(stdout);
        char buf[16]; if (!fgets(buf,sizeof(buf),stdin)) break;
        rstrip(buf);
        if (!buf[0] || buf[0]=='0') break;

        if (buf[0]=='a' || buf[0]=='A') {
            if (cfg->custom_field_count >= CFETCH_MAX_CUSTOM) {
                printf("Max custom fields reached.\n"); continue;
            }
            cfetch_custom_field_t *f = &cfg->custom_fields[cfg->custom_field_count];
            prompt_str("  Label", f->label, sizeof(f->label), "MyField");
            int as_cmd = prompt_yn("  Is this a shell command?", 1);
            f->is_command = as_cmd ? true : false;
            prompt_str(as_cmd ? "  Command" : "  Value",
                       f->value, sizeof(f->value), "");
            if (f->label[0] && f->value[0]) cfg->custom_field_count++;
        } else if (buf[0]=='d' || buf[0]=='D') {
            int idx = prompt_int("  Delete which #", 0);
            if (idx >= 1 && idx <= cfg->custom_field_count) {
                for (int i = idx-1; i < cfg->custom_field_count-1; i++)
                    cfg->custom_fields[i] = cfg->custom_fields[i+1];
                cfg->custom_field_count--;
            }
        }
    }
}

/* ─── Layout ──────────────────────────────────────────────── */

static void setup_layout(cfetch_config_t *cfg)
{
    if (!prompt_yn("\nCustomize layout (padding/separator)?", 0)) return;
    cfg->layout.padding = prompt_int("Padding",
                                     cfg->layout.padding > 0 ? cfg->layout.padding : 2);
    prompt_str("Separator", cfg->separator, sizeof(cfg->separator),
               cfg->separator[0] ? cfg->separator : ": ");
    cfg->layout.ascii_right = prompt_yn("Place ASCII on the right?",
                                        cfg->layout.ascii_right);
}

/* ─── Network ─────────────────────────────────────────────── */

static void setup_network(cfetch_config_t *cfg)
{
    if (!prompt_yn("\nFetch public IP (slower; needs network)?",
        cfg->public_ip_enabled)) {
        cfg->public_ip_enabled = false;
    return;
        }
        cfg->public_ip_enabled = true;
        cfg->public_ip_timeout_ms = prompt_int("Public IP timeout (ms)",
                                               cfg->public_ip_timeout_ms > 0 ? cfg->public_ip_timeout_ms : 2000);
}

/* ─── Entry point ─────────────────────────────────────────── */

int setup_wizard(cfetch_config_t *cfg)
{
    printf("\033[1;36m");
    printf("╔══════════════════════════════════════╗\n");
    printf("║       Cfetch Setup Wizard            ║\n");
    printf("╚══════════════════════════════════════╝\033[0m\n\n");
    printf("Welcome! This wizard will configure cfetch for your system.\n");

    if (!cfg->distro_id[0])
        sysinfo_detect_distro(cfg->distro_id, (int)sizeof(cfg->distro_id));
    printf("Detected distro: %s\n", cfg->distro_id[0] ? cfg->distro_id : "(unknown)");

    int custom_chosen = 0;
    setup_ascii(cfg, &custom_chosen);
    setup_size(cfg, custom_chosen);

    /* Re-resolve ascii_path now that minimal flag is finalized
     * (skip if user chose a custom path). */
    if (!custom_chosen && cfg->distro_id[0]) {
        char resolved[CFETCH_MAX_PATH];
        if (ascii_extract_resolve(cfg->distro_id, cfg->minimal_ascii,
            resolved, sizeof(resolved)) == 0) {
            scopy(cfg->ascii_path, sizeof(cfg->ascii_path), resolved);
            }
    }

    /* Seed module defaults based on size choice */
    load_defaults(cfg, cfg->minimal_ascii);

    setup_colors(cfg);
if (prompt_yn("\nLaunch interactive color editor (TUI)?", 0)) {
        /* Make sure ascii_path is resolved before launching TUI */
        if (!cfg->ascii_path[0] && cfg->distro_id[0]) {
            char resolved[CFETCH_MAX_PATH];
            if (ascii_extract_resolve(cfg->distro_id, cfg->minimal_ascii,
                                      resolved, sizeof(resolved)) == 0)
                scopy(cfg->ascii_path, sizeof(cfg->ascii_path), resolved);
        }
        char dir[CFETCH_MAX_PATH];
        config_get_dir(dir, CFETCH_MAX_PATH);
        mkdir_p(dir);
        config_save(cfg);
        color_tui_run(cfg);
    }
    setup_modules(cfg);
    setup_custom_modules(cfg);
    setup_layout(cfg);
    setup_network(cfg);

    /* Persist */
    char dir[CFETCH_MAX_PATH];
    config_get_dir(dir, CFETCH_MAX_PATH);
    mkdir_p(dir);
    if (config_save(cfg) == 0)
        printf("\n\033[32m✓ Configuration saved.\033[0m\n\n");
    else
        printf("\n\033[31m✗ Failed to save configuration.\033[0m\n\n");

    return 0;
}
