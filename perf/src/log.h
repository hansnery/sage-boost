/* Plain-text log file next to the DLL. CRT-free; safe to use from DllMain. */
#ifndef FU_LOG_H
#define FU_LOG_H

#include <windows.h>

/* Truncates/creates the file. Without an open file every fu_logf is a no-op. */
void fu_log_open(const WCHAR *path);
/* Opens for appending (creating the file if needed). */
void fu_log_append(const WCHAR *path);
void fu_log_close(void);

/* Writes one line (a newline is appended). Supports %s %ls %c %d %u %x %X
 * with an optional '0' flag and width, and %%. */
void fu_logf(const char *fmt, ...);

/* Lines written while the game runs: each fu_log_event appends one line to
 * the file set here (opened and closed per line; dropped if that fails).
 * No path: no-op. */
void fu_log_event_path(const WCHAR *path);
void fu_log_event(const char *fmt, ...);

#endif
