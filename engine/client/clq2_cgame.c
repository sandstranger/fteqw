/*
The Quake II rerelease's client game (cgame, CGAME_API_VERSION 2022): its HUD, as KEX draws it.

The rerelease's game library has the client game in it too (GetCGameAPI). When FTE runs that game
(server/svq2_rerelease.c), or one is in the game directory and the server speaks Q2EX, the cgame draws the status
bar, layouts, inventory, centerprints, health bars, keys and the rest from the player state (with Q2EX's 64 stats),
the layout/inventory messages and the configstrings, through these imports -- after Q2REPRO's cgame.c. Text is
drawn with FTE's font (unicode, for the localised strings), pictures as Quake II's.

Its Pmove predicts the player (cl_pred.c). And what KEX's engine does itself, with the cgame's help, is here too: the
weapon and powerup wheels (+wheel, +wheel2) and the weapon carousel (cl_weapnext, cl_weapprev).

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
static void CG_WheelEntry(int i, const char *s);
static void CG_WheelReset(void);

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
	CG_WheelReset();
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
	if (i >= RR_CS_WHEEL_WEAPONS && i < RR_CS_WHEEL_POWERUPS + RR_MAX_WHEEL_ITEMS)
		CG_WheelEntry(i, s);	//the weapon wheel's (KEX's engine has it)
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


/*
=====================================================================
The weapon wheel (+wheel), the powerup wheel (+wheel2) and the weapon carousel (cl_weapnext/cl_weapprev): KEX has
them in its engine, not in the cgame. After Q2REPRO's wheel.c: what's on them comes from the CS_WHEEL_*
configstrings and the cgame's reading of the stats (owned weapons, ammo, powerup counts); a choice is sent as
"use_index_only <item>". The mouse moves the wheel's cursor while it's open (not the view), and the weapon is
holstered meanwhile. Sizes are as Q2REPRO's: the wheel in screen pixels, the rest in the hud's units.
=====================================================================
*/
static cvar_t wc_screen_frac_y = CVARD("wc_screen_frac_y", "0.72", "Quake II rerelease: how far down the screen the weapon carousel (cl_weapnext/cl_weapprev) is.");
static cvar_t wc_timeout = CVARD("wc_timeout", "400", "Quake II rerelease: how long (ms) the weapon carousel waits after the last cl_weapnext/cl_weapprev before switching.");
static cvar_t wc_lock_time = CVARD("wc_lock_time", "300", "Quake II rerelease: how long (ms) firing is held off after the weapon carousel switches.");
static cvar_t wc_ammo_scale = CVARD("wc_ammo_scale", "0.66", "Quake II rerelease: the size of the weapon carousel's ammo counts.");
static cvar_t ww_timer_speed = CVARD("ww_timer_speed", "3", "Quake II rerelease: how fast the weapon wheel fades in and out.");
static cvar_t ww_ammo_scale = CVARD("ww_ammo_scale", "0.66", "Quake II rerelease: the size of the weapon wheel's ammo counts.");

#define WHEEL_CLOSED	0
#define WHEEL_CLOSING	1
#define WHEEL_OPEN		2

typedef struct
{
	int image;					//CS_IMAGES index
	qboolean resolved;
	char wheel[MAX_QPATH];		//wheel/<image>, or the image itself
	char selected[MAX_QPATH];	//wheel/<image>_selected, or the wheel's
} cgwheelicon_t;

static struct
{
	struct
	{
		int item_index;
		cgwheelicon_t icons;
		int ammo_index, min_ammo, is_powerup, sort_id, quantity_warn, can_drop;
	} weapons[RR_MAX_WHEEL_ITEMS];
	int num_weapons;
	struct
	{
		int item_index;
		cgwheelicon_t icons;
	} ammo[RR_MAX_WHEEL_ITEMS];
	int num_ammo;
	struct
	{
		int item_index;
		cgwheelicon_t icons;
		int is_toggle, sort_id, can_drop, ammo_index;
	} powerups[RR_MAX_WHEEL_ITEMS];
	int num_powerups;
} cgwheeldata;

typedef struct
{
	int data_id;
	int item_index;
	int sort_id;
	qboolean is_powerup;
	qboolean has_ammo;
	qboolean has_item;
	cgwheelicon_t *icons;
	float angle, dot;
	vec2_t dir;
} cgwheelslot_t;

static struct
{
	int state;
	qboolean is_powerup_wheel;
	cgwheelslot_t slots[RR_MAX_WHEEL_ITEMS];
	int num_slots;
	int selected;			//slot, or -1
	vec2_t position;		//the cursor, in screen pixels from the centre
	vec2_t dir;
	float distance;
	float slice_deg, slice_sin;
	double deselect_time;
	float timer;			//fading in (to 1) and out
	double lastupdate;
	float drawsize;			//the wheel's size in pixels, last drawn
} cgwheel;

static struct
{
	int state;
	struct
	{
		int data_id;
		int item_index;
		qboolean has_ammo;
	} slots[RR_MAX_WHEEL_ITEMS];
	int num_slots;
	int selected;			//item, or -1
	double close_time;
} cgcarousel;
static double cgweaponlock;	//no firing until then (the carousel just switched)

//CS_WHEEL_WEAPONS: item|icon|ammo|min ammo|powerup wheel|sort id|warn quantity|droppable
//CS_WHEEL_AMMO: item|icon
//CS_WHEEL_POWERUPS: item|icon|toggle|sort id|droppable|ammo
static void CG_WheelEntry(int i, const char *s)
{
	int values[8], n = 0;
	char *end;
	while (s && *s && n < countof(values))
	{
		values[n] = strtol(s, &end, 10);
		if (end == s)
			return;
		n++;
		s = strchr(end, '|');
		if (s)
			s++;
	}
	if (i >= RR_CS_WHEEL_POWERUPS)
	{
		i -= RR_CS_WHEEL_POWERUPS;
		if (n != 6)
			return;
		cgwheeldata.powerups[i].item_index = values[0];
		memset(&cgwheeldata.powerups[i].icons, 0, sizeof(cgwheeldata.powerups[i].icons));
		cgwheeldata.powerups[i].icons.image = values[1];
		cgwheeldata.powerups[i].is_toggle = values[2];
		cgwheeldata.powerups[i].sort_id = values[3];
		cgwheeldata.powerups[i].can_drop = values[4];
		cgwheeldata.powerups[i].ammo_index = values[5];
		cgwheeldata.num_powerups = max(i+1, cgwheeldata.num_powerups);
	}
	else if (i >= RR_CS_WHEEL_AMMO)
	{
		i -= RR_CS_WHEEL_AMMO;
		if (n != 2)
			return;
		cgwheeldata.ammo[i].item_index = values[0];
		memset(&cgwheeldata.ammo[i].icons, 0, sizeof(cgwheeldata.ammo[i].icons));
		cgwheeldata.ammo[i].icons.image = values[1];
		cgwheeldata.num_ammo = max(i+1, cgwheeldata.num_ammo);
	}
	else
	{
		i -= RR_CS_WHEEL_WEAPONS;
		if (n != 8)
			return;
		cgwheeldata.weapons[i].item_index = values[0];
		memset(&cgwheeldata.weapons[i].icons, 0, sizeof(cgwheeldata.weapons[i].icons));
		cgwheeldata.weapons[i].icons.image = values[1];
		cgwheeldata.weapons[i].ammo_index = values[2];
		cgwheeldata.weapons[i].min_ammo = values[3];
		cgwheeldata.weapons[i].is_powerup = values[4];
		cgwheeldata.weapons[i].sort_id = values[5];
		cgwheeldata.weapons[i].quantity_warn = values[6];
		cgwheeldata.weapons[i].can_drop = values[7];
		cgwheeldata.num_weapons = max(i+1, cgwheeldata.num_weapons);
	}
}
static void CG_WheelReset(void)
{
	memset(&cgwheeldata, 0, sizeof(cgwheeldata));
	memset(&cgwheel, 0, sizeof(cgwheel));
	memset(&cgcarousel, 0, sizeof(cgcarousel));
	cgwheel.selected = -1;
	cgcarousel.selected = -1;
	cgweaponlock = 0;
}

static cgwheelicon_t *CG_WheelIcons(cgwheelicon_t *icons)
{
	if (!icons->resolved)
	{
		const char *name = CGI_get_configstring(RR_CS_IMAGES + icons->image);
		icons->resolved = true;
		if (!name)
			name = "";
		Q_snprintfz(icons->wheel, sizeof(icons->wheel), "wheel/%s", name);
		if (!CLQ2_CGPicExists(icons->wheel))
		{	//no wheel version: the hud's icon for both
			Q_strncpyz(icons->wheel, name, sizeof(icons->wheel));
			Q_strncpyz(icons->selected, name, sizeof(icons->selected));
		}
		else
		{
			Q_snprintfz(icons->selected, sizeof(icons->selected), "wheel/%s_selected", name);
			if (!CLQ2_CGPicExists(icons->selected))
				Q_strncpyz(icons->selected, icons->wheel, sizeof(icons->selected));
		}
	}
	return icons;
}

//text of any size (Q2REPRO's draw_count can go below 100%)
static void CG_WheelText(float x, float y, float size, const char *str, const float *rgba, qboolean center)
{
	float sc[4] = {0, 0, 0, rgba[3]};
	const char *s;
	int err, n = 0;
	for (s = str; *s; n++)
		utf8_decode(&err, s, &s);
	if (center)
		x -= n*size/2;
	for (s = str; *s; x += size)
	{
		unsigned int ch = utf8_decode(&err, s, &s);
		if (ch == ' ')
			continue;
		CLQ2_CGChar(x+1, y+1, size, ch, sc);	//drop shadow
		CLQ2_CGChar(x, y, size, ch, rgba);
	}
}
static void CG_WheelItemName(float x, float y, int item, const float *rgba)
{
	const char *name = CGI_get_configstring(RR_CS_ITEMS + item);
	CG_WheelText(x, y, 8, name?CGI_Localize(name, NULL, 0):"", rgba, true);
}
static void CG_WheelPic(float x, float y, float w, float h, const char *name, float shadow, float alpha)
{
	float black[4] = {0, 0, 0, alpha}, white[4] = {1, 1, 1, alpha};
	CLQ2_CGPic(x + shadow, y + shadow, w, h, name, black);
	CLQ2_CGPic(x, y, w, h, name, white);
}

/*the carousel*/
static void CG_CarouselClose(void)
{
	cgcarousel.state = WHEEL_CLOSED;
}
static qboolean CG_CarouselPopulate(const rr_player_state_t *ps)
{
	uint32_t owned = cge->GetOwnedWeaponWheelWeapons(ps);
	int i;

	cgcarousel.num_slots = 0;
	for (i = 0; i < cgwheeldata.num_weapons; i++)
	{
		if (!(owned & (1u<<i)))
			continue;
		cgcarousel.slots[cgcarousel.num_slots].data_id = i;
		cgcarousel.slots[cgcarousel.num_slots].has_ammo = cgwheeldata.weapons[i].ammo_index == -1 || cge->GetWeaponWheelAmmoCount(ps, cgwheeldata.weapons[i].ammo_index);
		cgcarousel.slots[cgcarousel.num_slots].item_index = cgwheeldata.weapons[i].item_index;
		cgcarousel.num_slots++;
	}
	if (!cgcarousel.num_slots)
		return false;

	//do we still have the one picked?
	if (cgcarousel.selected == -1)
		cgcarousel.selected = cgcarousel.slots[0].item_index;
	for (i = 0; i < cgcarousel.num_slots; i++)
		if (cgcarousel.slots[i].item_index == cgcarousel.selected)
			return true;
	return false;
}
static int CG_ActiveWheelItem(const rr_player_state_t *ps)
{
	int w = cge->GetActiveWeaponWheelWeapon(ps);
	if (w < 0 || w >= cgwheeldata.num_weapons)
		return -1;
	return cgwheeldata.weapons[w].item_index;
}
static void CG_CarouselOpen(const rr_player_state_t *ps)
{
	if (cgcarousel.state == WHEEL_CLOSED)
		cgcarousel.selected = CG_ActiveWheelItem(ps);
	cgcarousel.state = WHEEL_OPEN;
	if (!CG_CarouselPopulate(ps))
		CG_CarouselClose();
}

#define CAROUSEL_ICON_SIZE	(24 + 2)
#define CAROUSEL_PAD		2
static void CG_CarouselDraw(const rr_player_state_t *ps, int w, int h)
{
	static const float white[4] = {1,1,1,1};
	int carousel_w = cgcarousel.num_slots * (CAROUSEL_ICON_SIZE + CAROUSEL_PAD);
	int center_x = w / 2;
	int x = center_x - carousel_w / 2;
	int y = h * wc_screen_frac_y.value;
	int i;

	if (cgcarousel.state != WHEEL_OPEN)
		return;

	for (i = 0; i < cgcarousel.num_slots; i++, x += CAROUSEL_ICON_SIZE + CAROUSEL_PAD)
	{
		qboolean selected = cgcarousel.selected == cgcarousel.slots[i].item_index;
		int id = cgcarousel.slots[i].data_id;
		cgwheelicon_t *icons = CG_WheelIcons(&cgwheeldata.weapons[id].icons);
		int pw, ph;

		CLQ2_CGPicSize(selected?icons->selected:icons->wheel, &pw, &ph);
		CG_WheelPic(x, y, pw, ph, selected?icons->selected:icons->wheel, 2, 1);
		if (selected)
		{
			if (CLQ2_CGPicSize("carousel/selected", &pw, &ph))
				CLQ2_CGPic(x - 1, y - 1, pw, ph, "carousel/selected", white);
			CG_WheelItemName(center_x, y - 16, cgcarousel.slots[i].item_index, white);
		}

		if (cgwheeldata.weapons[id].ammo_index >= 0)
		{
			int count = cge->GetWeaponWheelAmmoCount(ps, cgwheeldata.weapons[id].ammo_index);
			float red[4] = {1, selected?83/255.0:0, selected?83/255.0:0, 1}, yellow[4] = {1, 1, selected?83/255.0:1, 1};
			CG_WheelText(x + CAROUSEL_ICON_SIZE/2, y + CAROUSEL_ICON_SIZE + 2, max(1, 8*wc_ammo_scale.value), va("%i", count),
				(count <= cgwheeldata.weapons[id].quantity_warn)?red:yellow, true);
		}
	}
}

static void CG_CarouselInput(const rr_player_state_t *ps, int *buttons)
{
	if (cgcarousel.state != WHEEL_OPEN)
	{
		if (cgcarousel.state == WHEEL_CLOSING && realtime >= cgcarousel.close_time)
			cgcarousel.state = WHEEL_CLOSED;
		return;
	}
	if (!CG_CarouselPopulate(ps))
	{
		CG_CarouselClose();
		return;
	}

	*buttons |= RR_BUTTON_HOLSTER;	//always, while open

	if (realtime >= cgcarousel.close_time || (*buttons & RR_BUTTON_ATTACK))
	{
		if (cgcarousel.selected == CG_ActiveWheelItem(ps))
		{	//already using it
			CG_CarouselClose();
			return;
		}
		CL_SendClientCommand(true, "use_index_only %i", cgcarousel.selected);
		cgcarousel.state = WHEEL_CLOSING;
		cgcarousel.close_time = realtime + 0.1;
		cgweaponlock = realtime + wc_lock_time.value/1000;
	}
}

//cl_weapnext / cl_weapprev
qboolean CLQ2CG_WheelCycle(int offset)
{
	rr_player_state_t ps;
	int i, n, o;
	if (!CLQ2CG_Active() || !cgwheeldata.num_weapons)
		return false;
	CG_PlayerState(&ps, 0);

	if (cgwheel.state != WHEEL_OPEN)
		CG_CarouselOpen(&ps);
	else if (!CG_CarouselPopulate(&ps))
	{
		CG_CarouselClose();
		return true;
	}

	for (i = 0; i < cgcarousel.num_slots; i++)
	{
		if (cgcarousel.slots[i].item_index != cgcarousel.selected)
			continue;
		for (n = 0, o = i + offset; n < cgcarousel.num_slots - 1; n++, o += offset)
		{
			if (o < 0)
				o = cgcarousel.num_slots - 1;
			else if (o >= cgcarousel.num_slots)
				o = 0;
			if (!cgcarousel.slots[o].has_ammo)
				continue;
			cgcarousel.selected = cgcarousel.slots[o].item_index;
			break;
		}
		break;
	}
	cgcarousel.close_time = realtime + wc_timeout.value/1000;
	return true;
}

/*the wheels*/
static int QDECL CG_WheelSlotCompare(const void *a, const void *b)
{
	const cgwheelslot_t *sa = a, *sb = b;
	if (sa->sort_id == sb->sort_id)
		return sa->item_index - sb->item_index;
	return sa->sort_id - sb->sort_id;
}
static qboolean CG_WheelPopulate(const rr_player_state_t *ps)
{
	uint32_t owned = cge->GetOwnedWeaponWheelWeapons(ps);
	cgwheelslot_t *slot = cgwheel.slots;
	int i;

	cgwheel.num_slots = 0;
	if (cgwheel.is_powerup_wheel)
	{
		for (i = 0; i < cgwheeldata.num_powerups; i++, slot++, cgwheel.num_slots++)
		{
			slot->data_id = i;
			slot->is_powerup = true;
			slot->has_ammo = cgwheeldata.powerups[i].ammo_index == -1 || cge->GetWeaponWheelAmmoCount(ps, cgwheeldata.powerups[i].ammo_index);
			slot->item_index = cgwheeldata.powerups[i].item_index;
			slot->has_item = cge->GetPowerupWheelCount(ps, i) != 0;
			slot->sort_id = cgwheeldata.powerups[i].sort_id;
			slot->icons = &cgwheeldata.powerups[i].icons;
		}
	}
	else
	{
		for (i = 0; i < cgwheeldata.num_weapons; i++, slot++, cgwheel.num_slots++)
		{
			slot->data_id = i;
			slot->is_powerup = false;
			slot->has_ammo = cgwheeldata.weapons[i].ammo_index == -1 || cge->GetWeaponWheelAmmoCount(ps, cgwheeldata.weapons[i].ammo_index);
			slot->item_index = cgwheeldata.weapons[i].item_index;
			slot->has_item = !!(owned & (1u<<i));
			slot->sort_id = cgwheeldata.weapons[i].sort_id;
			slot->icons = &cgwheeldata.weapons[i].icons;
		}
	}
	if (!cgwheel.num_slots)
		return false;
	cgwheel.slice_deg = (M_PI * 2) / cgwheel.num_slots;
	cgwheel.slice_sin = cos(cgwheel.slice_deg / 2);
	qsort(cgwheel.slots, cgwheel.num_slots, sizeof(*cgwheel.slots), CG_WheelSlotCompare);
	return true;
}

//+wheel (weapons), +wheel2 (powerups). false when there's no wheel (not the rerelease's game)
qboolean CLQ2CG_WheelOpen(qboolean powerup)
{
	rr_player_state_t ps;
	if (!CLQ2CG_Active() || !(powerup?cgwheeldata.num_powerups:cgwheeldata.num_weapons))
		return false;
	CG_PlayerState(&ps, 0);
	cgwheel.is_powerup_wheel = powerup;
	cgwheel.selected = -1;
	if (!CG_WheelPopulate(&ps))
		return true;
	cgwheel.state = WHEEL_OPEN;
	cgwheel.deselect_time = 0;
	Vector2Clear(cgwheel.position);
	return true;
}
//-wheel, -wheel2: what's picked is used
qboolean CLQ2CG_WheelClose(qboolean released)
{
	if (cgwheel.state != WHEEL_OPEN)
		return cgwheel.state != WHEEL_CLOSED;
	cgwheel.state = WHEEL_CLOSING;
	if (released && cgwheel.selected >= 0 && cgwheel.selected < cgwheel.num_slots)
		CL_SendClientCommand(true, "use_index_only %i", cgwheel.slots[cgwheel.selected].item_index);
	return true;
}

//the mouse (or a stick), while a wheel is open: its cursor, not the view
qboolean CLQ2CG_WheelMouse(float x, float y)
{
	float inner;
	if (cgwheel.state != WHEEL_OPEN || !CLQ2CG_Active())
		return false;
	cgwheel.position[0] += x;
	cgwheel.position[1] += y;

	cgwheel.distance = sqrt(DotProduct2(cgwheel.position, cgwheel.position));
	inner = (cgwheel.drawsize?cgwheel.drawsize:512) * 0.64f;
	Vector2Clear(cgwheel.dir);
	if (cgwheel.distance)
	{
		cgwheel.dir[0] = cgwheel.position[0] / cgwheel.distance;
		cgwheel.dir[1] = cgwheel.position[1] / cgwheel.distance;
		if (cgwheel.distance > inner / 2)
		{
			cgwheel.distance = inner / 2;
			cgwheel.position[0] = cgwheel.dir[0] * inner / 2;
			cgwheel.position[1] = cgwheel.dir[1] * inner / 2;
		}
	}
	return true;
}

//each command sent: the weapon's holstered while a wheel's open, the carousel switches, firing's held off after
int CLQ2CG_WheelButtons(int buttons)
{
	rr_player_state_t ps;
	if (!CLQ2CG_Active())
		return buttons;
	CG_PlayerState(&ps, 0);

	if (cgwheel.state != WHEEL_CLOSED)
	{
		if (!cgwheel.is_powerup_wheel)
			buttons |= RR_BUTTON_HOLSTER;
		if (cgwheel.state == WHEEL_OPEN && !CG_WheelPopulate(&ps))
			CLQ2CG_WheelClose(false);
		if (cgwheel.state == WHEEL_CLOSING)
			cgwheel.state = WHEEL_CLOSED;	//it's had its last holstered command
	}

	CG_CarouselInput(&ps, &buttons);

	if (realtime < cgweaponlock)
		buttons &= ~RR_BUTTON_ATTACK;
	return buttons;
}

static void CG_WheelUpdate(void)
{
	float frac = cgwheel.lastupdate?(realtime - cgwheel.lastupdate):0;
	qboolean can_select;
	int i;
	cgwheel.lastupdate = realtime;

	if (cgwheel.state != WHEEL_OPEN)
	{
		if (cgwheel.timer > 0)
			cgwheel.timer = max(0, cgwheel.timer - frac * ww_timer_speed.value);
		return;
	}
	if (cgwheel.timer < 1)
		cgwheel.timer = min(1, cgwheel.timer + frac * ww_timer_speed.value);

	for (i = 0; i < cgwheel.num_slots; i++)
	{
		if (!cgwheel.slots[i].has_item)
			continue;
		cgwheel.slots[i].angle = cgwheel.slice_deg * i;
		cgwheel.slots[i].dir[0] = sin(cgwheel.slots[i].angle);
		cgwheel.slots[i].dir[1] = -cos(cgwheel.slots[i].angle);
		cgwheel.slots[i].dot = DotProduct2(cgwheel.dir, cgwheel.slots[i].dir);
	}

	//Q2REPRO's 140 pixels of a 768 pixel wheel's cursor range (245): the same share of a smaller (or bigger) one's
	can_select = cgwheel.distance > (cgwheel.drawsize?cgwheel.drawsize:512) * 0.64f / 2 * (140.f/245.76f);
	if (can_select)
	{
		for (i = 0; i < cgwheel.num_slots; i++)
		{
			if (!cgwheel.slots[i].has_item)
				continue;
			if (cgwheel.slots[i].dot > cgwheel.slice_sin)
			{
				cgwheel.selected = i;
				cgwheel.deselect_time = 0;
			}
		}
	}
	else if (cgwheel.selected >= 0)
	{
		if (!cgwheel.deselect_time)
			cgwheel.deselect_time = realtime + 0.2;
	}
	if (cgwheel.deselect_time && cgwheel.deselect_time < realtime)
	{
		cgwheel.selected = -1;
		cgwheel.deselect_time = 0;
	}
}

static void CG_WheelCountColour(float *c, qboolean selected, qboolean warn_low, float alpha)
{
	if (selected)	//the rerelease's tint for selected wheel icons
		Vector4Set(c, 1, warn_low?62/255.0:248/255.0, warn_low?33/255.0:134/255.0, alpha);
	else if (warn_low)
		Vector4Set(c, 1, 0, 0, alpha);
	else
		Vector4Set(c, 1, 1, 1, alpha);
}

static void CG_WheelDrawSlot(const rr_player_state_t *ps, int idx, float cx, float cy, float drawsize, float wheel_alpha, int *out_count, qboolean *out_warn_low)
{
	cgwheelslot_t *slot = &cgwheel.slots[idx];
	qboolean selected = cgwheel.selected == idx, active = selected, warn_low = false;
	float scale = selected?2:1, size, alpha = 1, c[4];
	vec2_t p;
	int count = -1, min_count;
	cgwheelicon_t *icons;

	if (!slot->has_item)
		return;

	p[0] = slot->dir[0] * (drawsize / 2) * 0.525f;
	p[1] = slot->dir[1] * (drawsize / 2) * 0.525f;
	size = 12 * scale;

	if (slot->is_powerup && cgwheeldata.powerups[slot->data_id].is_toggle)
	{	//a powerup that's on
		if (cge->GetPowerupWheelCount(ps, slot->data_id) == 2)
			active = true;
		if (cgwheeldata.powerups[slot->data_id].ammo_index != -1 && !slot->has_ammo)
			alpha = 0.5f;
	}
	alpha *= wheel_alpha;

	icons = CG_WheelIcons(slot->icons);
	CG_WheelPic(cx + p[0] - size, cy + p[1] - size, size * 2, size * 2, active?icons->selected:icons->wheel, 2, alpha);

	if (slot->is_powerup)
	{
		if (!cgwheeldata.powerups[slot->data_id].is_toggle)
			count = cge->GetPowerupWheelCount(ps, slot->data_id);
		else if (cgwheeldata.powerups[slot->data_id].ammo_index != -1)
			count = cge->GetWeaponWheelAmmoCount(ps, cgwheeldata.powerups[slot->data_id].ammo_index);
	}
	else if (cgwheeldata.weapons[slot->data_id].ammo_index != -1)
	{
		count = cge->GetWeaponWheelAmmoCount(ps, cgwheeldata.weapons[slot->data_id].ammo_index);
		warn_low = count <= cgwheeldata.weapons[slot->data_id].quantity_warn;
	}

	min_count = slot->is_powerup?2:0;
	if (count != -1 && count >= min_count)
	{
		CG_WheelCountColour(c, selected, warn_low, wheel_alpha);
		CG_WheelText(cx + p[0] + size, cy + p[1] + size, max(1, 8 * ww_ammo_scale.value * scale), va("%i", count), c, true);
	}
	if (out_count)
		*out_count = count;
	if (out_warn_low)
		*out_warn_low = warn_low;
}

static void CG_WheelDraw(const rr_player_state_t *ps, int w, int h, float pxscale)
{
	float cx = w / 2, cy = h / 2, t, tween, wheel_alpha, base[4], drawsize;
	int pw, ph, content, realh = h * pxscale, i;

	CG_WheelUpdate();
	if (cgwheel.state != WHEEL_OPEN && cgwheel.timer == 0)
		return;

	if (cgwheel.is_powerup_wheel)
		cx -= w / 4;
	else
		cx += w / 4;

	t = 1 - cgwheel.timer;
	tween = 0.5f - cos((t * t) * M_PI) * 0.5f;
	wheel_alpha = 1 - tween;
	Vector4Set(base, 1, 1, 1, wheel_alpha);

	//as big as it fits the screen, at a whole multiple or fraction of its size (it has 74 pixels of nothing round it)
	if (!CLQ2_CGPicSize("/gfx/weaponwheel.png", &pw, &ph) || pw <= 74)
		pw = 512;
	content = pw - 74;
	if (content >= realh)
		cgwheel.drawsize = pw / (float)((2 * content - 1) / max(1, realh));
	else
		cgwheel.drawsize = pw * (float)(realh / content);
	drawsize = cgwheel.drawsize / pxscale;

	CLQ2_CGPic(cx - drawsize / 2, cy - drawsize / 2, drawsize, drawsize, "/gfx/weaponwheel.png", base);

	//all but the selected one, then that on top
	for (i = 0; i < cgwheel.num_slots; i++)
		if (i != cgwheel.selected)
			CG_WheelDrawSlot(ps, i, cx, cy, drawsize, wheel_alpha, NULL, NULL);

	if (cgwheel.selected >= 0 && cgwheel.selected < cgwheel.num_slots)
	{
		const cgwheelslot_t *slot = &cgwheel.slots[cgwheel.selected];
		int count = -1, ammo_index;
		qboolean warn_low = false;
		float c[4];

		CG_WheelDrawSlot(ps, cgwheel.selected, cx, cy, drawsize, wheel_alpha, &count, &warn_low);
		CG_WheelItemName(cx, cy - drawsize / 8, slot->item_index, base);

		if (slot->is_powerup)
		{
			ammo_index = cgwheeldata.powerups[slot->data_id].ammo_index;
			if (!cgwheeldata.powerups[slot->data_id].is_toggle)
				CG_WheelText(cx, cy, 8, va("%i", cge->GetPowerupWheelCount(ps, slot->data_id)), base, true);
		}
		else
			ammo_index = cgwheeldata.weapons[slot->data_id].ammo_index;

		if (ammo_index >= 0 && ammo_index < cgwheeldata.num_ammo)
		{
			cgwheelicon_t *icons = CG_WheelIcons(&cgwheeldata.ammo[ammo_index].icons);
			CG_WheelPic(cx - (24 * 3) / 2, cy - (24 * 3) / 2, 24 * 3, 24 * 3, icons->wheel, 2, wheel_alpha);
			CG_WheelCountColour(c, false, warn_low, wheel_alpha);	//not the selected colour, in the wheel
			CG_WheelText(cx, cy + 32, 8, va("%i", count), c, true);
		}
	}

	//the cursor, in screen pixels
	if (CLQ2_CGPicSize("/gfx/wheelbutton.png", &pw, &ph))
	{
		float c[4] = {1, 1, 1, wheel_alpha * 0.5f};
		CLQ2_CGPic(cx + (cgwheel.position[0] - pw / 2) / pxscale, cy + (cgwheel.position[1] - ph / 2) / pxscale, pw / pxscale, ph / pxscale, "/gfx/wheelbutton.png", c);
	}
}

//after the hud: the wheels and the carousel
void CLQ2CG_WheelDraw(int seat, const vrect_t *area)
{
	rr_player_state_t ps;
	int w, h;
	if (seat || !CLQ2CG_Active() || !(cgwheeldata.num_weapons || cgwheeldata.num_powerups))
		return;
	if (!CLQ2_CGBegin(area, &w, &h))
		return;
	CG_PlayerState(&ps, 0);
	CG_CarouselDraw(&ps, w, h);
	CG_WheelDraw(&ps, w, h, h?area->height / (float)h:1);
	R2D_ImageColours(1, 1, 1, 1);
}

void CLQ2CG_Init(void)
{
	Cvar_Register(&cl_q2cgame, "Quake 2 HUD");
	Cvar_Register(&wc_screen_frac_y, "Quake 2 HUD");
	Cvar_Register(&wc_timeout, "Quake 2 HUD");
	Cvar_Register(&wc_lock_time, "Quake 2 HUD");
	Cvar_Register(&wc_ammo_scale, "Quake 2 HUD");
	Cvar_Register(&ww_timer_speed, "Quake 2 HUD");
	Cvar_Register(&ww_ammo_scale, "Quake 2 HUD");
}
#endif
