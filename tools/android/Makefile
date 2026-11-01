# Host-side checks of the Android port.  Everything here runs without the NDK or a device: the shell
# screens are rendered by the preview harness, the GLES backends are compiled against the stub headers
# in ndkstub/, and the Dreamcast twiddle is verified against tools/twiddle_disc.py.
#
#   make -C tools/android check        # everything
#   make -C tools/android preview      # render every shell screen to tools/preview/out/*.png
CC ?= cc
ROOT := $(abspath ../..)
STUB := ndkstub
# -Werror=implicit-function-declaration: gcc 14 (the CI runner) rejects what gcc 12 only warns about, and
# a GL call missing from ndkstub/ must fail here, not on the runner
SYNTAX := -fsyntax-only -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Werror=implicit-function-declaration -I$(STUB)

ANDROID_SRC := $(ROOT)/src/renderer/render_gles.c $(ROOT)/src/renderer/ui_gles.c \
               $(ROOT)/src/renderer/twiddle.c $(ROOT)/src/resources/assets.c \
               $(ROOT)/src/platform/android/plat_android.c $(ROOT)/src/platform/android/snd_android.c \
               $(ROOT)/src/platform/android/i18n.c

.PHONY: check syntax abi twiddle font colour uiorder present texlife anim audio shell selftest preview
check: syntax abi twiddle font colour uiorder present texlife anim audio shell selftest

syntax:
	@for f in $(ANDROID_SRC); do echo "  syntax-check $$f"; $(CC) $(SYNTAX) $$f || exit 1; done
	@echo "  android sources: ok (stub NDK headers)"

# abi: the APK ships a 32-bit library next to the 64-bit one, and this is where that cannot be lost
abi:
	@python3 check_abi.py

twiddle:
	@python3 check_twiddle.py

font:
	@python3 make_font.py --check

colour:
	@$(CC) -std=gnu11 -Wall -Wextra -o /tmp/fpdc_check_colour check_colour.c && /tmp/fpdc_check_colour

uiorder:
	@$(CC) -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-strict-prototypes -I$(STUB) \
	   -o /tmp/fpdc_check_ui_order check_ui_order.c -lm && /tmp/fpdc_check_ui_order

# texlife: a texture handle must die with the table it came from, and freeing a stale one must not delete
#          the atlas that took the slot (the black screen on the second launch)
texlife:
	@$(CC) -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-strict-prototypes -I$(STUB) -I$(ROOT)/src \
	   -o /tmp/fpdc_check_texlife check_texlife.c -lm && /tmp/fpdc_check_texlife

# anim: an animator that outlived its bank (or that kept an index from the previous level's bank) must draw
#       NOTHING - it used to index past anims[]/seq[]/frames[] and return a heap frame rect hundreds of
#       pixels wide, which is the long smeared sprite on both heroines
anim:
	@$(CC) -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -I$(STUB) -I$(ROOT)/src \
	   -o /tmp/fpdc_check_anim check_anim.c -lm && /tmp/fpdc_check_anim

# audio: one AAudio stream, the music/effect tempo, the ADPCM layouts (fake AAudio, no device)
audio:
	@$(CC) -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -I$(STUB) -I$(ROOT)/src \
	   -o /tmp/fpdc_check_audio check_audio.c -lm && /tmp/fpdc_check_audio

# where the 426x240 view lands on the screen and what part of the upscale it samples
present:
	@$(CC) -std=gnu11 -Wall -Wextra -o /tmp/fpdc_check_present check_present.c -lm && /tmp/fpdc_check_present

shell:
	@$(MAKE) -C ../preview >/dev/null && echo "  shell: preview harness builds"

# taps every screen the way a finger would: PLAY, the tabs, a row, a switch, a slider
selftest: shell
	@mkdir -p ../preview/out
	@out=$$(../preview/fpdc_preview ../preview/out selftest 2>&1); \
	 printf '%s\n' "$$out" | grep -E '^(  |selftest:)' | sed 's/^  /    /'; \
	 case "$$out" in *"selftest: ok"*) ;; *) echo "  selftest FAILED"; exit 1 ;; esac

preview: shell
	@mkdir -p ../preview/out
	@cd ../preview && ./fpdc_preview out all >/dev/null && for f in out/*.ppm; do python3 ppm2png.py $$f $${f%.ppm}.png >/dev/null; done
	@echo "  shells screens in tools/preview/out/*.png"
