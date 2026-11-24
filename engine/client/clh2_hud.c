/*
Hexen II's status bar and screen texts, ported from uHexen2 (sbar.c, gl_screen.c, cl_inlude.c), which keeps
Raven's: the bar that slides up and down (the full bar, the inventory panel on +showinfo, the small one at
viewsize 110-120, nothing past 120), the health chain, the artifact inventory in the order things were
picked up, the active rings and artifacts, the puzzle pieces (+showdm), the objectives (+infoplaque), the
centered texts and plaques in Hexen II's text box, and the intermissions -- with the three part ending.

Everything is laid out in Hexen II's 320 wide coordinates, centered on the seat's part of the screen.

Copyright (C) 1996-1997  Id Software, Inc.
Copyright (C) 1997-1998  Raven Software Corp.
Copyright (C) uHexen2 (Hammer of Thyrion) contributors.

This program is free software; you can redistribute it and/or modify it under the terms of the GNU
General Public License as published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.
*/
#include "quakedef.h"

#ifdef HEXEN2
#include "shader.h"

void Draw_TinyString (float x, float y, const qbyte *str);	//sbar.c

#define BAR_TOP_HEIGHT			46.0
#define BAR_BOTTOM_HEIGHT		98.0
#define BAR_TOTAL_HEIGHT		(BAR_TOP_HEIGHT+BAR_BOTTOM_HEIGHT)
#define BAR_BUMP_HEIGHT			23.0
#define INVENTORY_DISPLAY_TIME	4
#define INV_MAX_ICON			6	//inventory icons shown at once
#define MAX_INVENTORY			15

#define RING_FLIGHT				1
#define RING_WATER				2
#define RING_TURNING			8
#define ART_HASTE				1
#define ART_INVINCIBILITY		2
#define ART_TOMEOFPOWER			4
#define FL_SPECIAL_ABILITY1		4194304
#define FL_SPECIAL_ABILITY2		8388608
#define ABILITIES_STR_INDEX		400

#define PLAQUE_WIDTH			26
#define MAXLINES				27

static cvar_t barspeed	= CVARD("barspeed", "5", "Hexen II: how fast the status bar slides up and down.");

extern cvar_t scr_viewsize, scr_centertime;

static const char *h2classnames[] = {"Paladin", "Crusader", "Necromancer", "Assassin", "Demoness"};
//armour class each piece gives each class (helmet, amulet, breastplate, bracer: STAT_H2_ARMOUR1-4)
static const int h2armourac[5][4] =
{	//helmet amulet breastplate bracer
	{4,		8,		2,			6},	//Paladin
	{2,		4,		6,			8},	//Crusader
	{6,		2,		8,			4},	//Necromancer
	{8,		6,		4,			2},	//Assassin
	{8,		6,		4,			2},	//Demoness
};

static struct h2seat_s
{
	float barheight, bartarget;		//how much of the bar is up, from the bottom of the screen
	float chainpos;					//the health gem, sliding along the chain
	int lastviewsize;
	qboolean lastintermission;

	int inv_order[MAX_INVENTORY];	//artifacts in the order they were picked up
	int inv_count, inv_selected, inv_startpos, inv_sent;
	qboolean inv_flg;				//the inventory panel is up
	float inv_hidetime;

	qboolean showinfo;				//+showinfo: the whole bar
	qboolean showdm;				//+showdm: the puzzle pieces
	qboolean infoplaque;			//+infoplaque: the objectives
} h2seat[MAX_SPLITS];

static int H2_Seat(playerview_t *pv)
{
	int seat = pv - cl.playerview;
	return (seat >= 0 && seat < MAX_SPLITS) ? seat : 0;
}

static int H2_Count(playerview_t *pv, int artifact)
{
	return pv->stats[STAT_H2_CNT_FIRST+artifact];
}

static int H2_Class(playerview_t *pv)
{
	int c = cl.players[pv->playernum].h2playerclass;
	return (c >= 1 && c <= 5) ? c : 1;	//default to the paladin, as uHexen2
}

/* ---------------------------------------------------------------- drawing in Hexen II's coordinates */

static float sbx, sby;	//the bar: its left edge (320 wide, centered) and the bottom of the screen
static vrect_t h2rect;	//the seat's part of the screen

static void H2_Pic(float x, float y, const char *name)
{
	mpic_t *pic = R2D_SafeCachePic(name);
	int w, h;
	if (pic && R_GetShaderSizes(pic, &w, &h, true) > 0)
		R2D_ScalePic(x, y, w, h, pic);
}
static void SB_Pic(float x, float y, float barheight, const char *name)	//relative to the bar's top
{
	H2_Pic(sbx + x, sby - (int)barheight + y, name);
}
static void H2_MenuPic(float x, float y, const char *name)	//menu coordinates: 320 wide, centered
{
	H2_Pic(h2rect.x + (h2rect.width-320)/2 + x, h2rect.y + y, name);
}

static void H2_SmallString(float x, float y, const char *str)	//the tiny font, 6 apart
{
	char c[2] = {0, 0};
	for (; *str; str++, x += 6)
	{
		c[0] = *str;
		if (*c != ' ')
			Draw_TinyString(x, y, (const qbyte*)c);
	}
}
static void SB_SmallString(float x, float y, float barheight, const char *str)
{
	H2_SmallString(sbx + x, sby - (int)barheight + y, str);
}

/* the numbers are the wad's num_0-9 and num_minus */
static void SB_DrawNum(float x, float y, float barheight, int number, int digits)
{
	char str[16], *ptr;
	int l, w, h;
	mpic_t *pic;
	l = Q_snprintfz(str, sizeof(str), "%i", number);
	ptr = str;
	if (l > digits)
		ptr += l-digits;
	if (l < digits)
		x += ((digits-l)*13)/2;
	for (; *ptr; ptr++, x += 13)
	{
		pic = R2D_SafePicFromWad((*ptr == '-') ? "num_minus" : va("num_%c", *ptr));
		if (pic && R_GetShaderSizes(pic, &w, &h, true) > 0)
			R2D_ScalePic(sbx + x, sby - (int)barheight + y, w, h, pic);
	}
}

static void H2_Print(float cx, float cy, const char *str, qboolean white)
{	//8 wide; Hexen II's red characters (its second set, chars+256) unless 'white' (the first set, tan).
	//FTE loads only the first set; the second is the same glyphs in red, so that is a red tint.
	float x = h2rect.x + (h2rect.width-320)/2 + cx;
	int px, py;
	for (; *str; str++, x += 8)
	{
		if (*str == ' ')
			continue;
		Font_BeginString(font_default, x, h2rect.y + cy, &px, &py);
		Font_DrawChar(px, py, white ? CON_WHITEMASK : (COLOR_RED << CON_FGSHIFT), 0xe000 | (*str & 0x7f));
		Font_EndString(font_default);
	}
}

static void H2_DrawTextBox(float x, float y, int width, int lines)
{
	float cx, cy;
	int n;

	cx = x;
	cy = y;
	H2_MenuPic(cx, cy, "gfx/box_tl.lmp");
	for (n = 0; n < lines; n++)
		H2_MenuPic(cx, cy += 8, "gfx/box_ml.lmp");
	H2_MenuPic(cx, cy+8, "gfx/box_bl.lmp");

	cx += 8;
	while (width > 0)
	{
		cy = y;
		H2_MenuPic(cx, cy, "gfx/box_tm.lmp");
		for (n = 0; n < lines; n++)
			H2_MenuPic(cx, cy += 8, (n == 1) ? "gfx/box_mm2.lmp" : "gfx/box_mm.lmp");
		H2_MenuPic(cx, cy+8, "gfx/box_bm.lmp");
		width -= 2;
		cx += 16;
	}

	cy = y;
	H2_MenuPic(cx, cy, "gfx/box_tr.lmp");
	for (n = 0; n < lines; n++)
		H2_MenuPic(cx, cy += 8, "gfx/box_mr.lmp");
	H2_MenuPic(cx, cy+8, "gfx/box_br.lmp");
}

/* Hexen II's line breaking: at a width, at '@' (FTE's string tables already made those newlines) */
static int lines, StartC[MAXLINES], EndC[MAXLINES];
static void FindTextBreaks(const char *message, int Width)
{
	int pos, start, lastspace, oldlast;

	lines = pos = start = 0;
	lastspace = -1;

	for (;;)
	{
		qboolean brk = message[pos] == '@' || message[pos] == '\n';
		if (pos-start >= Width || brk || message[pos] == 0)
		{
			oldlast = lastspace;
			if (brk || lastspace == -1 || message[pos] == 0)
				lastspace = pos;

			StartC[lines] = start;
			EndC[lines] = lastspace;
			lines++;
			if (lines == MAXLINES)
				return;
			if (brk)
				start = pos + 1;
			else if (oldlast == -1)
				start = lastspace;
			else
				start = lastspace + 1;

			lastspace = -1;
		}

		if (message[pos] == 32)
			lastspace = pos;
		else if (message[pos] == 0)
			break;

		pos++;
	}
}
static const char *TextLine(const char *message, int line, char *temp, size_t tempsize)
{
	int cnt = EndC[line] - StartC[line];
	if (cnt < 0)
		cnt = 0;
	if (cnt >= tempsize)
		cnt = tempsize-1;
	memcpy(temp, message + StartC[line], cnt);
	temp[cnt] = 0;
	return temp;
}

/* ---------------------------------------------------------------- the inventory */

/* what the status bar shows as selected goes to the server's self.inventory, which drop (44) and use (23) act on */
static void Inv_Send(int seat, struct h2seat_s *s)
{
	int inv = (s->inv_selected >= 0 && s->inv_count > 0) ? s->inv_order[s->inv_selected] + 1 : 0;
	if (inv == s->inv_sent || cls.state != ca_active)
		return;
	s->inv_sent = inv;
	CL_SendSeatClientCommand(true, seat, "invselect %i", inv);
}

/* uHexen2's SB_InvChanged: keep the order the artifacts were picked up in, drop the ones gone */
static void Inv_Changed(playerview_t *pv, struct h2seat_s *s)
{
	int counter, position;
	qboolean examined[MAX_INVENTORY];

	memset(examined, 0, sizeof(examined));
	for (counter = position = 0; counter < s->inv_count; counter++)
	{
		if (H2_Count(pv, s->inv_order[counter]) > 0)
		{
			s->inv_order[position] = s->inv_order[counter];
			examined[s->inv_order[position]] = true;
			position++;
		}
	}
	for (counter = 0; counter < MAX_INVENTORY; counter++)
	{
		if (!examined[counter] && H2_Count(pv, counter) > 0)
			s->inv_order[position++] = counter;
	}

	s->inv_count = position;
	if (s->inv_selected >= s->inv_count)
		s->inv_selected = s->inv_count-1;
	if (s->inv_count && s->inv_selected < 0)
		s->inv_selected = 0;

	if (s->inv_count <= 1)
		s->inv_startpos = 0;
	else if (s->inv_startpos >= s->inv_count)
		s->inv_startpos = s->inv_selected;
	else
	{
		position = s->inv_selected - s->inv_startpos;
		if (position < 0)
			position += s->inv_count;
		if (position >= INV_MAX_ICON)
			s->inv_startpos = s->inv_selected;
	}
}

static void DrawBarArtifactIcon(playerview_t *pv, float x, float y, float barheight, int artifact)
{
	int count;
	if (artifact < 0 || artifact >= MAX_INVENTORY)
		return;
	SB_Pic(x, y, barheight, va("gfx/arti%02d.lmp", artifact));
	if ((count = H2_Count(pv, artifact)) > 0)
	{
		if (count >= 10)
			SB_Pic(x+16, y+21, barheight, va("gfx/artinum%d.lmp", (count%100)/10));
		SB_Pic(x+20, y+21, barheight, va("gfx/artinum%d.lmp", count%10));
	}
}

static void DrawArtifactInventory(playerview_t *pv, struct h2seat_s *s)
{
	int i;
	float x, y;

	if (s->inv_hidetime < cl.time)
		s->inv_flg = false;
	if (!s->inv_flg || !s->inv_count)
		return;

	y = (s->barheight < 0) ? s->barheight-34 : -37;
	for (i = 0, x = 64; i < INV_MAX_ICON && i < s->inv_count; i++, x += 33)
	{
		if ((s->inv_startpos + i) % s->inv_count == s->inv_selected)
			SB_Pic(x+9, y-12, s->barheight, "gfx/artisel.lmp");
		DrawBarArtifactIcon(pv, x, y, s->barheight, s->inv_order[(s->inv_startpos + i) % s->inv_count]);
	}
}

/* ---------------------------------------------------------------- the bar */

static void DrawActiveRings(playerview_t *pv, int *ring_row)
{
	int flag = pv->stats[STAT_H2_RINGS_ACTIVE];
	int frame = 1 + ((int)(cl.time * 16) & 15);
	float x = h2rect.x + h2rect.width - 50;

	*ring_row = 1;
	if (flag & RING_TURNING)
	{
		H2_Pic(x, h2rect.y + *ring_row, va("gfx/rngtrn%d.lmp", frame));
		*ring_row += 33;
	}
	if (flag & RING_WATER)
	{
		H2_Pic(x, h2rect.y + *ring_row, va("gfx/rngwtr%d.lmp", frame));
		*ring_row += 33;
	}
	if (flag & RING_FLIGHT)
	{
		H2_Pic(x, h2rect.y + *ring_row, va("gfx/rngfly%d.lmp", frame));
		*ring_row += 33;
	}
}

static void DrawActiveArtifacts(playerview_t *pv, int ring_row)
{
	int flag = pv->stats[STAT_H2_ARTIFACT_ACTIVE];
	int frame = 1 + ((int)(cl.time * 16) & 15);
	int art_col = (ring_row != 1) ? 100 : 50;
	float x = h2rect.x + h2rect.width;

	if (flag & ART_TOMEOFPOWER)
	{
		H2_Pic(x-art_col, h2rect.y + 1, va("gfx/pwrbook%d.lmp", frame));
		art_col += 50;
	}
	if (flag & ART_HASTE)
	{
		H2_Pic(x-art_col, h2rect.y + 1, va("gfx/durhst%d.lmp", frame));
		art_col += 50;
	}
	if (flag & ART_INVINCIBILITY)
	{
		H2_Pic(x-art_col, h2rect.y + 1, va("gfx/durshd%d.lmp", frame));
		art_col += 50;
	}
}

static int CalcAC(playerview_t *pv)
{
	int i, a = 0, pclass = H2_Class(pv)-1;
	for (i = 0; i < 4; i++)
		if (pv->statsf[STAT_H2_ARMOUR1+i] > 0)
		{
			a += h2armourac[pclass][i];
			a += pv->statsf[STAT_H2_ARMOUR1+i]/5;
		}
	return a;
}

/* the health gem slides along the chain (uHexen2's SetChainPosition) */
static void SetChainPosition(struct h2seat_s *s, float health, float maxhealth, qboolean immediate)
{
	float delta, target;
	if (maxhealth <= 0)
		maxhealth = 100;
	health = bound(0, health, maxhealth);
	target = (health*195)/maxhealth;
	if (fabs(s->chainpos-target) < 0.1)
		return;
	if (immediate)
		s->chainpos = target;
	if (s->chainpos < target)
	{
		delta = ((target-s->chainpos)*5)*host_frametime;
		s->chainpos = min(s->chainpos + max(delta, 0.5), target);
	}
	else if (s->chainpos > target)
	{
		delta = ((s->chainpos-target)*5)*host_frametime;
		s->chainpos = max(s->chainpos - max(delta, 0.5), target);
	}
}

static void DrawMana(playerview_t *pv, float x, float numx, float barheight, int mana, const char *pic, const char *cover)
{
	int maxmana = pv->stats[STAT_H2_MAXMANA];
	if (maxmana <= 0)
		maxmana = 1;
	mana = bound(0, mana, maxmana);
	SB_SmallString(numx, 22, barheight, va("%03d", mana));
	if (mana)
	{
		SB_Pic(x, 26-(int)((mana*18.0)/(float)maxmana+0.5), barheight, pic);
		SB_Pic(x, 27, barheight, cover);
	}
}

static void DrawFullScreenInfo(playerview_t *pv, struct h2seat_s *s)
{
	float y = s->barheight-37;
	int maxmana = pv->stats[STAT_H2_MAXMANA];

	SB_Pic(3, y, s->barheight, "gfx/bmmana.lmp");
	SB_Pic(3, y+18, s->barheight, "gfx/gmmana.lmp");
	SB_SmallString(10, y+6, s->barheight, va("%03d", bound(0, pv->stats[STAT_H2_BLUEMANA], maxmana)));
	SB_SmallString(10, y+18+6, s->barheight, va("%03d", bound(0, pv->stats[STAT_H2_GREENMANA], maxmana)));
	SB_DrawNum(38, y+18, s->barheight, pv->stats[STAT_HEALTH], 3);
	if (s->inv_selected >= 0 && s->inv_count)
		DrawBarArtifactIcon(pv, 288, y+7, s->barheight, s->inv_order[s->inv_selected]);
}

static void DrawLowerBar(playerview_t *pv, struct h2seat_s *s)
{
	static const char *rings[4][2] = {{"gfx/ring_f.lmp", "gfx/rhlthcvr.lmp"}, {"gfx/ring_w.lmp", "gfx/rhlthcvr.lmp"}, {"gfx/ring_t.lmp", "gfx/rhlthcvr.lmp"}, {"gfx/ring_r.lmp", "gfx/rhlthcv2.lmp"}};
	static const int ringx[4] = {6, 44, 81, 119}, ringbar[4] = {35, 73, 110, 148};
	static const int armourx[4] = {164, 205, 246, 285}, armourtx[4] = {168, 208, 249, 288};
	float bh = s->barheight;
	int i, piece, pclass = H2_Class(pv);
	const char *ability;

	SB_Pic(0, 46, bh, "gfx/btmbar1.lmp");
	SB_Pic(160, 46, bh, "gfx/btmbar2.lmp");

	SB_SmallString(11, 48, bh, h2classnames[pclass-1]);
	SB_SmallString(11, 58, bh, "int");
	SB_SmallString(33, 58, bh, va("%02d", pv->stats[STAT_H2_INTELLIGENCE]));
	SB_SmallString(11, 64, bh, "wis");
	SB_SmallString(33, 64, bh, va("%02d", pv->stats[STAT_H2_WISDOM]));
	SB_SmallString(11, 70, bh, "dex");
	SB_SmallString(33, 70, bh, va("%02d", pv->stats[STAT_H2_DEXTERITY]));
	SB_SmallString(58, 58, bh, "str");
	SB_SmallString(80, 58, bh, va("%02d", pv->stats[STAT_H2_STRENGTH]));
	SB_SmallString(58, 64, bh, "lvl");
	SB_SmallString(80, 64, bh, va("%02d", pv->stats[STAT_H2_LEVEL]));
	SB_SmallString(58, 70, bh, "exp");
	SB_SmallString(80, 70, bh, va("%06d", pv->stats[STAT_H2_EXPERIENCE]));

	SB_SmallString(11, 79, bh, "abilities");
	i = ABILITIES_STR_INDEX + (pclass-1)*2;
	if (pv->stats[STAT_H2_FLAGS] & FL_SPECIAL_ABILITY1)
	{
		ability = T_GetString(i);
		if (strcmp(ability, "BAD STRING"))
			SB_SmallString(8, 89, bh, ability);
	}
	if (pv->stats[STAT_H2_FLAGS] & FL_SPECIAL_ABILITY2)
	{
		ability = T_GetString(i+1);
		if (strcmp(ability, "BAD STRING"))
			SB_SmallString(8, 96, bh, ability);
	}

	SB_Pic(134, 50, bh, va("gfx/cport%d.lmp", pclass));

	for (i = 0; i < 4; i++)	//helmet, amulet, breastplate, bracer
		if (pv->statsf[STAT_H2_ARMOUR1+i] > 0)
		{
			SB_Pic(armourx[i], 115, bh, va("gfx/armor%d.lmp", i+1));
			SB_SmallString(armourtx[i], 136, bh, va("+%d", (int)pv->statsf[STAT_H2_ARMOUR1+i]));
		}

	for (i = 0; i < 4; i++)	//flight, water, turning, regeneration
		if (pv->statsf[STAT_H2_FLIGHT_T+i] > 0)
		{
			int ringhealth = min(100, (int)pv->statsf[STAT_H2_FLIGHT_T+i]);
			SB_Pic(ringx[i], 119, bh, rings[i][0]);
			SB_Pic(ringbar[i] - (int)(26 * (ringhealth/(float)100)), 142, bh, "gfx/ringhlth.lmp");
			SB_Pic(ringbar[i], 142, bh, rings[i][1]);
		}

	for (i = 0, piece = 0; i < 8; i++)
	{
		const char *p = pv->statsstr[STAT_H2_PUZZLE1+i];
		if (!p || !*p)
			continue;
		SB_Pic(194+(piece%4)*31, piece < 4 ? 51 : 82, bh, va("gfx/puzzle/%s.lmp", p));
		piece++;
	}
}

static void DrawTopBar(playerview_t *pv, struct h2seat_s *s)
{
	float bh = s->barheight;
	int health = pv->stats[STAT_HEALTH];

	SB_Pic(0, 0, bh, "gfx/topbar1.lmp");
	SB_Pic(160, 0, bh, "gfx/topbar2.lmp");

	DrawMana(pv, 190, 201, bh, pv->stats[STAT_H2_BLUEMANA], "gfx/bmana.lmp", "gfx/bmanacov.lmp");
	DrawMana(pv, 232, 243, bh, pv->stats[STAT_H2_GREENMANA], "gfx/gmana.lmp", "gfx/gmanacov.lmp");

	SB_DrawNum(58, 14, bh, (health < -99) ? -99 : health, 3);
	SetChainPosition(s, health, pv->stats[STAT_H2_MAXHEALTH], false);
	SB_Pic(45+((int)s->chainpos&7), 38, bh, "gfx/hpchain.lmp");
	SB_Pic(45+(int)s->chainpos, 36, bh, "gfx/hpgem.lmp");
	SB_Pic(43, 36, bh, "gfx/chnlcov.lmp");
	SB_Pic(267, 36, bh, "gfx/chnrcov.lmp");

	SB_DrawNum(105, 14, bh, CalcAC(pv), 2);

	if (s->barheight > BAR_TOP_HEIGHT)
		DrawLowerBar(pv, s);

	if (s->inv_selected >= 0 && s->inv_count)
		DrawBarArtifactIcon(pv, 144, 3, bh, s->inv_order[s->inv_selected]);
}

/* ---------------------------------------------------------------- the overlays */

static char *puzzlenames;
static unsigned int puzzlerestarts;
static const char *FindPuzzlePieceName(const char *which, char *name, size_t namesize)
{	//puzzles.txt: one "id name of the piece" a line (uHexen2's tolerant CL_LoadPuzzleStrings)
	const char *line, *e;
	size_t l = strlen(which);

	if (FS_Restarted(&puzzlerestarts) || !puzzlenames)
	{
		BZ_Free(puzzlenames);
		puzzlenames = FS_LoadMallocFile("puzzles.txt", NULL);
		if (!puzzlenames)
			puzzlenames = Z_StrDup("");
	}
	for (line = puzzlenames; *line; line = e)
	{
		while (*line == ' ' || *line == '\t' || *line == '\r' || *line == '\n')
			line++;
		for (e = line; *e && *e != '\n'; e++)
			;
		if (!Q_strncasecmp(line, which, l) && (line[l] == ' ' || line[l] == '\t'))
		{
			const char *n = line + l, *ne = e;
			while (*n == ' ' || *n == '\t')
				n++;
			while (ne > n && (ne[-1] == '\r' || ne[-1] == ' ' || ne[-1] == '\t'))
				ne--;
			if (ne > n)
			{
				Q_strncpyz(name, n, min(namesize, (size_t)(ne-n)+1));
				return name;
			}
		}
	}
	Q_strncpyz(name, "Unknown", namesize);
	return name;
}

static void PuzzlePieceOverlay(playerview_t *pv)
{
	int i, y = 40, piece = 0;
	char name[40];

	for (i = 0; i < 8; i++)
	{
		const char *p = pv->statsstr[STAT_H2_PUZZLE1+i];
		if (!p || !*p)
			continue;
		if (piece == 4)
			y = 40;
		FindPuzzlePieceName(p, name, sizeof(name));
		if (piece < 4)
		{
			H2_MenuPic(10, y, va("gfx/puzzle/%s.lmp", p));
			H2_Print(45, y, name, true);
		}
		else
		{
			H2_MenuPic(310-32, y, va("gfx/puzzle/%s.lmp", p));
			H2_Print(310-32-3-(strlen(name)*8), 18+y, name, true);
		}
		y += 32;
		piece++;
	}
}

static void InfoPlaque(playerview_t *pv)
{	//the objectives (uHexen2's UpdateInfoMessage and Info_Plaque_Draw)
	char message[1024], temp[80];
	int i, by;
	const char *obj;

	Q_strncpyz(message, "Objectives:", sizeof(message));
	for (i = 0; i < 64; i++)
		if (pv->stats[STAT_H2_OBJECTIVE1 + i/32] & (1u<<(i&31)))
		{
			obj = T_GetInfoString(i);
			if (!strcmp(obj, "BAD STRING"))
				continue;
			Q_strncatz(message, "@", sizeof(message));
			Q_strncatz(message, obj, sizeof(message));
		}

	FindTextBreaks(message, PLAQUE_WIDTH+4);
	if (lines == MAXLINES)
		lines = MAXLINES-1;
	by = (25-lines) * 8 / 2 + (h2rect.height - 200)/2;
	H2_DrawTextBox(15, by - 16, PLAQUE_WIDTH + 4 + 4, lines + 2);
	for (i = 0; i < lines; i++, by += 8)
	{
		TextLine(message, i, temp, sizeof(temp));
		H2_Print((40-strlen(temp)) * 8 / 2, by, temp, false);
	}
}

/* ---------------------------------------------------------------- the status bar */

void CLH2_DrawHUD(playerview_t *pv)
{
	int seat = H2_Seat(pv);
	struct h2seat_s *s = &h2seat[seat];
	int ring_row;
	float delta;
	qboolean intermission = cl.intermissionmode != IM_NONE;

	h2rect = r_refdef.grect;
	sbx = h2rect.x + (h2rect.width - 320)/2;
	sby = h2rect.y + h2rect.height;

	Inv_Changed(pv, s);
	if (!s->inv_flg || s->inv_hidetime < cl.time)
		Inv_Send(seat, s);	//as uHexen2 does once the panel closes

	//the bar sits where the viewsize says (uHexen2's SB_ViewSizeChanged)
	if (s->lastviewsize != scr_viewsize.ival || s->lastintermission != intermission || !s->bartarget)
	{
		s->lastviewsize = scr_viewsize.ival;
		s->lastintermission = intermission;
		if (s->showinfo && !intermission)
			s->bartarget = BAR_TOTAL_HEIGHT;
		else if (intermission || scr_viewsize.ival > 110)
			s->barheight = s->bartarget = -BAR_BUMP_HEIGHT;
		else
			s->barheight = s->bartarget = BAR_TOP_HEIGHT;
	}

	if (intermission)
		return;

	DrawArtifactInventory(pv, s);
	DrawActiveRings(pv, &ring_row);
	DrawActiveArtifacts(pv, ring_row);

	if (s->showdm && !cl.deathmatch)
		PuzzlePieceOverlay(pv);

	if (s->barheight < s->bartarget)
	{
		delta = ((s->bartarget-s->barheight)*barspeed.value) * host_frametime;
		s->barheight = min(s->barheight + max(delta, 0.5), s->bartarget);
	}
	else if (s->barheight > s->bartarget)
	{
		delta = ((s->barheight-s->bartarget)*barspeed.value) * host_frametime;
		s->barheight = max(s->barheight - max(delta, 0.5), s->bartarget);
	}

	SB_Pic(0, -23, s->barheight, "gfx/topbumpl.lmp");
	SB_Pic(138, -8, s->barheight, "gfx/topbumpm.lmp");
	SB_Pic(269, -23, s->barheight, "gfx/topbumpr.lmp");

	if (s->barheight < 0)
	{
		if (scr_viewsize.ival <= 120)
		{
			SetChainPosition(s, pv->stats[STAT_HEALTH], pv->stats[STAT_H2_MAXHEALTH], true);
			DrawFullScreenInfo(pv, s);
		}
	}
	else
		DrawTopBar(pv, s);

	if (s->infoplaque)
		InfoPlaque(pv);
}

/* the status bar's space at the bottom of the view (sb_lines) */
int CLH2_SbarLines(void)
{
	return (scr_viewsize.ival > 110) ? 0 : BAR_TOP_HEIGHT;
}

/* ---------------------------------------------------------------- the commands */

static int H2_CommandSeat(void)
{
	int seat = CL_TargettedSplit(false);
#ifdef CSQC_DAT
	if (CSQC_ConsoleCommand(seat, Cmd_Argv(0)))
		return -1;
#endif
	return seat;
}
/* the commands only Hexen II has: for any other game they are the server's (Quake 2's invdrop...) */
static int H2_OnlyCommandSeat(void)
{
	extern qboolean sbar_hexen2;
	int seat = H2_CommandSeat();
	if (seat >= 0 && (!sbar_hexen2 || cls.protocol == CP_QUAKE2))
	{
		CL_SendSeatClientCommand(true, seat, "%s %s", Cmd_Argv(0), Cmd_Args());
		return -1;
	}
	return seat;
}

static void H2_InvLeft_f(void)
{
	int seat = H2_CommandSeat();
	struct h2seat_s *s;
	if (seat < 0)
		return;
	if (cls.protocol == CP_QUAKE2)
	{
		CL_SendSeatClientCommand(true, seat, "invprev");
		return;
	}
	s = &h2seat[seat];
	Inv_Changed(&cl.playerview[seat], s);
	if (!s->inv_count || cl.intermissionmode != IM_NONE)
		return;
	if (s->inv_flg)
	{	//scroll the icons at the left-most already
		if (s->inv_selected == s->inv_startpos)
			s->inv_startpos = s->inv_selected = (s->inv_startpos - 1 + s->inv_count) % s->inv_count;
		else
			s->inv_selected = (s->inv_selected - 1 + s->inv_count) % s->inv_count;
	}
	else
		s->inv_flg = true;
	S_LocalSound("misc/invmove.wav");
	s->inv_hidetime = cl.time + INVENTORY_DISPLAY_TIME;
}
static void H2_InvRight_f(void)
{
	int seat = H2_CommandSeat(), right_icon;
	struct h2seat_s *s;
	if (seat < 0)
		return;
	if (cls.protocol == CP_QUAKE2)
	{
		CL_SendSeatClientCommand(true, seat, "invnext");
		return;
	}
	s = &h2seat[seat];
	Inv_Changed(&cl.playerview[seat], s);
	if (!s->inv_count || cl.intermissionmode != IM_NONE)
		return;
	if (s->inv_flg)
	{	//scroll the icons at the right-most already
		if (s->inv_count >= INV_MAX_ICON)
			right_icon = (s->inv_startpos + INV_MAX_ICON - 1) % s->inv_count;
		else
			right_icon = (s->inv_startpos + s->inv_count - 1) % s->inv_count;
		if (s->inv_selected == right_icon)
			s->inv_startpos = (s->inv_startpos + 1) % s->inv_count;
		s->inv_selected = (s->inv_selected + 1) % s->inv_count;
	}
	else
		s->inv_flg = true;
	S_LocalSound("misc/invmove.wav");
	s->inv_hidetime = cl.time + INVENTORY_DISPLAY_TIME;
}
static void H2_InvUse_f(void)
{
	int seat = H2_CommandSeat();
	struct h2seat_s *s;
	if (seat < 0)
		return;
	if (cls.protocol == CP_QUAKE2)
	{
		CL_SendSeatClientCommand(true, seat, "invuse");
		return;
	}
	s = &h2seat[seat];
	Inv_Changed(&cl.playerview[seat], s);
	if (!s->inv_count || s->inv_selected < 0 || cl.intermissionmode != IM_NONE)
		return;
	S_LocalSound("misc/invuse.wav");
	s->inv_flg = false;
	Inv_Send(seat, s);
	//this artifact, whatever the server last heard was selected
	Cmd_ExecuteString(va("impulse %d\n", 100+s->inv_order[s->inv_selected]), Cmd_ExecLevel);
}
static void H2_InvDrop_f(void)
{	//uHexen2: drop takes effect at once, with the selection sent first
	int seat = H2_OnlyCommandSeat();
	struct h2seat_s *s;
	if (seat < 0)
		return;
	s = &h2seat[seat];
	Inv_Changed(&cl.playerview[seat], s);
	if (!s->inv_count || cl.intermissionmode != IM_NONE)
		return;
	s->inv_flg = false;
	Inv_Send(seat, s);
	Cmd_ExecuteString("impulse 44\n", Cmd_ExecLevel);
}
static void H2_InvOff_f(void)
{
	int seat = H2_OnlyCommandSeat();
	if (seat >= 0)
		h2seat[seat].inv_flg = false;
}

static void H2_ShowInfo_f(void)
{
	int seat = H2_CommandSeat();
	struct h2seat_s *s;
	if (seat < 0)
		return;
	s = &h2seat[seat];
	if (*Cmd_Argv(0) == '+')
	{
		if (s->showinfo || cl.intermissionmode != IM_NONE || cls.state < ca_connected)
			return;
		S_LocalSound("misc/barmovup.wav");
		s->bartarget = BAR_TOTAL_HEIGHT;
		s->showinfo = true;
	}
	else
	{
		if (!s->showinfo)
			return;
		s->bartarget = (cl.intermissionmode != IM_NONE || scr_viewsize.ival > 110) ? -BAR_BUMP_HEIGHT : BAR_TOP_HEIGHT;
		S_LocalSound("misc/barmovdn.wav");
		s->showinfo = false;
	}
}
static void H2_ShowDM_f(void)
{
	int seat = H2_OnlyCommandSeat();
	if (seat < 0)
		return;
	if (cl.deathmatch)	//the scores, as +showscores
		Cmd_ExecuteString((*Cmd_Argv(0) == '+') ? "+showscores\n" : "-showscores\n", Cmd_ExecLevel);
	h2seat[seat].showdm = *Cmd_Argv(0) == '+';
}
static void H2_InfoPlaque_f(void)
{
	int seat = H2_CommandSeat();
	if (seat >= 0)
		h2seat[seat].infoplaque = *Cmd_Argv(0) == '+';
}

void CLH2_InitHUD(void)
{
	Cvar_Register(&barspeed, "Hexen II HUD");
	Cmd_AddCommand("invleft", H2_InvLeft_f);
	Cmd_AddCommand("invright", H2_InvRight_f);
	Cmd_AddCommand("invprev", H2_InvLeft_f);
	Cmd_AddCommand("invnext", H2_InvRight_f);
	Cmd_AddCommand("invuse", H2_InvUse_f);
	Cmd_AddCommandD("invdrop", H2_InvDrop_f, "Hexen II: drops the selected artifact.");
	Cmd_AddCommandD("invoff", H2_InvOff_f, "Hexen II: hides the inventory.");
	Cmd_AddCommandD("+showinfo", H2_ShowInfo_f, "Hexen II: shows the whole status bar.");
	Cmd_AddCommandD("-showinfo", H2_ShowInfo_f, "Hexen II: shows the whole status bar.");
	Cmd_AddCommandD("+showdm", H2_ShowDM_f, "Hexen II: shows the puzzle pieces (the scores in deathmatch).");
	Cmd_AddCommandD("-showdm", H2_ShowDM_f, "Hexen II: shows the puzzle pieces (the scores in deathmatch).");
	Cmd_AddCommandD("+infoplaque", H2_InfoPlaque_f, "Hexen II: shows the objectives.");
	Cmd_AddCommandD("-infoplaque", H2_InfoPlaque_f, "Hexen II: shows the objectives.");
}

/* a new map or connection: nothing picked up yet (uHexen2's SB_InvReset) */
void CLH2_ResetHUD(void)
{
	int i;
	for (i = 0; i < MAX_SPLITS; i++)
	{
		h2seat[i].inv_count = h2seat[i].inv_startpos = h2seat[i].inv_sent = 0;
		h2seat[i].inv_selected = -1;
		h2seat[i].inv_flg = false;
		h2seat[i].bartarget = 0;	//placed by the viewsize on the next frame
	}
}

/* ---------------------------------------------------------------- centered texts, plaques, intermissions */

static char h2centerstring[1024];
static float h2centertime_off;
static char h2plaque[1024];		//a plaque stays until the gamecode takes it down

static struct
{
	int num;				//0 when none
	float completed_time;
	const char *pic;
	char message[2048];
	int flags;
	float lasting_time;
	int next;
} h2im;
#define INTERMISSION_PRINT_TOP		(1<<3)	//centered in the top half of the screen
#define INTERMISSION_PRINT_TOPMOST	(1<<4)	//at the very top
#define INTERMISSION_PRINT_WHITE	(1<<5)	//white, not red
#define INTERMISSION_PRINT_DELAY	(1<<6)	//the text starts some 2.5s late

/* uHexen2's CL_SetupIntermission: what each of the gamecode's intermissions shows */
static void H2_SetupIntermission(int num, const char *servertext)
{
	static const struct
	{
		const char *pic;
		int message;
		int flags;
		float lasting_time;
		int next;
	} im[] =
	{
		{NULL},
		{"gfx/meso.lmp",	1+394},		//defeated famine: episode 1 to 2
		{"gfx/egypt.lmp",	2+394},		//defeated death: 2 to 3
		{"gfx/roman.lmp",	3+394},		//defeated pestilence: 3 to 4
		{"gfx/castle.lmp",	4+394},		//defeated war: 4 to the castle
		{"gfx/castle.lmp",	408},		//the demo's ending
		{"gfx/end-1.lmp",	6+386, INTERMISSION_PRINT_DELAY|INTERMISSION_PRINT_WHITE|INTERMISSION_PRINT_TOP, 15, 7},	//defeated eidolon, 1/3
		{"gfx/end-2.lmp",	7+386, INTERMISSION_PRINT_DELAY|INTERMISSION_PRINT_WHITE|INTERMISSION_PRINT_TOP, 15, 8},	//2/3
		{"gfx/end-3.lmp",	8+386, INTERMISSION_PRINT_WHITE|INTERMISSION_PRINT_DELAY|INTERMISSION_PRINT_TOPMOST},	//3/3
		{"gfx/castle.lmp",	391, INTERMISSION_PRINT_WHITE},	//the bundle (oem) version's ending
		{"gfx/mpend.lmp",	538},		//defeated praevus: the mission pack's ending
		{"gfx/mpmid.lmp",	545},		//the mission pack, on to tibet
		{"gfx/end-3.lmp",	561},		//the mission pack's start
	};
	const char *text;

	if (num <= 0 || num >= countof(im))
	{
		h2im.num = 0;
		return;
	}
	h2im.num = num;
	h2im.completed_time = cl.time;
	h2im.pic = im[num].pic;
	h2im.flags = im[num].flags;
	h2im.lasting_time = im[num].lasting_time;
	h2im.next = im[num].next;

	text = T_GetString(im[num].message);
	if (num == 5 && servertext)	//the demo's ending is 408 in hexen2's strings, 410 in the mission pack's: the server knows
		text = servertext;
	if (!strcmp(text, "BAD STRING") && servertext)
		text = servertext;
	Q_strncpyz(h2im.message, text, sizeof(h2im.message));
}

/* which intermission the server's finale is (it sends the picture and the text) */
static int H2_IntermissionNumber(const char *pic, const char *text)
{
	if (!Q_strcasecmp(pic, "gfx/meso.lmp"))		return 1;
	if (!Q_strcasecmp(pic, "gfx/egypt.lmp"))	return 2;
	if (!Q_strcasecmp(pic, "gfx/roman.lmp"))	return 3;
	if (!Q_strcasecmp(pic, "gfx/end-1.lmp"))	return 6;
	if (!Q_strcasecmp(pic, "gfx/end-2.lmp"))	return 7;
	if (!Q_strcasecmp(pic, "gfx/end-3.lmp"))	return strcmp(text, T_GetString(561)) ? 8 : 12;
	if (!Q_strcasecmp(pic, "gfx/mpend.lmp"))	return 10;
	if (!Q_strcasecmp(pic, "gfx/mpmid.lmp"))	return 11;
	if (!Q_strcasecmp(pic, "gfx/castle.lmp"))
	{
		if (!strcmp(text, T_GetString(4+394)))	return 4;
		if (!strcmp(text, T_GetString(391)))	return 9;
		return 5;
	}
	return 0;
}

/*
A centerprint, for Hexen II's way of showing it: true when it is taken. "/P" ones are the gamecode's plaques
(plaque_draw); "/Ff" with a picture its intermissions (as the server writes them for its svc_intermission).
*/
qboolean CLH2_CenterPrint(int pnum, const char *str)
{
	qboolean plaque = false, finale = false;
	char pic[MAX_QPATH] = "";
	const char *e;

	if (pnum != 0)
		return false;

	while (*str == '/')
	{
		switch (str[1])
		{
		case '.':	//no more flags
			str += 2;
			goto text;
		case 'P':
			plaque = true;
			str += 2;
			break;
		case 'F':
			if (str[2] == 'f')
			{
				finale = true;
				if (cl.intermissionmode == IM_NONE)
					cl.completed_time = cl.time;
				cl.intermissionmode = IM_H2FINALE;
				str += 3;
				break;
			}
			return false;	//not hexen2's
		case 'I':
			str += 2;
			e = strchr(str, ':');
			if (!e)
				return false;
			Q_strncpyz(pic, str, min(sizeof(pic), (size_t)(e-str)+1));
			str = e+1;
			break;
		default:
			return false;	//something else's flags: FTE's own centerprints take them
		}
	}
text:
	if (finale)
	{
		if (!*pic)
			return false;
		H2_SetupIntermission(H2_IntermissionNumber(pic, str), str);
		if (!h2im.num)
			return false;
		h2plaque[0] = 0;
		h2centertime_off = 0;
		return true;
	}
	if (plaque)
	{
		Q_strncpyz(h2plaque, str, sizeof(h2plaque));
		return true;
	}
	if (!*str)
	{	//plaque_draw with no string: the plaque goes
		h2plaque[0] = 0;
		h2centertime_off = 0;
		return true;
	}
	Q_strncpyz(h2centerstring, str, sizeof(h2centerstring));
	h2centertime_off = scr_centertime.value;
	return true;
}

static void Plaque_Draw(const char *message)
{
	char temp[80];
	int i, by;

	FindTextBreaks(message, PLAQUE_WIDTH);
	by = (25-lines) * 8 / 2 + (h2rect.height - 200)/2;
	H2_DrawTextBox(32, by - 16, PLAQUE_WIDTH + 4, lines + 2);
	for (i = 0; i < lines; i++, by += 8)
	{
		TextLine(message, i, temp, sizeof(temp));
		H2_Print((40-strlen(temp)) * 8 / 2, by, temp, false);
	}
}

static void IntermissionOverlay(void)
{	//uHexen2's SB_IntermissionOverlay: the picture over the whole view, the text typed out
	char temp[80];
	int elapsed, size, bx, by, i;
	mpic_t *pic = R2D_SafeCachePic(h2im.pic);
	float y0;

	if (pic)
		R2D_ScalePic(h2rect.x, h2rect.y, h2rect.width, h2rect.height, pic);

	elapsed = (cl.time - h2im.completed_time) * 20;
	if (h2im.flags & INTERMISSION_PRINT_DELAY)
	{
		elapsed -= 50;
		if (elapsed < 0)
			elapsed = 0;
	}

	FindTextBreaks(h2im.message, 38);
	by = (h2im.flags & INTERMISSION_PRINT_TOPMOST) ? 16 : (25-lines) * 8 / 2;
	y0 = (h2im.flags & (INTERMISSION_PRINT_TOP|INTERMISSION_PRINT_TOPMOST)) ? 0 : (h2rect.height - 200)/2;
	for (i = 0; i < lines; i++, by += 8)
	{
		size = EndC[i] - StartC[i];
		if (size > elapsed)
			size = elapsed;
		TextLine(h2im.message, i, temp, sizeof(temp));
		if (size < (int)strlen(temp))
			temp[size] = 0;
		bx = (40-strlen(temp))*8/2;	//what is typed so far is centered
		H2_Print(bx, y0 + by, temp, !!(h2im.flags & INTERMISSION_PRINT_WHITE));

		elapsed -= size;
		if (elapsed <= 0)
			break;
	}

	//the ending's first parts move on by themselves
	if (i == lines && h2im.lasting_time && elapsed >= 20*h2im.lasting_time)
		H2_SetupIntermission(h2im.next, NULL);
}

/* the centered texts' part of the screen: an intermission, the plaque, the centerprint */
void CLH2_DrawCenterString(vrect_t *rect)
{
	char temp[80];
	int i, by;

	h2rect = *rect;
	if (cl.intermissionmode == IM_NONE)
		h2im.num = 0;
	if (h2im.num && cl.intermissionmode == IM_H2FINALE)
	{
		IntermissionOverlay();
		return;
	}

	if (*h2plaque)
		Plaque_Draw(h2plaque);

	h2centertime_off -= host_frametime;
	if (h2centertime_off <= 0 || !*h2centerstring)
		return;
	FindTextBreaks(h2centerstring, 38);
	by = (25-lines) * 8 / 2 + (h2rect.height - 200)/2;
	for (i = 0; i < lines; i++, by += 8)
	{
		TextLine(h2centerstring, i, temp, sizeof(temp));
		H2_Print((40-strlen(temp)) * 8 / 2, by, temp, false);
	}
}

/* a new map: no plaque or centerprint from the last one */
void CLH2_ClearCenterString(void)
{
	h2plaque[0] = 0;
	h2centerstring[0] = 0;
	h2centertime_off = 0;
	h2im.num = 0;
}
#endif
