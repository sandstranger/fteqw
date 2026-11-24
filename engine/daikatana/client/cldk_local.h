/*
Daikatana's client, as much of it as its HUD and message handlers (from its engine's client/, ported here) have,
over FTE's client (cldk_fte.h): cl, cls, re, viddef, net_message, its cvars, its HUD's state.
*/
#ifndef CLDK_LOCAL_H
#define CLDK_LOCAL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dk_shared.h"
#include "p_user.h"
#include "l__language.h"
#include "cldk_fte.h"

//its renderer's (client/ref.h)
#define DSFLAG_RESOURCE		0x00000001
#define DSFLAG_SCALE		0x00000002
#define DSFLAG_COLOR		0x00000004
#define DSFLAG_ALPHA		0x00000008
#define DSFLAG_BLEND		0x00000010
#define DSFLAG_PALETTE		0x00000020
#define DSFLAG_SUBIMAGE		0x00000040
#define DSFLAG_FLAT			0x00000080
#define DSFLAG_FLIPX		0x00000100
#define DSFLAG_FLIPY		0x00000200
#define DSFLAG_NOMIP		0x00000400
#define DSFLAG_CLAMP		0x00000800
#define DSFLAG_NO_N64FILTER	0x00010000

typedef struct __DRAWSTRUCT
{
	int		nFlags;
	int		dwResource;
	float	fScaleX;
	float	fScaleY;
	float	fAlpha;
	int		nXPos;
	int		nYPos;
	int		nLeft;
	int		nTop;
	int		nRight;
	int		nBottom;
	void	*pImage;
	CVector	rgbColor;
	const char *szString;
} DRAWSTRUCT, *LPDRAWSTRUCT;

#define RESOURCE_GLOBAL		0
#define RESOURCE_LEVEL		1
#define RESOURCE_EPISODE	2
#define RESOURCE_INTERFACE	3

typedef struct invenIcon_s
{
	void		*model;
	char		modelname[MAX_QPATH];
	int			modelindex;
	int			count;		//	for displaying ammo counts, etc.
	float		alpha;
	CVector		render_scale;
} invenIcon_t;

#define MAX_ONSCREEN_ICONS    (5)
#define INVMODE_OFF           (0)
#define INVMODE_WEAPON_ICONS  (1)
#define INVMODE_ITEM_ICONS    (2)
#define INVMODE_SIDEKICK      (3)

typedef struct onscreen_icons_s
{
	void		*models[2];
	qboolean	blink;
	int			blink_time;
	float		alpha;
} onscreen_icons_t;

typedef struct onscreen_icons_info_s
{
	onscreen_icons_t icons[MAX_ONSCREEN_ICONS];
	int			num_icons;
} onscreen_icons_info_t;

//its client's (client/client.h)
#define MAX_SKILLS 5
#define XPLEVELUP_BLINKTIME  400
#define INVMODE_ALPHA_DEFAULT     (0.75)
#define INVMODE_FADETIME        (5250.0)
#define FLAP_ALPHA_RATE                 (0.04)
#define FLAP_ALPHA_RATE_DOUBLE          (0.02)
#define FLAP_ALPHA_RATE_DOUBLEDOUBLE    (0.01)

typedef struct alphafx_s
{
	float	alpha;
	int		direction;	// up 1, -1 down, 0 none
	float	fade_time;
	float	off_time;
} alphafx_t;

extern alphafx_t	flap_mikiko;
extern alphafx_t	flap_superfly;
extern alphafx_t	invmode_hud;
extern alphafx_t	skill_hud;
extern alphafx_t	status_hud;

extern int		xplevelup_mode;
extern int		xplevelup_selection;
extern float	xplevelup_timer;
extern qboolean	xplevelup_blink;

extern int		cl_death_gib;
extern float	cl_gameover_time, cl_gameover_alpha;
extern float	cl_theend_time, cl_theend_alpha;
extern int		cl_savegem_count;

#define SCR_VIEWSIZE_NOSKILLS   (110)
#define SCR_VIEWSIZE_NOSTATUS   (120)

typedef enum {ca_uninitialized, ca_disconnected, ca_connecting, ca_connected, ca_active} connstate_t;
typedef struct
{
	connstate_t	state;
} client_static_t;
extern client_static_t cls;

typedef struct
{
	int		character;
} clientinfo_t;

typedef struct
{
	int				time;			//its client's, in ms
	qboolean		refresh_prepped;
	struct
	{
		struct
		{
			long	stats[MAX_STATS];
		} playerstate;
	} frame;
	struct
	{
		int		rdflags;
	} refdef;
	clientinfo_t	baseclientinfo;
	int				sidekicks_active;
	int				sidekick_commanding;
	int				sidekick_mikiko_health, sidekick_mikiko_armor;
	int				sidekick_superfly_health, sidekick_superfly_armor;
	void			*model_draw[MAX_MODELS];	//what the HUD's code passes round as a model: its index
	char			configstrings[1][MAX_QPATH];	//(unused: CL_ConfigModelName has them)
} client_state_t;
extern client_state_t cl;

typedef struct
{
	int		width, height;
} viddef_t;
extern viddef_t viddef;

//its renderer's calls
typedef struct
{
	void	*(*RegisterPic)(const char *name, int *width, int *height, int resource);
	void	*(*LoadFont)(const char *name);
	void	(*DrawPic)(DRAWSTRUCT &ds);
	int		(*DrawString)(DRAWSTRUCT &ds);
	int		(*StringWidth)(const char *string, void *font, int max_chars_counted);
	int		(*FontHeight)(void *font);
} refexport_t;
extern refexport_t re;

//its messages, as FTE has them
typedef struct
{
	const unsigned char *data;
	int		cursize;
	int		readcount;
	qboolean badread;
} sizebuf_t;
extern sizebuf_t net_message;
int		MSG_ReadChar(sizebuf_t *sb);
int		MSG_ReadByte(sizebuf_t *sb);
int		MSG_ReadShort(sizebuf_t *sb);
int		MSG_ReadLong(sizebuf_t *sb);
float	MSG_ReadFloat(sizebuf_t *sb);
char	*MSG_ReadString(sizebuf_t *sb);
void	MSG_ReadPos(sizebuf_t *sb, CVector &pos);
void	MSG_ReadDir(sizebuf_t *sb, CVector &dir);
float	MSG_ReadAngle(sizebuf_t *sb);
float	MSG_ReadAngle16(sizebuf_t *sb);

//its cvars: mirrors of FTE's, read each frame
extern cvar_t	*scr_viewsize;
extern cvar_t	*statusbar_alpha;
extern cvar_t	*hud_scale;
extern cvar_t	*cl_dmhints;

void	S_StartLocalSound(const char *name);
void	Cbuf_AddText(const char *text);
void	Cvar_Set(const char *name, const char *value);
void	Cvar_ForceSet(const char *name, const char *value);
int		CL_ModelIndex(const char *name);
qboolean CIN_RunningWithView(void);
extern int scr_draw_progress;
extern qboolean override_showhuds;
extern void *con_font;
int		CL_Episode(void);	//the map's episode (its worldspawn's, CM_KeyValue(map_epairs, "episode"))
const char *CL_ConfigModelName(int modelindex);

//the HUD (cl_hud.cpp)
void	SCR_RegisterStatus(void);
void	SCR_DrawStats(void);
qboolean check_weapon_hud(void);
void	CL_ParseSidekickWeaponStatus(void);
void	CL_Boost_Icons_Think(void);

//its handlers (cl_hudmsg.cpp)
#define CL_MAX_INVENTORY_ICONS	6
extern invenIcon_t	inventoryIcons[CL_MAX_INVENTORY_ICONS];
extern int		numInventoryIcons;
extern int		inventory_mode;
extern int		inventory_selected;
extern float	inventory_alpha;
#define NUM_SIDEKICKCOMMANDS (5)
extern int		sidekickcommand_selected;
extern onscreen_icons_info_t onscreen_icons_info;
void	inventory_init(void);
void	inventory_ParseInventory(void);
void	CL_ParseWeaponIcons(void);
void	CL_ParseAmmoUpdate(void);
void	CL_Boost_Icons_Init(void);
void	CL_Boost_Icons(void);
void	sidekickcommand_Init(void);
void	CL_Sidekick_Update(void);
void	sidekicks_active_update(int old_status);
qboolean CL_ValidateSelectorPos(int selection);
void	CL_ParseSelectorCommand(void);
void	CL_ParseXPLevel(void);
void	CL_InformClientDeath(void);
void	CL_InformClientTheEnd(void);
void	CL_ParseSaveGem(void);
void	CL_ClientRespawn(void);
void	CL_ParseCrosshairMessage(void);
void	CL_ParseStartMusic(void);
void	CL_ParseStartMP3(void);
void	CL_ParseStopMP3(void);

//its sound engine's music (cldk_main.cpp)
void	S_StartMusic(const char *name, int chan);
void	S_StartMP3(const char *name, int chan, float vol, int play_count, qboolean bRestart);
void	S_StopMP3(int chan);

#ifndef _WIN32
static inline char *itoa(int value, char *str, int radix)
{
	sprintf(str, radix == 16?"%x":"%d", value);
	return str;
}
#endif

#endif
