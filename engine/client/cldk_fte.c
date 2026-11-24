/*
What Daikatana's client code (daikatana/client, C++ with headers that clash with FTE's) has of FTE's client, as plain
functions (daikatana/client/cldk_fte.h): pics and drawing on the HUD's area, sounds, cvars, files, music.

GPL v2 or later.
*/
#include "quakedef.h"

#if defined(Q2CLIENT) && defined(DAIKATANA)
#include "shader.h"
#include "../daikatana/client/cldk_fte.h"

static struct
{
	float x, y;		//the area's corner
	float scale;	//its pixels per the HUD's (which needs 480 of them down)
	int w, h;		//in the HUD's
} cldkarea = {0, 0, 1, 640, 480};

qboolean CLDK_IsDaikatana(void)
{
	return !strcmp(com_protocolname.string, "FTE-Daikatana");
}

void *CLDKF_RegisterPic(const char *name, int *width, int *height)
{
	mpic_t *pic = R2D_SafeCachePic(name);
	int w = 0, h = 0;
	if (!pic || R_GetShaderSizes(pic, &w, &h, true) <= 0 || w <= 0 || h <= 0)
	{
		*width = *height = 0;
		return NULL;
	}
	*width = w;
	*height = h;
	return pic;
}
void CLDKF_DrawPic(void *pic, float x, float y, float w, float h, float s1, float t1, float s2, float t2, const float *rgba)
{
	R2D_ImageColours(rgba[0], rgba[1], rgba[2], rgba[3]);
	R2D_Image(cldkarea.x + x*cldkarea.scale, cldkarea.y + y*cldkarea.scale, w*cldkarea.scale, h*cldkarea.scale, s1, t1, s2, t2, pic);
	R2D_ImageColours(1, 1, 1, 1);
}
void CLDKF_DrawFill(float x, float y, float w, float h, const float *rgba)
{
	R2D_ImageColours(rgba[0], rgba[1], rgba[2], rgba[3]);
	R2D_FillBlock(cldkarea.x + x*cldkarea.scale, cldkarea.y + y*cldkarea.scale, w*cldkarea.scale, h*cldkarea.scale);
	R2D_ImageColours(1, 1, 1, 1);
}
void CLDKF_VidSize(int *width, int *height)
{
	*width = cldkarea.w;
	*height = cldkarea.h;
}
int CLDKF_Milliseconds(void)
{
	return (int)(cl.time*1000);
}
float CLDKF_CvarValue(const char *name, const char *defaultvalue)
{
	cvar_t *v = Cvar_FindVar(name);
	if (!v)
		v = Cvar_Get(name, defaultvalue, CVAR_ARCHIVE, "Daikatana");
	return v?v->value:atof(defaultvalue);
}
void CLDKF_CvarSet(const char *name, const char *value)
{
	cvar_t *v = Cvar_FindVar(name);
	if (!v)
		v = Cvar_Get(name, value, 0, "Daikatana");
	if (v)
		Cvar_Set(v, value);
}
void CLDKF_LocalSound(const char *name)
{
	S_LocalSound(name);
}
void CLDKF_AddCommand(const char *text)
{
	Cbuf_AddText(text, RESTRICT_LOCAL);
}
int CLDKF_Stat(int i)
{
	if (i < 0 || i >= Q2MAX_STATS)
		return 0;
	return cl.q2frame.seat[0].playerstate.stats[i];
}
int CLDKF_RDFlags(void)
{
	return cl.q2frame.seat[0].playerstate.rdflags;
}
const char *CLDKF_MapName(void)
{
	return cl.model_name[1]?COM_SkipPath(cl.model_name[1]):"";
}
int CLDKF_ModelIndex(const char *name)
{
	int i;
	for (i = 1; i < MAX_PRECACHE_MODELS && cl.model_name[i]; i++)
		if (!strcasecmp(cl.model_name[i], name))
			return i;
	return 0;
}
const char *CLDKF_ModelName(int modelindex)
{
	if (modelindex > 0 && modelindex < MAX_PRECACHE_MODELS && cl.model_name[modelindex])
		return cl.model_name[modelindex];
	return "";
}
void *CLDKF_LoadFile(const char *name, int *length)
{
	size_t len;
	void *buf = FS_LoadMallocFile(name, &len);
	*length = buf?(int)len:0;
	return buf;
}
void CLDKF_FreeFile(void *buf)
{
	BZ_Free(buf);
}
void CLDKF_Music(const char *name, int loop)
{
	if (!name)
		Media_NamedTrack(NULL, NULL);
	else
		Media_NamedTrack(name, loop?name:NULL);
}

//the status bar and the rest of its HUD, for the first seat (it had no splitscreen)
qboolean CLDK_DrawHUDArea(int seat, const vrect_t *area)
{
	if (!CLDK_IsDaikatana())
		return false;
	if (seat)
		return true;
	cldkarea.x = area->x;
	cldkarea.y = area->y;
	cldkarea.scale = (area->height < 480)?area->height/480.0:1;
	cldkarea.w = area->width / cldkarea.scale;
	cldkarea.h = area->height / cldkarea.scale;
	CLDK_DrawHUD();
	return true;
}

void CLDK_ParseServerMessage(void)
{	//svcq2dk_message: [short length][that many bytes of Daikatana's own]
	static qbyte buf[65536];
	int len = MSG_ReadShort();
	if (len < 0 || len > sizeof(buf))
	{
		Host_EndGame("CLDK_ParseServerMessage: bad length %i", len);
		return;
	}
	MSG_ReadData(buf, len);
	if (CLDK_IsDaikatana())
		CLDK_ParseMessage(buf, len);
}
#endif
