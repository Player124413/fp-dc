/* When a story-room actor is drawn (src/game/cutscene.c csx_draw_actors).
 *
 * A story room's script comes from RV traces of the original, and a room recycles the memory of the objects it
 * destroys: the trace then reports another object's state under the same address, so everything the generator could
 * not tell apart ends up on one actor (tools/story_rv2scn.py --split-reuse / --by-class; its own note names room 76,
 * "sparkle3_399 -> playerskincaroltail_448 at frame 1"). Visibility is what suffers: in dv_meet (the Dragon Valley
 * opening) Carol stands in the view and speaks while every one of her 'v' entries says 0 - the room showed the dialog
 * box over an empty jungle, which is the "missing Carol" report.
 *
 * So a heroine is drawn on the strength of what the script places: an image and a spot inside the view. The ways the
 * original hides one still hold - the off-map sentinel (-32, -32), anything out of the view, a zero alpha, and an
 * actor the story code drives itself (the character select locks its slots). Props (gimmicks, effects, NPC sprites)
 * keep their own flag: they are another scene's leftovers, parked with visibility off.
 *
 * That last line is not a guess, it was tried the other way and taken back: letting a prop in on its placement alone
 * (Carol's bike in dv_meet, the "her bike is missing" report) put a whole airship over the Character Select screen,
 * whose props sit in the view with the flag off and are not locked -- D-126. A prop is a room's leftover for some
 * other scene of it, so its own flag is the only thing that tells the two apart; the bike is found in the log instead
 * (see the cast lines below and android/README.md), not by widening this rule.
 *
 * The flag is not enough either, because a recycled address does not only spoil visibility: it writes another
 * object's MOTION into the actor. The script's '<t> m <actor> <dx> <dy> <frames>' is that object's velocity for
 * one frame, and a real prop does not reverse a couple of hundred pixels between two frames -- the corrupted entries do,
 * hundreds of times (dv_meet's explosions swing +2213 then -2213 px on consecutive frames, and land on the
 * heroine's own position). Those are the sprites that appear on the jungle ledge during a quiet line of dialog:
 * "there are extra sprites in the cutscenes, they are not like the Dreamcast". So a prop whose script throws it
 * back and forth (STORY_FLIPS_JUNK sign reversals of more than STORY_FLIP_PX) is not drawn at all. A heroine is
 * not judged by it: her motion is equally noisy, and her own row is what the scene stages.
 *
 * Header-only and pure, so the truth table is checked on the host: tools/android/check_story_draw.c. */
#ifndef FP_STORY_DRAW_H
#define FP_STORY_DRAW_H

#define STORY_DRAW_MARGIN_X 96          /* how far past the screen edges a standing actor is still "in the view" */
#define STORY_DRAW_MARGIN_Y 160         /* tall sprites hang above their feet: their top may start off screen */
#define STORY_FLIP_PX 200               /* a one-frame step this large that reverses sign is not motion */
#define STORY_FLIPS_JUNK 2              /* two of them: the trace wrote another object's velocity into this actor */

static inline int story_actor_shown(int vis, int row, int skin, int locked, int alpha, int wx, int wy,
                                    float sx, float sy, int screen_w, int screen_h, int flips) {
    if (row < 0) return 0;                                     /* no image from the script yet */
    if (!skin && flips >= STORY_FLIPS_JUNK) return 0;          /* a prop thrown back and forth: not this scene's */
    if (vis) return 1;                                         /* what the script says */
    if (!skin || locked || alpha <= 0) return 0;
    if (wx == -32 && wy == -32) return 0;                      /* the room's parked template */
    if (sx < -(float)STORY_DRAW_MARGIN_X || sx > (float)screen_w + STORY_DRAW_MARGIN_X) return 0;
    if (sy < -(float)STORY_DRAW_MARGIN_Y || sy > (float)screen_h + STORY_DRAW_MARGIN_X) return 0;
    return 1;
}

#endif
