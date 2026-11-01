#include "i18n.h"

int fp_ru;

const char *tr(const char *en, const char *ru) { return fp_ru ? ru : en; }
