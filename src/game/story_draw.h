/* When a story-room actor is drawn (src/game/cutscene.c csx_draw_actors).
 *
 * A story room's script comes from RV traces of the original, and a room recycles the memory of the objects it
 * destroys: the trace then reports another object's state under the same address, so everything the generator could
 * not tell apart ends up on one actor (tools/story_rv2scn.py --split-reuse / --by-class; its own note names room 76,
 * "sparkle3_399 -> playerskincaroltail_448 at frame 1"). Visibility is what suffers: in dv_meet (the Dragon Valley
 * opening) Carol stands in the view and speaks while every one of her 'v' entries says 0 - the room showed the dialog
 * box over an empty jungle, which is the "missing Carol" report.
 *
 * So an actor is drawn on the strength of what the script places: an image and a spot inside the view. The ways the
 * original hides one still hold - the off-map sentinel (-32, -32), anything out of the view, a zero alpha, and an
 * actor the story code drives itself (the character select locks its slots).
 *
 * The rule first applied to the heroines only, and a prop of the scene was lost that way: dv_meet shows Carol, her
 * bike and Lilac on the cliff, and the bike's 'v' entries are as spoiled as Carol's ("the bike is missing in the
 * Android port, the cutscene plays wrong"). A prop that the room's other scenes park is off the view or on the
 * sentinel, which is what the two checks below are for; one that this script places inside the view belongs in the
 * picture, heroine or not. The converter only emits an actor it saw VISIBLE in the window (tools/story_rv2scn.py,
 * "an actor must be seen"), so being in the table at all is evidence that the object is on stage here.
 *
 * Header-only and pure, so the truth table is checked on the host: tools/android/check_story_draw.c. */
#ifndef FP_STORY_DRAW_H
#define FP_STORY_DRAW_H

#define STORY_DRAW_MARGIN_X 96          /* how far past the screen edges a standing actor is still "in the view" */
#define STORY_DRAW_MARGIN_Y 160         /* tall sprites hang above their feet: their top may start off screen */

static inline int story_actor_shown(int vis, int row, int skin, int locked, int alpha, int wx, int wy,
                                    float sx, float sy, int screen_w, int screen_h) {
    if (row < 0) return 0;                                     /* no image from the script yet */
    if (vis) return 1;                                         /* what the script says */
    (void)skin;                                                /* props are as unreliable as heroines here */
    if (locked || alpha <= 0) return 0;
    if (wx == -32 && wy == -32) return 0;                      /* the room's parked template */
    if (sx < -(float)STORY_DRAW_MARGIN_X || sx > (float)screen_w + STORY_DRAW_MARGIN_X) return 0;
    if (sy < -(float)STORY_DRAW_MARGIN_Y || sy > (float)screen_h + STORY_DRAW_MARGIN_X) return 0;
    return 1;
}

#endif
