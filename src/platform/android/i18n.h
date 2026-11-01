/* The shell speaks English and Russian.
 *
 * tr() is the whole mechanism: the English text is the key, the Russian one travels next to it in the
 * call, and a string that was never translated is simply the English one. The language is a setting
 * (sys.lang: 0 English, 1 Russian) read by shell_init(), so a screen can be drawn without asking. */
#ifndef FP_ANDROID_I18N_H
#define FP_ANDROID_I18N_H

extern int fp_ru;                       /* 1: draw the Russian text */

const char *tr(const char *en, const char *ru);
#define T(en, ru) tr((en), (ru))

#endif
