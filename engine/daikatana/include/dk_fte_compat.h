/*
FTE's build of Daikatana's game code: what it took from MSVC and its CRT, elsewhere. Included before everything
(-include), only when not building with MSVC.
*/
#ifndef DK_FTE_COMPAT_H
#define DK_FTE_COMPAT_H

#ifndef _MSC_VER
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <limits.h>

#ifndef _WIN32
#define __stdcall
#define __cdecl
#define _cdecl
#define WINAPI
#include "crtdbg.h"
#define _strdup		strdup
#define _stricmp	strcasecmp
#define stricmp		strcasecmp
#define strcmpi		strcasecmp
#define _strnicmp	strncasecmp
#define strnicmp	strncasecmp
static inline char *_strlwr(char *s)
{
	char *p;
	for (p = s; *p; p++)
		*p = (char)tolower((unsigned char)*p);
	return s;
}
static inline char *_strupr(char *s)
{
	char *p;
	for (p = s; *p; p++)
		*p = (char)toupper((unsigned char)*p);
	return s;
}
static inline char *itoa(int value, char *str, int radix)
{
	sprintf(str, radix == 16?"%x":"%d", value);
	return str;
}
#define _itoa		itoa
#define _vsnprintf	vsnprintf
#define _snprintf	snprintf
#endif

#ifdef __cplusplus
//MSVC's min and max macros (windows.h's), as templates, which don't trip over the C++ library's
template<class A, class B> static inline A dk_min(A a, B b) {return (a < b)?a:(A)b;}
template<class A, class B> static inline A dk_max(A a, B b) {return (a > b)?a:(A)b;}
#endif

#endif	//!_MSC_VER

//the DLLs are linked together, and into FTE: what would meet another's names is renamed (mkstatic.py)
#if defined(DK_MODULE_PHYSICS)
#include "dk_rename_physics.h"
#elif defined(DK_MODULE_WORLD)
#include "dk_rename_world.h"
#elif defined(DK_MODULE_WEAPONS)
#include "dk_rename_weapons.h"
#elif defined(DK_MODULE_USER)
#include "dk_rename_user.h"
#elif defined(DK_MODULE_FTE)
#include "dk_rename_fte.h"
#elif defined(DK_MODULE_CLIENT)
#include "dk_rename_client.h"
#endif

#endif
