/*
Quake 2 HUD and screen elements, ported from yquake2 (cl_screen.c, cl_inventory.c), which keeps id's.

The status bar and layouts (scoreboards, the help computer), the inventory, the centerprint, the crosshair
and the pause and net plaques draw exactly as yquake2 draws them: in real pixels, with yquake2's scales
(r_hudscale, r_consolescale, r_menuscale, crosshair_scale -- -1 picks the integer default
min(width/640, height/240)), and with Quake 2's own charset glyphs rather than FTE's markup-parsing text.
Positions are converted into FTE's virtual 2D space only at draw time, so vid_conautoscale does not
change the size of the Quake 2 HUD, just as yquake2's console scale does not.

Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) yquake2 contributors.

This program is free software; you can redistribute it and/or modify it under the terms of the GNU
General Public License as published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.
*/
#include "quakedef.h"
#include "shader.h"

#ifdef Q2CLIENT

static cvar_t r_hudscale		= CVARFD("r_hudscale", "-1", CVAR_ARCHIVE, "Quake 2: size of the status bar, layouts and inventory. -1 picks yquake2's default (the largest whole number up to width/640 and height/240), 0 hides them.");
static cvar_t r_consolescale	= CVARFD("r_consolescale", "-1", CVAR_ARCHIVE, "Quake 2: size of the centerprint text. -1 picks the default scale.");
cvar_t r_menuscale				= CVARFD("r_menuscale", "-1", CVAR_ARCHIVE, "Quake 2: size of the menus and the pause/net plaques. -1 picks the default scale.");
static cvar_t crosshair_scale	= CVARFD("crosshair_scale", "-1", CVAR_ARCHIVE, "Quake 2: size of the ch1-ch3 crosshair pics. -1 picks the default scale.");
static cvar_t crosshair_color_r	= CVARF("crosshair_color_r", "1", CVAR_ARCHIVE);
static cvar_t crosshair_color_g	= CVARF("crosshair_color_g", "1", CVAR_ARCHIVE);
static cvar_t crosshair_color_b	= CVARF("crosshair_color_b", "1", CVAR_ARCHIVE);
extern cvar_t crosshair, scr_centertime;

mpic_t *Sbar_Q2CachePic(const char *name);
const char *Get_Q2ConfigString(int i);

void CLQ2_InitHUD(void)
{
	Cvar_Register(&r_hudscale, "Quake 2 HUD");
	Cvar_Register(&r_consolescale, "Quake 2 HUD");
	Cvar_Register(&r_menuscale, "Quake 2 HUD");
	Cvar_Register(&crosshair_scale, "Quake 2 HUD");
	Cvar_Register(&crosshair_color_r, "Quake 2 HUD");
	Cvar_Register(&crosshair_color_g, "Quake 2 HUD");
	Cvar_Register(&crosshair_color_b, "Quake 2 HUD");
}

/*
=====================================================================
Drawing in yquake2's coordinates

q2s describes the area being drawn into as yquake2's viddef: w/h in real pixels, and where that area
is in FTE's virtual 2D space (vx, vy) along with how many virtual units one pixel is (sx, sy).
=====================================================================
*/
static struct
{
	float vx, vy;	//top left, virtual units
	float sx, sy;	//virtual units per pixel
	int w, h;		//size in pixels
} q2s;

static void Q2S_SetArea(float vx, float vy, float vw, float vh)
{
	q2s.sx = vid.rotpixelwidth ? (float)vid.width / vid.rotpixelwidth : 1;
	q2s.sy = vid.rotpixelheight ? (float)vid.height / vid.rotpixelheight : 1;
	q2s.vx = vx;
	q2s.vy = vy;
	q2s.w = vw / q2s.sx + 0.5;
	q2s.h = vh / q2s.sy + 0.5;
}
static void Q2S_FullScreen(void)
{
	Q2S_SetArea(0, 0, vid.width, vid.height);
}

//yquake2's SCR_ClampScale / SCR_GetDefaultScale, for the area being drawn into
static float Q2S_ClampScale(float scale)
{
	float f = q2s.w / 320.0f;
	if (scale > f)
		scale = f;
	f = q2s.h / 240.0f;
	if (scale > f)
		scale = f;
	if (scale < 1)
		scale = 1;
	return scale;
}
static float Q2S_DefaultScale(void)
{
	int i = q2s.w / 640;
	int j = q2s.h / 240;
	if (i > j)
		i = j;
	if (i < 1)
		i = 1;
	return i;
}
static float Q2S_Scale(cvar_t *var)
{
	if (var->value < 0)
		return Q2S_DefaultScale();
	return Q2S_ClampScale(var->value);
}
float CLQ2_MenuScale(void)
{
	Q2S_FullScreen();
	return Q2S_Scale(&r_menuscale);
}

//yquake2's Draw_FindPic naming: "name" is pics/name.pcx, "/path/name.ext" is that path
static mpic_t *Q2S_FindPic(const char *name, int *w, int *h)
{
	mpic_t *pic;
	if (*name == '/' || *name == '\\')
		pic = R2D_SafeCachePic(name+1);
	else
		pic = Sbar_Q2CachePic(name);
	if (!pic || R_GetShaderSizes(pic, w, h, false) <= 0)
		return NULL;
	return pic;
}
void CLQ2_GetPicSize(const char *name, int *w, int *h)
{
	if (!Q2S_FindPic(name, w, h))
		*w = *h = 0;
}
static void Q2S_DrawPic(float x, float y, const char *name, float scale)
{
	int w, h;
	mpic_t *pic = Q2S_FindPic(name, &w, &h);
	if (pic)
		R2D_ScalePic(q2s.vx + x*q2s.sx, q2s.vy + y*q2s.sy, w*scale*q2s.sx, h*scale*q2s.sy, pic);
}
void CLQ2_DrawPicScaled(float x, float y, const char *name, float scale)
{
	Q2S_FullScreen();
	Q2S_DrawPic(x, y, name, scale);
}

//one Quake 2 charset glyph (high bit = the alternate half), as yquake2's Draw_CharScaled
static void Q2S_DrawChar(float x, float y, int num, float scale)
{
	float px, py;
	num &= 255;
	if ((num & 127) == 32)
		return;	//space
	if (y <= -8*scale)
		return;	//totally off screen
	Font_BeginScaledString(font_default, q2s.vx + x*q2s.sx, q2s.vy + y*q2s.sy, 8*scale*q2s.sx, 8*scale*q2s.sy, &px, &py);
	Font_DrawScaleChar(px, py, CON_WHITEMASK, 0xe000|num);
	Font_EndString(font_default);
}
void CLQ2_DrawCharScaled(float x, float y, int num, float scale)
{
	Q2S_FullScreen();
	Q2S_DrawChar(x, y, num, scale);
}
static void Q2S_DrawString(float x, float y, const char *s, int xor, float scale)
{
	while (*s)
	{
		Q2S_DrawChar(x, y, *(const unsigned char*)s ^ xor, scale);
		x += 8*scale;
		s++;
	}
}
//yquake2's DrawHUDStringScaled: lines centred within centerwidth (unscaled) when it is set
static void Q2S_DrawHUDString(const char *string, float x, float y, int centerwidth, int xor, float scale)
{
	float margin = x;
	int i;
	while (*string)
	{
		char line[1024];
		int width = 0;
		while (*string && *string != '\n' && width < (int)sizeof(line)-1)
			line[width++] = *string++;
		line[width] = 0;
		if (centerwidth)
			x = margin + (centerwidth - width * 8)*scale / 2;
		else
			x = margin;
		for (i = 0; i < width; i++)
		{
			Q2S_DrawChar(x, y, (unsigned char)line[i] ^ xor, scale);
			x += 8*scale;
		}
		if (*string)
		{
			string++;	//skip the \n
			y += 8 * scale;
		}
	}
}

/*
=====================================================================
Status bar and layouts (yquake2's SCR_ExecuteLayoutString)
=====================================================================
*/
#define STAT_MINUS		10	//num frame for '-' stats digit
static const char *q2sb_nums[2][11] =
{
	{"num_0", "num_1", "num_2", "num_3", "num_4", "num_5", "num_6", "num_7", "num_8", "num_9", "num_minus"},
	{"anum_0", "anum_1", "anum_2", "anum_3", "anum_4", "anum_5", "anum_6", "anum_7", "anum_8", "anum_9", "anum_minus"}
};
#define CHARACTER_WIDTH 16

static void Q2S_DrawField(float x, float y, int color, int width, int value, float scale)
{
	char num[16], *ptr;
	int l, frame;

	if (width < 1)
		return;
	if (width > 5)
		width = 5;

	Q_snprintfz(num, sizeof(num), "%i", value);
	l = strlen(num);
	if (l > width)
		l = width;
	x += (2 + CHARACTER_WIDTH * (width - l)) * scale;

	for (ptr = num; *ptr && l; ptr++, l--)
	{
		if (*ptr == '-')
			frame = STAT_MINUS;
		else
			frame = *ptr - '0';
		Q2S_DrawPic(x, y, q2sb_nums[color][frame], scale);
		x += CHARACTER_WIDTH * scale;
	}
}

static void Q2S_ExecuteLayoutString(const char *s, q2player_state_t *ps, int playernum)
{
	float x = 0, y = 0;
	float scale = Q2S_Scale(&r_hudscale);
	const short *stats = ps->stats;
	int nstats = countof(ps->stats);

	if (cls.state != ca_active || !*s)
		return;
	if (r_hudscale.value == 0)
		return;	//yquake2: r_hudscale 0 hides the hud

	while (s)
	{
		s = COM_Parse(s);

		if (!strcmp(com_token, "xl"))
		{
			s = COM_Parse(s);
			x = scale * atoi(com_token);
			continue;
		}
		if (!strcmp(com_token, "xr"))
		{
			s = COM_Parse(s);
			x = q2s.w + scale * atoi(com_token);
			continue;
		}
		if (!strcmp(com_token, "xv"))
		{
			s = COM_Parse(s);
			x = q2s.w / 2 - scale * 160 + scale * atoi(com_token);
			continue;
		}
		if (!strcmp(com_token, "yt"))
		{
			s = COM_Parse(s);
			y = scale * atoi(com_token);
			continue;
		}
		if (!strcmp(com_token, "yb"))
		{
			s = COM_Parse(s);
			y = q2s.h + scale * atoi(com_token);
			continue;
		}
		if (!strcmp(com_token, "yv"))
		{
			s = COM_Parse(s);
			y = q2s.h / 2 - scale * 120 + scale * atoi(com_token);
			continue;
		}

		if (!strcmp(com_token, "pic"))
		{	//draw a pic from a stat number
			int index, value;
			const char *text;
			s = COM_Parse(s);
			index = atoi(com_token);
			if (index < 0 || index >= nstats)
				continue;	//yquake2 skips what the original turned into a fatal error
			value = stats[index];
			if (value < 0 || value >= Q2MAX_IMAGES)
				continue;
			text = Get_Q2ConfigString(Q2CS_IMAGES + value);
			if (*text)
				Q2S_DrawPic(x, y, text, scale);
			continue;
		}

		if (!strcmp(com_token, "client"))
		{	//draw a deathmatch client block
			int score, ping, time, value;
			const char *icon;
			int w, h;

			s = COM_Parse(s);
			x = q2s.w / 2 - scale * 160 + scale * atoi(com_token);
			s = COM_Parse(s);
			y = q2s.h / 2 - scale * 120 + scale * atoi(com_token);

			s = COM_Parse(s);
			value = atoi(com_token);
			if (value >= MAX_CLIENTS || value < 0)
				continue;

			s = COM_Parse(s);
			score = atoi(com_token);
			s = COM_Parse(s);
			ping = atoi(com_token);
			s = COM_Parse(s);
			time = atoi(com_token);

			Q2S_DrawString(x + scale * 32, y, cl.players[value].name, 0x80, scale);
			Q2S_DrawString(x + scale * 32, y + scale * 8, "Score: ", 0x80, scale);
			Q2S_DrawString(x + scale * (32 + 7 * 8), y + scale * 8, va("%i", score), 0x80, scale);
			Q2S_DrawString(x + scale * 32, y + scale * 16, va("Ping:  %i", ping), 0, scale);
			Q2S_DrawString(x + scale * 32, y + scale * 24, va("Time:  %i", time), 0, scale);

			//their skin's icon, or the base client's when that has none
			icon = va("/players/%s_i.pcx", InfoBuf_ValueForKey(&cl.players[value].userinfo, "skin"));
			CLQ2_GetPicSize(icon, &w, &h);
			if (!w)
				icon = "/players/male/grunt_i.pcx";
			Q2S_DrawPic(x, y, icon, scale);
			continue;
		}

		if (!strcmp(com_token, "ctf"))
		{	//draw a ctf client block
			int score, ping, value;
			char block[80];

			s = COM_Parse(s);
			x = q2s.w / 2 - scale * 160 + scale * atoi(com_token);
			s = COM_Parse(s);
			y = q2s.h / 2 - scale * 120 + scale * atoi(com_token);

			s = COM_Parse(s);
			value = atoi(com_token);
			if (value >= MAX_CLIENTS || value < 0)
				continue;

			s = COM_Parse(s);
			score = atoi(com_token);
			s = COM_Parse(s);
			ping = atoi(com_token);
			if (ping > 999)
				ping = 999;

			Q_snprintfz(block, sizeof(block), "%3d %3d %-12.12s", score, ping, cl.players[value].name);
			Q2S_DrawString(x, y, block, (value == playernum)?0x80:0, scale);
			continue;
		}

		if (!strcmp(com_token, "picn"))
		{	//draw a pic from a name
			s = COM_Parse(s);
			Q2S_DrawPic(x, y, com_token, scale);
			continue;
		}

		if (!strcmp(com_token, "num"))
		{	//draw a number
			int width, index;
			s = COM_Parse(s);
			width = atoi(com_token);
			s = COM_Parse(s);
			index = atoi(com_token);
			Q2S_DrawField(x, y, 0, width, (index >= 0 && index < nstats)?stats[index]:0, scale);
			continue;
		}

		if (!strcmp(com_token, "hnum"))
		{	//health number
			int color, value = stats[Q2STAT_HEALTH];
			if (value > 25)
				color = 0;	//green
			else if (value > 0)
				color = (cl.q2frame.serverframe >> 2) & 1;	//flash
			else
				color = 1;
			if (stats[Q2STAT_FLASHES] & 1)
				Q2S_DrawPic(x, y, "field_3", scale);
			Q2S_DrawField(x, y, color, 3, value, scale);
			continue;
		}

		if (!strcmp(com_token, "anum"))
		{	//ammo number
			int color, value = stats[Q2STAT_AMMO];
			if (value > 5)
				color = 0;	//green
			else if (value >= 0)
				color = (cl.q2frame.serverframe >> 2) & 1;	//flash
			else
				continue;	//negative number = don't show
			if (stats[Q2STAT_FLASHES] & 4)
				Q2S_DrawPic(x, y, "field_3", scale);
			Q2S_DrawField(x, y, color, 3, value, scale);
			continue;
		}

		if (!strcmp(com_token, "rnum"))
		{	//armor number
			int value = stats[Q2STAT_ARMOR];
			if (value < 1)
				continue;
			if (stats[Q2STAT_FLASHES] & 2)
				Q2S_DrawPic(x, y, "field_3", scale);
			Q2S_DrawField(x, y, 0, 3, value, scale);
			continue;
		}

		if (!strcmp(com_token, "stat_string"))
		{
			int index;
			s = COM_Parse(s);
			index = atoi(com_token);
			if (index < 0 || index >= nstats)
				continue;
			index = stats[index];
			if (index < 0 || index >= Q2MAX_CONFIGSTRINGS)
				continue;
			Q2S_DrawString(x, y, Get_Q2ConfigString(index), 0, scale);
			continue;
		}

		if (!strcmp(com_token, "cstring"))
		{
			s = COM_Parse(s);
			Q2S_DrawHUDString(com_token, x, y, 320, 0, scale);
			continue;
		}
		if (!strcmp(com_token, "string"))
		{
			s = COM_Parse(s);
			Q2S_DrawString(x, y, com_token, 0, scale);
			continue;
		}
		if (!strcmp(com_token, "cstring2"))
		{
			s = COM_Parse(s);
			Q2S_DrawHUDString(com_token, x, y, 320, 0x80, scale);
			continue;
		}
		if (!strcmp(com_token, "string2"))
		{
			s = COM_Parse(s);
			Q2S_DrawString(x, y, com_token, 0x80, scale);
			continue;
		}

		if (!strcmp(com_token, "if"))
		{
			int index, value;
			s = COM_Parse(s);
			index = atoi(com_token);
			value = (index >= 0 && index < nstats)?stats[index]:0;
			if (!value)
			{	//skip to endif
				while (s && strcmp(com_token, "endif"))
					s = COM_Parse(s);
			}
			continue;
		}
		//endif, or an empty token: nothing to do
	}
}

/*
=====================================================================
Inventory (yquake2's CL_DrawInventory)
=====================================================================
*/
#define DISPLAY_ITEMS 17
static void Q2S_DrawInventory(int seat, q2player_state_t *ps)
{
	int i, num = 0, selected_num = 0, top, item;
	int selected = ps->stats[Q2STAT_SELECTED_ITEM];
	int index[Q2MAX_ITEMS];
	char string[1024], cmd[1024];
	float x, y;
	float scale = Q2S_Scale(&r_hudscale);
	int keys[1], keymods[1];
	const char *bind;
	int deflang = TL_FindLanguage("");	//english: the names "use" takes

	for (i = 0; i < Q2MAX_ITEMS; i++)
	{
		if (i == selected)
			selected_num = num;
		if (cl.inventory[seat][i])
			index[num++] = i;
	}

	//determine scroll point
	top = selected_num - DISPLAY_ITEMS / 2;
	if (num - top < DISPLAY_ITEMS)
		top = num - DISPLAY_ITEMS;
	if (top < 0)
		top = 0;

	x = (q2s.w - scale*256) / 2;
	y = (q2s.h - scale*240) / 2;

	Q2S_DrawPic(x, y + scale*8, "inventory", scale);

	y += scale*24;
	x += scale*24;

	Q2S_DrawString(x, y, "hotkey ### item", 0, scale);
	Q2S_DrawString(x, y + scale*8, "------ --- ----", 0, scale);
	y += scale*16;

	for (i = top; i < num && i < top + DISPLAY_ITEMS; i++)
	{
		item = index[i];
		//search for a binding
		Q_snprintfz(cmd, sizeof(cmd), "use %s", TL_Translate(deflang, Get_Q2ConfigString(Q2CS_ITEMS + item)));
		if (M_FindKeysForCommand(0, 0, cmd, keys, keymods, countof(keys)))
			bind = Key_KeynumToString(keys[0], keymods[0]);
		else
			bind = "";

		Q_snprintfz(string, sizeof(string), "%6.6s %3i %s", bind, cl.inventory[seat][item], TL_Translate(com_language, Get_Q2ConfigString(Q2CS_ITEMS + item)));

		if (item != selected)
			Q2S_DrawString(x, y, string, 0x80, scale);	//yquake2 sets the high bit of every unselected row
		else
		{
			if ((int)(realtime * 10) & 1)	//a blinky cursor by the selected item
				Q2S_DrawChar(x - scale*8, y, 15, scale);
			Q2S_DrawString(x, y, string, 0, scale);
		}
		y += scale*8;
	}
}

/*
=====================================================================
For the rerelease's cgame (clq2_cgame.c): it draws in units of the HUD's scale (r_hudscale) within the area,
as Q2REPRO hands it a pre-scaled rect.
=====================================================================
*/
static float q2cgscale = 1;
//the area, and its size in the cgame's units. false when the hud is hidden (r_hudscale 0)
qboolean CLQ2_CGBegin(const vrect_t *area, int *w, int *h)
{
	Q2S_SetArea(area->x, area->y, area->width, area->height);
	if (!r_hudscale.value)
		return false;
	q2cgscale = Q2S_Scale(&r_hudscale);
	*w = q2s.w / q2cgscale;
	*h = q2s.h / q2cgscale;
	return true;
}
qboolean CLQ2_CGPicSize(const char *name, int *w, int *h)
{
	return Q2S_FindPic(name, w, h) != NULL;
}
//is there such a picture (waiting for it to load, where CLQ2_CGPicSize would say no meanwhile)
qboolean CLQ2_CGPicExists(const char *name)
{
	mpic_t *pic;
	if (*name == '/' || *name == '\\')
		pic = R2D_SafeCachePic(name+1);
	else
		pic = Sbar_Q2CachePic(name);
	return pic && R_GetShaderSizes(pic, NULL, NULL, true) > 0;
}
void CLQ2_CGPic(float x, float y, float w, float h, const char *name, const float *rgba)
{
	int pw, ph;
	mpic_t *pic = Q2S_FindPic(name, &pw, &ph);
	if (!pic)
		return;
	R2D_ImageColours(rgba[0], rgba[1], rgba[2], rgba[3]);
	R2D_ScalePic(q2s.vx + x*q2cgscale*q2s.sx, q2s.vy + y*q2cgscale*q2s.sy, w*q2cgscale*q2s.sx, h*q2cgscale*q2s.sy, pic);
	R2D_ImageColours(1, 1, 1, 1);
}
//a character (a unicode codepoint, or 0xe0xx for Quake 2's charset) size*size of the cgame's units
float CLQ2_CGChar(float x, float y, float size, unsigned int codepoint, const float *rgba)
{
	float px, py;
	vec4_t c;
	Vector4Copy(rgba, c);
	Font_InvalidateColour(c);
	Font_BeginScaledString(font_default, q2s.vx + x*q2cgscale*q2s.sx, q2s.vy + y*q2cgscale*q2s.sy, size*q2cgscale*q2s.sx, size*q2cgscale*q2s.sy, &px, &py);
	Font_DrawScaleChar(px, py, CON_WHITEMASK, codepoint);
	Font_EndString(font_default);
	Vector4Set(c, 1, 1, 1, 1);
	Font_InvalidateColour(c);
	return size;
}

//the status bar, the layout (when the server says to show it) and the inventory, for one seat
void CLQ2_DrawHUD(int seat, const vrect_t *area)
{
	q2player_state_t *ps = &cl.q2frame.seat[seat].playerstate;
	int playernum = cl.q2frame.seat[seat].clientnum;

	if (CLQ2CG_DrawHUD(seat, area))
	{	//the rerelease's own hud, and KEX's weapon wheel
		CLQ2CG_WheelDraw(seat, area);
		return;
	}

	Q2S_SetArea(area->x, area->y, area->width, area->height);
	R2D_ImageColours(1, 1, 1, 1);

	Q2S_ExecuteLayoutString(cl.q2statusbar, ps, playernum);
	if (ps->stats[Q2STAT_LAYOUTS] & 1)
		Q2S_ExecuteLayoutString(cl.q2layout[seat], ps, playernum);
	if (ps->stats[Q2STAT_LAYOUTS] & 2)
		Q2S_DrawInventory(seat, ps);
}

/*
=====================================================================
Crosshair (yquake2's SCR_DrawCrosshair): the ch1-ch3 pics, centred in the view
=====================================================================
*/
void CLQ2_DrawCrosshair(const vrect_t *view)
{
	char pic[16];
	int w, h, n = crosshair.ival;
	float scale;

	if (n <= 0)
		return;
	if (CLQ2CG_HideCrosshair(0))
		return;	//the rerelease's cgame says not now (cameras, intermissions, menus)
	if (n > 3)
		n = 3;	//Quake 2 has three
	Q_snprintfz(pic, sizeof(pic), "ch%i", n);

	Q2S_SetArea(view->x, view->y, view->width, view->height);
	CLQ2_GetPicSize(pic, &w, &h);
	if (!w)
		return;
	if (crosshair_scale.value < 0)
		scale = Q2S_DefaultScale();
	else
		scale = Q2S_ClampScale(crosshair_scale.value);

	R2D_ImageColours(crosshair_color_r.value, crosshair_color_g.value, crosshair_color_b.value, 1);
	Q2S_DrawPic((q2s.w - w*scale)/2, (q2s.h - h*scale)/2, pic, scale);
	R2D_ImageColours(1, 1, 1, 1);
}

/*
=====================================================================
Centerprint (yquake2's SCR_CenterPrint / SCR_DrawCenterString)
=====================================================================
*/
static char q2_centerstring[1024];
static float q2_centertime_off;
static int q2_center_lines;

void CLQ2_CenterPrint(const char *str)
{
	const char *s;
	static const char border[] = "\n\n\35\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\37\n\n";

	if (CLQ2CG_CenterPrint(0, str, false))
		return;	//the rerelease's cgame draws it

	Q_strncpyz(q2_centerstring, str, sizeof(q2_centerstring));
	q2_centertime_off = scr_centertime.value;

	//count the number of lines for centering
	q2_center_lines = 1;
	for (s = str; *s; s++)
		if (*s == '\n')
			q2_center_lines++;

	//echo it to the console, centred within 40 columns between two rules
	Con_Printf("%s", border);
	s = str;
	for (;;)
	{
		char line[64];
		int i, j, l;

		for (l = 0; l < 40; l++)	//scan the width of the line
			if (s[l] == '\n' || !s[l])
				break;
		for (i = 0; i < (40 - l) / 2; i++)
			line[i] = ' ';
		for (j = 0; j < l; j++)
			line[i++] = s[j];
		line[i] = '\n';
		line[i+1] = 0;
		Con_Printf("%s", line);

		while (*s && *s != '\n')
			s++;
		if (!*s)
			break;
		s++;	//skip the \n
	}
	Con_Printf("%s", border);
	Con_ClearNotify();
}

void CLQ2_DrawCenterString(void)
{
	const char *start = q2_centerstring;
	float scale, x, y;
	int l, j;

	q2_centertime_off -= host_frametime;
	if (q2_centertime_off <= 0)
		return;

	Q2S_FullScreen();
	scale = Q2S_Scale(&r_consolescale);

	if (q2_center_lines <= 4)
		y = (q2s.h * 0.35) / scale;
	else
		y = 48 / scale;

	for (;;)
	{
		for (l = 0; l < 40; l++)	//scan the width of the line
			if (start[l] == '\n' || !start[l])
				break;
		x = ((q2s.w / scale) - (l * 8)) / 2;
		for (j = 0; j < l; j++, x += 8)
			Q2S_DrawChar(x * scale, y * scale, (unsigned char)start[j], scale);
		y += 8;

		while (*start && *start != '\n')
			start++;
		if (!*start)
			break;
		start++;	//skip the \n
	}
}

void CLQ2_ClearCenterString(void)
{
	q2_centertime_off = 0;
}

/*
=====================================================================
Pause and net plaques (yquake2's SCR_DrawPause / SCR_DrawNet)
=====================================================================
*/
void CLQ2_DrawPause(void)
{
	int w, h;
	float scale = CLQ2_MenuScale();
	CLQ2_GetPicSize("pause", &w, &h);
	if (w)
		Q2S_DrawPic((q2s.w - w * scale) / 2, q2s.h / 2 + 8 * scale, "pause", scale);
}

void CLQ2_DrawNet(const vrect_t *view)
{
	float scale = CLQ2_MenuScale();
	Q2S_DrawPic(view->x/q2s.sx + 64 * scale, view->y/q2s.sy, "net", scale);
}

#endif
