/*
Functions of yquake2's shared code the menus use, copied verbatim (the whole files bring in OS headers
FTE's other builds would not like, or a client FTE replaces). The rename header gives them q2m_ names.

Copyright (C) 1997-2001 Id Software, Inc. -- GPL v2 or later, as yquake2.
*/
#include "q2menu.h"
#include <ctype.h>

char com_token[MAX_TOKEN_CHARS];

const char *
COM_Parse(char **data_p)
{
	int c;
	int len;
	char *data;

	data = *data_p;
	len = 0;
	com_token[0] = 0;

	if (!data)
	{
		*data_p = NULL;
		return "";
	}

skipwhite:

	while ((c = *data) <= ' ')
	{
		if (c == 0)
		{
			*data_p = NULL;
			return "";
		}

		data++;
	}

	/* skip // comments */
	if ((c == '/') && (data[1] == '/'))
	{
		while (*data && *data != '\n')
		{
			data++;
		}

		goto skipwhite;
	}

	/* handle quoted strings specially */
	if (c == '\"')
	{
		data++;

		while (1)
		{
			c = *data++;

			if ((c == '\"') || !c)
			{
				goto done;
			}

			if (len < MAX_TOKEN_CHARS)
			{
				com_token[len] = c;
				len++;
			}
		}
	}

	/* parse a regular word */
	do
	{
		if (len < MAX_TOKEN_CHARS)
		{
			com_token[len] = c;
			len++;
		}

		data++;
		c = *data;
	}
	while (c > 32);

done:
	if (len == MAX_TOKEN_CHARS)
	{
		len = 0;
	}

	com_token[len] = 0;

	*data_p = data;
	return com_token;
}

char *
COM_StripExtension2(char *path)
{
	char *s ;

	if (*path == '\0')
	{
		return NULL;
	}

	s = path + (strlen(path) - 1);

	if (*s != '.')
	{
		for (; s > path && *s != '/' && *s != '\\'; s--)
		{
			if (*s == '.')
			{
				*s = '\0';
				return s + 1;
			}
		}
	}

	return NULL;
}

void
Com_sprintf(char *dest, int size, const char *fmt, ...)
{
	int len;
	va_list argptr;

	va_start(argptr, fmt);
	len = vsnprintf(dest, size, fmt, argptr);
	va_end(argptr);

	if (len >= size)
	{
		Com_Printf("%s: overflow\n", __func__);
	}
}

int
Q_sort_stricmp(const void *s1, const void *s2)
{
	return Q_stricmp(*(char**)s1, *(char**)s2);
}

char *
Q_strchrs(const char *s, const char *chrs)
{
	for (; *s != '\0'; s++)
	{
		if (strchr(chrs, *s))
		{
			return (char *)s;
		}
	}

	return NULL;
}

void
Q_strdel(char *s, size_t i, size_t n)
{
	size_t len;

	if (!n)
	{
		return;
	}

	len = strlen(s);

	if (i >= len || n > (len - i))
	{
		return;
	}

	memmove(s + i, s + i + n, len - i);
	s[len - n] = '\0';
}

int
Q_stricmp(const char *s1, const char *s2)
{
#ifdef _MSC_VER
	return stricmp(s1, s2);
#else
	return strcasecmp(s1, s2);
#endif
}

size_t
Q_strins(char *dest, const char *src, size_t i, size_t n)
{
	size_t dlen;
	size_t slen;

	if (!src || *src == '\0')
	{
		return 0;
	}

	slen = strlen(src);
	dlen = strlen(dest);

	if (i > dlen || (dlen + slen + 1) > n)
	{
		return 0;
	}

	memmove(dest + i + slen, dest + i, dlen - i + 1);
	memcpy(dest + i, src, slen);

	return slen;
}

qboolean
Q_strisnum(const char *s)
{
	for (; *s != '\0'; s++)
	{
		if (!isdigit(*s))
		{
			return false;
		}
	}

	return true;
}

int
Q_strlcat(char *dst, const char *src, int size)
{
	char *d = dst;

	while (size > 0 && *d)
	{
		size--;
		d++;
	}

	return (d - dst) + Q_strlcpy(d, src, size);
}

int
Q_strlcpy(char *dst, const char *src, int size)
{
	const char *s = src;

	while (*s)
	{
		if (size > 1)
		{
			*dst++ = *s;
			size--;
		}
		s++;
	}
	if (size > 0)
	{
		*dst = '\0';
	}

	return s - src;
}

char *
Q_strrchrs(const char *s, const char *chrs)
{
	const char *curr;

	for (curr = s + (strlen(s) - 1); curr >= s; curr--)
	{
		if (strchr(chrs, *curr))
		{
			return (char *)curr;
		}
	}

	return NULL;
}

void
Q_strupr(char *s)
{
	for (; *s != '\0'; s++)
	{
		*s = toupper(*s);
	}
}

void
StrList_Append(strlist_t *sl, const char *s)
{
	char *new_s;

	if (sl->num >= sl->cap)
	{
		StrList_Expand(sl, NextPow2gt(sl->cap));

		if (sl->num >= sl->cap)
		{
			return;
		}
	}

	new_s = strdup(s);
	if (new_s)
	{
		sl->data[sl->num] = new_s;
		sl->num++;
	}
}

void
StrList_Compress(strlist_t *sl)
{
	char **new_data;

	if (sl->cap <= sl->num)
	{
		return;
	}

	new_data = realloc(sl->data, (sl->num + 1) * sizeof(char *));
	if (new_data)
	{
		sl->data = new_data;
		sl->cap = sl->num;
	}
}

void
StrList_Expand(strlist_t *sl, int new_cap)
{
	char **new_data;

	if (new_cap <= sl->cap)
	{
		return;
	}

	new_data = Q_realloc0(sl->data,
		!sl->cap ? 0 : ((sl->cap + 1) * sizeof(char *)),
		(new_cap + 1) * sizeof(char *));

	if (!new_data)
	{
		return;
	}

	sl->data = new_data;
	sl->cap = new_cap;
}

int
StrList_Find(const strlist_t *sl, sl_eqfunc eq, const char *s)
{
	int i;

	if (!eq)
	{
		eq = strcmp;
	}

	for (i = 0; i < sl->num; i++)
	{
		if (!eq(sl->data[i], s))
		{
			return i;
		}
	}

	return sl->num;
}

void
StrList_Free(strlist_t *sl)
{
	if (sl->data)
	{
		int i;

		for (i = 0; i < sl->num; i++)
		{
			if (sl->data[i])
			{
				free(sl->data[i]);
			}
		}

		free(sl->data);
		sl->data = NULL;
	}

	sl->cap = 0;
	sl->num = 0;
}

void
StrList_Init(strlist_t *sl, int cap)
{
	memset(sl, 0, sizeof(*sl));

	if (cap > 0)
	{
		/* allocating +1 ensures the list is always NULL terminated */
		sl->data = calloc(cap + 1, sizeof(char *));

		if (!sl->data)
		{
			return;
		}

		sl->cap = cap;
	}
}

char *
va(const char *format, ...)
{
	va_list argptr;
	static char string[1024];

	va_start(argptr, format);
	vsnprintf(string, 1024, format, argptr);
	va_end(argptr);

	return string;
}

int
Q_strcasecmp(const char *s1, const char *s2)
{
	return Q_strncasecmp(s1, s2, 99999);
}

int
Q_strncasecmp(const char *s1, const char *s2, int n)
{
	int c1;

	do
	{
		int c2;

		c1 = *s1++;
		c2 = *s2++;

		if (!n--)
		{
			return 0; /* strings are equal until end point */
		}

		if (c1 != c2)
		{
			if ((c1 >= 'a') && (c1 <= 'z'))
			{
				c1 -= ('a' - 'A');
			}

			if ((c2 >= 'a') && (c2 <= 'z'))
			{
				c2 -= ('a' - 'A');
			}

			if (c1 != c2)
			{
				return -1; /* strings not equal */
			}
		}
	}
	while (c1);

	return 0; /* strings are equal */
}

unsigned int
NextPow2(unsigned int i)
{
	if (!i)
	{
		return 1U;
	}

	i--;

	if (i & (1U << 31U))
	{
		return 0U;
	}

	i |= i >> 1U;
	i |= i >> 2U;
	i |= i >> 4U;
	i |= i >> 8U;
	i |= i >> 16U;

	return i + 1U;
}

unsigned int
NextPow2gt(unsigned int i)
{
	return NextPow2(i + 1U);
}

void *
Q_realloc0(void *ptr, size_t prev_n, size_t new_n)
{
	void *new_ptr = realloc(ptr, new_n);

	if (!new_ptr)
	{
		return NULL;
	}

	if (new_n > prev_n)
	{
		memset((char *)new_ptr + prev_n, 0, new_n - prev_n);
	}

	return new_ptr;
}

float
CalcFov(float fov_x, float width, float height)
{
	float a;
	float x;

	if ((fov_x < 1) || (fov_x > 179))
	{
		Com_Error(ERR_DROP, "Bad fov: %f", fov_x);
	}

	x = width / (float)tan(fov_x / 360 * M_PI);

	a = (float)atan(height / x);

	a = a * 360 / M_PI;

	return a;
}
