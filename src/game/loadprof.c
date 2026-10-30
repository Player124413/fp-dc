/* Loading profiler: where the time of one load goes (file access, reads, texture upload, sound,
 * music, object creation, loading-screen redraws, the rest = parsing/allocation/conversion).
 * Built into every build (a few timer reads per file); one plat_log line per load. On the DC the
 * clock is the SH4 timer, so in Flycast this is emulated time (GD-ROM DMA at 1.8 MB/s, no seek time). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(FP_DREAMCAST) || defined(_arch_dreamcast) || defined(__DREAMCAST__)
#define LP_DREAMCAST 1
#include <malloc.h>
#include <dc/pvr.h>
#endif
#include "../fp.h"

loadprof_t LP;
static uint64_t t0;
static char label[24];

void lp_begin(const char *name) {
    memset(&LP, 0, sizeof LP);
    snprintf(label, sizeof label, "%s", name ? name : "?");
    t0 = plat_time_us();
    LP.on = 1;
}

uint32_t lp_end(void) {
    if (!LP.on) return 0;
    LP.on = 0;
    uint64_t tot = plat_time_us() - t0;
    uint64_t known = LP.open_us + LP.read_us + LP.upload_us + LP.snd_us + LP.music_us + LP.obj_us + LP.draw_us + LP.decode_us;
    uint64_t other = tot > known ? tot - known : 0;
    unsigned kb = (unsigned)(LP.bytes / 1024);
    unsigned rate = LP.read_us ? (unsigned)(LP.bytes * 1000000ull / LP.read_us / 1024) : 0;
    unsigned heap_kb = 0, vram_kb = 0;
#ifdef LP_DREAMCAST
    { struct mallinfo mi = mallinfo(); heap_kb = (unsigned)(mi.uordblks / 1024); }   /* RAM in use after the load */
    vram_kb = (unsigned)(pvr_mem_available() / 1024);                                  /* largest free VRAM block */
#else
    (void)heap_kb; (void)vram_kb;                     /* host / Android: the platform owns both */
#endif
    plat_log("load[%s] heap %u KB vram free %u KB, %u ms: open %u (%u files) read %u (%u KB, %u KB/s) tex %u (%u, %u KB) sfx %u (%u) "
             "decode %u (%u) music %u obj %u draw %u (%u) other %u; allocs %u / %u KB; skipped %u (%u KB)\n",
             label, heap_kb, vram_kb, (unsigned)(tot / 1000), (unsigned)(LP.open_us / 1000), LP.nfiles, (unsigned)(LP.read_us / 1000), kb, rate,
             (unsigned)(LP.upload_us / 1000), LP.ntex, (unsigned)(LP.tex_bytes / 1024), (unsigned)(LP.snd_us / 1000), LP.nsnd,
             (unsigned)(LP.decode_us / 1000), LP.npacked,
             (unsigned)(LP.music_us / 1000), (unsigned)(LP.obj_us / 1000), (unsigned)(LP.draw_us / 1000), LP.ndraw,
             (unsigned)(other / 1000), LP.nalloc, (unsigned)(LP.alloc_bytes / 1024), LP.nskip, (unsigned)(LP.skip_bytes / 1024));
    return (uint32_t)(tot / 1000);
}
