/* The UI's colours are 0xAARRGGBB (ui.h). The soft preview and the GLES backend must unpack them the
 * same way -- when ui_gles.c unpacked them as RGBA, the launcher's dark blue background came out as
 * 10%-alpha red and the whole screen looked like neon garbage on a phone. This check pins the packing
 * and, if it can, the two backends against each other. */
#include <stdio.h>
#include "../../src/platform/android/ui.h"

static int fails;

static void expect(uint32_t c, int r, int g, int b, int a, const char *what) {
    if (ui_ch_r(c) != r || ui_ch_g(c) != g || ui_ch_b(c) != b || ui_ch_a(c) != a) {
        printf("  FAIL %s: 0x%08x -> %d,%d,%d,%d (want %d,%d,%d,%d)\n", what, c,
               ui_ch_r(c), ui_ch_g(c), ui_ch_b(c), ui_ch_a(c), r, g, b, a);
        fails++;
    }
}

int main(void) {
    expect(UI_BG_TOP,     0x0a, 0x0e, 0x1a, 0xff, "UI_BG_TOP (dark blue, not red)");
    expect(UI_BG_BOT,     0x14, 0x1a, 0x2e, 0xff, "UI_BG_BOT");
    expect(UI_INK,        0xe8, 0xee, 0xfc, 0xff, "UI_INK");
    expect(UI_ACCENT,     0x4f, 0xd8, 0xff, 0xff, "UI_ACCENT (cyan)");
    expect(UI_ACCENT2,    0xff, 0xc2, 0x4a, 0xff, "UI_ACCENT2 (gold)");
    expect(UI_MAGENTA,    0xff, 0x5f, 0xa2, 0xff, "UI_MAGENTA");
    expect(UI_BAD,        0xff, 0x5a, 0x5a, 0xff, "UI_BAD");
    expect(0x0effffffu,   0xff, 0xff, 0xff, 0x0e, "the dot grid: white at 5%");
    expect(0x21ffffffu,   0xff, 0xff, 0xff, 0x21, "white at 13%");
    printf("colour packing %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
