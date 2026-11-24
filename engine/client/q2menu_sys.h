/*
The boundary between yquake2's menus (engine/client/q2menu: q2menu.c (yquake2's menu.c), qmenu.c, videomenu.c) and FTE
(clq2_menu.c). As with the effects (q2fx_sys.h), each side is compiled with its own types, so everything
that crosses goes through here in plain C types only.
*/
#ifndef Q2MENU_SYS_H
#define Q2MENU_SYS_H

/* --- FTE calls the menus (q2menu/q2menu_bridge.c) --- */

void Q2M_Init(void);			/* yquake2's M_Init: cvars and menu_* commands */
int Q2M_IsActive(void);			/* a menu is up */
void Q2M_ForceOff(void);		/* M_ForceMenuOff */
void Q2M_Draw(void);			/* M_Draw, with the screen size and state set first (Q2M_SetFrame) */
void Q2M_KeyEvent(int q2key, int down);	/* M_Keydown on a press (yquake2's key numbers); both edges keep keydown[] */
void Q2M_MouseMove(int x, int y);	/* puts the menu's cursor on the item under the pointer, in screen pixels */
int Q2M_Click(int x, int y, int touch);	/* a click or tap there: activates the item (or steps a slider or list); 0 when it hit nothing */
int Q2M_TakeConsole(void);		/* 1 once when a menu asked for the console (yquake2's key_dest = key_console) */
void Q2M_SetFrame(int width, int height, int realtime_ms, int state, int servercount, int attractloop, const char *cdtrack);
void Q2M_AddServer(const char *address, const char *info);	/* M_AddToServerList */
void Q2M_RefreshCvars(void);	/* re-read every cvar the menus hold from FTE */
void Q2M_Command(const char *name);	/* one of the commands the menus registered (Q2MSYS_AddCommand) */

/* --- the menus call FTE (clq2_menu.c) --- */

/* drawing, in screen pixels (yquake2's viddef) */
void Q2MSYS_DrawPic(int x, int y, const char *pic, float scale);
void Q2MSYS_DrawChar(int x, int y, int num, float scale);
void Q2MSYS_DrawFill(int x, int y, int w, int h, int palettecolour);
void Q2MSYS_FadeScreen(void);
void Q2MSYS_GetPicSize(int *w, int *h, const char *pic);
float Q2MSYS_MenuScale(void);

/* the player setup's 3D preview: a scene of entities (model and weapon), no world */
typedef struct
{
	const char *model;
	const char *skin;	/* NULL: the model's own */
	float origin[3], angles[3];
	int frame, oldframe;
	float backlerp;
	int flags;
} q2msys_entity_t;
void Q2MSYS_RenderModels(int x, int y, int w, int h, float fov_x, float fov_y, float time,
		const q2msys_entity_t *ents, int numents);

/* cvars: by yquake2's name, which FTE's side translates where FTE calls it something else */
void Q2MSYS_CvarGet(const char *name, const char *def, int archive, char *value, int valuesize);
void Q2MSYS_CvarSet(const char *name, const char *value, int force);
void Q2MSYS_CvarString(const char *name, char *value, int valuesize);

void Q2MSYS_AddCommand(const char *name);	/* FTE calls Q2M_Command for it */
void Q2MSYS_ExecText(const char *text, int insert);	/* text NULL: run what is buffered now */
void Q2MSYS_LocalSound(const char *sound);
int Q2MSYS_Milliseconds(void);
int Q2MSYS_ServerState(void);
void Q2MSYS_Print(int developer, const char *text);
void Q2MSYS_Error(const char *text);

/* keys: yquake2's numbers */
const char *Q2MSYS_KeyName(int q2key);
const char *Q2MSYS_GetBinding(int q2key);
void Q2MSYS_SetBinding(int q2key, const char *binding);
int Q2MSYS_GetClipboard(char *text, int size);
void Q2MSYS_SetClipboard(const char *text);

/* files */
int Q2MSYS_LoadFile(const char *path, void **buffer);	/* -1 when missing; Q2MSYS_FreeFile the buffer */
void Q2MSYS_FreeFile(void *buffer);
int Q2MSYS_FileExists(const char *path);
/* the files (or directories, with a trailing /) matching a wildcard, one callback each */
void Q2MSYS_ListFiles(const char *wildcard, void (*callback)(const char *name, void *ctx), void *ctx);
void Q2MSYS_ListMods(void (*callback)(const char *gamedir, void *ctx), void *ctx);

/* saves: slot -1 is the quick save, 0 the newest autosave, N the regular slot N */
int Q2MSYS_SaveComment(int slot, char *comment, int size);	/* 0 when empty */
void Q2MSYS_LoadSlot(int slot);
void Q2MSYS_SaveSlot(int slot);
void Q2MSYS_DeleteSlot(int slot);

/* servers: a search fills the list through Q2M_AddServer */
void Q2MSYS_PingServers(void);

#endif
