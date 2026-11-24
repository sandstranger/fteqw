/*
yquake2's side of the menus' bridge: the client functions q2menu.c (yquake2's menu.c), qmenu.c and videomenu.c call, done over
../q2menu_sys.h, and the Q2M_ entry points FTE calls (clq2_menu.c is FTE's side). Compiled with yquake2's
types only, like the menus themselves.
*/
#include "q2menu.h"
#include "qmenu.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>

/* q2menu.c */
void M_Init(void);
void M_Draw(void);
void M_Keydown(int key);
void M_ForceMenuOff(void);

viddef_t viddef;
q2m_static_t q2m_cls;
q2m_client_t q2m_cl;

/* the player setup's preview cycles through these when a game sent vwep models; none outside one */
char cl_weaponmodels[MAX_CLIENTWEAPONMODELS][MAX_QPATH];
int num_cl_weaponmodels;

/* no gamepad, gyro or haptic menus: FTE's own input settings cover those */
qboolean japanese_confirm, show_gamepad, show_gyro, show_haptic;

char *keybindings[K_LAST];	/* copies of FTE's, by yquake2's key numbers (Q2M_RefreshCvars) */
qboolean keydown[K_LAST];

cvar_t *cl_run, *crosshair, *freelook, *gyro_mode, *joy_layout, *lookstrafe, *m_pitch;
cvar_t *name, *skin, *vid_fullscreen, *vid_renderer;

/* ---------------------------------------------------------------- cvars */

/*
The menus hold cvar_t pointers and read ->value / ->string at will, so each cvar they ask for gets a shadow
here that FTE's value is copied into (Q2M_RefreshCvars, called every frame a menu is up and before a key).
*/
static cvar_t *shadows;

/*
yquake2's r_mode numbers its vid_modes table (-2 the desktop size, -1 custom); FTE has vid_width and
vid_height (0 0 the desktop size)
*/
static const short vid_modes[][2] = {
	{320, 240}, {400, 300}, {512, 384}, {640, 400}, {640, 480}, {800, 500}, {800, 600}, {960, 720},
	{1024, 480}, {1024, 640}, {1024, 768}, {1152, 768}, {1152, 864}, {1280, 800}, {1280, 720},
	{1280, 960}, {1280, 1024}, {1366, 768}, {1440, 900}, {1600, 1200}, {1680, 1050}, {1920, 1080},
	{1920, 1200}, {2048, 1536}, {2560, 1080}, {2560, 1440}, {2560, 1600}, {3440, 1440},
	{3840, 1600}, {3840, 2160}, {4096, 2160}, {5120, 2880}, {1600, 900}
};

static void
Cvar_Read(const char *var_name, char *value, int size)
{
	if (!Q_stricmp(var_name, "r_mode"))
	{
		char w[32], h[32];
		int i, width, height, mode = -1;

		Q2MSYS_CvarString("vid_width", w, sizeof(w));
		Q2MSYS_CvarString("vid_height", h, sizeof(h));
		width = atoi(w);
		height = atoi(h);

		if (width <= 0 && height <= 0)
		{
			mode = -2;
		}
		else
		{
			for (i = 0; i < sizeof(vid_modes) / sizeof(vid_modes[0]); i++)
			{
				if (vid_modes[i][0] == width && vid_modes[i][1] == height)
				{
					mode = i;
				}
			}
		}

		Com_sprintf(value, size, "%i", mode);
		return;
	}

	Q2MSYS_CvarString(var_name, value, size);
}

static qboolean
Cvar_Write(const char *var_name, const char *value)
{
	if (!Q_stricmp(var_name, "r_mode"))
	{
		int mode = atoi(value);

		if (mode == -2)
		{
			Q2MSYS_CvarSet("vid_width", "0", false);
			Q2MSYS_CvarSet("vid_height", "0", false);
		}
		else if (mode >= 0 && mode < sizeof(vid_modes) / sizeof(vid_modes[0]))
		{
			Q2MSYS_CvarSet("vid_width", va("%i", vid_modes[mode][0]), false);
			Q2MSYS_CvarSet("vid_height", va("%i", vid_modes[mode][1]), false);
		}

		return true;
	}

	return false;
}

static void
Shadow_Read(cvar_t *var)
{
	char value[256];

	Cvar_Read(var->name, value, sizeof(value));

	if (!var->string || strcmp(var->string, value))
	{
		free(var->string);
		var->string = strdup(value);
		var->value = (float)atof(value);
		var->modified = true;
	}
}

static cvar_t *
Shadow_Find(const char *var_name)
{
	cvar_t *var;

	for (var = shadows; var; var = var->next)
	{
		if (!Q_stricmp(var->name, var_name))
		{
			return var;
		}
	}

	return NULL;
}

cvar_t *
Cvar_Get(const char *var_name, const char *var_value, int flags)
{
	cvar_t *var = Shadow_Find(var_name);
	char value[256];

	if (var)
	{
		Shadow_Read(var);
		return var;
	}

	if (!Q_stricmp(var_name, "r_mode"))
	{
		Cvar_Read(var_name, value, sizeof(value));
	}
	else
	{
		/* registers it in FTE with the default when FTE has no such cvar */
		Q2MSYS_CvarGet(var_name, var_value ? var_value : "", (flags & CVAR_ARCHIVE) != 0, value, sizeof(value));
	}

	var = calloc(1, sizeof(*var));
	var->name = strdup(var_name);
	var->string = strdup(value);
	var->default_string = strdup(var_value ? var_value : "");
	var->value = (float)atof(value);
	var->flags = flags;
	var->next = shadows;
	shadows = var;
	return var;
}

static cvar_t *
Cvar_Set2(const char *var_name, const char *value, int force)
{
	cvar_t *var;

	if (!Cvar_Write(var_name, value))
	{
		Q2MSYS_CvarSet(var_name, value, force);
	}

	var = Shadow_Find(var_name);
	if (var)
	{
		Shadow_Read(var);	/* what FTE made of it (clamping, latching) */
	}

	return var;
}

cvar_t *
Cvar_Set(const char *var_name, const char *value)
{
	return Cvar_Set2(var_name, value, false);
}

cvar_t *
Cvar_ForceSet(const char *var_name, const char *value)
{
	return Cvar_Set2(var_name, value, true);
}

void
Cvar_SetValue(const char *var_name, float value)
{
	char val[32];

	if (value == (int)value)
	{
		Com_sprintf(val, sizeof(val), "%i", (int)value);
	}
	else
	{
		Com_sprintf(val, sizeof(val), "%f", value);
	}

	Cvar_Set(var_name, val);
}

const char *
Cvar_VariableString(const char *var_name)
{
	static char values[4][256];
	static int next;
	char *value = values[next++ & 3];

	Cvar_Read(var_name, value, sizeof(values[0]));
	return value;
}

float
Cvar_VariableValue(const char *var_name)
{
	return (float)atof(Cvar_VariableString(var_name));
}

/* ---------------------------------------------------------------- commands */

static struct
{
	char name[32];
	void (*function)(void);
} commands[48];
static int numcommands;

void
Cmd_AddCommand(const char *cmd_name, void (*function)(void))
{
	int i;

	for (i = 0; i < numcommands; i++)
	{
		if (!strcmp(commands[i].name, cmd_name))
		{
			break;
		}
	}

	if (i == numcommands)
	{
		if (numcommands == sizeof(commands) / sizeof(commands[0]))
		{
			Com_Printf("Q2 menus: too many commands\n");
			return;
		}

		numcommands++;
	}

	Q_strlcpy(commands[i].name, cmd_name, sizeof(commands[i].name));
	commands[i].function = function;
	Q2MSYS_AddCommand(cmd_name);
}

void
Cbuf_AddText(const char *text)
{
	Q2MSYS_ExecText(text, false);
}

void
Cbuf_InsertText(const char *text)
{
	Q2MSYS_ExecText(text, true);
}

void
Cbuf_Execute(void)
{
	Q2MSYS_ExecText(NULL, false);
}

/* ---------------------------------------------------------------- printing */

void
Com_Printf(const char *fmt, ...)
{
	va_list argptr;
	char msg[1024];

	va_start(argptr, fmt);
	vsnprintf(msg, sizeof(msg), fmt, argptr);
	va_end(argptr);

	Q2MSYS_Print(false, msg);
}

void
Com_Error(int code, const char *fmt, ...)
{
	va_list argptr;
	char msg[1024];

	va_start(argptr, fmt);
	vsnprintf(msg, sizeof(msg), fmt, argptr);
	va_end(argptr);

	Q2MSYS_Error(msg);
}

int
Com_ServerState(void)
{
	return Q2MSYS_ServerState();
}

void
Con_ClearNotify(void)
{
}

/* ---------------------------------------------------------------- drawing */

void
Draw_PicScaled(int x, int y, const char *pic, float factor)
{
	Q2MSYS_DrawPic(x, y, pic, factor);
}

void
Draw_CharScaled(int x, int y, int num, float scale)
{
	Q2MSYS_DrawChar(x, y, num, scale);
}

void
Draw_Fill(int x, int y, int w, int h, int c)
{
	Q2MSYS_DrawFill(x, y, w, h, c);
}

void
Draw_FadeScreen(void)
{
	Q2MSYS_FadeScreen();
}

void
Draw_GetPicSize(int *w, int *h, const char *name)
{
	Q2MSYS_GetPicSize(w, h, name);
}

struct image_s *
Draw_FindPic(const char *name)
{
	int w = 0, h = 0;

	Q2MSYS_GetPicSize(&w, &h, name);	/* loads it */
	return (w > 0) ? (struct image_s *)name : NULL;	/* only ever tested for NULL */
}

float
SCR_GetMenuScale(void)
{
	return Q2MSYS_MenuScale();
}

void
SCR_DirtyScreen(void)
{
}

void
SCR_EndLoadingPlaque(void)
{
}

/* ---------------------------------------------------------------- the player model preview */

/*
A model or skin "handle" is its name, kept here; R_RenderFrame hands the names on to FTE, which loads and
draws them. Missing files give NULL, as yquake2's renderers do.
*/
static char *handles[32];
static int numhandles;

static const char *
Handle_For(const char *name)
{
	int i;

	for (i = 0; i < numhandles; i++)
	{
		if (!strcmp(handles[i], name))
		{
			return handles[i];
		}
	}

	if (!Q2MSYS_FileExists(name))
	{
		return NULL;
	}

	if (numhandles == sizeof(handles) / sizeof(handles[0]))
	{
		/* the preview cycles through a handful; start over rather than grow */
		for (i = 0; i < numhandles; i++)
		{
			free(handles[i]);
		}

		numhandles = 0;
	}

	handles[numhandles] = strdup(name);
	return handles[numhandles++];
}

struct model_s *
R_RegisterModel(const char *name)
{
	return (struct model_s *)Handle_For(name);
}

struct image_s *
R_RegisterSkin(const char *name)
{
	return (struct image_s *)Handle_For(name);
}

void
R_RenderFrame(refdef_t *fd)
{
	q2msys_entity_t ents[4];
	int i, n = 0;

	for (i = 0; i < fd->num_entities && n < 4; i++)
	{
		const entity_t *e = &fd->entities[i];

		if (!e->model)
		{
			continue;
		}

		ents[n].model = (const char *)e->model;
		ents[n].skin = (const char *)e->skin;
		VectorCopy(e->origin, ents[n].origin);
		VectorCopy(e->angles, ents[n].angles);
		ents[n].frame = e->frame;
		ents[n].oldframe = e->oldframe;
		ents[n].backlerp = e->backlerp;
		ents[n].flags = e->flags;
		n++;
	}

	Q2MSYS_RenderModels(fd->x, fd->y, fd->width, fd->height, fd->fov_x, fd->fov_y, fd->time, ents, n);
}

void
R_EndFrame(void)
{
}

qboolean
R_EndWorldRenderpass(void)
{
	return true;
}

qboolean
VID_HasRenderer(const char *renderer)
{
	return false;	/* FTE has its own; the renderer list is gone from the video menu */
}

/* ---------------------------------------------------------------- engine */

void
S_StartLocalSound(const char *sound)
{
	Q2MSYS_LocalSound(sound);
}

int
Sys_Milliseconds(void)
{
	return Q2MSYS_Milliseconds();
}

void
SV_Shutdown(char *finalmsg, qboolean reconnect)
{
	Q2MSYS_ExecText("killserver\n", true);
}

void
CL_Disconnect(void)
{
	Q2MSYS_ExecText("disconnect\n", true);
}

void
CL_PingServers_f(void)
{
	Q2MSYS_PingServers();
}

void
CL_Quit_f(void)
{
	Q2MSYS_ExecText("quit\n", false);
}

void
CL_Snd_Restart_f(void)
{
	Q2MSYS_ExecText("snd_restart\n", false);
}

char *
NET_AdrToString(netadr_t a)
{
	static char s[64];

	Q_strlcpy(s, a.str, sizeof(s));
	return s;
}

void
NET_Config(qboolean multiplayer)
{
}

/* music: FTE's cd command plays the tracks (and their ogg replacements) */
int
OGG_Status(void)
{
	return STOP;	/* never paused by the menus: FTE pauses the game, and with it the music */
}

void
OGG_Init(void)
{
}

void
OGG_InitTrackList(void)
{
}

void
OGG_PlayTrack(const char *track, qboolean cdtrack, qboolean immediate)
{
	if (track && *track && strcmp(track, "0"))
	{
		Q2MSYS_ExecText(va("cd play %s\n", track), false);
	}
}

void
OGG_Stop(void)
{
	Q2MSYS_ExecText("cd stop\n", false);
}

void
OGG_Shutdown(void)
{
	OGG_Stop();
}

/* ---------------------------------------------------------------- files */

int
FS_LoadFile2(const char *path, void **buffer, int pad)
{
	void *data;
	char *copy;
	int len;

	if (!buffer)
	{
		return Q2MSYS_FileExists(path) ? 0 : -1;
	}

	*buffer = NULL;
	len = Q2MSYS_LoadFile(path, &data);

	if (len < 0)
	{
		return -1;
	}

	/* a copy in our own heap, so FS_FreeFile is free() */
	copy = malloc(len + (pad > 0 ? pad : 0) + 1);
	memcpy(copy, data, len);
	memset(copy + len, 0, (pad > 0 ? pad : 0) + 1);
	Q2MSYS_FreeFile(data);

	*buffer = copy;
	return len;
}

int
FS_LoadFile(const char *path, void **buffer)
{
	return FS_LoadFile2(path, buffer, 0);
}

void
FS_FreeFile(void *buffer)
{
	free(buffer);
}

static void
List_Add(const char *name, void *ctx)
{
	strlist_t *list = ctx;

	/* the same file can be in several packages and directories */
	if (!StrList_Contains(list, name))
	{
		StrList_Append(list, name);
	}
}

strlist_t
FS_ListFiles2(const char *findname, unsigned musthave, unsigned canthave)
{
	strlist_t list;

	StrList_Init(&list, 0);
	Q2MSYS_ListFiles(findname, List_Add, &list);
	return list;
}

strlist_t
FS_ListMods(void)
{
	strlist_t list;

	StrList_Init(&list, 0);
	Q2MSYS_ListMods(List_Add, &list);

	if (!StrList_Contains(&list, BASEDIRNAME))
	{
		StrList_Append(&list, BASEDIRNAME);
	}

	qsort(list.data, list.num, sizeof(char *), Q_sort_stricmp);
	return list;
}

/* ---------------------------------------------------------------- input */

qboolean
IN_NumpadIsOn(void)
{
	return false;
}

void
IN_GetClipboardText(char *out, size_t n)
{
	if (!Q2MSYS_GetClipboard(out, (int)n) && n)
	{
		*out = '\0';
	}
}

int
IN_SetClipboardText(const char *s)
{
	Q2MSYS_SetClipboard(s);
	return 0;
}

qboolean
IN_MatchJoyPreset(void)
{
	return true;
}

void
IN_ApplyJoyPreset(void)
{
}

qboolean
IsCalibrationZero(void)
{
	return true;
}

void
StartCalibration(void)
{
}

void
Key_ClearTyping(void)
{
}

void
Key_MarkAllUp(void)
{
	memset(keydown, 0, sizeof(keydown));
}

const char *
Key_KeynumToString(int keynum)
{
	return Q2MSYS_KeyName(keynum);
}

char *
Key_KeynumToString_Joy(int key)
{
	return (char *)Q2MSYS_KeyName(key);
}

static void
Binding_Store(int keynum, const char *binding)
{
	if (binding && !*binding)
	{
		binding = NULL;
	}

	if (binding && keybindings[keynum] && !strcmp(binding, keybindings[keynum]))
	{
		return;
	}

	free(keybindings[keynum]);
	keybindings[keynum] = binding ? strdup(binding) : NULL;
}

void
Key_SetBinding(int keynum, char *binding)
{
	if (keynum < 0 || keynum >= K_LAST)
	{
		return;
	}

	Q2MSYS_SetBinding(keynum, binding);
	Binding_Store(keynum, binding);
}

/* ---------------------------------------------------------------- FTE calls these */

void
Q2M_RefreshCvars(void)
{
	cvar_t *var;
	int k;

	for (var = shadows; var; var = var->next)
	{
		Shadow_Read(var);
	}

	for (k = 0; k < K_LAST; k++)
	{
		Binding_Store(k, Q2MSYS_GetBinding(k));
	}
}

void
Q2M_Init(void)
{
	static qboolean initialised;
	int i;

	if (initialised)
	{
		/* FTE dropped its menu commands (a menu restart): give it ours again */
		for (i = 0; i < numcommands; i++)
		{
			Q2MSYS_AddCommand(commands[i].name);
		}

		return;
	}

	initialised = true;

	cl_run = Cvar_Get("cl_run", "0", CVAR_ARCHIVE);
	crosshair = Cvar_Get("crosshair", "0", CVAR_ARCHIVE);
	freelook = Cvar_Get("freelook", "1", CVAR_ARCHIVE);
	gyro_mode = Cvar_Get("gyro_mode", "2", CVAR_ARCHIVE);
	joy_layout = Cvar_Get("joy_layout", "0", CVAR_ARCHIVE);
	lookstrafe = Cvar_Get("lookstrafe", "0", CVAR_ARCHIVE);
	m_pitch = Cvar_Get("m_pitch", "0.022", CVAR_ARCHIVE);
	name = Cvar_Get("name", "unnamed", CVAR_USERINFO | CVAR_ARCHIVE);
	skin = Cvar_Get("skin", "male/grunt", CVAR_USERINFO | CVAR_ARCHIVE);
	vid_fullscreen = Cvar_Get("vid_fullscreen", "0", CVAR_ARCHIVE);
	vid_renderer = Cvar_Get("vid_renderer", "", CVAR_ARCHIVE);

	M_Init();
}

void
Q2M_SetFrame(int width, int height, int realtime_ms, int state, int servercount, int attractloop, const char *cdtrack)
{
	viddef.width = width;
	viddef.height = height;
	cls.realtime = realtime_ms;
	cls.state = state;
	cl.servercount = servercount;
	cl.attractloop = attractloop;
	Q_strlcpy(cl.configstrings[CS_CDTRACK], cdtrack ? cdtrack : "", sizeof(cl.configstrings[CS_CDTRACK]));
}

int
Q2M_IsActive(void)
{
	return M_ActiveMenu() != NULL;
}

void
Q2M_ForceOff(void)
{
	if (M_ActiveMenu())
	{
		M_ForceMenuOff();
	}
}

void
Q2M_Draw(void)
{
	M_Draw();
}

void
Q2M_KeyEvent(int q2key, int down)
{
	if (q2key <= 0 || q2key >= K_LAST)
	{
		return;
	}

	keydown[q2key] = down != 0;

	if (down)
	{
		Q2M_RefreshCvars();
		M_Keydown(q2key);
	}
}

int
Q2M_TakeConsole(void)
{
	if (cls.key_dest == key_console)
	{
		cls.key_dest = M_ActiveMenu() ? key_menu : key_game;
		return true;
	}

	return false;
}

/* the active menu's item under a point in screen pixels (and puts the cursor on it), or NULL */
static menucommon_s *
Item_Under(menuframework_s *menu, int x, int y)
{
	float scale = SCR_GetMenuScale();
	int i;

	for (i = 0; i < menu->nitems; i++)
	{
		menucommon_s *item = menu->items[i];
		int top, bottom;

		if (item->type == MTYPE_SEPARATOR || (item->flags & QMF_INACTIVE))
		{
			continue;
		}

		if (item->type == MTYPE_BITMAP)
		{
			/* the main menu's pictures sit at their own position, in menu units */
			int w = 0, h = 0;

			Q2MSYS_GetPicSize(&w, &h, item->name);
			top = item->y * scale;
			bottom = top + h * scale;
		}
		else
		{
			top = (menu->y + item->y) * scale;
			bottom = top + 10 * scale;
		}

		if (y >= top && y < bottom)
		{
			if (menu->cursor != i)
			{
				menu->cursor = i;
				if (item->statusbarfunc)
				{
					item->statusbarfunc(item);
				}
			}

			return item;
		}
	}

	return NULL;
}

void
Q2M_MouseMove(int x, int y)
{
	menuframework_s *menu = M_ActiveMenu();

	if (menu)
	{
		Item_Under(menu, x, y);
	}
}

int
Q2M_Click(int x, int y, int touch)
{
	menuframework_s *menu = M_ActiveMenu();
	menucommon_s *item;

	if (!menu)
	{
		return false;
	}

	Q2M_RefreshCvars();
	item = Item_Under(menu, x, y);

	if (item && (item->type == MTYPE_SLIDER || item->type == MTYPE_SPINCONTROL))
	{
		/* the left or the right half of the value steps it down or up */
		float scale = SCR_GetMenuScale();
		int middle = menu->x + item->x + (RCOLUMN_OFFSET + 40) * scale;

		M_Keydown(x < middle ? K_LEFTARROW : K_RIGHTARROW);
		return true;
	}

	if (!item && touch && menu->nitems)
	{
		return false;	/* a tap beside the items */
	}

	/* a mouse button is a key too (the key binding menus take it as one); a tap is enter, which also
	   answers the item-less menus (quit) */
	M_Keydown(touch ? K_ENTER : K_MOUSE1);
	return true;
}

void
Q2M_AddServer(const char *address, const char *info)
{
	netadr_t adr;
	char text[128];

	Q_strlcpy(adr.str, address, sizeof(adr.str));
	Q_strlcpy(text, info, sizeof(text));
	M_AddToServerList(adr, text);
}

void
Q2M_Command(const char *cmd_name)
{
	int i;

	for (i = 0; i < numcommands; i++)
	{
		if (!strcmp(commands[i].name, cmd_name))
		{
			Q2M_RefreshCvars();
			commands[i].function();
			return;
		}
	}
}
