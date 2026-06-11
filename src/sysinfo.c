#include "sysinfo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/utsname.h>
#include <sys/statvfs.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <poll.h>

/* ─── Utility ─────────────────────────────────────────────── */

static void trim(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r' ||
                       s[len-1] == ' '  || s[len-1] == '\t'))
        s[--len] = '\0';
    char *p = s;
    while (*p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
}

static int read_file_line(const char *path, char *buf, int buflen)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;
    buf[0] = '\0';
    if (fgets(buf, buflen, fp)) trim(buf);
    fclose(fp);
    return 0;
}

static int run_cmd(const char *cmd, char *buf, int buflen)
{
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;
    buf[0] = '\0';
    if (fgets(buf, buflen, fp)) trim(buf);
    pclose(fp);
    return buf[0] ? 0 : -1;
}

static int run_cmd_count_lines(const char *cmd)
{
    FILE *fp = popen(cmd, "r");
    if (!fp) return 0;
    int count = 0;
    char line[512];
    while (fgets(line, sizeof(line), fp)) count++;
    pclose(fp);
    return count;
}

/* ─── OS / Distro ─────────────────────────────────────────── */

void sysinfo_detect_distro(char *id, int idlen)
{
    FILE *fp = fopen("/etc/os-release", "r");
    if (!fp) {
        scopy(id, (size_t)idlen, "linux");
        return;
    }
    char line[256];
    id[0] = '\0';
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "ID=", 3) == 0) {
            char *val = line + 3;
            /* Remove quotes */
            if (*val == '"') val++;
            scopy(id, (size_t)idlen, val);
            id[idlen - 1] = '\0';
            trim(id);
            size_t len = strlen(id);
            if (len > 0 && id[len-1] == '"') id[len-1] = '\0';
            break;
        }
    }
    fclose(fp);
    if (!id[0]) scopy(id, (size_t)idlen, "linux");
}

void sysinfo_os(sysinfo_t *info)
{
    FILE *fp = fopen("/etc/os-release", "r");
    if (!fp) {
        scopy(info->os_pretty, sizeof(info->os_pretty), "Linux");
        scopy(info->distro_id, sizeof(info->distro_id), "linux");
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
            char *val = line + 12;
            if (*val == '"') val++;
            scopy(info->os_pretty, sizeof(info->os_pretty), val);
            trim(info->os_pretty);
            size_t len = strlen(info->os_pretty);
            if (len > 0 && info->os_pretty[len-1] == '"')
                info->os_pretty[len-1] = '\0';
        } else if (strncmp(line, "ID=", 3) == 0) {
            char *val = line + 3;
            if (*val == '"') val++;
            scopy(info->distro_id, sizeof(info->distro_id), val);
            trim(info->distro_id);
            size_t len = strlen(info->distro_id);
            if (len > 0 && info->distro_id[len-1] == '"')
                info->distro_id[len-1] = '\0';
        } else if (strncmp(line, "NAME=", 5) == 0) {
            char *val = line + 5;
            if (*val == '"') val++;
            scopy(info->os_name, sizeof(info->os_name), val);
            trim(info->os_name);
            size_t len = strlen(info->os_name);
            if (len > 0 && info->os_name[len-1] == '"')
                info->os_name[len-1] = '\0';
        } else if (strncmp(line, "VERSION_ID=", 11) == 0) {
            char *val = line + 11;
            if (*val == '"') val++;
            scopy(info->os_version, sizeof(info->os_version), val);
            trim(info->os_version);
            size_t len = strlen(info->os_version);
            if (len > 0 && info->os_version[len-1] == '"')
                info->os_version[len-1] = '\0';
        }
    }
    fclose(fp);
    if (!info->os_pretty[0])
        snprintf(info->os_pretty, sizeof(info->os_pretty), "%s %s",
                 info->os_name, info->os_version);
    if (!info->distro_id[0])
        scopy(info->distro_id, sizeof(info->distro_id), "linux");
}

/* ─── Kernel ──────────────────────────────────────────────── */

void sysinfo_kernel(sysinfo_t *info)
{
    struct utsname uts;
    if (uname(&uts) == 0) {
        snprintf(info->kernel, sizeof(info->kernel), "%s", uts.release);
    }
}

/* ─── Hostname ────────────────────────────────────────────── */

void sysinfo_hostname(sysinfo_t *info)
{
    if (read_file_line("/etc/hostname", info->hostname,
                       (int)sizeof(info->hostname)) != 0) {
        gethostname(info->hostname, sizeof(info->hostname));
    }
}

/* ─── Shell ───────────────────────────────────────────────── */

void sysinfo_shell(sysinfo_t *info)
{
    const char *shell = getenv("SHELL");
    if (shell) {
        const char *name = strrchr(shell, '/');
        name = name ? name + 1 : shell;
        scopy(info->shell, sizeof(info->shell), name);
    } else {
        scopy(info->shell, sizeof(info->shell), "unknown");
    }
}

/* ─── Terminal ────────────────────────────────────────────── */

void sysinfo_terminal(sysinfo_t *info)
{
    /* Try common env variables */
    const char *term = getenv("TERM_PROGRAM");
    if (!term) term = getenv("TERMINAL");
    if (!term) term = getenv("TERM");
    if (term) {
        scopy(info->terminal, sizeof(info->terminal), term);
    } else {
        scopy(info->terminal, sizeof(info->terminal), "unknown");
    }
}

/* ─── Uptime ──────────────────────────────────────────────── */

void sysinfo_uptime(sysinfo_t *info)
{
    char buf[64];
    if (read_file_line("/proc/uptime", buf, (int)sizeof(buf)) == 0) {
        double up = 0;
        sscanf(buf, "%lf", &up);
        info->uptime_secs = (long)up;
    }
    long s = info->uptime_secs;
    int days = (int)(s / 86400);
    int hours = (int)((s % 86400) / 3600);
    int mins = (int)((s % 3600) / 60);
    char *p = info->uptime_str;
    int rem = (int)sizeof(info->uptime_str);
    if (days > 0) {
        int n = snprintf(p, (size_t)rem, "%d day%s, ", days, days > 1 ? "s" : "");
        p += n; rem -= n;
    }
    if (hours > 0) {
        int n = snprintf(p, (size_t)rem, "%d hour%s, ", hours, hours > 1 ? "s" : "");
        p += n; rem -= n;
    }
    snprintf(p, (size_t)rem, "%d min%s", mins, mins > 1 ? "s" : "");
}

/* ─── CPU ─────────────────────────────────────────────────── */

void sysinfo_cpu(sysinfo_t *info)
{
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (!fp) return;
    char line[512];
    int threads = 0;
    info->cpu_cores = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "model name", 10) == 0 && !info->cpu_model[0]) {
            char *val = strchr(line, ':');
            if (val) {
                val++;
                while (*val == ' ') val++;
                scopy(info->cpu_model, sizeof(info->cpu_model), val);
                trim(info->cpu_model);
            }
        } else if (strncmp(line, "processor", 9) == 0) {
            threads++;
        } else if (strncmp(line, "cpu cores", 9) == 0 && info->cpu_cores == 0) {
            char *val = strchr(line, ':');
            if (val) info->cpu_cores = atoi(val + 1);
        }
    }
    fclose(fp);
    info->cpu_threads = threads;
    if (info->cpu_cores == 0) info->cpu_cores = threads;

    /* Try to read current freq */
    char buf[64];
    if (read_file_line(
            "/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq",
            buf, (int)sizeof(buf)) == 0) {
        long khz = atol(buf);
        info->cpu_freq_ghz = khz / 1000000.0;
    } else if (read_file_line(
            "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq",
            buf, (int)sizeof(buf)) == 0) {
        long khz = atol(buf);
        info->cpu_freq_ghz = khz / 1000000.0;
    }
}

/* ─── GPU ─────────────────────────────────────────────────── */

static void parse_gpu_name(const char *line, char *out, int outlen)
{
    /* lspci output: "XX:XX.X VGA compatible controller: Vendor Device [rev XX]"
     * or "XX:XX.X 3D controller: ..." */
    const char *colon = strstr(line, ": ");
    if (!colon) { out[0] = '\0'; return; }
    colon += 2; /* skip ": " */
    /* Skip to next colon+space for the device description */
    const char *desc = strstr(colon, ": ");
    if (!desc) desc = colon;
    else desc += 2;

    scopy(out, (size_t)outlen, desc);
    out[outlen - 1] = '\0';
    trim(out);

    /* Remove revision info [rev XX] */
    char *rev = strstr(out, " (rev");
    if (rev) *rev = '\0';
    rev = strstr(out, " [rev");
    if (rev) *rev = '\0';

    /* Remove "Corporation " prefix if present */
    char *corp = strstr(out, "Corporation ");
    if (corp) {
        memmove(corp, corp + 12, strlen(corp + 12) + 1);
    }

    /* Determine discrete/integrated hint */
    trim(out);
    int is_integrated = (strstr(out, "Intel") != NULL &&
                         strstr(out, "UHD") != NULL) ||
                        strstr(out, "Integrated") != NULL;

    size_t len = strlen(out);
    if (len + 15 < (size_t)outlen) {
        const char *tag = is_integrated ? " [integrated]" : " [discrete]";
        scopy(out + len, (size_t)outlen - len, tag);
    }
}

void sysinfo_gpu(sysinfo_t *info)
{
    /* Use lspci to detect VGA and 3D controllers */
    FILE *fp = popen("lspci 2>/dev/null | grep -iE 'VGA|3D|Display'", "r");
    if (!fp) return;
    char line[512];
    while (fgets(line, sizeof(line), fp) && info->gpu_count < SYSINFO_MAX_GPUS) {
        parse_gpu_name(line, info->gpu[info->gpu_count], 256);
        if (info->gpu[info->gpu_count][0])
            info->gpu_count++;
    }
    pclose(fp);
}

/* ─── Memory ──────────────────────────────────────────────── */

void sysinfo_memory(sysinfo_t *info)
{
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return;
    char line[256];
    long total = 0, avail = 0, free_mem = 0, buffers = 0, cached = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "MemTotal:", 9) == 0)
            sscanf(line + 9, " %ld", &total);
        else if (strncmp(line, "MemAvailable:", 13) == 0)
            sscanf(line + 13, " %ld", &avail);
        else if (strncmp(line, "MemFree:", 8) == 0)
            sscanf(line + 8, " %ld", &free_mem);
        else if (strncmp(line, "Buffers:", 8) == 0)
            sscanf(line + 8, " %ld", &buffers);
        else if (strncmp(line, "Cached:", 7) == 0)
            sscanf(line + 7, " %ld", &cached);
    }
    fclose(fp);
    info->mem_total_mb = total / 1024;
    if (avail > 0)
        info->mem_used_mb = (total - avail) / 1024;
    else
        info->mem_used_mb = (total - free_mem - buffers - cached) / 1024;
}

/* ─── Disk ────────────────────────────────────────────────── */

void sysinfo_disk(sysinfo_t *info)
{
    struct statvfs st;
    if (statvfs("/", &st) == 0) {
        unsigned long long total = (unsigned long long)st.f_blocks * st.f_frsize;
        unsigned long long avail = (unsigned long long)st.f_bavail * st.f_frsize;
        info->disk_total_gb = (double)total / (1024.0 * 1024.0 * 1024.0);
        info->disk_used_gb = (double)(total - avail) / (1024.0 * 1024.0 * 1024.0);
    }
}

/* ─── DE / WM ─────────────────────────────────────────────── */

void sysinfo_de_wm(sysinfo_t *info)
{
    /* Check desktop session env vars */
    const char *de = getenv("XDG_CURRENT_DESKTOP");
    if (!de) de = getenv("DESKTOP_SESSION");
    if (!de) de = getenv("XDG_SESSION_DESKTOP");

    /* Check for common WMs */
    const char *wm = NULL;
    const char *wayland = getenv("WAYLAND_DISPLAY");
    if (wayland) {
        const char *wtype = getenv("XDG_SESSION_TYPE");
        if (wtype && strcmp(wtype, "wayland") == 0) {
            if (!de) de = "Wayland";
        }
    }

    /* Try wmctrl or xprop for WM name */
    if (!de) {
        char buf[128];
        if (run_cmd("wmctrl -m 2>/dev/null | head -1 | cut -d: -f2", buf, (int)sizeof(buf)) == 0 && buf[0]) {
            trim(buf);
            wm = NULL; /* We'll use buf directly */
            scopy(info->de_wm, sizeof(info->de_wm), buf);
            return;
        }
    }

    if (de) {
        scopy(info->de_wm, sizeof(info->de_wm), de);
    } else {
        scopy(info->de_wm, sizeof(info->de_wm), "N/A");
    }
    (void)wm;
}

/* ─── Packages ────────────────────────────────────────────── */

static void try_pkg_manager(sysinfo_t *info, const char *name,
                            const char *check_cmd, const char *count_cmd)
{
    /* First check if the command exists */
    char which[256];
    snprintf(which, sizeof(which), "command -v %s >/dev/null 2>&1", check_cmd);
    if (system(which) != 0) return;

    int count = run_cmd_count_lines(count_cmd);
    if (count > 0 && info->pkg_manager_count < SYSINFO_MAX_PKGS) {
        pkg_manager_t *pm = &info->packages[info->pkg_manager_count];
        scopy(pm->name, sizeof(pm->name), name);
        pm->count = count;
        info->pkg_total += count;
        info->pkg_manager_count++;
    }
}

void sysinfo_packages(sysinfo_t *info)
{
    info->pkg_total = 0;
    info->pkg_manager_count = 0;

    /* Native package managers */
    try_pkg_manager(info, "dpkg", "dpkg",
                    "dpkg-query -f '\\n' -W 2>/dev/null");
    try_pkg_manager(info, "rpm", "rpm",
                    "rpm -qa 2>/dev/null");
    try_pkg_manager(info, "pacman", "pacman",
                    "pacman -Qq 2>/dev/null");
    try_pkg_manager(info, "xbps", "xbps-query",
                    "xbps-query -l 2>/dev/null");
    try_pkg_manager(info, "apk", "apk",
                    "apk list --installed 2>/dev/null");
    try_pkg_manager(info, "eopkg", "eopkg",
                    "eopkg list-installed 2>/dev/null");
    try_pkg_manager(info, "emerge", "emerge",
                    "find /var/db/pkg -mindepth 2 -maxdepth 2 -type d 2>/dev/null");
    try_pkg_manager(info, "nix", "nix-env",
                    "nix-env --query --installed 2>/dev/null");

    /* Universal / third-party */
    try_pkg_manager(info, "flatpak", "flatpak",
                    "flatpak list 2>/dev/null");
    try_pkg_manager(info, "snap", "snap",
                    "snap list 2>/dev/null | tail -n +2");
    try_pkg_manager(info, "brew", "brew",
                    "brew list 2>/dev/null");

    /* Language-specific */
    try_pkg_manager(info, "pip", "pip3",
                    "pip3 list 2>/dev/null | tail -n +3");
    try_pkg_manager(info, "npm", "npm",
                    "npm -g list --depth=0 2>/dev/null | tail -n +2");
    try_pkg_manager(info, "cargo", "cargo",
                    "cargo install --list 2>/dev/null | grep -c ':'");
    try_pkg_manager(info, "gem", "gem",
                    "gem list 2>/dev/null");
    try_pkg_manager(info, "go", "go",
                    "ls $(go env GOPATH 2>/dev/null)/bin 2>/dev/null");

    /* Build summary string */
    char *p = info->pkg_summary;
    int rem = (int)sizeof(info->pkg_summary);
    int n = snprintf(p, (size_t)rem, "%d", info->pkg_total);
    p += n; rem -= n;
    if (info->pkg_manager_count > 0) {
        n = snprintf(p, (size_t)rem, " (");
        p += n; rem -= n;
        for (int i = 0; i < info->pkg_manager_count && rem > 10; i++) {
            if (i > 0) { n = snprintf(p, (size_t)rem, ", "); p += n; rem -= n; }
            n = snprintf(p, (size_t)rem, "%d %s",
                        info->packages[i].count, info->packages[i].name);
            p += n; rem -= n;
        }
        snprintf(p, (size_t)rem, ")");
    }
}

/* ─── Local IP ────────────────────────────────────────────── */

void sysinfo_local_ip(sysinfo_t *info)
{
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) return;

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;
        /* Skip loopback */
        if (strcmp(ifa->ifa_name, "lo") == 0) continue;

        struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
        inet_ntop(AF_INET, &addr->sin_addr, info->local_ip,
                  sizeof(info->local_ip));
        break; /* Use first non-loopback IPv4 */
    }
    freeifaddrs(ifaddr);
}

/* ─── Public IP ───────────────────────────────────────────── */

void sysinfo_public_ip(sysinfo_t *info, int timeout_ms)
{
    /* Connect to ifconfig.me with timeout */
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo("ifconfig.me", "80", &hints, &res) != 0) return;

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) { freeaddrinfo(res); return; }

    /* Set non-blocking */
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    connect(sock, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);

    /* Wait for connection with timeout */
    struct pollfd pfd = { .fd = sock, .events = POLLOUT };
    if (poll(&pfd, 1, timeout_ms) <= 0) { close(sock); return; }

    /* Check for connection error */
    int err = 0;
    socklen_t errlen = sizeof(err);
    getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &errlen);
    if (err != 0) { close(sock); return; }

    /* Restore blocking */
    fcntl(sock, F_SETFL, flags);

    /* Set socket timeout for recv */
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    const char *req = "GET / HTTP/1.1\r\nHost: ifconfig.me\r\nUser-Agent: cfetch\r\nConnection: close\r\n\r\n";
    if (send(sock, req, strlen(req), 0) < 0) { close(sock); return; }

    char buf[1024];
    int total = 0;
    memset(buf, 0, sizeof(buf));
    while (total < (int)sizeof(buf) - 1) {
        int n = (int)recv(sock, buf + total, (size_t)(sizeof(buf) - 1 - (size_t)total), 0);
        if (n <= 0) break;
        total += n;
    }
    close(sock);

    /* Parse response body (after \r\n\r\n) */
    char *body = strstr(buf, "\r\n\r\n");
    if (body) {
        body += 4;
        trim(body);
        /* Validate IP-like string */
        if (strlen(body) < sizeof(info->public_ip) && strchr(body, '.')) {
            scopy(info->public_ip, sizeof(info->public_ip), body);
        }
    }
}

/* ─── Resolution ──────────────────────────────────────────── */

void sysinfo_resolution(sysinfo_t *info)
{
    /* Try /sys first for framebuffer */
    char buf[64];
    if (read_file_line("/sys/class/graphics/fb0/virtual_size", buf, (int)sizeof(buf)) == 0) {
        /* Format: "1920,1080" */
        char *comma = strchr(buf, ',');
        if (comma) {
            *comma = 'x';
            scopy(info->resolution, sizeof(info->resolution), buf);
            return;
        }
    }
    /* Fallback: xdpyinfo or xrandr */
    if (run_cmd("xrandr 2>/dev/null | grep '\\*' | head -1 | awk '{print $1}'",
                buf, (int)sizeof(buf)) == 0 && buf[0]) {
        scopy(info->resolution, sizeof(info->resolution), buf);
        return;
    }
    scopy(info->resolution, sizeof(info->resolution), "N/A");
}

/* ─── Battery ─────────────────────────────────────────────── */

void sysinfo_battery(sysinfo_t *info)
{
    char capacity[16] = "", status[32] = "";
    read_file_line("/sys/class/power_supply/BAT0/capacity", capacity, (int)sizeof(capacity));
    read_file_line("/sys/class/power_supply/BAT0/status", status, (int)sizeof(status));

    if (capacity[0]) {
        if (status[0])
            snprintf(info->battery, sizeof(info->battery), "%s%% [%s]", capacity, status);
        else
            snprintf(info->battery, sizeof(info->battery), "%s%%", capacity);
    } else {
        scopy(info->battery, sizeof(info->battery), "N/A");
    }
}

/* ─── Parallel Gather ─────────────────────────────────────── */

typedef struct {
    sysinfo_t *info;
    const cfetch_config_t *cfg;
    int task;
} thread_arg_t;

static void *gather_thread(void *arg)
{
    thread_arg_t *ta = (thread_arg_t *)arg;
    sysinfo_t *info = ta->info;

    switch (ta->task) {
        case MOD_OS:        sysinfo_os(info); break;
        case MOD_KERNEL:    sysinfo_kernel(info); break;
        case MOD_HOSTNAME:  sysinfo_hostname(info); break;
        case MOD_SHELL:     sysinfo_shell(info); break;
        case MOD_TERMINAL:  sysinfo_terminal(info); break;
        case MOD_UPTIME:    sysinfo_uptime(info); break;
        case MOD_CPU:       sysinfo_cpu(info); break;
        case MOD_GPU:       sysinfo_gpu(info); break;
        case MOD_MEMORY:    sysinfo_memory(info); break;
        case MOD_DISK:      sysinfo_disk(info); break;
        case MOD_DE_WM:     sysinfo_de_wm(info); break;
        case MOD_PACKAGES:  sysinfo_packages(info); break;
        case MOD_LOCAL_IP:  sysinfo_local_ip(info); break;
        case MOD_PUBLIC_IP:
            sysinfo_public_ip(info, ta->cfg->public_ip_timeout_ms);
            break;
        case MOD_RESOLUTION: sysinfo_resolution(info); break;
        case MOD_BATTERY:   sysinfo_battery(info); break;
        default: break;
    }
    return NULL;
}

void sysinfo_gather(sysinfo_t *info, const cfetch_config_t *cfg)
{
    memset(info, 0, sizeof(*info));

    /* Group 1: fast /proc reads - run sequentially (very fast) */
    /* Group 2: potentially slow operations - parallelize */
    pthread_t threads[MOD_COUNT];
    thread_arg_t args[MOD_COUNT];
    int thread_count = 0;

    /* Fast sequential reads */
    if (cfg->modules[MOD_OS])       sysinfo_os(info);
    if (cfg->modules[MOD_KERNEL])   sysinfo_kernel(info);
    if (cfg->modules[MOD_HOSTNAME]) sysinfo_hostname(info);
    if (cfg->modules[MOD_SHELL])    sysinfo_shell(info);
    if (cfg->modules[MOD_TERMINAL]) sysinfo_terminal(info);
    if (cfg->modules[MOD_UPTIME])   sysinfo_uptime(info);
    if (cfg->modules[MOD_CPU])      sysinfo_cpu(info);
    if (cfg->modules[MOD_MEMORY])   sysinfo_memory(info);
    if (cfg->modules[MOD_DISK])     sysinfo_disk(info);

    /* Slow operations in parallel threads */
    int slow_mods[] = { MOD_GPU, MOD_PACKAGES, MOD_DE_WM,
                        MOD_LOCAL_IP, MOD_PUBLIC_IP,
                        MOD_RESOLUTION, MOD_BATTERY };
    int nslow = (int)(sizeof(slow_mods) / sizeof(slow_mods[0]));

    for (int i = 0; i < nslow; i++) {
        int mod = slow_mods[i];
        if (!cfg->modules[mod]) continue;
        if (mod == MOD_PUBLIC_IP && !cfg->public_ip_enabled) continue;

        args[thread_count].info = info;
        args[thread_count].cfg = cfg;
        args[thread_count].task = mod;
        pthread_create(&threads[thread_count], NULL, gather_thread,
                       &args[thread_count]);
        thread_count++;
    }

    for (int i = 0; i < thread_count; i++) {
        pthread_join(threads[i], NULL);
    }
}
