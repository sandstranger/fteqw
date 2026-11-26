/*
Daikatana's client code on FTE's client: what its HUD and handlers (cl_hud.cpp, cl_hudmsg.cpp) have of its client
and renderer, over FTE's (cldk_fte.h); the messages of its own that FTE's server sends in svcq2dk_message.

GPL v2 or later.
*/
#include "cldk_local.h"
#include "vertnormals.h"
#include "../fte/dk_fteproto.h"

client_static_t	cls;
client_state_t	cl;
viddef_t		viddef;
sizebuf_t		net_message;
int				scr_draw_progress;
qboolean		override_showhuds;
void			*con_font;
int				scr_centerstring_bottom;	//(FTE's centerprint's its own: the scoreboard's not kept below it)
float			scr_centertime_off;
void			*scoreboard_font;			//dk_scoreboard.cpp's (cl_scrn.cpp had it)

void			sb_InitScoreboard(void);			//dk_scoreboard.cpp
void			sb_ParseScoreboardMessage(void);
void			sb_DrawScoreboard(void);

static long		dkstats[MAX_STATS];		//the server's (svcdk_fte_stats)
static bool		dkstatsvalid;
static bool		dkinitialised;

/*
=====================================================================
cvars: its own, mirrors of FTE's (which have them archived), read each frame
=====================================================================
*/
static cvar_t	cv_viewsize = {(char *)"viewsize"}, cv_statusbar_alpha = {(char *)"statusbar_alpha"}, cv_hud_scale = {(char *)"hud_scale"};
static cvar_t	cv_dmhints = {(char *)"cl_dmhints"};
cvar_t			*scr_viewsize = &cv_viewsize;
cvar_t			*statusbar_alpha = &cv_statusbar_alpha;
cvar_t			*hud_scale = &cv_hud_scale;
cvar_t			*cl_dmhints = &cv_dmhints;

static void CLDK_ReadCvars(void)
{
	cv_viewsize.value = CLDKF_CvarValue("viewsize", "100");
	cv_statusbar_alpha.value = CLDKF_CvarValue("statusbar_alpha", "0.60");
	cv_hud_scale.value = CLDKF_CvarValue("hud_scale", "1.00");
	cv_dmhints.value = CLDKF_CvarValue("cl_dmhints", "1");
}

static void CLDK_S_StartLocalSound(const char *name)	{CLDKF_LocalSound(name);}
void (*S_StartLocalSound)(const char *s) = CLDK_S_StartLocalSound;
void Cbuf_AddText(const char *text)						{CLDKF_AddCommand(text);}
int CL_ModelIndex(char *name)							{return CLDKF_ModelIndex(name);}
int CIN_RunningWithView(void)							{return 0;}	//(CIN_Running's the server's: fte/cin_main.cpp)

int CL_Episode(void)
{	//its maps are e<episode>m<map><letter>
	const char *map = CLDKF_MapName();
	if (map && (map[0] == 'e' || map[0] == 'E') && map[1] >= '1' && map[1] <= '9')
		return map[1] - '0';
	return 1;
}

/*
=====================================================================
its renderer's: pics, fonts (fonts/<name>.dkf, fonts/<name>.font.bmp)
=====================================================================
*/
typedef struct dkpic_s
{
	char		name[MAX_QPATH];
	void		*pic;		//FTE's
	int			width, height;
	struct dkpic_s *next;
} dkpic_t;
static dkpic_t *dkpics;

typedef struct dkfont_s
{
	char		name[MAX_QPATH];
	dkpic_t		*image;
	int			height;
	unsigned char char_width[256];
	unsigned char char_pos_x[256];
	unsigned char char_pos_y[256];
	struct dkfont_s *next;
} dkfont_t;
static dkfont_t *dkfonts;

static void *RE_RegisterPic(const char *name, int *width, int *height, resource_t resource)
{
	dkpic_t *p;
	for (p = dkpics; p; p = p->next)
		if (!strcasecmp(p->name, name))
			break;
	if (!p)
	{
		p = (dkpic_t *)calloc(1, sizeof(*p));
		snprintf(p->name, sizeof(p->name), "%s", name);
		p->pic = CLDKF_RegisterPic(name, &p->width, &p->height);
		p->next = dkpics;
		dkpics = p;
	}
	if (width)
		*width = p->width;
	if (height)
		*height = p->height;
	return p->pic?p:NULL;
}

static void *RE_LoadFont(const char *name)
{
	dkfont_t *f;
	char path[MAX_QPATH];
	unsigned char *buf;
	int len;
	if (!name || !*name)
		return NULL;
	for (f = dkfonts; f; f = f->next)
		if (!strcasecmp(f->name, name))
			return f->image?f:NULL;
	f = (dkfont_t *)calloc(1, sizeof(*f));
	snprintf(f->name, sizeof(f->name), "%s", name);
	f->next = dkfonts;
	dkfonts = f;

	snprintf(path, sizeof(path), "fonts/%s.dkf", name);
	buf = (unsigned char *)CLDKF_LoadFile(path, &len);
	if (!buf)
		return NULL;
	//"dkf ", version 0, height, the widths, the x and y of each character in the image
	if (len >= 12 + 256*3 && !memcmp(buf, "dkf ", 4) && !(buf[4]|buf[5]|buf[6]|buf[7]))
	{
		f->height = buf[8] | (buf[9]<<8) | (buf[10]<<16) | (buf[11]<<24);
		memcpy(f->char_width, buf+12, 256);
		memcpy(f->char_pos_x, buf+12+256, 256);
		memcpy(f->char_pos_y, buf+12+512, 256);
		snprintf(path, sizeof(path), "fonts/%s.font.bmp", name);
		f->image = (dkpic_t *)RE_RegisterPic(path, NULL, NULL, RESOURCE_GLOBAL);
	}
	CLDKF_FreeFile(buf);
	return f->image?f:NULL;
}

static int RE_FontHeight(void *vfont)
{
	return vfont?((dkfont_t *)vfont)->height:8;
}

static int RE_StringWidth(const char *string, void *vfont, int max_chars_counted)
{
	dkfont_t *font = (dkfont_t *)vfont;
	int total = 0;
	if (!string || !font)
		return 1;
	if (max_chars_counted == -1)
		max_chars_counted = 9999;
	for (; *string && max_chars_counted > 0; string++, max_chars_counted--)
	{
		unsigned char c = *string;
		if (c == ' ')
			total += font->height >> 1;
		else if (font->char_width[c])
			total += font->char_width[c] + 1;
	}
	return total;
}

static void RE_Colour(const DRAWSTRUCT &ds, float *rgba)
{
	int i;
	rgba[0] = rgba[1] = rgba[2] = rgba[3] = 1;
	if (ds.nFlags & DSFLAG_ALPHA)
		rgba[3] = ds.fAlpha;
	if (ds.nFlags & DSFLAG_COLOR)
	{
		rgba[0] = ds.rgbColor.x;
		rgba[1] = ds.rgbColor.y;
		rgba[2] = ds.rgbColor.z;
	}
	for (i = 0; i < 4; i++)	//as GL clamped them (it's 255s in places)
		rgba[i] = (rgba[i] < 0)?0:((rgba[i] > 1)?1:rgba[i]);
}

static void RE_DrawPic(DRAWSTRUCT &ds)
{
	float rgba[4], w, h, s1 = 0, t1 = 0, s2 = 1, t2 = 1, f;
	dkpic_t *p = (dkpic_t *)ds.pImage;
	RE_Colour(ds, rgba);
	if (ds.nFlags & DSFLAG_FLAT)
	{
		CLDKF_DrawFill(ds.nLeft, ds.nTop, ds.nRight, ds.nBottom, rgba);
		return;
	}
	if (!p || !p->pic || p->width <= 0 || p->height <= 0)
		return;
	w = p->width;
	h = p->height;
	if (ds.nFlags & DSFLAG_SUBIMAGE)
	{
		s1 = (float)ds.nLeft / p->width;
		t1 = (float)ds.nTop / p->height;
		s2 = (float)(ds.nLeft + ds.nRight) / p->width;
		t2 = (float)(ds.nTop + ds.nBottom) / p->height;
	}
	if (ds.nFlags & DSFLAG_FLIPX)
		f = s1, s1 = s2, s2 = f;
	if (ds.nFlags & DSFLAG_FLIPY)
		f = t1, t1 = t2, t2 = f;
	if (ds.nFlags & DSFLAG_SCALE)
	{
		w = (int)(w * ds.fScaleX + 0.5f);
		h = (int)(h * ds.fScaleY + 0.5f);
	}
	CLDKF_DrawPic(p->pic, ds.nXPos, ds.nYPos, w, h, s1, t1, s2, t2, rgba);
}

static int RE_DrawString(DRAWSTRUCT &ds)
{
	dkfont_t *font = (dkfont_t *)ds.pImage;
	const unsigned char *s = (const unsigned char *)ds.szString;
	float rgba[4], x, sx = 1, sy = 1;
	if (!s || !*s || !font || !font->image || !font->image->pic)
		return ds.nXPos;
	RE_Colour(ds, rgba);
	if (ds.nFlags & DSFLAG_SCALE)
		sx = ds.fScaleX, sy = ds.fScaleY;
	x = ds.nXPos;
	for (; *s; s++)
	{
		unsigned char c = *s;
		float iw = font->image->width, ih = font->image->height;
		if (c == ' ')
		{
			x += (font->height >> 1) * sx;
			continue;
		}
		if (!font->char_width[c])
			continue;
		CLDKF_DrawPic(font->image->pic, (int)x, ds.nYPos, font->char_width[c] * sx, font->height * sy,
			font->char_pos_x[c] / iw, font->char_pos_y[c] / ih,
			(font->char_pos_x[c] + font->char_width[c]) / iw, (font->char_pos_y[c] + font->height) / ih, rgba);
		x += (1 + font->char_width[c]) * sx;
	}
	return (int)x;
}

static void *RE_RegisterModel(const char *name, resource_t resource)
{
	return CLDKF_RegisterModel(name);
}
static void *RE_RegisterSkin(const char *name, resource_t resource)
{	//an image as a model's skin (its menus' buttons)
	return CLDKF_RegisterSkin(name);
}

//its menus' (menu/)
static int RE_FontNumCharsInWidth(const char *string, void *vfont, int allowed, int start_pos)
{	//how many of the string's, from start_pos, fit in the width
	dkfont_t *font = (dkfont_t *)vfont;
	int w = 0, n = 0;
	if (!string || !font)
		return 0;
	if (start_pos > 0 && start_pos <= (int)strlen(string))
		string += start_pos;
	for (; *string; string++, n++)
	{
		unsigned char c = *string;
		int cw = (c == ' ')?(font->height >> 1):(font->char_width[c]?font->char_width[c] + 1:0);
		if (w + cw > allowed)
			break;
		w += cw;
	}
	return n;
}
static void RE_DrawFill(int x, int y, int w, int h, CVector rgb, float alpha)
{
	float rgba[4] = {rgb.x, rgb.y, rgb.z, alpha};
	CLDKF_DrawFill(x, y, w, h, rgba);
}
static void RE_RenderFrame(refdef_t *fd)
{	//(its menus': no world, its buttons' models)
	static cldk_rentity_t ents[256];
	float org[3] = {fd->vieworg.x, fd->vieworg.y, fd->vieworg.z}, ang[3] = {fd->viewangles.x, fd->viewangles.y, fd->viewangles.z};
	int i, n = 0;
	for (i = 0; i < fd->num_entities && n < 256; i++)
	{
		const entity_t *e = &fd->entities[i];
		cldk_rentity_t *r = &ents[n];
		if (!e->model || (size_t)e->model < MAX_MODELS)
			continue;
		r->model = e->model;
		r->skin = e->skin;
		r->origin[0] = e->origin.x; r->origin[1] = e->origin.y; r->origin[2] = e->origin.z;
		r->angles[0] = e->angles.x; r->angles[1] = e->angles.y; r->angles[2] = e->angles.z;
		r->frame = e->frame;
		r->oldframe = e->oldframe;
		r->backlerp = e->backlerp;
		r->alpha = (e->flags & RF_TRANSLUCENT)?e->alpha:1;
		r->scale = e->render_scale.x;
		if (e->color.x || e->color.y || e->color.z)	//(its renderer's vertex colours, past the lighting)
			r->rgb[0] = e->color.x, r->rgb[1] = e->color.y, r->rgb[2] = e->color.z;
		else
			r->rgb[0] = r->rgb[1] = r->rgb[2] = 1;
		r->fullbright = !!(e->flags & RF_FULLBRIGHT);
		n++;
	}
	CLDKF_RenderView(fd->x, fd->y, fd->width, fd->height, fd->fov_x, fd->fov_y, org, ang, n, ents);
}
static void RE_PurgeResources(resource_t resource)		{}
static void RE_SetInterfacePalette(const unsigned char *palette)	{}
static void RE_DrawChar(int x, int y, int c)			{}
static void RE_GetSavegameScreenShot(void)				{}
static void RE_ClearSavegameScreenShot(void)			{}
static void RE_SaveSavegameScreenShot(char *path, char *savename)	{CLDKF_AddCommand("cldk_saveshot\n");}	//(shot.tga, in the write dir)
//a surface's point (its hierarchical models' hardpoints, hr_*): FTE's models don't say yet, so the entity's own
void CLDK_GetModelHardpoint(char *pSurfName, int curFrame, int lastFrame, entity_t &ent, CVector &hardPt);	//cldk_models.cpp
void CLDK_ModelsReset(void);
//surface sprites (bullet holes, blood, scorch marks: cldk_surfsprites.cpp)
void CLDK_AddSurfaceSprite(CVector &vOrigin, CVector &vEntOrigin, CVector &vEntAngles, void *pHitModel, int nIndex,
	void *pSpriteModel, int nFrame, float fRoll, float fScale, byte nFlags);
void CLDK_SurfSpritesReset(void);

refexport_t re;	//what of it there is here: CLDK_InitRefExport
static void CLDK_InitRefExport(void)
{
	re.RegisterModel = RE_RegisterModel;
	re.RegisterSkin = RE_RegisterSkin;
	re.GetModelHardpoint = CLDK_GetModelHardpoint;
	re.AddSurfaceSprite = CLDK_AddSurfaceSprite;
	re.RegisterPic = RE_RegisterPic;
	re.LoadFont = RE_LoadFont;
	re.DrawPic = RE_DrawPic;
	re.DrawString = RE_DrawString;
	re.StringWidth = RE_StringWidth;
	re.FontHeight = RE_FontHeight;
	re.FontNumCharsInWidth = RE_FontNumCharsInWidth;
	re.DrawFill = RE_DrawFill;
	re.RenderFrame = RE_RenderFrame;
	re.PurgeResources = RE_PurgeResources;
	re.SetInterfacePalette = RE_SetInterfacePalette;
	re.DrawChar = RE_DrawChar;
	re.GetSavegameScreenShot = RE_GetSavegameScreenShot;
	re.ClearSavegameScreenShot = RE_ClearSavegameScreenShot;
	re.SaveSavegameScreenShot = RE_SaveSavegameScreenShot;
}

/*
=====================================================================
its messages
=====================================================================
*/
static bool msg_badread;	//its sizebuf_t has no such thing
int MSG_ReadChar(sizebuf_t *sb)
{
	if (sb->readcount + 1 > sb->cursize)
		return msg_badread = true, -1;
	return (signed char)sb->data[sb->readcount++];
}
int MSG_ReadByte(sizebuf_t *sb)
{
	if (sb->readcount + 1 > sb->cursize)
		return msg_badread = true, -1;
	return sb->data[sb->readcount++];
}
int MSG_ReadShort(sizebuf_t *sb)
{
	int v;
	if (sb->readcount + 2 > sb->cursize)
		return msg_badread = true, -1;
	v = (short)(sb->data[sb->readcount] | (sb->data[sb->readcount+1]<<8));
	sb->readcount += 2;
	return v;
}
int MSG_ReadLong(sizebuf_t *sb)
{
	int v;
	if (sb->readcount + 4 > sb->cursize)
		return msg_badread = true, -1;
	v = sb->data[sb->readcount] | (sb->data[sb->readcount+1]<<8) | (sb->data[sb->readcount+2]<<16) | (sb->data[sb->readcount+3]<<24);
	sb->readcount += 4;
	return v;
}
float MSG_ReadFloat(sizebuf_t *sb)
{
	union {float f; int i;} u;
	u.i = MSG_ReadLong(sb);
	return u.f;
}
char *MSG_ReadString(sizebuf_t *sb)
{
	static char string[2048];
	int l = 0, c;
	do
	{
		c = MSG_ReadChar(sb);
		if (c == -1 || c == 0)
			break;
		string[l++] = c;
	} while (l < (int)sizeof(string)-1);
	string[l] = 0;
	return string;
}
void MSG_ReadPos(sizebuf_t *sb, CVector &pos)
{
	pos.x = MSG_ReadShort(sb) * (1.0f/8);
	pos.y = MSG_ReadShort(sb) * (1.0f/8);
	pos.z = MSG_ReadShort(sb) * (1.0f/8);
}
static CVertexNormals avertexnormals;
void MSG_ReadDir(sizebuf_t *sb, CVector &dir)
{
	int b = MSG_ReadByte(sb);
	if (b < 0 || b >= NUMVERTEXNORMALS)
		dir.Zero();
	else
		dir = avertexnormals.GetNormal(b);
}
float MSG_ReadAngle(sizebuf_t *sb)
{
	return MSG_ReadByte(sb) * (360.0f/256);
}
float MSG_ReadAngle16(sizebuf_t *sb)
{
	return SHORT2ANGLE(MSG_ReadShort(sb));
}

/*
=====================================================================
music: its sound engine's music channels, as FTE's one music track
=====================================================================
*/
static void CL_MusicPath(char *out, size_t outsize, const char *name)
{
	size_t l = strlen(name);
	if (!strncasecmp(name, "music/", 6))
		snprintf(out, outsize, "%s", name);
	else if (!strncasecmp(name, "data/", 5))
		snprintf(out, outsize, "%s", name+5);
	else if (l > 4 && !strcasecmp(name+l-4, ".mp3"))
		snprintf(out, outsize, "music/%s", name);
	else
		snprintf(out, outsize, "music/%s.mp3", name);
}
//its MP3 channels: the map's and the menu's music (one at a time) are FTE's music track; the rest (voices:
//sounds/voices/..., the sound menu's cinematic volume preview) play as sounds of their own channels, heard everywhere
//(FTE decodes mp3 as sounds: snd_minimp3.c). The map's is remembered: the menu pauses it as it comes up
//(S_StopMusic(CHAN_MUSIC_MAP)) and brings it back as it goes (S_StartMusic(NULL, CHAN_MUSIC_MAP)).
static int dkmusicchan = -1;
static char dkmapmusic[MAX_QPATH];
static bool dkmapmusicloop;
static bool CL_IsMusic(const char *name)
{
	return !strncasecmp(name, "music/", 6) || !strncasecmp(name, "data/music/", 11) || !strchr(name, '/');
}
static void CL_PlayMusic(const char *path, int chan, bool loop)
{
	if (chan == CHAN_MUSIC_MAP)
	{
		snprintf(dkmapmusic, sizeof(dkmapmusic), "%s", path);
		dkmapmusicloop = loop;
	}
	CLDKF_Music(path, loop);
	dkmusicchan = chan;
}
static void CLDK_S_StartMusic(const char *name, int chan)
{
	char path[MAX_QPATH];
	if (!name || !*name)
	{	//the map's again, after the menu's
		if (chan == CHAN_MUSIC_MAP && *dkmapmusic && dkmusicchan != CHAN_MUSIC_MAP)
		{
			snprintf(path, sizeof(path), "%s", dkmapmusic);
			CL_PlayMusic(path, chan, dkmapmusicloop);
		}
		return;
	}
	CL_MusicPath(path, sizeof(path), name);
	if (chan == CHAN_MUSIC_MAP || chan == CHAN_MUSIC_MENU)
		CL_PlayMusic(path, chan, true);
	else	//(the sound menu's preview of the cinematics' volume)
		CLDKF_PlayStream(path, chan, CLDKF_CvarValue("s_cine", "1"), false);
}
static void CLDK_S_StartMP3(const char *name, int chan, float vol, int play_count, qboolean bRestart)
{
	char path[MAX_QPATH];
	if (!name || !*name)
		return;
	if (CL_IsMusic(name))
	{
		CL_MusicPath(path, sizeof(path), name);
		CL_PlayMusic(path, chan, play_count <= 0);
		return;
	}
	snprintf(path, sizeof(path), "%s", strncasecmp(name, "data/", 5)?name:name+5);
	CLDKF_PlayStream(path, chan, vol, play_count <= 0);
}
static void CLDK_S_StopMP3(int chan)
{
	if (chan == CHAN_MUSIC_MAP)
		*dkmapmusic = 0;	//(the server's: gone, not paused)
	if (chan == dkmusicchan)
	{
		CLDKF_Music(NULL, false);
		dkmusicchan = -1;
	}
	else
		CLDKF_StopStream(chan);
}
static void CLDK_S_StopMusic(int chan)
{	//the menu's (the map's stays remembered)
	if (chan == dkmusicchan)
	{
		CLDKF_Music(NULL, false);
		dkmusicchan = -1;
	}
	else
		CLDKF_StopStream(chan);
}
void (*S_StartMusic)(const char *name, int chan) = CLDK_S_StartMusic;
void (*S_StartMP3)(const char *name, int chan, float fvol, int play_count, qboolean bRestart) = CLDK_S_StartMP3;
void (*S_StopMP3)(int chan) = CLDK_S_StopMP3;
void (*S_StopMusic)(int chan) = CLDK_S_StopMusic;

/*
=====================================================================
FTE's client's
=====================================================================
*/
const char *CL_ConfigModelName(int modelindex)
{
	return CLDKF_ModelName(modelindex);
}

static void CLDK_Init(void)
{
	int i;
	if (dkinitialised)
		return;
	dkinitialised = true;
	CLDK_InitRefExport();
	for (i = 0; i < MAX_MODELS; i++)
		cl.model_draw[i] = (void *)(size_t)i;	//what the HUD has of models: their indexes
}

void CLDK_SceneReset(void);	//cldk_scene.cpp
extern "C" void CLDK_Reset(void)
{
	CLDK_Init();
	*dkmapmusic = 0;	//(the new map's server says its own)
	CLDK_SceneReset();
	CLDK_ModelsReset();
	CLDK_SurfSpritesReset();
	sb_InitScoreboard();	//(its client's at its start: here too, so one that's up doesn't stay up into the next)
	dkstatsvalid = false;
	memset(dkstats, 0, sizeof(dkstats));
	cl.sidekicks_active = 0;
	cl.sidekick_commanding = 0;
	xplevelup_mode = false;
	cl_death_gib = 0;
	cl_gameover_time = cl_gameover_alpha = 0;
	cl_theend_time = cl_theend_alpha = 0;
	memset(&status_hud, 0, sizeof(status_hud));
	memset(&skill_hud, 0, sizeof(skill_hud));
	memset(&flap_mikiko, 0, sizeof(flap_mikiko));
	memset(&flap_superfly, 0, sizeof(flap_superfly));
	sidekickcommand_Init();
	inventory_init();
	CL_Boost_Icons_Init();

	//its SCR_ initialisation: the HUDs up as the view size has them
	CLDK_ReadCvars();
	if (scr_viewsize->value < SCR_VIEWSIZE_NOSKILLS)
	{
		skill_hud.alpha = INVMODE_ALPHA_DEFAULT;
		invmode_hud.alpha = INVMODE_ALPHA_DEFAULT;
		inventory_alpha = invmode_hud.alpha;
	}
	else
		inventory_alpha = 0.0f;
	if (scr_viewsize->value < SCR_VIEWSIZE_NOSTATUS)
		status_hud.alpha = statusbar_alpha->value;
}

//what its client had at the start of a frame
static void CLDK_Frame(void)
{
	int i;
	CLDK_Init();
	cls.state = ca_active;
	cl.refresh_prepped = true;
	cl.time = CLDKF_Milliseconds();
	cl.refdef.rdflags = CLDKF_RDFlags();
	for (i = 0; i < MAX_STATS; i++)
		cl.frame.playerstate.stats[i] = dkstatsvalid?dkstats[i]:CLDKF_Stat(i);
	CLDKF_VidSize(&viddef.width, &viddef.height);
	CLDK_ReadCvars();
}
//its menus': as the HUD's, whether it's in a game or not
void CLDK_MenuFrame(void)
{
	CLDK_Init();
	cls.state = CLDKF_InGame()?ca_active:ca_disconnected;
	cl.time = CLDKF_Milliseconds();
	cl.refdef.rdflags = CLDKF_RDFlags();
	CLDKF_VidSize(&viddef.width, &viddef.height);
	CLDK_ReadCvars();
}

extern "C" void CLDK_ParseMessage(const unsigned char *data, int length)
{
	int svc, old;
	CLDK_Frame();
	net_message.data = (byte *)data;
	net_message.maxsize = net_message.cursize = length;
	net_message.readcount = 0;
	msg_badread = false;
	while (net_message.readcount < net_message.cursize && !msg_badread)
	{
		svc = MSG_ReadByte(&net_message);
		if (CLDKF_CvarValue("cl_dkshownet", "0"))
		{	//debugging: its svcs (and temp events), as they come
			char line[64];
			if (svc == svc_temp_entity && net_message.readcount < net_message.cursize)
				snprintf(line, sizeof(line), "echo dk svc %i te %i\n", svc, net_message.data[net_message.readcount]);
			else
				snprintf(line, sizeof(line), "echo dk svc %i\n", svc);
			CLDKF_AddCommand(line);
		}
		switch(svc)
		{
		case svcdk_fte_stats:
			for (old = 0; old < MAX_STATS; old++)
				dkstats[old] = MSG_ReadLong(&net_message);
			cldk_pmflags = MSG_ReadLong(&net_message);
			cldk_pmtype = MSG_ReadByte(&net_message);
			cldk_maxspeed = MSG_ReadFloat(&net_message);
			dkstatsvalid = !msg_badread;
			break;
		case svcdk_fte_entstate:	CLDK_ParseEntState();			break;
		case svc_layout:			MSG_ReadString(&net_message);	break;
		case svc_temp_entity:		CL_ParseTEnt();					break;
		case svc_effect_notify:		CL_ParseEffect();				break;
		case svc_inventory:			inventory_ParseInventory();		break;
		case svc_onscreen_icon:		CL_Boost_Icons();				break;
		case svc_clientdeath:		CL_InformClientDeath();			break;
		case svc_sidekick_update:
			old = cl.sidekicks_active;
			CL_Sidekick_Update();
			if (!(cl.refdef.rdflags & RDF_COOP))
				sidekicks_active_update(old);
			break;
		case svc_selector:			CL_ParseSelectorCommand();		break;
		case svc_weapon_icon:		CL_ParseWeaponIcons();			break;
		case svc_xplevel:			CL_ParseXPLevel();				break;
		case svc_savegem:			CL_ParseSaveGem();				break;
		case svc_clientrespawn:		CL_ClientRespawn();				break;
		case svc_nop:												break;
		case svc_crosshair:			CL_ParseCrosshairMessage();		break;
		case svc_scoreboard:		sb_ParseScoreboardMessage();	break;
		case svc_disconnect:		CLDKF_AddCommand("disconnect\n");	return;	//(coop's COOP_DropClient)
		case svc_end_of_game:		CL_InformClientTheEnd();		break;
		case svc_ammo_count_update:	CL_ParseAmmoUpdate();			break;
		case svc_sidekick_weapon_status: CL_ParseSidekickWeaponStatus(); break;
		case svc_start_music:		CL_ParseStartMusic();			break;
		case svc_start_mp3:			CL_ParseStartMP3();				break;
		case svc_stop_mp3:			CL_ParseStopMP3();				break;
		default:
			//not handled yet: the rest of this message is lost
			return;
		}
	}
}

/*
=====================================================================
its crosshair (cl_view.cpp's SCR_DrawCrosshair): crosshair's pics (cldk_menu_sys.cpp's crosshairList), the one for
what's aimed at (V_CheckAutoAim), at the middle of the view (the HUD's units, its pixels at 640x480)
=====================================================================
*/
autoAim_t	autoAim;
static int	crosshairregistered;

static void V_CheckAutoAim(void)
{
	CVector end, shootpos, v;
	float s[3], e[3], mn[3], mx[3], endpos[3], fovx, fovy, z, box;
	int entnum, startsolid, i;
	centity_t *cent;

	autoAim.item = 0;
	autoAim.xofs = autoAim.yofs = 0;
	autoAim.ent = NULL;

	box = CLDKF_CvarValue("autoaim", "0")?10:0;
	VectorMA(cl.refdef.vieworg, cl.v_forward, 2000, end);
	for (i = 0; i < 3; i++)
	{
		mn[i] = -box;
		mx[i] = box;
	}
	s[0] = cl.refdef.vieworg.x;	s[1] = cl.refdef.vieworg.y;	s[2] = cl.refdef.vieworg.z;
	e[0] = end.x;				e[1] = end.y;				e[2] = end.z;
	CLDKF_TraceShot(s, mn, mx, e, cl.playernum+1, endpos, &entnum, &startsolid);
	if (entnum <= 0 || entnum >= MAX_EDICTS || startsolid)
		return;
	cent = &cl_entities[entnum];
	if (!(cent->current.flags & SFL_TARGETABLE))
		return;
	autoAim.ent = &cent->current;

	if (box)
	{	//only alter its position if autoaim's on: onto what it'd aim at
		shootpos = cent->lerp_origin;
		shootpos.z += cent->current.maxs.z * 0.25;
		v = shootpos - cl.refdef.vieworg;
		z = DotProduct(v, cl.v_forward);
		CLDKF_ViewFov(&fovx, &fovy);
		if (z < 1 || fovx <= 0 || fovy <= 0)
			return;
		autoAim.xofs = DotProduct(v, cl.v_right) / (z * tan(fovx * M_PI / 360)) * viddef.width / 2;
		autoAim.yofs = -DotProduct(v, cl.v_up) / (z * tan(fovy * M_PI / 360)) * viddef.height / 2;
	}
}

static void SCR_DrawCrosshair(void)
{
	crosshair_t *ch1, *ch2 = NULL;
	DRAWSTRUCT ds;
	int cnum = (int)CLDKF_CvarValue("crosshair", "2"), i;

	//its cinematics, cameras (letterboxed): none
	if (cl.refdef.rdflags & RDF_LETTERBOX)
		return;
	//only certain weapons have one (its server's svc_crosshair)
	if (!CLDKF_CvarValue("cv_crosshair_enabled", "1") || cnum <= 0)
		return;
	if (cl.frame.playerstate.stats[STAT_HEALTH] <= 0)
		return;
	if (cnum > MAX_CROSSHAIRS)
		cnum = MAX_CROSSHAIRS;
	if (cnum != crosshairregistered)
	{	//SCR_RegisterCrosshair
		crosshairregistered = cnum;
		for (i = 0; i < MAX_CROSSHAIR_TYPES; i++)
		{
			crosshair_t *c = &crosshairList[cnum-1][i];
			if (c->pic[0])
				re.RegisterPic(c->pic, &c->width, &c->height, RESOURCE_GLOBAL);
		}
	}

	V_CheckAutoAim();
	cnum--;
	if (autoAim.ent)
	{
		ch2 = &crosshairList[cnum][CROSSHAIR_TARGET];
		ch1 = &crosshairList[cnum][CROSSHAIR_CENTER];
	}
	else if (autoAim.item)
		ch1 = &crosshairList[cnum][CROSSHAIR_ITEM];
	else
		ch1 = &crosshairList[cnum][CROSSHAIR_NOTARGET];
	if (!ch1->pic[0] || !ch1->width)
		return;

	memset(&ds, 0, sizeof(ds));
	ds.nFlags = DSFLAG_BLEND | DSFLAG_NO_N64FILTER;
	ds.pImage = re.RegisterPic(ch1->pic, NULL, NULL, RESOURCE_GLOBAL);
	ds.nXPos = ((viddef.width - ch1->width) >> 1) + (int)autoAim.xofs;
	ds.nYPos = ((viddef.height - ch1->height) >> 1) + (int)autoAim.yofs;
	re.DrawPic(ds);
	if (ch2 && ch2->pic[0] && ch2->width)
	{
		ds.pImage = re.RegisterPic(ch2->pic, NULL, NULL, RESOURCE_GLOBAL);
		ds.nXPos = (viddef.width - ch2->width) >> 1;
		ds.nYPos = (viddef.height - ch2->height) >> 1;
		re.DrawPic(ds);
	}
}

void CLDK_DrawHUDModels(void);	//cldk_icons.cpp
extern "C" void CLDK_DrawHUD(void)
{
	CLDK_Frame();
	SCR_DrawCrosshair();
	CLDK_DrawHUDModels();	//(its renderer drew them with the scene, under the HUD)
	SCR_DrawStats();
	sb_DrawScoreboard();
	CL_Boost_Icons_Think();
}
