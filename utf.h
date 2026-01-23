#ifndef UTF_H
#define UTF_H

#include "wee.h"

/* utfprev steps to the previous utf-8 codepoint boundary (or 0). */
size_t utfprev(const char *s, size_t len, size_t i);

/* utfnext steps to the next utf-8 codepoint boundary (or len). */
size_t utfnext(const char *s, size_t len, size_t i);

#endif
