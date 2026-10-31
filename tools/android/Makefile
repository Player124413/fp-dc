# Host-side checks of the Android port.  Everything here runs without the NDK or a device: the shell
# screens are rendered by the preview harness, the GLES backends are compiled against the stub headers
# in ndkstub/, and the Dreamcast twiddle is verified against tools/twiddle_disc.py.
#
#   make -C tools/android check        # everything
#   make -C tools/android preview      # render every shell screen to tools/preview/out/*.png
CC ?= cc
ROOT := $(abspath ../..)
STUB := ndkstub
SYNTAX := -fsyntax-only -std=gnu11 -Wall -Wextra -Wno-unused-parameter -I$(STUB)

ANDROID_SRC := $(ROOT)/src/renderer/render_gles.c $(ROOT)/src/renderer/ui_gles.c \
               $(ROOT)/src/renderer/twiddle.c \
               $(ROOT)/src/platform/android/plat_android.c $(ROOT)/src/platform/android/snd_android.c

.PHONY: check syntax twiddle font colour shell preview
check: syntax twiddle font colour shell

syntax:
	@for f in $(ANDROID_SRC); do echo "  syntax-check $$f"; $(CC) $(SYNTAX) $$f || exit 1; done
	@echo "  android sources: ok (stub NDK headers)"

twiddle:
	@python3 check_twiddle.py

font:
	@python3 make_font.py --check

colour:
	@$(CC) -std=gnu11 -Wall -Wextra -o /tmp/fpdc_check_colour check_colour.c && /tmp/fpdc_check_colour

shell:
	@$(MAKE) -C ../preview >/dev/null && echo "  shell: preview harness builds"

preview: shell
	@mkdir -p ../preview/out
	@cd ../preview && ./fpdc_preview out all >/dev/null && for f in out/*.ppm; do python3 ppm2png.py $$f $${f%.ppm}.png >/dev/null; done
	@echo "  shells screens in tools/preview/out/*.png"
