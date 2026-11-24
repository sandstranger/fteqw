/*
What yquake2's menus (q2menu.c -- yquake2's menu.c --, qmenu.c, videomenu.c -- copied from yquake2, changes marked "FTE:") expect
of the client around them: the parts of yquake2's client.h, ref.h, screen.h and console.h they use. FTE
fills them in through q2menu_bridge.c; none of FTE's headers come in here (see ../q2menu_sys.h).
*/
#ifndef Q2MENU_H
#define Q2MENU_H

#include "q2menu_rename.h"	/* q2m_ names for the globals FTE has too */
#include <limits.h>
#include "../q2fx/q2shared.h"
#include "q2keys.h"
#include "q2gyro.h"
#include "../q2menu_sys.h"

/* --- video and drawing (yquake2's ref.h / vid.h): the whole screen, in pixels --- */
typedef struct { int width, height; } viddef_t;
extern viddef_t viddef;
#define VID_WIDTH viddef.width
#define VID_HEIGHT viddef.height

typedef struct entity_s
{
	struct model_s *model;
	float angles[3];
	float origin[3];
	int frame;
	float oldorigin[3];
	int oldframe;
	float backlerp;
	int skinnum;
	int lightstyle;
	float alpha;
	struct image_s *skin;
	int flags;
} entity_t;
typedef struct
{
	vec3_t origin;
	vec3_t color;
	float intensity;
} dlight_t;
typedef struct
{
	float rgb[3];
	float white;
} lightstyle_t;
typedef struct
{
	vec3_t origin;
	int color;
	float alpha;
} particle_t;
typedef struct
{
	int x, y, width, height;
	float fov_x, fov_y;
	float vieworg[3];
	float viewangles[3];
	float blend[4];
	float time;
	int rdflags;
	byte *areabits;
	lightstyle_t *lightstyles;
	int num_entities;
	entity_t *entities;
	int num_dlights;
	dlight_t *dlights;
	int num_particles;
	particle_t *particles;
} refdef_t;

void Draw_PicScaled(int x, int y, const char *pic, float factor);
void Draw_CharScaled(int x, int y, int num, float scale);
void Draw_Fill(int x, int y, int w, int h, int c);
void Draw_FadeScreen(void);
void Draw_GetPicSize(int *w, int *h, const char *name);
struct image_s *Draw_FindPic(const char *name);
struct model_s *R_RegisterModel(const char *name);
struct image_s *R_RegisterSkin(const char *name);
void R_RenderFrame(refdef_t *fd);
void R_EndFrame(void);
qboolean R_EndWorldRenderpass(void);
float SCR_GetMenuScale(void);
void SCR_DirtyScreen(void);
void SCR_EndLoadingPlaque(void);
void Con_ClearNotify(void);

/* --- the client state the menus read (yquake2's client.h) --- */
typedef enum { key_game, key_console, key_message, key_menu } keydest_t;
typedef enum { ca_uninitialized, ca_disconnected, ca_connecting, ca_connected, ca_active } connstate_t;
typedef struct
{
	connstate_t state;
	keydest_t key_dest;
	int realtime;		/* milliseconds */
} q2m_static_t;
typedef struct
{
	qboolean attractloop;
	int servercount;
	int cinematictime;
	char cinematic_file[MAX_QPATH];
	char configstrings[CS_CDTRACK + 1][MAX_QPATH];	/* FTE: only the cd track is read */
} q2m_client_t;
extern q2m_static_t q2m_cls;
extern q2m_client_t q2m_cl;
#define cls q2m_cls
#define cl q2m_cl

/* --- engine services --- */
cvar_t *Cvar_Get(const char *var_name, const char *value, int flags);
cvar_t *Cvar_Set(const char *var_name, const char *value);
cvar_t *Cvar_ForceSet(const char *var_name, const char *value);
void Cvar_SetValue(const char *var_name, float value);
float Cvar_VariableValue(const char *var_name);
const char *Cvar_VariableString(const char *var_name);
void Cmd_AddCommand(const char *cmd_name, void (*function)(void));
void Cbuf_AddText(const char *text);
void Cbuf_InsertText(const char *text);
void Cbuf_Execute(void);
void S_StartLocalSound(const char *sound);
int Sys_Milliseconds(void);
int Com_ServerState(void);
void Com_Printf(const char *fmt, ...);
void Com_DPrintf(const char *fmt, ...);
void Com_Error(int code, const char *fmt, ...);
char *va(const char *format, ...);
const char *Key_KeynumToString(int keynum);

int FS_LoadFile(const char *path, void **buffer);
int FS_LoadFile2(const char *path, void **buffer, int pad);
#define FS_FileExists(path) (FS_LoadFile2(path, NULL, 0) >= 0)
void FS_FreeFile(void *buffer);
typedef int fileHandle_t;
int FS_FOpenFile(const char *name, fileHandle_t *f, qboolean gamedir_only);
void FS_FCloseFile(fileHandle_t f);
int FS_Read(void *buffer, int size, fileHandle_t f);
strlist_t FS_ListFiles2(const char *findname, unsigned musthave, unsigned canthave);
strlist_t FS_ListMods(void);
const char *FS_Gamedir(void);
void Sys_RemoveDir(const char *path);
#define BASEDIRNAME "baseq2"

/* network: an address is only ever shown and connected to here, so it is kept as text (FTE: was a netadr) */
typedef struct { char str[64]; } netadr_t;
char *NET_AdrToString(netadr_t a);
void NET_Config(qboolean multiplayer);
void M_AddToServerList(netadr_t adr, char *info);
#define NUM_ADDRESSBOOK_ENTRIES 9

void CL_Snd_Restart_f(void);
void CL_Quit_f(void);
void CL_PingServers_f(void);
void CL_Disconnect(void);
void SV_Shutdown(char *finalmsg, qboolean reconnect);

#define MAX_CLIENTWEAPONMODELS 20
extern char cl_weaponmodels[MAX_CLIENTWEAPONMODELS][MAX_QPATH];
extern int num_cl_weaponmodels;

char *Key_KeynumToString_Joy(int key);
qboolean IN_MatchJoyPreset(void);
void IN_ApplyJoyPreset(void);
qboolean VID_HasRenderer(const char *renderer);
void VID_MenuInit(void);
void M_Menu_Video_f(void);
struct _tag_menuframework *M_ActiveMenu(void);	/* FTE: q2menu.c's M_GetActiveMenu, for the bridge */
extern qboolean japanese_confirm, show_gamepad, show_gyro;

/* music (yquake2's ogg.h), mapped onto FTE's cd/music */
typedef enum { PLAY, PAUSE, STOP } ogg_status_t;
int OGG_Status(void);
void OGG_InitTrackList(void);
void OGG_Init(void);
void OGG_PlayTrack(const char *track, qboolean cdtrack, qboolean immediate);
void OGG_Shutdown(void);
void OGG_Stop(void);

/* the cvars the menus keep as globals */
extern cvar_t *m_pitch, *freelook, *lookstrafe, *crosshair, *cl_run, *hand, *sensitivity, *s_volume;
extern cvar_t *joy_layout, *gyro_mode;

#endif
