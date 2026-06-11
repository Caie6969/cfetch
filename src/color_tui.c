#define _GNU_SOURCE
#include "color_tui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <ctype.h>
#include <errno.h>

/* ─── terminal state ──────────────────────────────────────── */
static struct termios g_orig_term;
static int g_term_saved = 0;
static int g_mouse_on   = 0;

static void term_restore(void)
{
    if (g_mouse_on) {
        fputs("\033[?1000l\033[?1002l\033[?1006l", stdout);
        g_mouse_on = 0;
    }
    fputs("\033[?25h\033[?1049l", stdout); /* show cursor, exit alt screen */
    fflush(stdout);
    if (g_term_saved) tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_term);
}

static int term_setup(void)
{
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) return -1;
    if (tcgetattr(STDIN_FILENO, &g_orig_term) != 0) return -1;
    g_term_saved = 1;
    struct termios raw = g_orig_term;
    raw.c_lflag &= ~(ICANON | ECHO | ISIG);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) return -1;
    fputs("\033[?1049h\033[?25l", stdout);   /* alt screen, hide cursor */
    /* enable SGR mouse reporting (works on xterm, kitty, alacritty, etc.) */
    fputs("\033[?1000h\033[?1002h\033[?1006h", stdout);
    g_mouse_on = 1;
    atexit(term_restore);
    return 0;
}

static void term_size(int *rows, int *cols)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        *rows = ws.ws_row; *cols = ws.ws_col;
    } else { *rows = 24; *cols = 80; }
}

/* ─── input events ────────────────────────────────────────── */
typedef enum {
    EV_NONE, EV_KEY, EV_MOUSE_DOWN, EV_MOUSE_UP, EV_MOUSE_MOVE, EV_RESIZE
} ev_kind_t;

typedef struct {
    ev_kind_t kind;
    int key;          /* for EV_KEY: ascii or special (KEY_*) */
    int mx, my;       /* mouse: 1-based terminal coords */
    int button;       /* 0=left, 1=middle, 2=right, 64=wheel up, 65=wheel dn */
} ev_t;

enum { KEY_UP=1001, KEY_DN=1002, KEY_RT=1003, KEY_LT=1004,
    KEY_ESC=27, KEY_ENTER=10 };

    /* read one byte, blocking */
    static int read_byte(void) { unsigned char c; ssize_t n = read(STDIN_FILENO,&c,1); return n==1?c:-1; }

    static ev_t read_event(void)
    {
        ev_t e = {EV_NONE,0,0,0,0};
        int c = read_byte();
        if (c < 0) return e;
        if (c != 27) { e.kind = EV_KEY; e.key = c; return e; }

        /* ESC sequence */
        int c2 = read_byte();
        if (c2 < 0) { e.kind = EV_KEY; e.key = KEY_ESC; return e; }
        if (c2 != '[' && c2 != 'O') { e.kind = EV_KEY; e.key = KEY_ESC; return e; }

        int c3 = read_byte();
        if (c3 == 'A') { e.kind=EV_KEY; e.key=KEY_UP; return e; }
        if (c3 == 'B') { e.kind=EV_KEY; e.key=KEY_DN; return e; }
        if (c3 == 'C') { e.kind=EV_KEY; e.key=KEY_RT; return e; }
        if (c3 == 'D') { e.kind=EV_KEY; e.key=KEY_LT; return e; }

        /* SGR mouse: ESC [ < b ; x ; y (M|m) */
        if (c3 == '<') {
            char buf[64]; size_t n = 0;
            int ch;
            while ((ch = read_byte()) >= 0 && n < sizeof(buf)-1) {
                buf[n++] = (char)ch;
                if (ch == 'M' || ch == 'm') break;
            }
            buf[n] = 0;
            int b, x, y; char k;
            if (sscanf(buf, "%d;%d;%d%c", &b, &x, &y, &k) == 4) {
                e.button = b & 0x43;
                e.mx = x; e.my = y;
                int is_move = (b & 32) != 0;
                if (is_move)        e.kind = EV_MOUSE_MOVE;
                else if (k == 'M')  e.kind = EV_MOUSE_DOWN;
                else                e.kind = EV_MOUSE_UP;
                return e;
            }
        }
        e.kind = EV_KEY; e.key = KEY_ESC; return e;
    }

    /* ─── palette ─────────────────────────────────────────────── */
    /* A color spec: either ANSI index (0..255) or truecolor RGB. */
    typedef struct { int is_rgb; int idx; int r,g,b; } color_t;

    static const char *ansi16_names[16] = {
        "Black","Red","Green","Yellow","Blue","Magenta","Cyan","White",
        "Gray","BrRed","BrGreen","BrYellow","BrBlue","BrMag","BrCyan","BrWhite"
    };

    static void color_to_fg(const color_t *c, char *out, size_t n)
    {
        if (!c) { snprintf(out,n,""); return; }
        if (c->is_rgb)
            snprintf(out,n,"\033[38;2;%d;%d;%dm", c->r, c->g, c->b);
        else
            snprintf(out,n,"\033[38;5;%dm", c->idx);
    }

    static void color_to_bg(const color_t *c, char *out, size_t n)
    {
        if (c->is_rgb)
            snprintf(out,n,"\033[48;2;%d;%d;%dm", c->r, c->g, c->b);
        else
            snprintf(out,n,"\033[48;5;%dm", c->idx);
    }

    static void color_to_spec(const color_t *c, char *out, size_t n)
    {
        if (c->is_rgb) snprintf(out,n,"#%02x%02x%02x", c->r, c->g, c->b);
        else           snprintf(out,n,"a%d", c->idx);
    }

    static int color_from_spec(const char *s, color_t *c)
    {
        if (!s || !*s) return -1;
        if (s[0] == '#' && strlen(s) >= 7) {
            unsigned r,g,b;
            if (sscanf(s+1,"%2x%2x%2x",&r,&g,&b)==3) {
                c->is_rgb=1; c->r=r; c->g=g; c->b=b; return 0;
            }
            return -1;
        }
        if (s[0] == 'a') {
            int idx = atoi(s+1);
            if (idx < 0 || idx > 255) return -1;
            c->is_rgb=0; c->idx=idx; return 0;
        }
        return -1;
    }

    /* ─── ASCII grid ──────────────────────────────────────────── */
    #define MAX_R 64
    #define MAX_C 200

    typedef struct {
        int rows, cols;
        char  ch[MAX_R][MAX_C];     /* glyph (0 if empty) */
        color_t col[MAX_R][MAX_C];  /* color */
        char  has[MAX_R][MAX_C];    /* 1 if colored */
    } grid_t;

    /* strip ANSI escapes when loading */
    static void strip_ansi_into(const char *src, char *dst, size_t dn)
    {
        size_t j = 0; int in_esc = 0;
        for (size_t i = 0; src[i] && j+1 < dn; i++) {
            if (src[i] == '\033') { in_esc = 1; continue; }
            if (in_esc) {
                if ((src[i] >= '@' && src[i] <= '~')) in_esc = 0;
                continue;
            }
            dst[j++] = src[i];
        }
        dst[j] = 0;
    }

    static int load_grid(const char *ascii_path, grid_t *g)
    {
        FILE *fp = fopen(ascii_path, "r");
        if (!fp) return -1;
        memset(g, 0, sizeof(*g));
        char line[1100], clean[1100];
        while (g->rows < MAX_R && fgets(line, sizeof(line), fp)) {
            size_t L = strlen(line);
            while (L && (line[L-1]=='\n'||line[L-1]=='\r')) line[--L]=0;
            strip_ansi_into(line, clean, sizeof(clean));
            int c = 0;
            for (size_t i = 0; clean[i] && c < MAX_C; i++, c++)
                g->ch[g->rows][c] = clean[i];
            if (c > g->cols) g->cols = c;
            g->rows++;
        }
        fclose(fp);
        return 0;
    }

    static int save_overlay(const char *ascii_path, const grid_t *g)
    {
        char path[1024];
        snprintf(path, sizeof(path), "%s.colors", ascii_path);
        FILE *fp = fopen(path, "w");
        if (!fp) return -1;
        fprintf(fp, "# cfetch color map v1\n");
        fprintf(fp, "# format: row col color_spec   (a<0-255> or #RRGGBB)\n");
        for (int r = 0; r < g->rows; r++)
            for (int c = 0; c < g->cols; c++)
                if (g->has[r][c]) {
                    char spec[32];
                    color_to_spec(&g->col[r][c], spec, sizeof(spec));
                    fprintf(fp, "%d %d %s\n", r, c, spec);
                }
                fclose(fp);
            return 0;
    }

    /* ─── undo stack ──────────────────────────────────────────── */
    typedef struct { int r, c; char had; color_t prev; } op_t;
    #define UNDO_MAX 4096
    static op_t  g_undo[UNDO_MAX]; static int g_undo_n = 0;
    static int   g_batch_marks[256]; static int g_batch_n = 0;

    static void undo_begin(void) {
        if (g_batch_n < 256) g_batch_marks[g_batch_n++] = g_undo_n;
    }
    static void undo_push(grid_t *g, int r, int c) {
        if (g_undo_n >= UNDO_MAX) return;
        g_undo[g_undo_n].r = r; g_undo[g_undo_n].c = c;
        g_undo[g_undo_n].had = g->has[r][c];
        g_undo[g_undo_n].prev = g->col[r][c];
        g_undo_n++;
    }
    static void undo_pop_batch(grid_t *g) {
        if (g_batch_n == 0) return;
        int start = g_batch_marks[--g_batch_n];
        while (g_undo_n > start) {
            g_undo_n--;
            op_t *o = &g_undo[g_undo_n];
            g->has[o->r][o->c] = o->had;
            g->col[o->r][o->c] = o->prev;
        }
    }

    /* ─── painting ops ────────────────────────────────────────── */
    static void paint_cell(grid_t *g, int r, int c, color_t col) {
        if (r<0||r>=g->rows||c<0||c>=g->cols) return;
        if (g->ch[r][c]==0||g->ch[r][c]==' ') return;
        undo_push(g, r, c);
        g->col[r][c] = col; g->has[r][c] = 1;
    }
    static void paint_by_char(grid_t *g, char ch, color_t col) {
        if (ch==0||ch==' ') return;
        undo_begin();
        for (int r=0;r<g->rows;r++) for (int c=0;c<g->cols;c++)
            if (g->ch[r][c]==ch) paint_cell(g, r, c, col);
    }
    static void paint_rect(grid_t *g, int r1,int c1,int r2,int c2, color_t col) {
        if (r1>r2){int t=r1;r1=r2;r2=t;}
        if (c1>c2){int t=c1;c1=c2;c2=t;}
        undo_begin();
        for (int r=r1;r<=r2;r++) for (int c=c1;c<=c2;c++) paint_cell(g, r, c, col);
    }
    static void paint_auto(grid_t *g) {
        undo_begin();
        /* assign brights 9..15 cycling per distinct char */
        char seen[256] = {0}; int n_assigned = 0; int map[256] = {0};
        for (int r=0;r<g->rows;r++) for (int c=0;c<g->cols;c++) {
            unsigned char ch = (unsigned char)g->ch[r][c];
            if (!ch||ch==' ') continue;
            if (!seen[ch]) { seen[ch]=1; map[ch] = 9 + (n_assigned++ % 7); }
            color_t col = {0, map[ch], 0,0,0};
            paint_cell(g, r, c, col);
        }
    }
    static void clear_all(grid_t *g) {
        undo_begin();
        for (int r=0;r<g->rows;r++) for (int c=0;c<g->cols;c++)
            if (g->has[r][c]) { undo_push(g,r,c); g->has[r][c]=0; }
    }

    /* ─── drawing ─────────────────────────────────────────────── */
    #define ART_TOP    2
    #define ART_LEFT   3

    static void clear_screen(void){ fputs("\033[2J\033[H", stdout); }
    static void goto_xy(int r,int c){ printf("\033[%d;%dH", r, c); }
    static void reset_attr(void){ fputs("\033[0m", stdout); }

    static void draw_art(const grid_t *g, int hi_r, int hi_c)
    {
        for (int r=0;r<g->rows;r++) {
            goto_xy(ART_TOP + r, ART_LEFT);
            for (int c=0;c<g->cols;c++) {
                char ch = g->ch[r][c]; if (!ch) ch = ' ';
                if (r==hi_r && c==hi_c) fputs("\033[7m", stdout);
                if (g->has[r][c]) {
                    char fg[32]; color_to_fg(&g->col[r][c], fg, sizeof(fg));
                    fputs(fg, stdout);
                }
                fputc(ch, stdout);
                reset_attr();
            }
        }
    }

    static int palette_top(const grid_t *g) { return ART_TOP + g->rows + 2; }

    /* palette layout — returns 1 if click hit a swatch; sets out_col */
    static int draw_palette_16(int top, const color_t *cur, color_t *out_hit,
                               int click_x, int click_y)
    {
        int hit = 0;
        goto_xy(top, ART_LEFT);
        fputs("\033[1mPalette (1:16  2:256  T:truecolor):\033[0m", stdout);
        for (int i = 0; i < 16; i++) {
            int x = ART_LEFT + (i%8)*7;
            int y = top + 1 + (i/8)*2;
            goto_xy(y, x);
            printf("\033[48;5;%dm  %2d  \033[0m", i, i);
            goto_xy(y+1, x);
            printf("%-6.6s", ansi16_names[i]);
            if (click_y == y && click_x >= x && click_x < x+6) {
                out_hit->is_rgb=0; out_hit->idx=i; hit=1;
            }
        }
        /* current color preview */
        goto_xy(top, ART_LEFT + 40);
        char fg[32]; color_to_fg(cur, fg, sizeof(fg));
        char spec[32]; color_to_spec(cur, spec, sizeof(spec));
        printf("Current: %s███\033[0m  %s", fg, spec);
        (void)cur;
        return hit;
    }

    static int draw_palette_256(int top, color_t *out_hit, int cx, int cy)
    {
        int hit = 0;
        goto_xy(top, ART_LEFT);
        fputs("\033[1m256-color (click any):\033[0m", stdout);
        /* 16 base */
        for (int i=0;i<16;i++) {
            int x = ART_LEFT + i*3;
            int y = top+1;
            goto_xy(y,x); printf("\033[48;5;%dm   \033[0m", i);
            if (cy==y && cx>=x && cx<x+3) { out_hit->is_rgb=0; out_hit->idx=i; hit=1; }
        }
        /* 6x6x6 cube as 6 rows of 36 */
        for (int i=0;i<216;i++) {
            int idx = 16+i;
            int x = ART_LEFT + (i%36)*2;
            int y = top+3 + (i/36);
            goto_xy(y,x); printf("\033[48;5;%dm  \033[0m", idx);
            if (cy==y && cx>=x && cx<x+2) { out_hit->is_rgb=0; out_hit->idx=idx; hit=1; }
        }
        /* grayscale */
        for (int i=0;i<24;i++) {
            int idx = 232+i;
            int x = ART_LEFT + i*3;
            int y = top+10;
            goto_xy(y,x); printf("\033[48;5;%dm   \033[0m", idx);
            if (cy==y && cx>=x && cx<x+3) { out_hit->is_rgb=0; out_hit->idx=idx; hit=1; }
        }
        return hit;
    }

    static void draw_palette_truecolor(int top, color_t *cur)
    {
        goto_xy(top, ART_LEFT);
        fputs("\033[1mTruecolor (R/G/B  ±:r/R g/G b/B   Enter to apply)\033[0m", stdout);
        if (!cur->is_rgb) { cur->is_rgb=1; cur->r=255; cur->g=85; cur->b=85; }
        goto_xy(top+2, ART_LEFT);
        printf("R %3d  G %3d  B %3d   ", cur->r, cur->g, cur->b);
        char bg[32]; color_to_bg(cur, bg, sizeof(bg));
        printf("%s          \033[0m", bg);
    }

    static void draw_help(int top, const char *mode, const char *msg)
    {
        goto_xy(top, ART_LEFT);
        fputs("\033[2K", stdout);
        printf("Mode: \033[1;33m%s\033[0m  "
        "[p]aint [c]har [r]ect [a]uto  [u]ndo [x]clear  [s]ave [q]uit", mode);
        goto_xy(top+1, ART_LEFT);
        fputs("\033[2K", stdout);
        if (msg && *msg) printf("→ %s", msg);
    }

    /* ─── helpers to find which glyph the mouse hit ───────────── */
    static int hit_glyph(const grid_t *g, int mx, int my, int *r, int *c)
    {
        int rr = my - ART_TOP;
        int cc = mx - ART_LEFT;
        if (rr < 0 || rr >= g->rows || cc < 0 || cc >= g->cols) return 0;
        if (g->ch[rr][cc] == 0 || g->ch[rr][cc] == ' ') return 0;
        *r = rr; *c = cc; return 1;
    }

    /* ─── resolve ascii path the same way render.c does ───────── */
    #include "ascii_extract.h"

    static int resolve_ascii_path(const cfetch_config_t *cfg, char *out, size_t n)
    {
        if (cfg->ascii_path[0] == '/' || cfg->ascii_path[0] == '~') {
            snprintf(out, n, "%s", cfg->ascii_path);
            return 0;
        }
        if (cfg->distro_id[0]) {
            if (ascii_extract_resolve(cfg->distro_id,
                cfg->minimal_ascii ? 1 : 0, out, n) == 0) return 0;
        }
        if (cfg->ascii_path[0]) { snprintf(out, n, "%s", cfg->ascii_path); return 0; }
        return -1;
    }

    /* ─── public: load overlay for render.c ───────────────────── */
    int color_overlay_load(const char *ascii_path, color_overlay_t *out)
    {
        memset(out, 0, sizeof(*out));
        char path[1024];
        snprintf(path, sizeof(path), "%s.colors", ascii_path);
        FILE *fp = fopen(path, "r");
        if (!fp) return -1;
        /* first pass: dimensions */
        int max_r = 0, max_c = 0;
        char line[256];
        long start = ftell(fp);
        while (fgets(line, sizeof(line), fp)) {
            if (line[0]=='#'||line[0]=='\n') continue;
            int r, c; char spec[32];
            if (sscanf(line,"%d %d %31s",&r,&c,spec)==3) {
                if (r+1 > max_r) max_r = r+1;
                if (c+1 > max_c) max_c = c+1;
            }
        }
        if (max_r == 0 || max_c == 0) { fclose(fp); return -1; }
        out->rows = max_r; out->cols = max_c;
        out->cells = calloc((size_t)max_r * max_c, sizeof(char*));
        if (!out->cells) { fclose(fp); return -1; }
        fseek(fp, start, SEEK_SET);
        while (fgets(line, sizeof(line), fp)) {
            if (line[0]=='#'||line[0]=='\n') continue;
            int r, c; char spec[32];
            if (sscanf(line,"%d %d %31s",&r,&c,spec)==3) {
                color_t col; if (color_from_spec(spec,&col)!=0) continue;
                char fg[32]; color_to_fg(&col, fg, sizeof(fg));
                out->cells[r * max_c + c] = strdup(fg);
            }
        }
        fclose(fp);
        return 0;
    }

    void color_overlay_free(color_overlay_t *o)
    {
        if (!o || !o->cells) return;
        for (int i = 0; i < o->rows * o->cols; i++) free(o->cells[i]);
        free(o->cells); o->cells = NULL; o->rows = o->cols = 0;
    }

    /* ─── main run loop ───────────────────────────────────────── */
    int color_tui_run(const cfetch_config_t *cfg)
    {
        char ascii_path[1024];
        if (resolve_ascii_path(cfg, ascii_path, sizeof(ascii_path)) != 0) {
            fprintf(stderr, "color_tui: could not resolve ASCII path\n");
            return -1;
        }

        grid_t G;
        if (load_grid(ascii_path, &G) != 0) {
            fprintf(stderr, "color_tui: cannot open %s\n", ascii_path);
            return -1;
        }
        /* pre-load existing overlay so editing is iterative */
        color_overlay_t ov;
        if (color_overlay_load(ascii_path, &ov) == 0) {
            /* re-parse colors file directly into grid for in-memory edit */
            char path[1024]; snprintf(path,sizeof(path),"%s.colors",ascii_path);
            FILE *fp = fopen(path,"r");
            if (fp) {
                char line[256];
                while (fgets(line,sizeof(line),fp)) {
                    if (line[0]=='#'||line[0]=='\n') continue;
                    int r,c; char spec[32];
                    if (sscanf(line,"%d %d %31s",&r,&c,spec)==3) {
                        color_t col;
                        if (color_from_spec(spec,&col)==0 &&
                            r<G.rows && c<G.cols) {
                            G.col[r][c]=col; G.has[r][c]=1;
                            }
                    }
                }
                fclose(fp);
            }
            color_overlay_free(&ov);
        }

        if (term_setup() != 0) {
            fprintf(stderr, "color_tui: not a tty\n");
            return -1;
        }

        color_t cur   = {0, 9, 0,0,0};     /* bright red default */
        int palette_mode = 16;             /* 16 / 256 / -1 (truecolor) */
        int mode = 'p';                    /* p/c/r/a */
        int rect_r1=-1, rect_c1=-1;
        int cur_r = 0, cur_c = 0;          /* keyboard cursor */
        char msg[128] = "click a glyph to paint";
        int saved = 0;

        for (;;) {
            clear_screen();
            /* header */
            goto_xy(1, ART_LEFT);
            fputs("\033[1;36mcfetch color editor\033[0m  —  ", stdout);
            printf("file: %s", ascii_path);

            draw_art(&G, cur_r, cur_c);

            int ptop = palette_top(&G);
            if (palette_mode == 16) {
                color_t dummy;
                draw_palette_16(ptop, &cur, &dummy, -1, -1);
            } else if (palette_mode == 256) {
                color_t dummy;
                draw_palette_256(ptop, &dummy, -1, -1);
            } else {
                draw_palette_truecolor(ptop, &cur);
            }

            const char *mode_name =
            mode=='p'?"PAINT":
            mode=='c'?"CHAR":
            mode=='r'?(rect_r1<0?"RECT (corner1)":"RECT (corner2)"):
            mode=='a'?"AUTO":"PAINT";
            draw_help(ptop + 13, mode_name, msg);
            fflush(stdout);

            ev_t e = read_event();
            msg[0] = 0;

            if (e.kind == EV_KEY) {
                int k = e.key;
                if (k == 'q' || k == KEY_ESC) { saved = 0; break; }
                if (k == 's') { save_overlay(ascii_path, &G); saved = 1; break; }
                if (k == '1') { palette_mode = 16; continue; }
                if (k == '2') { palette_mode = 256; continue; }
                if (k == 't' || k == 'T') { palette_mode = -1; continue; }
                if (k == 'p'||k=='c'||k=='r'||k=='a') {
                    mode = k; rect_r1=rect_c1=-1;
                    if (k=='a') { paint_auto(&G); snprintf(msg,sizeof(msg),"auto-coloring applied; tweak with p/c"); }
                    continue;
                }
                if (k == 'u') { undo_pop_batch(&G); snprintf(msg,sizeof(msg),"undid last op"); continue; }
                if (k == 'x') { clear_all(&G); snprintf(msg,sizeof(msg),"all colors cleared"); continue; }

                if (palette_mode == -1) {
                    if (k=='r') { if(cur.r>0)cur.r--; }
                    if (k=='R') { if(cur.r<255)cur.r++; }
                    if (k=='g') { if(cur.g>0)cur.g--; }
                    if (k=='G') { if(cur.g<255)cur.g++; }
                    if (k=='b') { if(cur.b>0)cur.b--; }
                    if (k=='B') { if(cur.b<255)cur.b++; }
                    continue;
                }
                /* arrow-key cursor + Enter to paint */
                if (k==KEY_UP && cur_r>0) cur_r--;
                if (k==KEY_DN && cur_r<G.rows-1) cur_r++;
                if (k==KEY_LT && cur_c>0) cur_c--;
                if (k==KEY_RT && cur_c<G.cols-1) cur_c++;
                if (k==KEY_ENTER || k==' ') {
                    if (mode=='c') {
                        paint_by_char(&G, G.ch[cur_r][cur_c], cur);
                        snprintf(msg,sizeof(msg),"painted all '%c'", G.ch[cur_r][cur_c]?G.ch[cur_r][cur_c]:' ');
                    } else if (mode=='r') {
                        if (rect_r1<0) { rect_r1=cur_r; rect_c1=cur_c; snprintf(msg,sizeof(msg),"corner1 set; pick corner2"); }
                        else { paint_rect(&G, rect_r1,rect_c1,cur_r,cur_c, cur); rect_r1=rect_c1=-1; snprintf(msg,sizeof(msg),"rectangle filled"); }
                    } else {
                        undo_begin(); paint_cell(&G, cur_r, cur_c, cur);
                    }
                }
                continue;
            }

            if (e.kind == EV_MOUSE_DOWN && e.button == 0) {
                /* palette hit? */
                color_t hit;
                int got = 0;
                if (palette_mode == 16)
                    got = draw_palette_16(palette_top(&G), &cur, &hit, e.mx, e.my);
                else if (palette_mode == 256)
                    got = draw_palette_256(palette_top(&G), &hit, e.mx, e.my);
                if (got) { cur = hit; snprintf(msg,sizeof(msg),"color selected"); continue; }
                /* glyph hit? */
                int r, c;
                if (hit_glyph(&G, e.mx, e.my, &r, &c)) {
                    cur_r = r; cur_c = c;
                    if (mode=='c') {
                        paint_by_char(&G, G.ch[r][c], cur);
                        snprintf(msg,sizeof(msg),"painted all '%c'", G.ch[r][c]);
                    } else if (mode=='r') {
                        if (rect_r1<0) { rect_r1=r; rect_c1=c; snprintf(msg,sizeof(msg),"corner1 set"); }
                        else { paint_rect(&G, rect_r1,rect_c1,r,c, cur); rect_r1=rect_c1=-1; snprintf(msg,sizeof(msg),"rectangle filled"); }
                    } else {
                        undo_begin(); paint_cell(&G, r, c, cur);
                    }
                }
            }
        }

        term_restore();
        return saved ? 0 : 1;
    }


