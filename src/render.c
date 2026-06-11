#include "render.h"
#include "ascii_extract.h"
#include "color_tui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>


/* ─── Color Helpers ───────────────────────────────────────── */

/* ─── per-cell color overlay ──────────────────────────────── */
static color_overlay_t g_overlay;
static int g_overlay_loaded = 0;
static char g_overlay_path[1024] = {0};

static void overlay_ensure(const cfetch_config_t *cfg)
{
    char path[1024];
    if (cfg->ascii_path[0] == '/' || cfg->ascii_path[0] == '~') {
        snprintf(path, sizeof(path), "%s", cfg->ascii_path);
    } else if (cfg->distro_id[0]) {
        if (ascii_extract_resolve(cfg->distro_id,
            cfg->minimal_ascii?1:0, path, sizeof(path)) != 0) return;
    } else { return; }

    if (g_overlay_loaded && strcmp(g_overlay_path, path) == 0) return;
    if (g_overlay_loaded) color_overlay_free(&g_overlay);
    g_overlay_loaded = 0;
    if (color_overlay_load(path, &g_overlay) == 0) {
        g_overlay_loaded = 1;
        snprintf(g_overlay_path, sizeof(g_overlay_path), "%s", path);
    }
}

/* Returns ANSI escape prefix for (row, col) or NULL. */
static const char *overlay_at(int row, int col)
{
    if (!g_overlay_loaded) return NULL;
    if (row < 0 || row >= g_overlay.rows) return NULL;
    if (col < 0 || col >= g_overlay.cols) return NULL;
    return g_overlay.cells[row * g_overlay.cols + col];
}

static void print_logo_line(const char *line, int row,
                            const char *c_logo, const char *c_reset)
{
    if (!g_overlay_loaded) {
        printf("%s%s%s", c_logo, line, c_reset);
        return;
    }
    /* Overlay active: strip any embedded ANSI from source; emit our own. */
    int col = 0;
    int in_esc = 0;
    const char *last = NULL;   /* NULL = nothing emitted, "" = default fg */
    for (const char *p = line; *p; p++) {
        if (*p == '\033') { in_esc = 1; continue; }
        if (in_esc) {
            if ((*p >= '@' && *p <= '~')) in_esc = 0;
            continue;
        }
        const char *ov = overlay_at(row, col);
        if (ov) {
            if (ov != last) { fputs(ov, stdout); last = ov; }
        } else {
            if (last != (const char *)"") {
                fputs("\033[39m", stdout);
                last = (const char *)"";
            }
        }
        putchar(*p);
        col++;
    }
    fputs(c_reset, stdout);
}

/* Default distro colors */
static int distro_default_color(const char *distro_id)
{
    if (!distro_id || !distro_id[0]) return 4; /* blue */

        /* A */
        if (strcmp(distro_id, "aix") == 0)            return 2;   /* green */
            if (strcmp(distro_id, "almalinux") == 0)      return 1;   /* red */
                if (strcmp(distro_id, "alpine") == 0)         return 4;   /* blue */
                    if (strcmp(distro_id, "alter") == 0)          return 6;   /* cyan */
                        if (strcmp(distro_id, "amazon") == 0)         return 3;   /* yellow */
                            if (strcmp(distro_id, "anarchy") == 0)        return 7;   /* white */
                                if (strcmp(distro_id, "android") == 0)        return 2;   /* green */
                                    if (strcmp(distro_id, "antergos") == 0)       return 4;   /* blue */
                                        if (strcmp(distro_id, "antiX") == 0)          return 1;   /* red */
                                            if (strcmp(distro_id, "aosc") == 0)           return 4;   /* blue */
                                                if (strcmp(distro_id, "apricity") == 0)       return 4;   /* blue */
                                                    if (strcmp(distro_id, "arch") == 0)           return 6;   /* cyan */
                                                        if (strcmp(distro_id, "archbox") == 0)        return 2;   /* green */
                                                            if (strcmp(distro_id, "archcraft") == 0)      return 6;   /* cyan */
                                                                if (strcmp(distro_id, "archlabs") == 0)       return 6;   /* cyan */
                                                                    if (strcmp(distro_id, "archmerge") == 0)      return 6;   /* cyan */
                                                                        if (strcmp(distro_id, "archstrike") == 0)     return 8;   /* dark gray */
                                                                            if (strcmp(distro_id, "arcolinux") == 0)      return 7;   /* white */
                                                                                if (strcmp(distro_id, "artix") == 0)          return 6;   /* cyan */
                                                                                    if (strcmp(distro_id, "arya") == 0)           return 2;   /* green */
                                                                                        if (strcmp(distro_id, "asteroidos") == 0)     return 160; /* dark red */
                                                                                            if (strcmp(distro_id, "ataraxia") == 0)       return 4;   /* blue */

                                                                                                /* B */
                                                                                                if (strcmp(distro_id, "bedrock") == 0)        return 8;   /* dark gray */
                                                                                                    if (strcmp(distro_id, "bitrig") == 0)         return 2;   /* green */
                                                                                                        if (strcmp(distro_id, "blackarch") == 0)      return 1;   /* red */
                                                                                                            if (strcmp(distro_id, "blag") == 0)           return 5;   /* magenta */
                                                                                                                if (strcmp(distro_id, "blankon") == 0)        return 1;   /* red */
                                                                                                                    if (strcmp(distro_id, "bluelight") == 0)      return 7;   /* white */
                                                                                                                        if (strcmp(distro_id, "bodhi") == 0)          return 7;   /* white */
                                                                                                                            if (strcmp(distro_id, "buildroot") == 0)      return 3;   /* yellow */
                                                                                                                                if (strcmp(distro_id, "bunsenlabs") == 0)     return 7;   /* white */

                                                                                                                                    /* C */
                                                                                                                                    if (strcmp(distro_id, "calculate") == 0)      return 7;   /* white */
                                                                                                                                        if (strcmp(distro_id, "carbs") == 0)          return 4;   /* blue */
                                                                                                                                            if (strcmp(distro_id, "celos") == 0)          return 4;   /* blue */
                                                                                                                                                if (strcmp(distro_id, "centos") == 0)         return 3;   /* yellow */
                                                                                                                                                    if (strcmp(distro_id, "chakra") == 0)         return 4;   /* blue */
                                                                                                                                                        if (strcmp(distro_id, "chaletos") == 0)       return 4;   /* blue */
                                                                                                                                                            if (strcmp(distro_id, "chapeau") == 0)        return 2;   /* green */
                                                                                                                                                                if (strcmp(distro_id, "chromeos") == 0)       return 2;   /* green */
                                                                                                                                                                    if (strcmp(distro_id, "cleanjaro") == 0)      return 7;   /* white */
                                                                                                                                                                        if (strcmp(distro_id, "clearlinux") == 0)     return 4;   /* blue */
                                                                                                                                                                            if (strcmp(distro_id, "clearos") == 0)        return 2;   /* green */
                                                                                                                                                                                if (strcmp(distro_id, "clover") == 0)         return 2;   /* green */
                                                                                                                                                                                    if (strcmp(distro_id, "condres") == 0)        return 2;   /* green */
                                                                                                                                                                                        if (strcmp(distro_id, "crux") == 0)           return 4;   /* blue */
                                                                                                                                                                                            if (strcmp(distro_id, "crystalLinux") == 0)   return 6;   /* cyan */
                                                                                                                                                                                                if (strcmp(distro_id, "cyberos") == 0)        return 50;  /* teal */

                                                                                                                                                                                                    /* D */
                                                                                                                                                                                                    if (strcmp(distro_id, "dahlia") == 0)         return 5;   /* magenta */
                                                                                                                                                                                                        if (strcmp(distro_id, "darkos") == 0)         return 1;   /* red */
                                                                                                                                                                                                            if (strcmp(distro_id, "darwin") == 0)         return 2;   /* green */
                                                                                                                                                                                                                if (strcmp(distro_id, "debian") == 0)         return 1;   /* red */
                                                                                                                                                                                                                    if (strcmp(distro_id, "deepin") == 0)         return 2;   /* green */
                                                                                                                                                                                                                        if (strcmp(distro_id, "desaos") == 0)         return 2;   /* green */
                                                                                                                                                                                                                            if (strcmp(distro_id, "devuan") == 0)         return 5;   /* magenta */
                                                                                                                                                                                                                                if (strcmp(distro_id, "dracos") == 0)         return 1;   /* red */
                                                                                                                                                                                                                                    if (strcmp(distro_id, "dragonfly") == 0)      return 1;   /* red */
                                                                                                                                                                                                                                        if (strcmp(distro_id, "drauger") == 0)        return 1;   /* red */

                                                                                                                                                                                                                                            /* E */
                                                                                                                                                                                                                                            if (strcmp(distro_id, "elementary") == 0)     return 4;   /* blue */
                                                                                                                                                                                                                                                if (strcmp(distro_id, "endeavouros") == 0)    return 1;   /* red */
                                                                                                                                                                                                                                                    if (strcmp(distro_id, "endless") == 0)        return 1;   /* red */
                                                                                                                                                                                                                                                        if (strcmp(distro_id, "eurolinux") == 0)      return 4;   /* blue */
                                                                                                                                                                                                                                                            if (strcmp(distro_id, "exherbo") == 0)        return 4;   /* blue */

                                                                                                                                                                                                                                                                /* F */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "fedora") == 0)         return 12;  /* bright blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "feren") == 0)          return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "freebsd") == 0)        return 1;   /* red */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "freemint") == 0)       return 7;   /* white */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "frugalware") == 0)     return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "funtoo") == 0)         return 5;   /* magenta */

                                                                                                                                                                                                                                                                /* G */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "galliumos") == 0)      return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "garuda") == 0)         return 7;   /* white */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "gentoo") == 0)         return 5;   /* magenta */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "gnome") == 0)          return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "gnu") == 0)            return 7;   /* white */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "gobolinux") == 0)      return 5;   /* magenta */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "grombyang") == 0)      return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "guix") == 0)           return 3;   /* yellow */

                                                                                                                                                                                                                                                                /* H */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "haiku") == 0)          return 1;   /* red */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "hash") == 0)           return 123; /* light blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "huayra") == 0)         return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "hydroos") == 0)        return 1;   /* red */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "hyperbola") == 0)      return 8;   /* dark gray */

                                                                                                                                                                                                                                                                /* I */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "iglunix") == 0)        return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "instantos") == 0)      return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "irix") == 0)           return 4;   /* blue */

                                                                                                                                                                                                                                                                /* J */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "januslinux") == 0)     return 4;   /* blue */

                                                                                                                                                                                                                                                                /* K */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "kaisen") == 0)         return 1;   /* red */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "kali") == 0)           return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "kaos") == 0)           return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "kde") == 0)            return 2;   /* green */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "kdeneon") == 0)        return 2;   /* green */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "kibojoe") == 0)        return 2;   /* green */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "kogaion") == 0)        return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "korora") == 0)         return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "kslinux") == 0)        return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "kubuntu") == 0)        return 4;   /* blue */

                                                                                                                                                                                                                                                                /* L */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "langitketujuh") == 0)  return 7;   /* white */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "laxeros") == 0)        return 7;   /* white */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "lede") == 0)           return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "libreelec") == 0)      return 2;   /* green */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "linuxlite") == 0)      return 3;   /* yellow */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "linuxmint") == 0)      return 2;   /* green */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "lmde") == 0)           return 2;   /* green */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "lubuntu") == 0)        return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "lunar") == 0)          return 4;   /* blue */

                                                                                                                                                                                                                                                                /* M */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "mageia") == 0)         return 6;   /* cyan */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "magpieos") == 0)       return 2;   /* green */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "mandrake") == 0)       return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "mandriva") == 0)       return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "manjaro") == 0)        return 2;   /* green */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "maui") == 0)           return 6;   /* cyan */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "mer") == 0)            return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "minix") == 0)          return 1;   /* red */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "mx") == 0)             return 4;   /* blue */

                                                                                                                                                                                                                                                                /* N */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "namib") == 0)          return 1;   /* red */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "neptune") == 0)        return 7;   /* white */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "netbsd") == 0)         return 5;   /* magenta */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "netrunner") == 0)      return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "nitrux") == 0)         return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "nixos") == 0)          return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "nurunner") == 0)       return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "nutyx") == 0)          return 4;   /* blue */

                                                                                                                                                                                                                                                                /* O */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "obarun") == 0)         return 6;   /* cyan */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "obrevenge") == 0)      return 1;   /* red */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "openbsd") == 0)        return 3;   /* yellow */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "openeuler") == 0)      return 1;   /* red */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "openindiana") == 0)    return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "openmamba") == 0)      return 2;   /* green */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "openmandriva") == 0)   return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "opensuse") == 0)       return 2;   /* green */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "openstage") == 0)      return 2;   /* green */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "openwrt") == 0)        return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "oracle") == 0)         return 1;   /* red */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "osmc") == 0)           return 4;   /* blue */

                                                                                                                                                                                                                                                                /* P */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "pacbsd") == 0)         return 1;   /* red */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "parabola") == 0)       return 5;   /* magenta */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "pardus") == 0)         return 3;   /* yellow */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "parrot") == 0)         return 6;   /* cyan */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "parsix") == 0)         return 3;   /* yellow */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "pcbsd") == 0)          return 1;   /* red */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "pclinuxos") == 0)      return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "pengwin") == 0)        return 5;   /* magenta */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "pentoo") == 0)         return 5;   /* magenta */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "peppermint") == 0)     return 1;   /* red */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "pisi") == 0)           return 12;  /* bright blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "pop_os") == 0)         return 6;   /* cyan */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "porteus") == 0)        return 6;   /* cyan */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "postmarketos") == 0)   return 2;   /* green */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "proxmox") == 0)        return 202; /* orange-red */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "puffos") == 0)         return 3;   /* yellow */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "pureos") == 0)         return 2;   /* green */

                                                                                                                                                                                                                                                                /* Q */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "qubes") == 0)          return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "qubyt") == 0)          return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "quibian") == 0)        return 3;   /* yellow */

                                                                                                                                                                                                                                                                /* R */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "radix") == 0)          return 1;   /* red */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "raspbian") == 0)       return 2;   /* green */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "reborn") == 0)         return 2;   /* green */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "redcore") == 0)        return 1;   /* red */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "redstar") == 0)        return 1;   /* red */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "regata") == 0)         return 7;   /* white */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "regolith") == 0)       return 1;   /* red */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "rfremix") == 0)        return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "rhel") == 0)           return 1;   /* red */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "rocky") == 0)          return 2;   /* green */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "rosa") == 0)           return 4;   /* blue */

                                                                                                                                                                                                                                                                /* S */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "sabayon") == 0)        return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "sailfish") == 0)       return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "salentOS") == 0)       return 2;   /* green */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "sambabox") == 0)       return 3;   /* yellow */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "scientific") == 0)     return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "septor") == 0)         return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "serene") == 0)         return 6;   /* cyan */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "serenityos") == 0)     return 6;   /* cyan */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "sharklinux") == 0)     return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "siduction") == 0)      return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "skiffos") == 0)        return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "slackware") == 0)      return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "slitaz") == 0)         return 3;   /* yellow */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "smartos") == 0)        return 6;   /* cyan */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "solaris") == 0)        return 3;   /* yellow */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "solus") == 0)          return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "source_mage") == 0)    return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "sparky") == 0)         return 1;   /* red */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "star") == 0)           return 7;   /* white */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "steamos") == 0)        return 5;   /* magenta */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "sunos") == 0)          return 3;   /* yellow */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "suse") == 0)           return 2;   /* green */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "swagarch") == 0)       return 4;   /* blue */

                                                                                                                                                                                                                                                                /* T */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "tails") == 0)          return 5;   /* magenta */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "tearch") == 0)         return 39;  /* sky blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "trisquel") == 0)       return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "trueos") == 0)         return 1;   /* red */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "tumbleweed") == 0)     return 2;   /* green */

                                                                                                                                                                                                                                                                /* U */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "ubuntu") == 0)         return 208; /* orange */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "ubuntu-budgie") == 0)  return 208; /* orange */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "ubuntu-cinnamon") == 0) return 208; /* orange */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "ubuntu-gnome") == 0)   return 208; /* orange */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "ubuntu-mate") == 0)    return 208; /* orange */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "ubuntu-studio") == 0)  return 208; /* orange */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "univention") == 0)     return 1;   /* red */

                                                                                                                                                                                                                                                                /* V */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "venom") == 0)          return 8;   /* dark gray */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "void") == 0)           return 2;   /* green */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "vnux") == 0)           return 11;  /* bright yellow */

                                                                                                                                                                                                                                                                /* W */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "windows") == 0)        return 1;   /* red */

                                                                                                                                                                                                                                                                /* X */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "xeonix") == 0)         return 4;   /* blue */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "xubuntu") == 0)        return 4;   /* blue */

                                                                                                                                                                                                                                                                /* Z */
                                                                                                                                                                                                                                                                if (strcmp(distro_id, "zorin") == 0)          return 4;   /* blue */

                                                                                                                                                                                                                                                                return 4; /* blue default */
}

static void set_color(int color, char *buf, int buflen)
{
    if (color < 0) {
        buf[0] = '\0';
        return;
    }
    if (color < 8)
        snprintf(buf, (size_t)buflen, "\033[1;%dm", 30 + color);
    else if (color < 16)
        snprintf(buf, (size_t)buflen, "\033[1;%dm", 90 + color - 8);
    else
        snprintf(buf, (size_t)buflen, "\033[38;5;%dm", color);
}

/* ─── ASCII Art Loading ───────────────────────────────────── */

typedef struct {
    char lines[CFETCH_MAX_ASCII_LINES][CFETCH_MAX_ASCII_WIDTH];
    int  line_count;
    int  max_width;
} ascii_art_t;

/* Calculate visible width (strip ANSI escape sequences) */
static int visible_len(const char *s)
{
    int len = 0;
    int in_esc = 0;
    for (; *s; s++) {
        if (*s == '\033') { in_esc = 1; continue; }
        if (in_esc) {
            /* CSI ends on any byte 0x40..0x7E (@ A-Z [ \ ] ^ _ ` a-z { | } ~) */
            if ((unsigned char)*s >= 0x40 && (unsigned char)*s <= 0x7E)
                in_esc = 0;
            continue;
        }
        /* UTF-8: only count lead bytes, skip continuation bytes */
        if (((unsigned char)*s & 0xC0) == 0x80) continue;
        len++;
    }
    return len;
}

static void load_ascii_art(ascii_art_t *art, const cfetch_config_t *cfg)
{
    memset(art, 0, sizeof(*art));

    /* Prefer resolving by distro_id + minimal flag so size changes
     * are honored even if ascii_path was cached from an earlier choice.
     * Fall back to ascii_path only for custom (absolute) paths. */
    const char *ascii_id = cfg->distro_id;
    if (cfg->ascii_path[0] == '/' || cfg->ascii_path[0] == '~') {
        ascii_id = cfg->ascii_path;
    }

    /* Disabled */
    if (strcmp(ascii_id, "none") == 0) return;

    FILE *fp = NULL;
    char path[CFETCH_MAX_PATH + 64];

    /* 1. Absolute / user-supplied path (custom art) */
    if (ascii_id[0] == '/' || ascii_id[0] == '~') {
        fp = fopen(ascii_id, "r");
    }

    /* 2. Extracted cache in ~/.config/cfetch/ascii/ — honors minimal flag */
    if (!fp) {
        char resolved[CFETCH_MAX_PATH];
        if (ascii_extract_resolve(cfg->distro_id,
            cfg->minimal_ascii ? 1 : 0,
            resolved, sizeof(resolved)) == 0) {
            fp = fopen(resolved, "r");
            }
    }

    /* 3. Legacy install dirs (in case the user shipped some) */
    const char *search_dirs[] = {
        "/usr/share/cfetch/ascii",
        "/usr/local/share/cfetch/ascii",
        "ascii",
        NULL
    };
    if (!fp) {
        const char *suffix = cfg->minimal_ascii ? "_small" : "";
        for (int i = 0; !fp && search_dirs[i]; i++) {
            snprintf(path, sizeof(path), "%s/%s%s.txt",
                     search_dirs[i], ascii_id, suffix);
            fp = fopen(path, "r");
        }
        /* fallback without the _small suffix */
        if (!fp && cfg->minimal_ascii) {
            for (int i = 0; !fp && search_dirs[i]; i++) {
                snprintf(path, sizeof(path), "%s/%s.txt",
                         search_dirs[i], ascii_id);
                fp = fopen(path, "r");
            }
        }
    }

    /* 4. Generic linux fallback */
    if (!fp) {
        for (int i = 0; !fp && search_dirs[i]; i++) {
            snprintf(path, sizeof(path), "%s/linux%s.txt",
                     search_dirs[i],
                     cfg->minimal_ascii ? "_small" : "");
            fp = fopen(path, "r");
        }
    }

    if (!fp) return;

    while (art->line_count < CFETCH_MAX_ASCII_LINES &&
        fgets(art->lines[art->line_count], CFETCH_MAX_ASCII_WIDTH, fp)) {
        size_t len = strlen(art->lines[art->line_count]);
    while (len > 0 && (art->lines[art->line_count][len-1] == '\n' ||
        art->lines[art->line_count][len-1] == '\r'))
        art->lines[art->line_count][--len] = '\0';

    int vlen = visible_len(art->lines[art->line_count]);
    if (vlen > art->max_width) art->max_width = vlen;
    art->line_count++;
        }
        fclose(fp);
}

/* ─── Info Line Building ──────────────────────────────────── */

typedef struct {
    char label[64];
    char value[512];
} info_line_t;

typedef struct {
    const char *cmd;
    char        out[CFETCH_MAX_VALUE];
    int         timeout_ms;
    int         done;
} cf_job_t;

static void run_cmd_timed(cf_job_t *j)
{
    int pfd[2];
    if (pipe(pfd) != 0) { j->out[0]='\0'; return; }
    pid_t pid = fork();
    if (pid == 0) {
        dup2(pfd[1], 1); dup2(pfd[1], 2);
        close(pfd[0]); close(pfd[1]);
        execl("/bin/sh", "sh", "-c", j->cmd, (char*)NULL);
        _exit(127);
    }
    close(pfd[1]);

    struct timespec start; clock_gettime(CLOCK_MONOTONIC, &start);
    fcntl(pfd[0], F_SETFL, O_NONBLOCK);

    size_t pos = 0;
    while (pos < sizeof(j->out) - 1) {
        struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
        long ms = (now.tv_sec - start.tv_sec)*1000 + (now.tv_nsec - start.tv_nsec)/1000000;
        if (ms >= j->timeout_ms) { kill(pid, SIGKILL); break; }

        ssize_t n = read(pfd[0], j->out + pos, sizeof(j->out)-1-pos);
        if (n > 0)       pos += (size_t)n;
        else if (n == 0) break;
        else { struct timespec ts={0,5*1000*1000}; nanosleep(&ts,NULL); }
    }
    close(pfd[0]);
    int st; waitpid(pid, &st, 0);
    j->out[pos] = '\0';
    while (pos && (j->out[pos-1]=='\n' || j->out[pos-1]=='\r')) j->out[--pos]='\0';
    j->done = 1;
}

static void *cf_thread(void *arg) { run_cmd_timed((cf_job_t*)arg); return NULL; }

static void render_custom_fields_parallel(
    info_line_t *lines, int *count, int max_lines,
    const cfetch_config_t *cfg)
{
    int n = cfg->custom_field_count;
    if (n <= 0) return;

    pthread_t th[CFETCH_MAX_CUSTOM];
    cf_job_t  jobs[CFETCH_MAX_CUSTOM];
    int       ran[CFETCH_MAX_CUSTOM] = {0};
    int default_timeout = 800;

    for (int i = 0; i < n; i++) {
        const cfetch_custom_field_t *f = &cfg->custom_fields[i];
        if (!f->is_command) continue;
        jobs[i].cmd        = f->value;
        jobs[i].timeout_ms = default_timeout;
        jobs[i].done       = 0;
        jobs[i].out[0]     = '\0';
        if (pthread_create(&th[i], NULL, cf_thread, &jobs[i]) == 0) ran[i] = 1;
    }

    for (int i = 0; i < n && *count < max_lines; i++) {
        const cfetch_custom_field_t *f = &cfg->custom_fields[i];
        scopy(lines[*count].label, sizeof(lines[*count].label), f->label);
        if (f->is_command) {
            if (ran[i]) pthread_join(th[i], NULL);
            scopy(lines[*count].value, sizeof(lines[*count].value),
                  jobs[i].out[0] ? jobs[i].out : "(timeout)");
        } else {
            scopy(lines[*count].value, sizeof(lines[*count].value), f->value);
        }
        (*count)++;
    }
}

static int build_info_lines(info_line_t *lines, int max_lines,
                            const sysinfo_t *info, const cfetch_config_t *cfg)
{
    int count = 0;

    for (int i = 0; i < cfg->module_count && count < max_lines; i++) {
        int mod = cfg->module_order[i];
        if (!cfg->modules[mod]) continue;

        const char *label = config_module_name((cfetch_module_t)mod);
        const char *value = NULL;
        char tmpbuf[512];

        switch (mod) {
            case MOD_OS:
                value = info->os_pretty;
                break;
            case MOD_KERNEL:
                value = info->kernel;
                break;
            case MOD_HOSTNAME:
                value = info->hostname;
                break;
            case MOD_SHELL:
                value = info->shell;
                break;
            case MOD_TERMINAL:
                value = info->terminal;
                break;
            case MOD_UPTIME:
                value = info->uptime_str;
                break;
            case MOD_CPU:
                if (info->cpu_freq_ghz > 0.01)
                    snprintf(tmpbuf, sizeof(tmpbuf), "%s (%d) @ %.2fGHz",
                             info->cpu_model, info->cpu_threads,
                             info->cpu_freq_ghz);
                else
                    snprintf(tmpbuf, sizeof(tmpbuf), "%s (%d)",
                             info->cpu_model, info->cpu_threads);
                value = tmpbuf;
                break;
            case MOD_GPU:
                if (info->gpu_count > 0) {
                    for (int g = 0; g < info->gpu_count && count < max_lines; g++) {
                        scopy(lines[count].label,
                              sizeof(lines[count].label), label);
                        scopy(lines[count].value,
                              sizeof(lines[count].value), info->gpu[g]);
                        count++;
                    }
                    continue; /* Skip the common copy below */
                }
                value = "N/A";
                break;
            case MOD_MEMORY:
                snprintf(tmpbuf, sizeof(tmpbuf), "%ldMiB / %ldMiB",
                         info->mem_used_mb, info->mem_total_mb);
                value = tmpbuf;
                break;
            case MOD_DISK:
                snprintf(tmpbuf, sizeof(tmpbuf), "%.1fGiB / %.1fGiB (%.0f%%)",
                         info->disk_used_gb, info->disk_total_gb,
                         info->disk_total_gb > 0 ?
                         (info->disk_used_gb / info->disk_total_gb * 100) : 0);
                value = tmpbuf;
                break;
            case MOD_DE_WM:
                value = info->de_wm;
                break;
            case MOD_PACKAGES:
                value = info->pkg_summary[0] ? info->pkg_summary : "0";
                break;
            case MOD_LOCAL_IP:
                value = info->local_ip[0] ? info->local_ip : "N/A";
                break;
            case MOD_PUBLIC_IP:
                value = info->public_ip[0] ? info->public_ip : "N/A";
                break;
            case MOD_RESOLUTION:
                value = info->resolution;
                break;
            case MOD_BATTERY:
                value = info->battery;
                break;
            default:
                continue;
        }

if (value && count < max_lines) {
            scopy(lines[count].label, sizeof(lines[count].label), label);
            scopy(lines[count].value, sizeof(lines[count].value), value);
            count++;
        }
    }

    /* Custom command modules run after built-ins, in parallel */
    render_custom_fields_parallel(lines, &count, max_lines, cfg);

    return count;
}

/* ─── Main Render ─────────────────────────────────────────── */

void render_output(const sysinfo_t *info, const cfetch_config_t *cfg,
                   bool use_color)
{
    ascii_art_t art;
    load_ascii_art(&art, cfg);

    info_line_t lines[64];
    int nlines = build_info_lines(lines, 64, info, cfg);

    /* Determine ASCII art width */
    overlay_ensure(cfg);
    int art_width = cfg->layout.ascii_width;
    if (art_width <= 0 || art_width < art.max_width) art_width = art.max_width;

    int padding = cfg->layout.padding;

    /* Resolve colors */
    const char *distro_id = cfg->distro_id[0] ? cfg->distro_id :
                            info->distro_id;
    int logo_col = cfg->colors.logo_color;
    if (logo_col < 0) logo_col = distro_default_color(distro_id);

    int label_col = cfg->colors.label_color;
    if (label_col < 0) label_col = logo_col; /* Match logo by default */

    int value_col = cfg->colors.value_color;
    int sep_col = cfg->colors.separator_color;

    char c_logo[32] = "", c_label[32] = "", c_value[32] = "";
    char c_sep[32] = "", c_reset[32] = "";

    if (use_color) {
        set_color(logo_col, c_logo, sizeof(c_logo));
        set_color(label_col, c_label, sizeof(c_label));
        if (value_col >= 0) set_color(value_col, c_value, sizeof(c_value));
        if (sep_col >= 0) set_color(sep_col, c_sep, sizeof(c_sep));
        scopy(c_reset, sizeof(c_reset), "\033[0m");
    }

    /* Print title line (user@host) */
    int total_lines = nlines + 2; /* +2 for title + separator */
    int max_rows = total_lines > art.line_count ? total_lines : art.line_count;

    printf("\n");
    for (int row = 0; row < max_rows; row++) {
        /* ASCII art column */
        if (!cfg->layout.ascii_right) {
            if (row < art.line_count) {
                int vlen = visible_len(art.lines[row]);
                int pad_right = art_width - vlen;
                if (pad_right < 0) pad_right = 0;
                print_logo_line(art.lines[row], row, c_logo, c_reset);
                printf("%*s", pad_right + padding, "");
            } else if (art.line_count > 0) {
                printf("%*s", art_width + padding, "");
            }
        }

        /* Info column */
        int info_row = row;
        if (info_row == 0) {
            /* Title: user@hostname */
            const char *user = getenv("USER");
            if (!user) user = "user";
            printf("%s%s%s@%s%s%s", c_label, user, c_reset,
                   c_label, info->hostname, c_reset);
        } else if (info_row == 1) {
            /* Separator line */
            const char *user = getenv("USER");
            if (!user) user = "user";
            int slen = (int)(strlen(user) + 1 + strlen(info->hostname));
            for (int j = 0; j < slen; j++) putchar('-');
        } else if (info_row - 2 < nlines) {
            int idx = info_row - 2;
            printf("%s%s%s%s%s%s%s%s",
                   c_label, lines[idx].label, c_reset,
                   c_sep[0] ? c_sep : c_label, cfg->separator, c_reset,
                   c_value, lines[idx].value);
            if (c_value[0]) printf("%s", c_reset);
        }

        /* ASCII art on right side */
        if (cfg->layout.ascii_right && row < art.line_count) {
            /* Pad to align right-side art */
            printf("%*s%s%s%s", padding, "", c_logo, art.lines[row], c_reset);
        }

        printf("\n");
    }
    printf("\n");
}

/* ─── JSON Output ─────────────────────────────────────────── */

static void json_str(const char *s)
{
    putchar('"');
    for (; *s; s++) {
        switch (*s) {
            case '"':  printf("\\\""); break;
            case '\\': printf("\\\\"); break;
            case '\n': printf("\\n"); break;
            case '\r': printf("\\r"); break;
            case '\t': printf("\\t"); break;
            default:   putchar(*s); break;
        }
    }
    putchar('"');
}

void render_json(const sysinfo_t *info, const cfetch_config_t *cfg)
{
    printf("{\n");

    info_line_t lines[64];
    int nlines = build_info_lines(lines, 64, info, cfg);

    for (int i = 0; i < nlines; i++) {
        printf("  ");
        json_str(lines[i].label);
        printf(": ");
        json_str(lines[i].value);
        if (i < nlines - 1) printf(",");
        printf("\n");
    }

    /* Add raw numeric fields */
    if (nlines > 0) printf(",\n");
    printf("  \"_mem_used_mb\": %ld,\n", info->mem_used_mb);
    printf("  \"_mem_total_mb\": %ld,\n", info->mem_total_mb);
    printf("  \"_disk_used_gb\": %.2f,\n", info->disk_used_gb);
    printf("  \"_disk_total_gb\": %.2f,\n", info->disk_total_gb);
    printf("  \"_uptime_secs\": %ld\n", info->uptime_secs);

    printf("}\n");
}
