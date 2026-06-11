#ifndef CFETCH_ASCII_EXTRACT_H
#define CFETCH_ASCII_EXTRACT_H

#include <stddef.h>

/* Ensure the per-distro ASCII files exist in ~/.config/cfetch/ascii/.
 * Parses the embedded pfetch + neofetch blobs on first call.
 * Returns 0 on success, -1 on failure. Idempotent — safe to call often. */
int ascii_extract_ensure(void);

/* Resolve a cached ascii file path for a given distro id.
 * `minimal` = 1 -> tries "<id>_small.txt" first, then falls back to big.
 *           = 0 -> tries "<id>.txt", falls back to "linux.txt" / "tux".
 * Returns 0 on success and writes path into out (size n). -1 on failure. */
int ascii_extract_resolve(const char *distro_id, int minimal,
                          char *out, size_t n);

#endif
