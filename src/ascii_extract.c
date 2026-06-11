/* src/ascii_extract.c — parse embedded pfetch + neofetch shell sources
 * and cache per-distro ASCII art under ~/.config/cfetch/ascii/.
 */
#include "ascii_extract.h"
#include "embedded_blobs.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#define MAX_ART_SIZE 16384

/* ─── helpers ─────────────────────────────────────────────── */

static void mkdir_p(const char *path)
{
    char tmp[CFETCH_MAX_PATH];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t L = strlen(tmp);
    if (L && tmp[L-1] == '/') tmp[L-1] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') { *p = '\0'; mkdir(tmp, 0755); *p = '/'; }
    }
    mkdir(tmp, 0755);
}

/* Strip the common leading whitespace (spaces + tabs) from every non-empty line. */
static void strip_common_indent(char *s)
{
    /* find minimum leading-whitespace count across non-empty lines */
    size_t min_indent = (size_t)-1;
    const char *p = s;
    while (*p) {
        size_t ws = 0;
        while (p[ws] == ' ' || p[ws] == '\t') ws++;
        if (p[ws] != '\n' && p[ws] != '\0') {
            if (ws < min_indent) min_indent = ws;
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    if (min_indent == (size_t)-1 || min_indent == 0) return;

    /* rewrite in place */
    char *src = s, *dst = s;
    while (*src) {
        size_t skip = 0;
        while (skip < min_indent && (src[skip] == ' ' || src[skip] == '\t')) skip++;
        src += skip;
        while (*src && *src != '\n') *dst++ = *src++;
        if (*src == '\n') *dst++ = *src++;
    }
    *dst = '\0';
}
static void cache_dir(char *out, size_t n)
{
    char base[CFETCH_MAX_PATH - 16];
    config_get_dir(base, (int)sizeof(base));
    snprintf(out, n, "%s/ascii", base);
}

static int file_exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

static void to_lower_id(char *s)
{
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == ' ' || c == '\t' || c == '/') *s = '_';
        else *s = (char)tolower(c);
    }
}

/* Strip shell color escapes like ${c1}..${c9}, ${c10}.., ${c0}, ${reset} */
static void strip_color_escapes(char *s)
{
    char *src = s, *dst = s;
    while (*src) {
        if (src[0] == '$' && src[1] == '{') {
            char *end = strchr(src + 2, '}');
            if (end) { src = end + 1; continue; }
        }
        *dst++ = *src++;
    }
    *dst = '\0';
}

/* Write a single ascii file (overwrites). Strips trailing whitespace. */
static int write_ascii_file(const char *dir, const char *name,
                            const char *suffix, const char *art)
{
    char path[CFETCH_MAX_PATH + 96];
    snprintf(path, sizeof(path), "%s/%s%s.txt", dir, name, suffix);
    FILE *fp = fopen(path, "w");
    if (!fp) return -1;
    fputs(art, fp);
    size_t L = strlen(art);
    if (L == 0 || art[L-1] != '\n') fputc('\n', fp);
    fclose(fp);
    return 0;
}

/* ─── shared parser for `read -rd '' ascii_data <<...EOF ... EOF` blocks ─── */

/* Skip whitespace + comments on one logical line; advance *p past a token
 * Returns 1 if it found a heredoc-bounded ascii_data block and writes the
 * extracted text (color-stripped, leading-tab-stripped) into `out` (size n).
 * Returns 0 if no block was found before `end`. Advances *p past the block. */
/* Find next heredoc body bounded by EOF on the current line.
 * Looks for "<<EOF" or "<<-EOF" within ~200 bytes of *p, then captures
 * lines until a line whose first non-tab/space content is "EOF".
 * Strips a single leading tab per line (for <<- heredocs) and color escapes.
 * Returns 1 on success and advances *p past the closing EOF. */
static int read_ascii_heredoc(const char **p, const char *end,
                              char *out, size_t n)
{
    const char *q = *p;
    const char *limit = q + 400 < end ? q + 400 : end;

    /* Find heredoc opener */
    const char *opener = NULL;
    for (const char *s = q; s + 4 < limit; s++) {
        if (s[0] == '<' && s[1] == '<') {
            /* skip optional '-' and quote */
            const char *t = s + 2;
            if (*t == '-') t++;
            if (*t == '\'' || *t == '"') t++;
            if (t + 2 < end && t[0] == 'E' && t[1] == 'O' && t[2] == 'F') {
                opener = s;
                break;
            }
        }
    }
    if (!opener) return 0;

    /* body starts after the newline following the opener */
    const char *body = strchr(opener, '\n');
    if (!body || body >= end) return 0;
    body++;

    /* find closing EOF on its own line (allowing leading tabs/spaces) */
    const char *scan = body;
    const char *body_end = NULL;
    const char *after_eof = NULL;
    while (scan < end) {
        const char *nl = memchr(scan, '\n', (size_t)(end - scan));
        const char *line_end = nl ? nl : end;
        const char *t = scan;
        while (t < line_end && (*t == '\t' || *t == ' ')) t++;
        if (line_end - t >= 3 && strncmp(t, "EOF", 3) == 0) {
            const char *after = t + 3;
            while (after < line_end && (*after == ' ' || *after == '\t')) after++;
            if (after == line_end) {
                body_end  = scan;
                after_eof = nl ? nl + 1 : end;
                break;
            }
        }
        if (!nl) break;
        scan = nl + 1;
    }
    if (!body_end) return 0;

    size_t o = 0;
    const char *l = body;
    while (l < body_end && o + 1 < n) {
        if (*l == '\t') l++;  /* strip one leading tab for <<- heredocs */
            const char *nl = memchr(l, '\n', (size_t)(body_end - l));
        size_t L = nl ? (size_t)(nl - l) : (size_t)(body_end - l);
        if (o + L + 1 >= n) L = n - o - 2;
        memcpy(out + o, l, L);
        o += L;
        out[o++] = '\n';
        if (!nl) break;
        l = nl + 1;
    }
    out[o] = '\0';
    strip_color_escapes(out);
    *p = after_eof;
    return 1;
}

/* Find a case-branch pattern: optional quote, name, optional quote, "*)" */
/* Find a case-branch pattern: name1[|name2|...]*)  on its own line.
 * Writes the FIRST alternative name into name_out (lowercase, sanitized).
 * Also writes additional aliases via the alias callback... actually we
 * keep it simple: only emit the first alternative. */
static const char *find_next_case(const char *p, const char *end,
                                  char *name_out, size_t nn)
{
    while (p < end) {
        const char *star = strstr(p, "*)");
        if (!star || star >= end) return NULL;

        /* walk back to start of logical token (newline or ';') */
        const char *t = star;
        while (t > p && t[-1] != '\n' && t[-1] != ';') t--;
        while (t < star && (*t == ' ' || *t == '\t' || *t == '(')) t++;

        /* token is [t, star). Take only first alternative (before '|'). */
        const char *first_end = t;
        while (first_end < star && *first_end != '|') first_end++;

        const char *name_s = t, *name_e = first_end;
        /* trim trailing whitespace/star */
        while (name_e > name_s &&
            (name_e[-1] == ' ' || name_e[-1] == '\t' ||
            name_e[-1] == '*')) name_e--;
        /* trim surrounding quotes */
        if (name_s < name_e && (*name_s == '"' || *name_s == '\'')) name_s++;
        if (name_e > name_s &&
            (name_e[-1] == '"' || name_e[-1] == '\'')) name_e--;

        size_t L = (size_t)(name_e - name_s);
        if (L > 0 && L < nn) {
            /* validate: must look like an identifier-ish name */
            int ok = 1;
            for (size_t i = 0; i < L; i++) {
                unsigned char c = (unsigned char)name_s[i];
                if (!(isalnum(c) || c == '_' || c == '-' ||
                    c == ' ' || c == '/' || c == '.')) { ok = 0; break; }
            }
            if (ok) {
                memcpy(name_out, name_s, L);
                name_out[L] = '\0';
                return star + 2;
            }
        }
        p = star + 2;
    }
    return NULL;
}

/* ─── extract pfetch -> small ──────────────────────────────────────────────── */

/* Parse a pfetch-style case label like:
 *   ([Aa]rch*)
 *   ([Cc]el[Oo][Ss]*)
 *   ([Gg]uix[Ss][Dd]*|[Gg]uix*)
 *   ([Ll]inux*[Ll]ite*|[Ll]ite*)
 *
 * For each alternative, collapse character-classes [Xx] -> 'x', drop the
 * trailing '*', drop any inner '*', and emit a lowercase id.
 * Writes up to max_ids names into ids[]. Returns count.
 */
static int parse_pfetch_label(const char *label, char ids[][64], int max_ids)
{
    int count = 0;
    const char *p = label;
    while (*p && count < max_ids) {
        char buf[64]; int o = 0;
        while (*p && *p != '|' && *p != ')' && o + 1 < (int)sizeof(buf)) {
            if (*p == '[') {
                /* take first letter inside [..] */
                const char *q = p + 1;
                if (*q) { buf[o++] = (char)tolower((unsigned char)*q); }
                /* skip until ] */
                while (*p && *p != ']') p++;
                if (*p == ']') p++;
                continue;
            }
            if (*p == '*' || *p == ' ' || *p == '\t' ||
                *p == '"' || *p == '\'') { p++; continue; }
            buf[o++] = (char)tolower((unsigned char)*p);
            p++;
        }
        buf[o] = '\0';
        if (o > 0) { snprintf(ids[count++], 64, "%s", buf); }
        if (*p == '|') p++;
        else break;
    }
    return count;
}

static void extract_pfetch(const char *dir,
                           const unsigned char *blob, size_t len)
{
    const char *text = (const char *)blob;
    const char *end  = text + len;
    const char *p    = text;

    /* Focus on get_ascii() body to avoid spurious matches. */
    const char *start = strstr(text, "get_ascii()");
    if (start) p = start;

    while (p < end) {
        /* Find next "([X" pattern */
        const char *open = NULL;
        for (const char *s = p; s < end - 2; s++) {
            if (s[0] == '(' && s[1] == '[' && isalpha((unsigned char)s[2])) {
                /* must be preceded by whitespace or ';' to be a case label */
                const char *b = s;
                while (b > text && (b[-1] == ' ' || b[-1] == '\t')) b--;
                if (b == text || b[-1] == '\n' || b[-1] == ';') {
                    open = s;
                    break;
                }
            }
        }
        if (!open) break;

        /* Find matching ')' on same line (or up to newline) */
        const char *close = open;
        while (close < end && *close != ')' && *close != '\n') close++;
        if (close >= end || *close != ')') { p = open + 1; continue; }

        /* Label is open+1 .. close (excluding outer parens) */
        char label[256];
        size_t L = (size_t)(close - (open + 1));
        if (L >= sizeof(label)) L = sizeof(label) - 1;
        memcpy(label, open + 1, L);
        label[L] = '\0';

        const char *after = close + 1;

        /* Heredoc body */
        char art[MAX_ART_SIZE];
        const char *scan = after;
        if (!read_ascii_heredoc(&scan, end, art, sizeof(art))) {
            p = after;
            continue;
        }
        strip_common_indent(art);

        /* Expand label into one or more distro ids */
        char ids[8][64];
        int n = parse_pfetch_label(label, ids, 8);
        for (int i = 0; i < n; i++) {
            if (!ids[i][0]) continue;
            write_ascii_file(dir, ids[i], "_small", art);
            /* alias short form: foo_linux -> foo */
            size_t IL = strlen(ids[i]);
            if (IL > 6 && strcmp(ids[i] + IL - 6, "_linux") == 0) {
                char shortid[64];
                snprintf(shortid, sizeof(shortid), "%.*s",
                         (int)(IL - 6), ids[i]);
                write_ascii_file(dir, shortid, "_small", art);
            }
        }
        p = scan;
    }
}

/* ─── extract neofetch -> big ──────────────────────────────────────────────── */

static void extract_neofetch(const char *dir,
                             const unsigned char *blob, size_t len)
{
    const char *text = (const char *)blob;
    const char *end  = text + len;

    /* Focus on get_distro_ascii() body for cleanliness, if found. */
    const char *start = strstr(text, "get_distro_ascii()");
    const char *p = start ? start : text;

    static const char *skip_subs[] = {
        "bsd", "minix", "haiku", "darwin", "macos", "mac_os", "ios",
        "windows", "solaris", "openindiana", "redstar", "aix", "irix",
        "gnu", "hurd", "plan9", NULL
    };

    for (;;) {
        char rawname[128];
        const char *after = find_next_case(p, end, rawname, sizeof(rawname));
        if (!after) break;
        p = after;
        char art[MAX_ART_SIZE];
        if (!read_ascii_heredoc(&p, end, art, sizeof(art))) continue;
        strip_common_indent(art);

        char id[128];
        snprintf(id, sizeof(id), "%s", rawname);
        to_lower_id(id);

        int skip = 0;
        for (int i = 0; skip_subs[i]; i++) {
            if (strstr(id, skip_subs[i])) { skip = 1; break; }
        }
        if (skip) continue;

        /* neofetch uses "<distro>_small" identifiers for compact variants —
         * pfetch already covers those, so skip. */
        size_t IL = strlen(id);
        if (IL > 6 && strcmp(id + IL - 6, "_small") == 0) continue;

        write_ascii_file(dir, id, "", art);

        /* alias short form: "arch_linux" -> "arch" */
        if (IL > 6 && strcmp(id + IL - 6, "_linux") == 0) {
            char short_id[128];
            snprintf(short_id, sizeof(short_id), "%.*s", (int)(IL - 6), id);
            write_ascii_file(dir, short_id, "", art);
        }
    }
}

/* ─── public API ──────────────────────────────────────────── */


/* (defined here for the static heredoc buffer — keep above functions) */

int ascii_extract_ensure(void)
{
    char dir[CFETCH_MAX_PATH];
    cache_dir(dir, sizeof(dir));
    mkdir_p(dir);

    /* Sentinel file marks a successful extraction. */
    char stamp[CFETCH_MAX_PATH + 32];
    snprintf(stamp, sizeof(stamp), "%s/.extracted", dir);
    if (file_exists(stamp)) return 0;

    extract_pfetch  (dir, pfetch_blob,   pfetch_blob_len);
    extract_neofetch(dir, neofetch_blob, neofetch_blob_len);

    /* Write standalone tux logos directly — not embedded in pfetch/neofetch */
    write_ascii_file(dir, "tux",         "", (const char *)tux_blob);
    write_ascii_file(dir, "tux_small",   "", (const char *)minimal_tux_blob);

    FILE *fp = fopen(stamp, "w");
    if (fp) { fputs("ok\n", fp); fclose(fp); }
    return 0;
}

int ascii_extract_resolve(const char *distro_id, int minimal,
                          char *out, size_t n)
{
    if (!distro_id || !*distro_id) distro_id = "linux";
    if (ascii_extract_ensure() != 0) return -1;

    char dir[CFETCH_MAX_PATH];
    cache_dir(dir, sizeof(dir));

    char path[CFETCH_MAX_PATH + 64];

    if (minimal) {
        snprintf(path, sizeof(path), "%s/%s_small.txt", dir, distro_id);
        if (file_exists(path)) { snprintf(out, n, "%s", path); return 0; }
    }
    snprintf(path, sizeof(path), "%s/%s.txt", dir, distro_id);
    if (file_exists(path)) { snprintf(out, n, "%s", path); return 0; }

    if (minimal) {
        snprintf(path, sizeof(path), "%s/linux_small.txt", dir);
        if (file_exists(path)) { snprintf(out, n, "%s", path); return 0; }
    }
    snprintf(path, sizeof(path), "%s/linux.txt", dir);
    if (file_exists(path)) { snprintf(out, n, "%s", path); return 0; }

    return -1;
}
