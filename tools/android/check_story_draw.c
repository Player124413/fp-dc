/* Host check of the story actor draw rule (src/game/story_draw.h, used by src/game/cutscene.c).
 * It exists for the "missing Carol" report: a story room's script comes from RV traces in which the original recycles
 * the memory of destroyed objects, so a heroine's visibility flag can be wrong while she stands in the view and talks.
 * The rule draws her then, and this check pins both halves of it: what must be drawn, and what must stay hidden. */
#include <stdio.h>
#include "../../src/game/story_draw.h"

#define SW 426
#define SH 240
static int fails;

static void expect(const char *what, int got, int want) {
    if (got != want) { printf("  FAIL %s (got %d, want %d)\n", what, got, want); fails++; }
    else printf("  %-58s ok\n", what);
}
static int shown(int vis, int row, int skin, int locked, int alpha, int wx, int wy, float sx, float sy) {
    return story_actor_shown(vis, row, skin, locked, alpha, wx, wy, sx, sy, SW, SH, 0);
}
/* the same, with the motion the script gives the actor: how many times it reverses by more than STORY_FLIP_PX */
static int shown_j(int vis, int row, int skin, int locked, int alpha, int wx, int wy, float sx, float sy, int flips) {
    return story_actor_shown(vis, row, skin, locked, alpha, wx, wy, sx, sy, SW, SH, flips);
}

int main(void) {
    printf("story actor draw rule\n");
    /* the script's own flag */
    expect("a visible actor with an image is drawn", shown(1, 7, 0, 0, 255, 100, 100, 100, 100), 1);
    expect("a visible actor without an image is not", shown(1, -1, 0, 0, 255, 100, 100, 100, 100), 0);
    expect("a prop with v=0 stays hidden", shown(0, 7, 0, 0, 255, 100, 100, 100, 100), 0);
    /* the heroine of dv_meet: v=0, standing on the ground in the view (this is the report) */
    expect("Carol with v=0, standing on screen, is drawn", shown(110, 3, 1, 0, 255, 950, 1189, 371, 120), 1);
    expect("the same actress off the view stays hidden", shown(0, 3, 1, 0, 255, 900, 1189, 900, 120), 0);
    expect("far above the screen stays hidden", shown(0, 3, 1, 0, 255, 950, 1189, 371, 900), 0);
    /* the parked templates and the driven actors */
    expect("an actor parked at (-32,-32) stays hidden", shown(0, 3, 1, 0, 255, -32, -32, 394, 208), 0);
    expect("a locked (character select) actor keeps v", shown(0, 3, 1, 1, 255, 950, 1189, 371, 120), 0);
    expect("a fully transparent actress is not drawn", shown(0, 3, 1, 0, 0, 950, 1189, 371, 120), 0);
    /* tall sprites: the feet are in the view, the head hangs above it */
    expect("feet on the ground, head above the screen: drawn", shown(0, 3, 1, 0, 255, 950, 1300, 371, 231), 1);
    expect("one pixel of the frame just past the margin: hidden", shown(0, 3, 1, 0, 255, 950, 1189, 5000, 120), 0);
    /* the extra sprites of a cutscene: the trace wrote another object's motion into a prop (dv_meet's explosions
     * swing +2213 then -2213 px on consecutive frames and land on the heroine's own position).  Such a prop is not
     * drawn even when its own visibility flag says 1; a heroine is not judged by it, and a prop that moves for real
     * (a walk, a charge: < STORY_FLIPS_JUNK reversals) keeps its flag. */
    expect("a prop with v=1 that the script reverses is not drawn", shown_j(1, 7, 0, 0, 255, 700, 1189, 371, 120, STORY_FLIPS_JUNK), 0);
    expect("one reversal is still motion: the prop keeps its flag", shown_j(1, 7, 0, 0, 255, 700, 1189, 371, 120, STORY_FLIPS_JUNK - 1), 1);
    expect("a heroine the script reverses is drawn by her placement", shown_j(0, 3, 1, 0, 255, 950, 1189, 371, 120, 133), 1);
    printf("story draw: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
