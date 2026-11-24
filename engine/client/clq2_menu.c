/*
FTE's side of the Quake 2 menus. The menus themselves are yquake2's (engine/client/q2menu: q2menu.c (yquake2's menu.c), qmenu.c,
videomenu.c), compiled apart with yquake2's types and joined to FTE through q2menu_sys.h; this file gives
them FTE's drawing (in yquake2's real pixels, through the Quake 2 HUD's helpers), cvars, keys, files,
saved games and server browser, and puts them up as one of FTE's menus.

What is FTE's rather than yquake2's:
- saved games are FTE's (saves/<name>/info.fsv): the quick save, the newest of the three autosaves, and
  the regular slots s0, s1, ... ;
- the video menu sets FTE's cvars (vid_width/vid_height for yquake2's r_mode, gamma for vid_gamma, ...),
  and has no renderer list or per-renderer options;
- there are no gamepad or gyro menus (FTE's input settings cover those), so gamepad buttons only
  navigate.

Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) yquake2 contributors.

This program is free software; you can redistribute it and/or modify it under the terms of the GNU
General Public License as published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.
*/
#include "quakedef.h"

#ifdef Q2CLIENT
#include "shader.h"
#include "cl_master.h"
#include "q2menu_sys.h"

/* yquake2's key numbers (q2menu/q2keys.h), generated from it: the two engines number keys differently
   past ASCII, and their headers cannot meet */
#define Q2K_TAB              9
#define Q2K_ENTER            13
#define Q2K_ESCAPE           27
#define Q2K_SPACE            32
#define Q2K_BACKSPACE        127
#define Q2K_COMMAND          128
#define Q2K_CAPSLOCK         129
#define Q2K_POWER            130
#define Q2K_PAUSE            131
#define Q2K_UPARROW          132
#define Q2K_DOWNARROW        133
#define Q2K_LEFTARROW        134
#define Q2K_RIGHTARROW       135
#define Q2K_ALT              136
#define Q2K_CTRL             137
#define Q2K_SHIFT            138
#define Q2K_INS              139
#define Q2K_DEL              140
#define Q2K_PGDN             141
#define Q2K_PGUP             142
#define Q2K_HOME             143
#define Q2K_END              144
#define Q2K_F1               145
#define Q2K_KP_HOME          160
#define Q2K_KP_UPARROW       161
#define Q2K_KP_PGUP          162
#define Q2K_KP_LEFTARROW     163
#define Q2K_KP_5             164
#define Q2K_KP_RIGHTARROW    165
#define Q2K_KP_END           166
#define Q2K_KP_DOWNARROW     167
#define Q2K_KP_PGDN          168
#define Q2K_KP_ENTER         169
#define Q2K_KP_INS           170
#define Q2K_KP_DEL           171
#define Q2K_KP_SLASH         172
#define Q2K_KP_MINUS         173
#define Q2K_KP_PLUS          174
#define Q2K_KP_NUMLOCK       175
#define Q2K_KP_STAR          176
#define Q2K_KP_EQUALS        177
#define Q2K_MOUSE1           178
#define Q2K_MOUSE2           179
#define Q2K_MOUSE3           180
#define Q2K_MOUSE4           181
#define Q2K_MOUSE5           182
#define Q2K_MWHEELDOWN       183
#define Q2K_MWHEELUP         184
#define Q2K_SUPER            185
#define Q2K_PRINT            189
#define Q2K_SCROLLOCK        191
#define Q2K_MENU             192
#define Q2K_BTN_SOUTH        250
#define Q2K_BTN_EAST         251
#define Q2K_BTN_WEST         252
#define Q2K_BTN_NORTH        253
#define Q2K_BTN_BACK         254
#define Q2K_BTN_GUIDE        255
#define Q2K_BTN_START        256
#define Q2K_STICK_LEFT       257
#define Q2K_STICK_RIGHT      258
#define Q2K_SHOULDER_LEFT    259
#define Q2K_SHOULDER_RIGHT   260
#define Q2K_DPAD_UP          261
#define Q2K_DPAD_DOWN        262
#define Q2K_DPAD_LEFT        263
#define Q2K_DPAD_RIGHT       264
#define Q2K_BTN_MISC1        265
#define Q2K_PADDLE_RIGHT_1   266
#define Q2K_PADDLE_LEFT_1    267
#define Q2K_PADDLE_RIGHT_2   268
#define Q2K_PADDLE_LEFT_2    269
#define Q2K_TOUCHPAD         270
#define Q2K_TRIG_LEFT        273
#define Q2K_TRIG_RIGHT       274
#define Q2K_LAST             303

static const struct
{
	unsigned short q2, fte;
} q2m_keys[] =
{
	{Q2K_CAPSLOCK, K_CAPSLOCK}, {Q2K_POWER, K_POWER}, {Q2K_PAUSE, K_PAUSE},
	{Q2K_UPARROW, K_UPARROW}, {Q2K_DOWNARROW, K_DOWNARROW}, {Q2K_LEFTARROW, K_LEFTARROW}, {Q2K_RIGHTARROW, K_RIGHTARROW},
	/* yquake2 has one of each modifier: the left keys are its names for FTE's bindings, both sides map to it */
	{Q2K_ALT, K_LALT}, {Q2K_CTRL, K_LCTRL}, {Q2K_SHIFT, K_LSHIFT},
	{Q2K_ALT, K_RALT}, {Q2K_CTRL, K_RCTRL}, {Q2K_SHIFT, K_RSHIFT},
	{Q2K_INS, K_INS}, {Q2K_DEL, K_DEL}, {Q2K_PGDN, K_PGDN}, {Q2K_PGUP, K_PGUP}, {Q2K_HOME, K_HOME}, {Q2K_END, K_END},
	{Q2K_F1+0, K_F1}, {Q2K_F1+1, K_F2}, {Q2K_F1+2, K_F3}, {Q2K_F1+3, K_F4}, {Q2K_F1+4, K_F5}, {Q2K_F1+5, K_F6},
	{Q2K_F1+6, K_F7}, {Q2K_F1+7, K_F8}, {Q2K_F1+8, K_F9}, {Q2K_F1+9, K_F10}, {Q2K_F1+10, K_F11}, {Q2K_F1+11, K_F12},
	{Q2K_F1+12, K_F13}, {Q2K_F1+13, K_F14}, {Q2K_F1+14, K_F15},
	{Q2K_KP_HOME, K_KP_HOME}, {Q2K_KP_UPARROW, K_KP_UPARROW}, {Q2K_KP_PGUP, K_KP_PGUP}, {Q2K_KP_LEFTARROW, K_KP_LEFTARROW},
	{Q2K_KP_5, K_KP_5}, {Q2K_KP_RIGHTARROW, K_KP_RIGHTARROW}, {Q2K_KP_END, K_KP_END}, {Q2K_KP_DOWNARROW, K_KP_DOWNARROW},
	{Q2K_KP_PGDN, K_KP_PGDN}, {Q2K_KP_ENTER, K_KP_ENTER}, {Q2K_KP_INS, K_KP_INS}, {Q2K_KP_DEL, K_KP_DEL},
	{Q2K_KP_SLASH, K_KP_SLASH}, {Q2K_KP_MINUS, K_KP_MINUS}, {Q2K_KP_PLUS, K_KP_PLUS}, {Q2K_KP_NUMLOCK, K_KP_NUMLOCK},
	{Q2K_KP_STAR, K_KP_STAR}, {Q2K_KP_EQUALS, K_KP_EQUALS},
	{Q2K_MOUSE1, K_MOUSE1}, {Q2K_MOUSE2, K_MOUSE2}, {Q2K_MOUSE3, K_MOUSE3}, {Q2K_MOUSE4, K_MOUSE4}, {Q2K_MOUSE5, K_MOUSE5},
	{Q2K_MWHEELDOWN, K_MWHEELDOWN}, {Q2K_MWHEELUP, K_MWHEELUP},
	{Q2K_SUPER, K_LWIN}, {Q2K_SUPER, K_RWIN}, {Q2K_PRINT, K_PRINTSCREEN}, {Q2K_SCROLLOCK, K_SCRLCK}, {Q2K_MENU, K_APP},
	{Q2K_BTN_SOUTH, K_GP_DIAMOND_DOWN}, {Q2K_BTN_EAST, K_GP_DIAMOND_RIGHT}, {Q2K_BTN_WEST, K_GP_DIAMOND_LEFT},
	{Q2K_BTN_NORTH, K_GP_DIAMOND_UP}, {Q2K_BTN_BACK, K_GP_VIEW}, {Q2K_BTN_GUIDE, K_GP_GUIDE}, {Q2K_BTN_START, K_GP_MENU},
	{Q2K_STICK_LEFT, K_GP_LEFT_STICK}, {Q2K_STICK_RIGHT, K_GP_RIGHT_STICK},
	{Q2K_SHOULDER_LEFT, K_GP_LEFT_SHOULDER}, {Q2K_SHOULDER_RIGHT, K_GP_RIGHT_SHOULDER},
	{Q2K_DPAD_UP, K_GP_DPAD_UP}, {Q2K_DPAD_DOWN, K_GP_DPAD_DOWN}, {Q2K_DPAD_LEFT, K_GP_DPAD_LEFT}, {Q2K_DPAD_RIGHT, K_GP_DPAD_RIGHT},
	{Q2K_BTN_MISC1, K_GP_MISC1}, {Q2K_PADDLE_RIGHT_1, K_GP_PADDLE1}, {Q2K_PADDLE_LEFT_1, K_GP_PADDLE2},
	{Q2K_PADDLE_RIGHT_2, K_GP_PADDLE3}, {Q2K_PADDLE_LEFT_2, K_GP_PADDLE4}, {Q2K_TOUCHPAD, K_GP_TOUCHPAD},
	{Q2K_TRIG_LEFT, K_GP_LEFT_TRIGGER}, {Q2K_TRIG_RIGHT, K_GP_RIGHT_TRIGGER},
};

static int CLQ2M_FteKey(int q2key)
{
	size_t i;
	if (q2key == Q2K_TAB || q2key == Q2K_ENTER || q2key == Q2K_ESCAPE || q2key == Q2K_BACKSPACE || (q2key >= 32 && q2key < 127))
		return q2key;	//ASCII is ASCII in both
	for (i = 0; i < countof(q2m_keys); i++)
		if (q2m_keys[i].q2 == q2key)
			return q2m_keys[i].fte;
	return 0;
}
static int CLQ2M_Q2Key(int key)
{
	size_t i;
	if (key == K_TAB || key == K_ENTER || key == K_ESCAPE || key == K_BACKSPACE || (key >= 32 && key < 127))
		return key;
	for (i = 0; i < countof(q2m_keys); i++)
		if (q2m_keys[i].fte == key)
			return q2m_keys[i].q2;
	return 0;
}

/* ---------------------------------------------------------------- drawing, in screen pixels */

static float CLQ2M_ToVirtualX(float x)	{return vid.rotpixelwidth ? x * vid.width / vid.rotpixelwidth : x;}
static float CLQ2M_ToVirtualY(float y)	{return vid.rotpixelheight ? y * vid.height / vid.rotpixelheight : y;}

void Q2MSYS_DrawPic(int x, int y, const char *pic, float scale)
{
	CLQ2_DrawPicScaled(x, y, pic, scale);
}
void Q2MSYS_DrawChar(int x, int y, int num, float scale)
{
	CLQ2_DrawCharScaled(x, y, num, scale);
}
void Q2MSYS_DrawFill(int x, int y, int w, int h, int palettecolour)
{
	R2D_ImagePaletteColour(palettecolour & 0xff, 1);
	R2D_FillBlock(CLQ2M_ToVirtualX(x), CLQ2M_ToVirtualY(y), CLQ2M_ToVirtualX(w), CLQ2M_ToVirtualY(h));
	R2D_ImageColours(1, 1, 1, 1);
}
void Q2MSYS_FadeScreen(void)
{
	R2D_FadeScreen();
}
void Q2MSYS_GetPicSize(int *w, int *h, const char *pic)
{
	CLQ2_GetPicSize(pic, w, h);
}
float Q2MSYS_MenuScale(void)
{
	return CLQ2_MenuScale();
}

/* the player setup's preview: the model (with the chosen skin) and its weapon, on no world */
void Q2MSYS_RenderModels(int x, int y, int w, int h, float fov_x, float fov_y, float time, const q2msys_entity_t *ents, int numents)
{
	static char skinname[MAX_QPATH];
	static skinid_t skinid;
	entity_t ent;
	int i;

	if (R2D_Flush)
		R2D_Flush();

	CL_ClearEntityLists();
	for (i = 0; i < numents; i++)
	{
		V_ClearEntity(&ent);
		ent.model = Mod_ForName(ents[i].model, MLV_SILENT);
		if (!ent.model)
			continue;
		VectorCopy(ents[i].origin, ent.origin);
		VectorCopy(ents[i].angles, ent.angles);
		ent.framestate.g[FS_REG].frame[0] = ents[i].frame;
		ent.framestate.g[FS_REG].frame[1] = ents[i].oldframe;
		ent.framestate.g[FS_REG].lerpweight[0] = 1 - ents[i].backlerp;
		ent.framestate.g[FS_REG].lerpweight[1] = ents[i].backlerp;
		ent.framestate.g[FS_REG].frametime[0] = ent.framestate.g[FS_REG].frametime[1] = time;
		ent.scale = 1;
		ent.flags = ents[i].flags;	//Quake 2's renderfx bits are FTE's
		Vector4Set(ent.shaderRGBAf, 1, 1, 1, 1);
		VectorSet(ent.glowmod, 1, 1, 1);

		if (ents[i].skin)
		{
			if (!skinid || strcmp(skinname, ents[i].skin))
			{
				if (skinid)
					Mod_WipeSkin(skinid, false);
				Q_strncpyz(skinname, ents[i].skin, sizeof(skinname));
				skinid = Mod_ReadSkinFile(va("%s.skin", skinname), va("replace \"\" \"%s\"", skinname));
			}
			ent.customskin = skinid;
		}
		V_AddEntity(&ent);
	}

	//looking down +x from the origin, as yquake2's refdef does
	VectorClear(r_refdef.vieworg);
	VectorClear(r_refdef.viewangles);
	VectorSet(r_refdef.viewaxis[0], 1, 0, 0);
	VectorSet(r_refdef.viewaxis[1], 0, 1, 0);
	VectorSet(r_refdef.viewaxis[2], 0, 0, 1);
	r_refdef.flags = RDF_NOWORLDMODEL;
	r_refdef.fovv_x = r_refdef.fov_x = fov_x;
	r_refdef.fovv_y = r_refdef.fov_y = fov_y;
	r_refdef.vrect.x = CLQ2M_ToVirtualX(x);
	r_refdef.vrect.y = CLQ2M_ToVirtualY(y);
	r_refdef.vrect.width = CLQ2M_ToVirtualX(w);
	r_refdef.vrect.height = CLQ2M_ToVirtualY(h);
	r_refdef.time = time;
	r_refdef.useperspective = true;
	r_refdef.mindist = bound(0.1, gl_mindist.value, 4);
	r_refdef.maxdist = gl_maxdist.value;
	r_refdef.playerview = &cl.playerview[0];
	memset(&r_refdef.globalfog, 0, sizeof(r_refdef.globalfog));
	r_refdef.areabitsknown = false;

	R_RenderView();
	r_refdef.playerview = NULL;
	r_refdef.time = 0;
}

/* ---------------------------------------------------------------- cvars */

/* yquake2's names for cvars FTE calls something else */
static const struct
{
	const char *q2, *fte;
} q2m_cvarnames[] =
{
	{"s_volume",		"volume"},
	{"ogg_volume",		"bgmvolume"},
	{"r_vsync",			"vid_vsync"},
	{"r_anisotropic",	"gl_texture_anisotropy"},
	{"r_msaa_samples",	"vid_multisample"},
};
static const char *CLQ2M_CvarName(const char *name)
{
	size_t i;
	for (i = 0; i < countof(q2m_cvarnames); i++)
		if (!strcmp(q2m_cvarnames[i].q2, name))
			return q2m_cvarnames[i].fte;
	return name;
}

/* the ones that are not just renamed: freelook is FTE's +mlook, vid_gamma is brighter upwards where
   FTE's gamma is brighter downwards, and paused is FTE's own business (single player pauses under any menu) */
static qboolean CLQ2M_CvarSpecialGet(const char *name, char *value, int valuesize)
{
	extern kbutton_t in_mlook;
	extern cvar_t v_gamma;
	if (!strcmp(name, "freelook"))
		Q_snprintfz(value, valuesize, "%i", (in_mlook.state[0] & 1) ? 1 : 0);
	else if (!strcmp(name, "vid_gamma"))
		Q_snprintfz(value, valuesize, "%g", v_gamma.value > 0 ? 1 / v_gamma.value : 1);
	else if (!strcmp(name, "paused"))
		Q_strncpyz(value, "0", valuesize);
	else
		return false;
	return true;
}
static qboolean CLQ2M_CvarSpecialSet(const char *name, const char *value)
{
	extern cvar_t v_gamma;
	if (!strcmp(name, "freelook"))
		Cbuf_AddText(atoi(value) ? "+mlook\n" : "-mlook\n", RESTRICT_LOCAL);
	else if (!strcmp(name, "vid_gamma"))
	{
		float g = atof(value);
		Cvar_SetValue(&v_gamma, g > 0 ? 1 / g : 1);
	}
	else if (!strcmp(name, "paused"))
		;
	else
		return false;
	return true;
}

void Q2MSYS_CvarGet(const char *name, const char *def, int archive, char *value, int valuesize)
{
	cvar_t *var;
	if (CLQ2M_CvarSpecialGet(name, value, valuesize))
		return;
	var = Cvar_Get(CLQ2M_CvarName(name), def, archive ? CVAR_ARCHIVE : 0, "Quake 2 menus");
	Q_strncpyz(value, var ? var->string : def, valuesize);
}
void Q2MSYS_CvarString(const char *name, char *value, int valuesize)
{
	cvar_t *var;
	if (CLQ2M_CvarSpecialGet(name, value, valuesize))
		return;
	var = Cvar_FindVar(CLQ2M_CvarName(name));
	Q_strncpyz(value, var ? var->string : "", valuesize);
}
void Q2MSYS_CvarSet(const char *name, const char *value, int force)
{
	cvar_t *var;
	if (CLQ2M_CvarSpecialSet(name, value))
		return;
	name = CLQ2M_CvarName(name);
	var = Cvar_FindVar(name);
	if (!var)
		var = Cvar_Get(name, value, 0, "Quake 2 menus");
	if (!var)
		return;
	if (force)
		Cvar_ForceSet(var, value);
	else
		Cvar_Set(var, value);
}

/* ---------------------------------------------------------------- commands, sounds, printing */

static void CLQ2M_Sync(void);
static void CLQ2M_SetFrame(void);

static void CLQ2M_Command_f(void)
{
	if (M_GameType() != MGT_QUAKE2)
		return;
	CLQ2M_SetFrame();
	Q2M_Command(Cmd_Argv(0));
	CLQ2M_Sync();
}
void Q2MSYS_AddCommand(const char *name)
{
	//yquake2's menu_main, menu_options etc. replace FTE's own while Quake 2 is the game
	if (Cmd_Exists(name))
		Cmd_RemoveCommand(name);
	Cmd_AddCommand(name, CLQ2M_Command_f);
}
void Q2MSYS_ExecText(const char *text, int insert)
{
	if (!text)
		Cbuf_Execute();
	else if (insert)
		Cbuf_InsertText(text, RESTRICT_LOCAL, false);
	else
		Cbuf_AddText(text, RESTRICT_LOCAL);
}
void Q2MSYS_LocalSound(const char *sound)
{
	S_LocalSound(sound);
}
int Q2MSYS_Milliseconds(void)
{
	return Sys_Milliseconds();
}
int Q2MSYS_ServerState(void)
{
#ifdef HAVE_SERVER
	return sv.state != ss_dead;
#else
	return 0;
#endif
}
void Q2MSYS_Print(int developer, const char *text)
{
	if (developer)
		Con_DPrintf("%s", text);
	else
		Con_Printf("%s", text);
}
void Q2MSYS_Error(const char *text)
{
	Con_Printf(CON_ERROR "%s\n", text);
}

/* yquake2's "loading" (the new game command starts with it) */
static void CLQ2M_Loading_f(void)
{
	SCR_BeginLoadingPlaque();
}

/* ---------------------------------------------------------------- keys */

const char *Q2MSYS_KeyName(int q2key)
{
	int key = CLQ2M_FteKey(q2key);
	return key ? Key_KeynumToString(key, 0) : "???";
}
const char *Q2MSYS_GetBinding(int q2key)
{
	int key = CLQ2M_FteKey(q2key);
	return key ? keybindings[key][0] : NULL;
}
void Q2MSYS_SetBinding(int q2key, const char *binding)
{
	int key = CLQ2M_FteKey(q2key);
	if (key)
		Key_SetBinding(key, ~0, (binding && *binding) ? binding : NULL, RESTRICT_LOCAL);
}

static char q2m_paste[256];
static qboolean q2m_pasted;
static void CLQ2M_Pasted(void *ctx, const char *utf8)
{	//may come later than asked for on some systems, so into a buffer of our own
	if (utf8)
	{
		Q_strncpyz(q2m_paste, utf8, sizeof(q2m_paste));
		q2m_pasted = true;
	}
}
int Q2MSYS_GetClipboard(char *text, int size)
{
	q2m_pasted = false;
	Sys_Clipboard_PasteText(CBT_CLIPBOARD, CLQ2M_Pasted, NULL);
	if (!q2m_pasted)
		return false;
	Q_strncpyz(text, q2m_paste, size);
	return true;
}
void Q2MSYS_SetClipboard(const char *text)
{
	Sys_SaveClipboard(CBT_CLIPBOARD, text);
}

/* ---------------------------------------------------------------- files */

int Q2MSYS_LoadFile(const char *path, void **buffer)
{
	size_t len;
	*buffer = FS_LoadMallocFile(path, &len);
	return *buffer ? (int)len : -1;
}
void Q2MSYS_FreeFile(void *buffer)
{
	BZ_Free(buffer);
}
int Q2MSYS_FileExists(const char *path)
{
	return COM_FCheckExists(path);
}

typedef struct
{
	void (*callback)(const char *name, void *ctx);
	void *ctx;
	const char *basedir;
} q2mlist_t;
static int QDECL CLQ2M_Listed(const char *name, qofs_t size, time_t mtime, void *parm, searchpathfuncs_t *spath)
{
	q2mlist_t *l = parm;
	l->callback(name, l->ctx);
	return true;
}
void Q2MSYS_ListFiles(const char *wildcard, void (*callback)(const char *name, void *ctx), void *ctx)
{
	q2mlist_t l = {callback, ctx};
	size_t len = strlen(wildcard);
	COM_EnumerateFiles(wildcard, CLQ2M_Listed, &l);
	//yquake2 lists players/* to find the player model directories, which packages have no entries for:
	//FTE's * stops at a slash, so the files one level down stand in for them
	if (len >= 2 && !strcmp(wildcard + len - 2, "/*"))
		COM_EnumerateFiles(va("%s/*", wildcard), CLQ2M_Listed, &l);
}

static int QDECL CLQ2M_ModContent(const char *name, qofs_t size, time_t mtime, void *parm, searchpathfuncs_t *spath)
{
	return false;	//found one, stop
}
static int QDECL CLQ2M_ModDir(const char *name, qofs_t size, time_t mtime, void *parm, searchpathfuncs_t *spath)
{
	static const char *contents[] = {"*.pak", "*.pk2", "*.pk3", "*.zip", "game*"};
	q2mlist_t *l = parm;
	char dir[MAX_QPATH];
	size_t len = strlen(name), i;

	if (!len || name[len-1] != '/' || len >= sizeof(dir) || *name == '.')
		return true;
	memcpy(dir, name, len-1);
	dir[len-1] = 0;
	if (strchr(dir, '/') || strchr(dir, '\\'))
		return true;

	//as yquake2: a directory with packages or a game library in it
	for (i = 0; i < countof(contents); i++)
		if (!Sys_EnumerateFiles(va("%s%s/", l->basedir, dir), contents[i], CLQ2M_ModContent, NULL, NULL))
		{
			l->callback(dir, l->ctx);
			break;
		}
	return true;
}
void Q2MSYS_ListMods(void (*callback)(const char *gamedir, void *ctx), void *ctx)
{
	extern qboolean com_homepathenabled;
	q2mlist_t l = {callback, ctx};

	l.basedir = com_gamepath;
	Sys_EnumerateFiles(com_gamepath, "*", CLQ2M_ModDir, &l, NULL);
	if (com_homepathenabled)
	{
		l.basedir = com_homepath;
		Sys_EnumerateFiles(com_homepath, "*", CLQ2M_ModDir, &l, NULL);
	}
}

/* ---------------------------------------------------------------- saved games */

/* the save's level name and date from saves/<name>/info.fsv (as FTE's own load menu reads them) */
static qboolean CLQ2M_ReadSave(const char *sname, char *level, size_t levelsize, char *datetime, size_t dtsize)
{
	size_t len, i;
	char *file = FS_LoadMallocFile(va("saves/%s/info.fsv", sname), &len), *line, *end;
	if (!file)
		return false;

	line = strchr(file, '\n');	//past the version
	if (line)
	{
		line++;
		end = strchr(line, '\n');
		if (end)
			*end = 0;
		for (i = 0; line[i]; i++)
			if (line[i] == '_' || line[i] == '\r')
				line[i] = ' ';

		//the level is the first 22 columns, the date from column 39
		Q_strncpyz(level, line, min(levelsize, 23));
		for (i = strlen(level); i > 0 && level[i-1] == ' '; i--)
			level[i-1] = 0;
		Q_strncpyz(datetime, (strlen(line) > 39) ? line + 39 : "", dtsize);
	}
	else
		*level = *datetime = 0;

	BZ_Free(file);
	return true;
}
/* FTE's name for one of yquake2's slots: -1 the quick save, 0 the newest autosave, N the regular slot N */
static const char *CLQ2M_SaveName(int slot)
{
	static char name[16];
	char level[32], dt[64], best[64] = "";
	int i;

	if (slot < 0)
		return "quick";
	if (slot > 0)
	{
		Q_snprintfz(name, sizeof(name), "s%i", slot - 1);
		return name;
	}

	Q_strncpyz(name, "a0", sizeof(name));
	for (i = 0; i < 3; i++)
		if (CLQ2M_ReadSave(va("a%i", i), level, sizeof(level), dt, sizeof(dt)) && strcmp(dt, best) > 0)
		{	//"YYYY-MM-DD HH:MM:SS" sorts as text
			Q_strncpyz(best, dt, sizeof(best));
			Q_snprintfz(name, sizeof(name), "a%i", i);
		}
	return name;
}
int Q2MSYS_SaveComment(int slot, char *comment, int size)
{
	char level[32], dt[64];

	if (!CLQ2M_ReadSave(CLQ2M_SaveName(slot), level, sizeof(level), dt, sizeof(dt)))
		return false;

	if (slot == 0)	//as yquake2 names its autosaves
		Q_snprintfz(comment, size, "ENTERING %s", level);
	else if (strlen(dt) >= 16)	//yquake2's 'HH:MM mm:dd  Level Name'
		Q_snprintfz(comment, size, "%.5s %.2s:%.2s  %s", dt + 11, dt + 5, dt + 8, level);
	else
		Q_snprintfz(comment, size, "--:-- --:--  %s", level);
	return true;
}
void Q2MSYS_LoadSlot(int slot)
{
	Cbuf_AddText(va("loadgame %s\n", CLQ2M_SaveName(slot)), RESTRICT_LOCAL);
}
void Q2MSYS_SaveSlot(int slot)
{
	Cbuf_AddText(va("savegame %s\n", CLQ2M_SaveName(slot)), RESTRICT_LOCAL);
}
void Q2MSYS_DeleteSlot(int slot)
{
	Cbuf_AddText(va("unsavegame %s\n", CLQ2M_SaveName(slot)), RESTRICT_LOCAL);
}

/* ---------------------------------------------------------------- servers */

#ifdef CL_MASTER
static double q2m_searchend;
static serverinfo_t *q2m_listed[64];
static int q2m_numlisted;
static void CLQ2M_PollServers(void)
{
	unsigned int i;
	int j;
	char adr[256], info[128];

	if (q2m_searchend < realtime)
		return;
	Master_CheckPollSockets();

	for (i = 0; i < Master_TotalCount() && q2m_numlisted < countof(q2m_listed); i++)
	{
		serverinfo_t *s = Master_InfoForNum(i);
		if (!s || !(s->status & SRVSTATUS_ALIVE) || (s->special & SS_PROTOCOLMASK) != SS_QUAKE2)
			continue;
		for (j = 0; j < q2m_numlisted; j++)
			if (q2m_listed[j] == s)
				break;
		if (j < q2m_numlisted)
			continue;
		q2m_listed[q2m_numlisted++] = s;

		//yquake2's servers answer with this line
		Q_snprintfz(info, sizeof(info), "%-16.16s %-8.8s %2i/%2i", s->name, s->map, s->players, s->maxplayers);
		Q2M_AddServer(Master_ServerToString(adr, sizeof(adr), s), info);
	}
}
void Q2MSYS_PingServers(void)
{
	q2m_numlisted = 0;
	q2m_searchend = realtime + 10;
	MasterInfo_Refresh(true);
}
#else
static void CLQ2M_PollServers(void)
{
}
void Q2MSYS_PingServers(void)
{
}
#endif

/* ---------------------------------------------------------------- the menu layer */

static menu_t q2m_menu;
static float q2m_mousex = -1, q2m_mousey = -1;

static void CLQ2M_SetFrame(void)
{
	int state;
	switch (cls.state)
	{
	case ca_active:		state = 4; break;	//yquake2's ca_active
	case ca_connected:
	case ca_onserver:	state = 3; break;	//ca_connected
	default:			state = 1; break;	//ca_disconnected
	}
	Q2M_SetFrame(vid.rotpixelwidth, vid.rotpixelheight, (int)(realtime * 1000), state, cl.servercount, cls.demoplayback != DPB_NONE, "");
}

/* the menus open and close themselves; FTE's menu layer follows */
static void CLQ2M_Sync(void)
{
	if (Q2M_TakeConsole())
		Key_Dest_Add(kdm_console);

	if (Q2M_IsActive())
	{
		if (!Menu_IsLinked(&q2m_menu))
		{
			Menu_Push(&q2m_menu, false);
			q2m_mousex = mousecursor_x;	//where the pointer was is not a choice
			q2m_mousey = mousecursor_y;
		}
	}
	else if (Menu_IsLinked(&q2m_menu))
		Menu_Unlink(&q2m_menu, false);
}

/* the pointer (mouse or touch), in screen pixels */
static int CLQ2M_PointerX(void)	{return vid.width ? mousecursor_x * vid.rotpixelwidth / vid.width : mousecursor_x;}
static int CLQ2M_PointerY(void)	{return vid.height ? mousecursor_y * vid.rotpixelheight / vid.height : mousecursor_y;}

/* moving the pointer puts the cursor on the item under it (only moving it: a menu opened with the
   keyboard keeps its own first item) */
static void CLQ2M_Pointer(void)
{
	if (q2m_mousex == mousecursor_x && q2m_mousey == mousecursor_y)
		return;
	q2m_mousex = mousecursor_x;
	q2m_mousey = mousecursor_y;
	Q2M_MouseMove(CLQ2M_PointerX(), CLQ2M_PointerY());
}

static qboolean CLQ2M_KeyEvent(menu_t *m, qboolean isdown, unsigned int devid, int key, int unicode)
{
	int q2key;

	switch (key)
	{
	case K_TOUCH:
	case K_TOUCHSLIDE:
	case K_TOUCHLONG:
		return true;	//a touch moves the cursor; the tap is the click
	case K_TOUCHTAP:
	case K_MOUSE1:
		CLQ2M_SetFrame();
		if (isdown)
			Q2M_Click(CLQ2M_PointerX(), CLQ2M_PointerY(), key == K_TOUCHTAP);
		else if (key == K_MOUSE1)
			Q2M_KeyEvent(Q2K_MOUSE1, false);
		CLQ2M_Sync();
		return true;
	//FTE's sticks as buttons: navigation only, they have no yquake2 names to bind
	case K_GP_LEFT_THUMB_UP:	q2key = Q2K_UPARROW;	break;
	case K_GP_LEFT_THUMB_DOWN:	q2key = Q2K_DOWNARROW;	break;
	case K_GP_LEFT_THUMB_LEFT:	q2key = Q2K_LEFTARROW;	break;
	case K_GP_LEFT_THUMB_RIGHT:	q2key = Q2K_RIGHTARROW;	break;
	default:
		//typed text by character, so shift and keyboard layouts are honoured; ctrl+letters stay keys
		if (isdown && unicode >= 32 && unicode < 127 && !keydown[K_LCTRL] && !keydown[K_RCTRL])
			q2key = unicode;
		else
			q2key = CLQ2M_Q2Key(key);
		break;
	}
	if (!q2key)
		return true;

	CLQ2M_SetFrame();
	Q2M_KeyEvent(q2key, isdown);
	CLQ2M_Sync();
	return true;
}

static void CLQ2M_DrawMenu(menu_t *m)
{
	CLQ2M_SetFrame();
	CLQ2M_PollServers();
	CLQ2M_Pointer();
	Q2M_RefreshCvars();
	Q2M_Draw();
}

static void CLQ2M_Release(menu_t *m, qboolean forced)
{
	Q2M_ForceOff();	//FTE closed the menus (a map start, closemenu, ...)
}

/* ---------------------------------------------------------------- FTE's menu code calls these */

/* with FTE's menus registered, while Quake 2 is the game: yquake2's take over their commands */
void CLQ2_InitMenus(void)
{
	q2m_menu.keyevent = CLQ2M_KeyEvent;
	q2m_menu.drawmenu = CLQ2M_DrawMenu;
	q2m_menu.release = CLQ2M_Release;
	q2m_menu.cursor = &key_customcursor[kc_console];	//absolute pointer, for touch and the mouse

	Q2M_Init();
	if (!Cmd_Exists("loading"))
		Cmd_AddCommand("loading", CLQ2M_Loading_f);
}

/* opens one of yquake2's menus by its command (menu_main, ...); false when they are not in use */
qboolean CLQ2_OpenMenu(const char *command)
{
	if (M_GameType() != MGT_QUAKE2)
		return false;
	if (!q2m_menu.drawmenu)	//opened before the menus were (re)started with Quake 2 as the game
		CLQ2_InitMenus();
	CLQ2M_SetFrame();
	Q2M_Command(command);
	CLQ2M_Sync();
	return true;
}
#endif
