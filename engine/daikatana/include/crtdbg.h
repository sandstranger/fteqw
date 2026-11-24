/*
MSVC's debug CRT header, for Daikatana's game code elsewhere: its checks do nothing.
*/
#ifndef DK_CRTDBG_H
#define DK_CRTDBG_H
#ifdef _MSC_VER
#include_next <crtdbg.h>
#else
#define _ASSERT(x)				((void)0)
#define _ASSERTE(x)				((void)0)
#define _RPT0(t,m)				((void)0)
#define _RPT1(t,m,a)			((void)0)
#define _RPT2(t,m,a,b)			((void)0)
#define _RPT3(t,m,a,b,c)		((void)0)
#define _RPTF0(t,m)				((void)0)
#define _RPTF1(t,m,a)			((void)0)
#define _RPTF2(t,m,a,b)			((void)0)
#define _RPTF3(t,m,a,b,c)		((void)0)
#define _CRT_WARN	0
#define _CRT_ERROR	1
#define _CRT_ASSERT	2
#endif
#endif
