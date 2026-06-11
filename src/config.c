#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <pwd.h>
#include <unistd.h>

static const char *module_names[MOD_COUNT] = {
    [MOD_OS]        = "OS",
    [MOD_KERNEL]    = "Kernel",
    [MOD_HOSTNAME]  = "Hostname",
    [MOD_SHELL]     = "Shell",
    [MOD_TERMINAL]  = "Terminal",
    [MOD_UPTIME]    = "Uptime",
    [MOD_CPU]       = "CPU",
    [MOD_GPU]       = "GPU",
    [MOD_MEMORY]    = "Memory",
    [MOD_DISK]      = "Disk",
    [MOD_DE_WM]     = "DE/WM",
    [MOD_PACKAGES]  = "Packages",
    [MOD_LOCAL_IP]  = "Local IP",
    [MOD_PUBLIC_IP] = "Public IP",
    [MOD_RESOLUTION]= "Resolution",
    [MOD_BATTERY]   = "Battery",
};

const char *config_module_name(cfetch_module_t mod)
{
    if (mod >= 0 && mod < MOD_COUNT)
        return module_names[mod];
    return "Unknown";
}

static const char *get_home_dir(void)
{
    const char *home = getenv("HOME");
    if (home) return home;
    struct passwd *pw = getpwuid(getuid());
    return pw ? pw->pw_dir : "/tmp";
}

void config_get_dir(char *buf, int buflen)
{
    snprintf(buf, (size_t)buflen, "%s/.config/cfetch", get_home_dir());
}

static void ensure_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) == -1) {
        /* Try to create parent first */
        char parent[CFETCH_MAX_PATH];
        snprintf(parent, sizeof(parent), "%s/.config", get_home_dir());
        if (stat(parent, &st) == -1) {
            mkdir(parent, 0755);
        }
        mkdir(path, 0755);
    }
}

void config_init(cfetch_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

    config_get_dir(cfg->config_path, CFETCH_MAX_PATH);
    {
        size_t cur = strlen(cfg->config_path);
        scopy(cfg->config_path + cur, CFETCH_MAX_PATH - cur, "/config");
    }

    /* Enable all standard modules by default */
    for (int i = 0; i < MOD_COUNT; i++) {
        cfg->modules[i] = true;
        cfg->module_order[i] = i;
    }
    /* Public IP disabled by default (network call) */
    cfg->modules[MOD_PUBLIC_IP] = false;
    cfg->module_count = MOD_COUNT;

    cfg->colors.logo_color = -1;       /* auto/default */
    cfg->colors.label_color = -1;
    cfg->colors.value_color = -1;
    cfg->colors.separator_color = -1;

    cfg->layout.ascii_width = 0;       /* auto */
    cfg->layout.padding = 2;
    cfg->layout.ascii_right = false;

    cfg->public_ip_enabled = false;
    cfg->public_ip_timeout_ms = 2000;

    scopy(cfg->separator, sizeof(cfg->separator), ": ");
    cfg->custom_field_count = 0;
}

int config_load(cfetch_config_t *cfg)
{
    FILE *fp = fopen(cfg->config_path, "r");
    if (!fp) return -1;

    char line[CFETCH_MAX_LINE];
    /* Reset module order for reconstruction */
    cfg->module_count = 0;
    for (int i = 0; i < MOD_COUNT; i++)
        cfg->modules[i] = false;

    while (fgets(line, sizeof(line), fp)) {
        /* Strip newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        /* Skip empty lines and comments */
        if (len == 0 || line[0] == '#') continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = line;
        char *val = eq + 1;

        /* Trim leading spaces from value */
        while (*val == ' ') val++;

        if (strcmp(key, "ascii_path") == 0) {
            scopy(cfg->ascii_path, sizeof(cfg->ascii_path), val);
        } else if (strcmp(key, "distro_id") == 0) {
            scopy(cfg->distro_id, sizeof(cfg->distro_id), val);
        } else if (strcmp(key, "color_logo") == 0) {
            cfg->colors.logo_color = atoi(val);
        } else if (strcmp(key, "color_label") == 0) {
            cfg->colors.label_color = atoi(val);
        } else if (strcmp(key, "minimal_ascii") == 0) {
            cfg->minimal_ascii = atoi(val);
        } else if (strcmp(key, "color_value") == 0) {
            cfg->colors.value_color = atoi(val);
        } else if (strcmp(key, "color_separator") == 0) {
            cfg->colors.separator_color = atoi(val);
        } else if (strcmp(key, "ascii_width") == 0) {
            cfg->layout.ascii_width = atoi(val);
        } else if (strcmp(key, "padding") == 0) {
            cfg->layout.padding = atoi(val);
        } else if (strcmp(key, "ascii_right") == 0) {
            cfg->layout.ascii_right = (strcmp(val, "true") == 0);
        } else if (strcmp(key, "public_ip") == 0) {
            cfg->public_ip_enabled = (strcmp(val, "true") == 0);
            cfg->modules[MOD_PUBLIC_IP] = cfg->public_ip_enabled;
        } else if (strcmp(key, "public_ip_timeout") == 0) {
            cfg->public_ip_timeout_ms = atoi(val);
        } else if (strcmp(key, "separator") == 0) {
            scopy(cfg->separator, sizeof(cfg->separator), val);
        } else if (strcmp(key, "module") == 0) {
            /* Match module name to enum */
            for (int i = 0; i < MOD_COUNT; i++) {
                if (strcasecmp(val, module_names[i]) == 0) {
                    cfg->modules[i] = true;
                    cfg->module_order[cfg->module_count++] = i;
                    break;
                }
            }
        } else if (strcmp(key, "custom_static") == 0) {
            /* format: label|value */
            if (cfg->custom_field_count < CFETCH_MAX_CUSTOM) {
                cfetch_custom_field_t *f =
                    &cfg->custom_fields[cfg->custom_field_count];
                char *pipe = strchr(val, '|');
                if (pipe) {
                    *pipe = '\0';
                    scopy(f->label, sizeof(f->label), val);
                    scopy(f->value, sizeof(f->value), pipe + 1);
                    f->is_command = false;
                    cfg->custom_field_count++;
                }
            }
        } else if (strcmp(key, "custom_cmd") == 0) {
            /* format: label|command */
            if (cfg->custom_field_count < CFETCH_MAX_CUSTOM) {
                cfetch_custom_field_t *f =
                    &cfg->custom_fields[cfg->custom_field_count];
                char *pipe = strchr(val, '|');
                if (pipe) {
                    *pipe = '\0';
                    scopy(f->label, sizeof(f->label), val);
                    scopy(f->value, sizeof(f->value), pipe + 1);
                    f->is_command = true;
                    cfg->custom_field_count++;
                }
            }
        }
    }

    /* If no modules were listed, enable defaults */
    if (cfg->module_count == 0) {
        for (int i = 0; i < MOD_COUNT; i++) {
            cfg->modules[i] = true;
            cfg->module_order[i] = i;
        }
        cfg->modules[MOD_PUBLIC_IP] = cfg->public_ip_enabled;
        cfg->module_count = MOD_COUNT;
    }

    fclose(fp);
    return 0;
}

int config_save(const cfetch_config_t *cfg)
{
    char dir[CFETCH_MAX_PATH];
    config_get_dir(dir, CFETCH_MAX_PATH);
    ensure_dir(dir);

    FILE *fp = fopen(cfg->config_path, "w");
    if (!fp) {
        fprintf(stderr, "cfetch: cannot write config: %s\n", strerror(errno));
        return -1;
    }

    fprintf(fp, "minimal_ascii=%d\n", cfg->minimal_ascii);
    fprintf(fp, "# Cfetch configuration\n");
    fprintf(fp, "# Generated by cfetch setup wizard\n\n");

    if (cfg->distro_id[0])
        fprintf(fp, "distro_id=%s\n", cfg->distro_id);
    if (cfg->ascii_path[0])
        fprintf(fp, "ascii_path=%s\n", cfg->ascii_path);

    fprintf(fp, "\n# Colors (-1 = auto/default, 0-255 = ANSI color)\n");
    fprintf(fp, "color_logo=%d\n", cfg->colors.logo_color);
    fprintf(fp, "color_label=%d\n", cfg->colors.label_color);
    fprintf(fp, "color_value=%d\n", cfg->colors.value_color);
    fprintf(fp, "color_separator=%d\n", cfg->colors.separator_color);

    fprintf(fp, "\n# Layout\n");
    fprintf(fp, "ascii_width=%d\n", cfg->layout.ascii_width);
    fprintf(fp, "padding=%d\n", cfg->layout.padding);
    fprintf(fp, "ascii_right=%s\n", cfg->layout.ascii_right ? "true" : "false");
    fprintf(fp, "separator=%s\n", cfg->separator);

    fprintf(fp, "\n# Network\n");
    fprintf(fp, "public_ip=%s\n", cfg->public_ip_enabled ? "true" : "false");
    fprintf(fp, "public_ip_timeout=%d\n", cfg->public_ip_timeout_ms);

    fprintf(fp, "\n# Enabled modules (in display order)\n");
    for (int i = 0; i < cfg->module_count; i++) {
        int mod = cfg->module_order[i];
        if (mod >= 0 && mod < MOD_COUNT && cfg->modules[mod]) {
            fprintf(fp, "module=%s\n", module_names[mod]);
        }
    }

    if (cfg->custom_field_count > 0) {
        fprintf(fp, "\n# Custom fields\n");
        for (int i = 0; i < cfg->custom_field_count; i++) {
            const cfetch_custom_field_t *f = &cfg->custom_fields[i];
            if (f->is_command)
                fprintf(fp, "custom_cmd=%s|%s\n", f->label, f->value);
            else
                fprintf(fp, "custom_static=%s|%s\n", f->label, f->value);
        }
    }

    fclose(fp);
    return 0;
}
