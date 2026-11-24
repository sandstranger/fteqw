/*
The Quake II rerelease's client game (cgame, CGAME_API_VERSION 2022): its HUD, as KEX draws it.

The rerelease's game library has the client game in it too (GetCGameAPI). When FTE runs that game
(server/svq2_rerelease.c), or one is in the game directory and the server speaks Q2EX, the cgame draws the status
bar, layouts, inventory, centerprints, health bars, keys and the rest from the player state (with Q2EX's 64 stats),
the layout/inventory messages and the configstrings, through these imports -- after Q2REPRO's cgame.c. Text is
drawn with FTE's font (unicode, for the localised strings), pictures as Quake II's.

GPL v2 or later.
*/
#include "quakedef.h"

#ifdef Q2CLIENT
#include "../server/q2rr_api.h"

static cvar_t cl_q2cgame = CVARFD("cl_q2cgame", "1", CVAR_ARCHIVE, "Quake II rerelease: its hud drawn by its own client game (from the game library), as KEX draws it. 0 draws the original Quake II status bar instead.");

static rr_cgame_export_t *cge;
static rr_cgame_import_t cgi;
static dllhandle_t *cglib;		//one of our own, when the server isn't running the game here
static char *cgcs[Q2EXMAX_CONFIGSTRINGS];	//configstrings, as the rerelease numbers them (Q2EX's)
static rr_cg_server_data_t cgdata;

qboolean CLQ2CG_Active(void)
{
	return cge && cl_q2cgame.ival && cls.protocol == CP_QUAKE2;
}

/*
=====================================================================
imports
=====================================================================
*/
static void CGI_Com_Print(const char *msg)		{Con_Printf("%s", msg);}
static void CGI_Com_Error(const char *msg)		{Host_EndGame("cgame: %s", msg);}
static const char *CGI_get_configstring(int i)
{
	if (i < 0 || i >= Q2EXMAX_CONFIGSTRINGS || !cgcs[i])
		return "";
	return cgcs[i];
}
#define CGTAGS 0x4000	//apart from the server game's
static void *CGI_TagMalloc(size_t size, int tag)	{return Z_TagMalloc(size, tag+CGTAGS);}
static void CGI_TagFree(void *block)				{Z_TagFree(block);}
static void CGI_FreeTags(int tag)					{Z_FreeTags(tag+CGTAGS);}

typedef struct cgcvar_s
{
	rr_cvar_t rr;
	cvar_t *var;
	int modifiedcount;
	struct cgcvar_s *nextmirror;
} cgcvar_t;
static cgcvar_t *cgcvars;
static void CG_CvarUpdate(cgcvar_t *m)
{
	cvar_t *v = m->var;
	if (m->rr.string == v->string && m->modifiedcount == v->modifiedcount)
		return;
	m->rr.string = v->string;
	m->rr.latched_string = v->latched_string;
	m->rr.value = v->value;
	m->rr.integer = v->ival;
	m->rr.flags = v->flags & (CVAR_ARCHIVE|CVAR_USERINFO|CVAR_SERVERINFO|CVAR_NOSET);
	if (++m->rr.modified_count == 0)
		m->rr.modified_count = 1;
	m->modifiedcount = v->modifiedcount;
}
static rr_cvar_t *CG_CvarMirror(cvar_t *v)
{
	cgcvar_t *m;
	if (!v)
		return NULL;
	for (m = cgcvars; m; m = m->nextmirror)
		if (m->var == v)
			break;
	if (!m)
	{
		m = Z_Malloc(sizeof(*m));
		m->var = v;
		m->rr.name = v->name;
		m->modifiedcount = v->modifiedcount-1;
		m->nextmirror = cgcvars;
		cgcvars = m;
	}
	CG_CvarUpdate(m);
	return &m->rr;
}
static void CG_CvarsUpdate(void)
{
	cgcvar_t *m;
	for (m = cgcvars; m; m = m->nextmirror)
		CG_CvarUpdate(m);
}
static rr_cvar_t *CGI_cvar(const char *name, const char *value, uint32_t flags)
{
	return CG_CvarMirror(Cvar_Get(name, value, flags & (CVAR_ARCHIVE|CVAR_USERINFO|CVAR_SERVERINFO), "Quake2 rerelease cgame"));
}
static rr_cvar_t *CGI_cvar_set(const char *name, const char *value)
{
	cvar_t *v = Cvar_FindVar(name);
	if (v)
		Cvar_Set(v, value);
	return CG_CvarMirror(v);
}
static rr_cvar_t *CGI_cvar_forceset(const char *name, const char *value)
{
	cvar_t *v = Cvar_FindVar(name);
	if (v)
		Cvar_ForceSet(v, value);
	return CG_CvarMirror(v);
}
static void CGI_AddCommandString(const char *text)	{Cbuf_AddText(text, RESTRICT_LOCAL);}
static void *CGI_GetExtension(const char *name)		{return NULL;}
static rr_bool CGI_CL_FrameValid(void)				{return cl.q2frame.valid;}
static float CGI_CL_FrameTime(void)					{return host_frametime;}
static uint64_t CGI_CL_ClientTime(void)				{return (uint64_t)(cl.time*1000);}
static uint64_t CGI_CL_ClientRealTime(void)			{return (uint64_t)(realtime*1000);}
static int32_t CGI_CL_ServerFrame(void)				{return cl.q2frame.serverframe;}
static int32_t CGI_CL_ServerProtocol(void)			{return cls.protocol_q2;}
static const char *CGI_CL_GetClientName(int32_t i)	{return (i >= 0 && i < MAX_CLIENTS)?cl.players[i].name:"";}
static const char *CGI_CL_GetClientPic(int32_t i)	{return "";}
static const char *CGI_CL_GetClientDogtag(int32_t i)	{return "";}
static const char *CGI_CL_GetKeyBinding(const char *binding)
{
	int keys[1], mods[1];
	if (M_FindKeysForCommand(0, 0, binding, keys, mods, 1) && keys[0] >= 0)
		return Key_KeynumToString(keys[0], mods[0]);
	return "";
}
static rr_bool CGI_Draw_RegisterPic(const char *name)
{
	int w, h;
	return CLQ2_CGPicSize(name, &w, &h);
}
static void CGI_Draw_GetPicSize(int *w, int *h, const char *name)
{
	if (!CLQ2_CGPicSize(name, w, h))
		*w = *h = 0;
}
static const float cgwhite[4] = {1,1,1,1};
static void CGI_SCR_DrawChar(int x, int y, int scale, int num, rr_bool shadow)
{
	if (shadow)
	{
		static const float black[4] = {0,0,0,1};
		CLQ2_CGChar(x+scale, y+scale, 8*scale, 0xe000|(num&255), black);
	}
	CLQ2_CGChar(x, y, 8*scale, 0xe000|(num&255), cgwhite);
}
static void CGI_SCR_DrawPic(int x, int y, int w, int h, const char *name)
{
	CLQ2_CGPic(x, y, w, h, name, cgwhite);
}
static void CGI_SCR_DrawColorPic(int x, int y, int w, int h, const char *name, const rr_rgba_t *color)
{
	float c[4] = {color->r/255.0, color->g/255.0, color->b/255.0, color->a/255.0};
	CLQ2_CGPic(x, y, w, h, name, c);
}
static void CGI_SCR_SetAltTypeface(rr_bool enabled)	{}

//text: FTE's font, 8 units a character at scale 1 (KEX's is proportional; this keeps its layouts' room)
static float CGI_SCR_FontLineHeight(int scale)
{
	return 10*scale;
}
static rr_vec2_t CGI_SCR_MeasureFontString(const char *str, int scale)
{
	rr_vec2_t r = {0, 0};
	int lines = 1, chars = 0, err;
	const char *s = str;
	while (*s)
	{
		unsigned int c = utf8_decode(&err, s, &s);
		if (c == '\n')
		{
			lines++;
			chars = 0;
			continue;
		}
		chars++;
		if (chars*8*scale > r.x)
			r.x = chars*8*scale;
	}
	r.y = lines * CGI_SCR_FontLineHeight(scale);
	return r;
}
static void CGI_SCR_DrawFontString(const char *str, int x, int y, int scale, const rr_rgba_t *color, rr_bool shadow, int32_t align)
{
	float c[4] = {color->r/255.0, color->g/255.0, color->b/255.0, color->a/255.0};
	static const float black[4] = {0,0,0,1};
	const char *s = str, *line = str;
	int err;
	while (*line)
	{
		float px, w = 0;
		const char *e = line;
		while (*e && *e != '\n')
		{
			utf8_decode(&err, e, &e);
			w += 8*scale;
		}
		px = x;
		if (align == RR_TEXT_ALIGN_CENTER)
			px -= w/2;
		else if (align == RR_TEXT_ALIGN_RIGHT)
			px -= w;
		for (s = line; s < e; )
		{
			unsigned int ch = utf8_decode(&err, s, &s);
			if (ch != ' ')
			{
				if (shadow)
				{
					float sc[4] = {0, 0, 0, c[3]};
					CLQ2_CGChar(px+scale, y+scale, 8*scale, ch, sc);
				}
				CLQ2_CGChar(px, y, 8*scale, ch, c);
			}
			px += 8*scale;
		}
		(void)black;
		y += CGI_SCR_FontLineHeight(scale);
		line = *e?e+1:e;
	}
}
static rr_bool CGI_CL_GetTextInput(const char **msg, rr_bool *is_team)	{return false;}
static int32_t CGI_CL_GetWarnAmmoCount(int32_t weapon_id)
{	//CS_WHEEL_WEAPONS: item|icon|ammo|min ammo|powerup wheel|sort id|warn quantity|droppable
	const char *s = CGI_get_configstring(RR_CS_WHEEL_WEAPONS + weapon_id);
	int i;
	for (i = 0; i < 6 && s; i++)
		if ((s = strchr(s, '|')))
			s++;
	return s?atoi(s):0;
}
static const char *CGI_Localize(const char *base, const char **args, size_t num_args)
{
	static char out[4][1024];
	static int which;
	const char *arg[RR_MAX_LOCALIZATION_ARGS+1];
	size_t i;
	char *o = out[which++ & 3];
	arg[0] = base;
	for (i = 0; i < num_args && i < RR_MAX_LOCALIZATION_ARGS; i++)
		arg[i+1] = args[i];
	TL_Reformat(com_language, o, sizeof(out[0]), i+1, arg);
	return o;
}
static int32_t CGI_SCR_DrawBind(int32_t isplit, const char *binding, const char *purpose, int x, int y, int scale)
{
	char str[256];
	const char *key = CGI_CL_GetKeyBinding(binding);
	static const rr_rgba_t white = {255,255,255,255};
	Q_snprintfz(str, sizeof(str), "[%s] %s", *key?key:"?", CGI_Localize(purpose, NULL, 0));
	CGI_SCR_DrawFontString(str, x, y, scale, &white, false, RR_TEXT_ALIGN_CENTER);
	return CGI_SCR_FontLineHeight(scale);
}
static rr_bool CGI_CL_InAutoDemoLoop(void)	{return false;}

/*
=====================================================================
the library, and what the rest of the client calls
=====================================================================
*/
static void *CG_FindCGameAPI(void)
{
	void *GetCGameAPI;
#ifdef HAVE_SERVER
	extern dllhandle_t *q2gamedll;
	if (q2gamedll && (GetCGameAPI = Sys_GetAddressForName(q2gamedll, "GetCGameAPI")))
		return GetCGameAPI;	//the server's, here
#endif
	if (!cglib && com_gamedirnativecode.ival)
	{	//as the server looks for the rerelease's
		static const char *names[] = {
			"game_" ARCH_CPU_POSTFIX ARCH_DL_POSTFIX,
#ifdef ARCH_ALTCPU_POSTFIX
			"game_" ARCH_ALTCPU_POSTFIX ARCH_DL_POSTFIX,
#endif
		};
		char syspath[MAX_OSPATH], gamepath[MAX_OSPATH], name[MAX_OSPATH];
		void *iterator = NULL;
		size_t i;
		while (!cglib && COM_IteratePaths(&iterator, syspath, sizeof(syspath), gamepath, sizeof(gamepath)))
			for (i = 0; i < countof(names) && !cglib; i++)
			{
				dllfunction_t funcs[] = {{&GetCGameAPI, "GetCGameAPI"}, {NULL,NULL}};
				Q_snprintfz(name, sizeof(name), "%s%s", syspath, names[i]);
				cglib = Sys_LoadLibrary(name, funcs);
			}
	}
	if (cglib)
		return Sys_GetAddressForName(cglib, "GetCGameAPI");
	return NULL;
}

void CLQ2CG_Stop(void)
{
	if (cge)
		cge->Shutdown();
	cge = NULL;
}
//the server's game library is going: its cgame with it
void CLQ2CG_LibraryUnloading(void)
{
	CLQ2CG_Stop();
}

void CLQ2CG_Start(void)
{
	rr_cgame_export_t *(*GetCGameAPI)(rr_cgame_import_t *);
	size_t i;

	CLQ2CG_Stop();
	for (i = 0; i < countof(cgcs); i++)
		Z_Free(cgcs[i]), cgcs[i] = NULL;	//the level's come next
	memset(&cgdata, 0, sizeof(cgdata));
	if (!cl_q2cgame.ival)
		return;
	GetCGameAPI = CG_FindCGameAPI();
	if (!GetCGameAPI)
		return;

	memset(&cgi, 0, sizeof(cgi));
	cgi.tick_rate = 10;
	cgi.frame_time_s = 0.1;
	cgi.frame_time_ms = 100;
	cgi.Com_Print = CGI_Com_Print;
	cgi.get_configstring = CGI_get_configstring;
	cgi.Com_Error = CGI_Com_Error;
	cgi.TagMalloc = CGI_TagMalloc;
	cgi.TagFree = CGI_TagFree;
	cgi.FreeTags = CGI_FreeTags;
	cgi.cvar = CGI_cvar;
	cgi.cvar_set = CGI_cvar_set;
	cgi.cvar_forceset = CGI_cvar_forceset;
	cgi.AddCommandString = CGI_AddCommandString;
	cgi.GetExtension = CGI_GetExtension;
	cgi.CL_FrameValid = CGI_CL_FrameValid;
	cgi.CL_FrameTime = CGI_CL_FrameTime;
	cgi.CL_ClientTime = CGI_CL_ClientTime;
	cgi.CL_ClientRealTime = CGI_CL_ClientRealTime;
	cgi.CL_ServerFrame = CGI_CL_ServerFrame;
	cgi.CL_ServerProtocol = CGI_CL_ServerProtocol;
	cgi.CL_GetClientName = CGI_CL_GetClientName;
	cgi.CL_GetClientPic = CGI_CL_GetClientPic;
	cgi.CL_GetClientDogtag = CGI_CL_GetClientDogtag;
	cgi.CL_GetKeyBinding = CGI_CL_GetKeyBinding;
	cgi.Draw_RegisterPic = CGI_Draw_RegisterPic;
	cgi.Draw_GetPicSize = CGI_Draw_GetPicSize;
	cgi.SCR_DrawChar = CGI_SCR_DrawChar;
	cgi.SCR_DrawPic = CGI_SCR_DrawPic;
	cgi.SCR_DrawColorPic = CGI_SCR_DrawColorPic;
	cgi.SCR_SetAltTypeface = CGI_SCR_SetAltTypeface;
	cgi.SCR_DrawFontString = CGI_SCR_DrawFontString;
	cgi.SCR_MeasureFontString = CGI_SCR_MeasureFontString;
	cgi.SCR_FontLineHeight = CGI_SCR_FontLineHeight;
	cgi.CL_GetTextInput = CGI_CL_GetTextInput;
	cgi.CL_GetWarnAmmoCount = CGI_CL_GetWarnAmmoCount;
	cgi.Localize = CGI_Localize;
	cgi.SCR_DrawBind = CGI_SCR_DrawBind;
	cgi.CL_InAutoDemoLoop = CGI_CL_InAutoDemoLoop;

	cge = GetCGameAPI(&cgi);
	if (!cge || cge->apiversion != RR_CGAME_API_VERSION)
	{
		Con_Printf(CON_WARNING"rerelease cgame is version %i, not %i\n", cge?cge->apiversion:0, RR_CGAME_API_VERSION);
		cge = NULL;
		return;
	}
	cge->Init();
	Con_DPrintf("Quake II rerelease cgame\n");
}

void CLQ2CG_ConfigString(int i, const char *s)
{
	if (i < 0 || i >= Q2EXMAX_CONFIGSTRINGS)
		return;
	Z_Free(cgcs[i]);
	cgcs[i] = (s && *s)?Z_StrDup(s):NULL;
	if (cge)
		cge->ParseConfigString(i, s?s:"");
}

qboolean CLQ2CG_CenterPrint(int seat, const char *str, qboolean instant)
{
	if (!CLQ2CG_Active())
		return false;
	while (*str == '/' && str[1] && str[1] != '.')
	{	//FTE's centerprint markup (/Q, /W...): the cgame has its own ways
		str += 2;
		while (*str >= '0' && *str <= '9')
			str++;
	}
	if (*str == '/' && str[1] == '.')
		str += 2;
	cge->ParseCenterPrint(str, seat, instant);
	return true;
}

static void CG_PlayerState(rr_player_state_t *o, int seat)
{
	q2player_state_t *ps = &cl.q2frame.seat[seat].playerstate;
	int i;
	const q2expmove_state_t *ex = &cl.q2frame.seat[seat].expmove;
	memset(o, 0, sizeof(*o));
	if (ex->valid)
	{	//Q2EX's, as it came
		o->pmove.pm_type = ex->pm_type;
		VectorCopy(ex->origin, o->pmove.origin);
		VectorCopy(ex->velocity, o->pmove.velocity);
		VectorCopy(ex->delta_angles, o->pmove.delta_angles);
		o->pmove.pm_flags = ex->pm_flags;
		o->pmove.pm_time = ex->pm_time;
		o->pmove.gravity = ex->gravity;
		o->pmove.viewheight = ex->viewheight;
	}
	else
	{
		o->pmove.pm_type = ps->pmove.pm_type;
		for (i = 0; i < 3; i++)
		{
			o->pmove.origin[i] = ps->pmove.origin[i] / 8.0f;
			o->pmove.velocity[i] = ps->pmove.velocity[i] / 8.0f;
			o->pmove.delta_angles[i] = SHORT2ANGLE(ps->pmove.delta_angles[i]);
		}
		o->pmove.pm_flags = ps->pmove.pm_flags;
		o->pmove.pm_time = ps->pmove.pm_time * 8;
		o->pmove.gravity = ps->pmove.gravity;
	}
	VectorCopy(ps->viewangles, o->viewangles);
	VectorCopy(ps->viewoffset, o->viewoffset);
	o->viewoffset[2] -= o->pmove.viewheight;
	VectorCopy(ps->kick_angles, o->kick_angles);
	VectorCopy(ps->gunangles, o->gunangles);
	VectorCopy(ps->gunoffset, o->gunoffset);
	o->gunindex = ps->gunindex;
	o->gunframe = ps->gunframe;
	Vector4Copy(ps->blend, o->screen_blend);
	o->fov = ps->fov;
	o->rdflags = ps->rdflags;
	for (i = 0; i < 32; i++)
	{
		o->stats[i] = ps->stats[i];
		o->stats[32+i] = cl.q2frame.seat[seat].exstats[i];
	}
}

//the hud for a seat, from the cgame. false to draw the original's instead
qboolean CLQ2CG_DrawHUD(int seat, const vrect_t *area)
{
	rr_player_state_t ps;
	rr_vrect_t hud, safe;
	int w, h, i;

	if (!CLQ2CG_Active())
		return false;
	if (!CLQ2_CGBegin(area, &w, &h))
		return true;	//hidden

	CG_CvarsUpdate();
	CG_PlayerState(&ps, seat);
	Q_strncpyz(cgdata.layout, cl.q2layout[seat], sizeof(cgdata.layout));
	for (i = 0; i < RR_MAX_ITEMS && i < Q2MAX_ITEMS; i++)
		cgdata.inventory[i] = cl.inventory[seat][i];
	hud.x = hud.y = 0;
	hud.width = w;
	hud.height = h;
	safe.x = w * 0.02;
	safe.y = h * 0.02;
	safe.width = w - 2*safe.x;
	safe.height = h - 2*safe.y;
	cge->DrawHUD(seat, &cgdata, hud, safe, 1, cl.q2frame.seat[seat].clientnum, &ps);
	R2D_ImageColours(1, 1, 1, 1);
	return true;
}

//the crosshair, when the cgame's layout flags don't hide it
qboolean CLQ2CG_HideCrosshair(int seat)
{
	rr_player_state_t ps;
	if (!CLQ2CG_Active())
		return false;
	CG_PlayerState(&ps, seat);
	return !!(cge->LayoutFlags(&ps) & (RR_LAYOUTS_HIDE_CROSSHAIR|RR_LAYOUTS_HIDE_HUD|RR_LAYOUTS_INTERMISSION));
}

//the rerelease's player movement, to predict with (cl_pred.c)
qboolean CLQ2CG_CanPmove(void)
{
	return cge && cge->Pmove && cls.protocol == CP_QUAKE2 && cl.worldmodel;
}
void CLQ2CG_Pmove(void *rrpmove)
{
	cge->Pmove(rrpmove);
}

void CLQ2CG_Init(void)
{
	Cvar_Register(&cl_q2cgame, "Quake 2 HUD");
}
#endif
