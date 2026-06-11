#ifndef CFETCH_SYSINFO_H
#define CFETCH_SYSINFO_H

#include "config.h"
#include <stdbool.h>

#define SYSINFO_MAX_GPUS  4
#define SYSINFO_MAX_PKGS  20

/* Package manager result */
typedef struct {
    char name[32];
    int  count;
} pkg_manager_t;

/* All gathered system information */
typedef struct {
    /* Basic */
    char os_name[128];
    char os_version[64];
    char os_pretty[256];
    char kernel[128];
    char hostname[128];

    /* Shell / Terminal */
    char shell[128];
    char terminal[128];

    /* Uptime */
    long uptime_secs;
    char uptime_str[128];

    /* CPU */
    char cpu_model[256];
    int  cpu_cores;
    int  cpu_threads;
    double cpu_freq_ghz;

    /* GPU */
    char gpu[SYSINFO_MAX_GPUS][256];
    int  gpu_count;

    /* Memory (in MiB) */
    long mem_total_mb;
    long mem_used_mb;

    /* Disk (root fs, in GiB) */
    double disk_total_gb;
    double disk_used_gb;

    /* DE/WM */
    char de_wm[128];

    /* Packages */
    pkg_manager_t packages[SYSINFO_MAX_PKGS];
    int pkg_manager_count;
    int pkg_total;
    char pkg_summary[512];

    /* Network */
    char local_ip[64];
    char public_ip[64];

    /* Display */
    char resolution[64];

    /* Battery */
    char battery[128];

    /* Distro ID (for ascii art selection) */
    char distro_id[64];
} sysinfo_t;

/* Gather all system info (uses pthreads for parallelism) */
void sysinfo_gather(sysinfo_t *info, const cfetch_config_t *cfg);

/* Individual gatherers (can be called standalone) */
void sysinfo_os(sysinfo_t *info);
void sysinfo_kernel(sysinfo_t *info);
void sysinfo_hostname(sysinfo_t *info);
void sysinfo_shell(sysinfo_t *info);
void sysinfo_terminal(sysinfo_t *info);
void sysinfo_uptime(sysinfo_t *info);
void sysinfo_cpu(sysinfo_t *info);
void sysinfo_gpu(sysinfo_t *info);
void sysinfo_memory(sysinfo_t *info);
void sysinfo_disk(sysinfo_t *info);
void sysinfo_de_wm(sysinfo_t *info);
void sysinfo_packages(sysinfo_t *info);
void sysinfo_local_ip(sysinfo_t *info);
void sysinfo_public_ip(sysinfo_t *info, int timeout_ms);
void sysinfo_resolution(sysinfo_t *info);
void sysinfo_battery(sysinfo_t *info);

/* Detect distro ID string */
void sysinfo_detect_distro(char *id, int idlen);

#endif /* CFETCH_SYSINFO_H */
