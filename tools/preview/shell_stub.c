/* Stub platform for the host preview and the CI image tests: the reference for what a real
 * platform (plat_android.c) must provide to the shell (shell.h).  Nothing is written to disk, the
 * data package is faked, and the file picker is a no-op, so a preview run is deterministic. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/platform/android/shell.h"

#ifndef PREVIEW_W
#define PREVIEW_W 2400
#define PREVIEW_H 1080
#define PREVIEW_DP 2.75f
#endif

static int data_ok = 1;
static long data_bytes = 168L * 1024 * 1024;
static int vibrations;
static int import_status;
static const char *import_msg = "";
static int launched, exited;

void stub_reset(void) {
    data_ok = 1; data_bytes = 168L * 1024 * 1024; vibrations = 0; import_status = 0; import_msg = "";
    launched = exited = 0;
}
void stub_set_data(int ok, long bytes) { data_ok = ok; data_bytes = bytes; }
int stub_vibrations(void) { return vibrations; }
int stub_launched(void) { return launched; }
int stub_exited(void) { return exited; }
int stub_screen_w(void) { return PREVIEW_W; }
int stub_screen_h(void) { return PREVIEW_H; }
float stub_dp(void) { return PREVIEW_DP; }

/* ------------------------------------------------------------------ shell.h services */
void shell_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
void shell_vibrate(int ms, int amplitude) {
    (void)ms; (void)amplitude;
    vibrations++;
}
int shell_data_ok(void) { return data_ok; }
long shell_data_bytes(void) { return data_ok ? data_bytes : 0; }
const char *shell_data_dir(void) { return "/sdcard/Android/data/com.fpdc.port/files/data"; }
const char *shell_log_file(void) { return "out/fpdc.log"; }
void shell_log_share(void) {}
void shell_data_import(void) {
    /* the preview pretends the user picked a valid package */
    import_status = 1;
    import_msg = "IMPORTING 1 OF 3128 FILES...";
}
void shell_data_import_done_cb(void) {}
int shell_data_import_status(void) { return import_status; }
const char *shell_data_import_message(void) { return import_msg; }
int shell_file_read(const char *name, char *buf, int cap) { (void)name; (void)buf; (void)cap; return 0; }
int shell_file_write(const char *name, const char *data, int len) { (void)name; (void)data; (void)len; return len; }
void shell_launch_game(void) { launched = 1; }
void shell_exit_app(void) { exited = 1; }
const char *shell_build_version(void) { return "V0.1  PREVIEW"; }
void shell_device_info(char *buf, int cap) {
    snprintf(buf, (size_t)cap, "%dx%d  %.2fx  GLES3", PREVIEW_W, PREVIEW_H, (double)PREVIEW_DP);
}
