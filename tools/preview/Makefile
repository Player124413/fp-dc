# Host preview of the Android shell: the app's shell sources + the software UI backend.
#   make -C tools/preview            -> fpdc_preview
#   ./fpdc_preview out/ all          -> PPM frames of every screen (tools/preview/run.sh: also PNG)
SRC = preview.c shell_stub.c ui_soft.c \
      ../../src/platform/android/shell.c ../../src/platform/android/launcher.c ../../src/platform/android/i18n.c \
      ../../src/platform/android/ctl_editor.c ../../src/platform/android/pad_draw.c \
      ../../src/platform/android/widgets.c ../../src/platform/android/touch.c \
      ../../src/platform/android/cfg.c
HDR = $(wildcard ../../src/platform/android/*.h)
# -Werror=implicit-function-declaration: gcc 14 (the CI runner) treats a missing prototype as an error
# while gcc 12 here only warns, so a forgotten stub declaration must fail on the host, not on the runner
CFLAGS = -O2 -g -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wno-unused-parameter \
         -Werror=implicit-function-declaration
fpdc_preview: $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o $@ $(SRC) -lm
check: fpdc_preview
	./fpdc_preview out all
clean:
	rm -f fpdc_preview
.PHONY: check clean
