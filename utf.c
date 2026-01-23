#include "utf.h"

/*
 * utf-8 helpers.
 *
 * wee treats the buffer as bytes, but cursor/motions step over utf-8 sequences.
 */

/* isutfcont reports whether c is a utf-8 continuation byte. */
static bool
isutfcont(unsigned char c)
{
	return (c & 0xc0) == 0x80;
}

/* utfprev steps to the previous utf-8 codepoint boundary (or 0). */
size_t
utfprev(const char *s, size_t len, size_t i)
{
	(void)len;
	if (i == 0)
		return 0;
	i--;
	while (i > 0 && isutfcont((unsigned char)s[i]))
		i--;
	return i;
}

/* utfnext steps to the next utf-8 codepoint boundary (or len). */
size_t
utfnext(const char *s, size_t len, size_t i)
{
	size_t j;

	if (i >= len)
		return len;
	j = i + 1;
	while (j < len && isutfcont((unsigned char)s[j]))
		j++;
	return j;
}
