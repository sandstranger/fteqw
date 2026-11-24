/*
What of its engine its menus (menu/: dk_menu*.cpp, ported as they are) had, over FTE's (cldk_fte.h): its cvars
(mirrors of FTE's, which the menus read as they are), its keys and their bindings (Quake II's numbers), the saved
games' and configs' files, video modes, addresses; and FTE's menu's calls into them (CLDK_Menu*).

Not here: its GameSpy server lists (no master servers: they stay empty), its renderer's palettes (FTE's is true
colour), its saved games' screenshots.

GPL v2 or later.
*/
#include "cldk_local.h"
#include "dk_misc.h"
#include "dk_buffer.h"
#include "dk_array.h"
#include "menu/dk_menup.h"
#include "menu/dk_menu.h"
#include "menu/goaceng.h"
#include <sys/stat.h>
#include <time.h>
#ifdef _WIN32
#include <io.h>
#include <direct.h>
#else
#include <dirent.h>
#include <fnmatch.h>
#include <unistd.h>
#endif

/*
=====================================================================
its cvars: mirrors of FTE's, read each frame the menu's up
=====================================================================
*/
static cvar_t *dkcvars;

static cvar_t *CvarFind(const char *name)
{
	cvar_t *v;
	for (v = dkcvars; v; v = v->next)
		if (!strcmp(v->name, name))
			return v;
	return NULL;
}
static void CvarRefresh(cvar_t *v)
{
	const char *s = CLDKF_CvarString(v->name);
	if (!v->string || strcmp(v->string, s))
	{
		free(v->string);
		v->string = strdup(s);
		v->value = (float)atof(s);
		v->modified = true;
	}
}
static cvar_t *CvarMirror(const char *name, const char *value, int flags)
{
	cvar_t *v = CvarFind(name);
	if (!v)
	{
		CLDKF_CvarCreate(name, value, flags & CVAR_ARCHIVE);
		v = (cvar_t *)calloc(1, sizeof(*v));
		v->name = strdup(name);
		v->flags = flags;
		v->next = dkcvars;
		dkcvars = v;
	}
	CvarRefresh(v);
	return v;
}
void CLDK_CvarsRefresh(void)
{
	cvar_t *v;
	for (v = dkcvars; v; v = v->next)
		CvarRefresh(v);
}
cvar_t *Cvar_Get(const char *name, const char *value, int flags)
{
	if (!name || !*name)
		return NULL;
	return CvarMirror(name, value, flags);
}
cvar_t *Cvar_Set(const char *name, const char *value)
{
	cvar_t *v;
	CLDKF_CvarSet(name, value);
	v = CvarFind(name);
	if (v)
		CvarRefresh(v);
	return v;
}
cvar_t *Cvar_ForceSet(const char *name, const char *value)
{
	return Cvar_Set(name, value);
}
cvar_t *Cvar_FullSet(const char *name, const char *value, int flags)
{
	CvarMirror(name, value, flags);
	return Cvar_Set(name, value);
}
void Cvar_SetValue(const char *name, float value)
{
	char s[64];
	if (value == (int)value)
		snprintf(s, sizeof(s), "%i", (int)value);
	else
		snprintf(s, sizeof(s), "%g", value);
	Cvar_Set(name, s);
}
float Cvar_VariableValue(const char *name)
{
	return (float)atof(CLDKF_CvarString(name));
}
char *Cvar_VariableString(const char *name)
{
	static char s[4][256];
	static int i;
	i = (i + 1) & 3;
	snprintf(s[i], sizeof(s[i]), "%s", CLDKF_CvarString(name));
	return s[i];
}

//its client's (cl_main.cpp's...) the menus have
cvar_t *deathmatch, *freelook, *lookstrafe, *m_forward, *m_pitch, *m_side, *m_yaw;
cvar_t *vid_gamma, *vid_ref, *modelname, *skinname, *skincolor, *character;
int cl_episode_num = 1;
userEpair_t *map_epairs;
crosshair_t crosshairList[MAX_CROSSHAIRS][MAX_CROSSHAIR_TYPES] =
{	//cl_scrn.cpp's
	{{"pics/crosshair/ch_notarget.tga",0,0}, {"pics/crosshair/ch_shoot.tga",0,0}, {"pics/crosshair/ch_use.tga",0,0}, {"pics/crosshair/ch_center.tga",0,0}},
	{{"pics/crosshair/ch_notarget2.tga",0,0}, {"pics/crosshair/ch_shoot2.tga",0,0}, {"pics/crosshair/ch_use2.tga",0,0}, {"pics/crosshair/ch_center2.tga",0,0}},
	{{"pics/crosshair/ch_notarget3.tga",0,0}, {"pics/crosshair/ch_shoot3.tga",0,0}, {"pics/crosshair/ch_use3.tga",0,0}, {"pics/crosshair/ch_center3.tga",0,0}},
	{{"pics/crosshair/ch_notarget4.tga",0,0}, {"pics/crosshair/ch_shoot4.tga",0,0}, {"pics/crosshair/ch_use4.tga",0,0}, {"pics/crosshair/ch_center4.tga",0,0}},
};

static void CLDK_MenuCvars(void)
{	//(its client registered these at its start)
	deathmatch = Cvar_Get("deathmatch", "0", CVAR_LATCH);
	freelook = Cvar_Get("freelook", "1", CVAR_ARCHIVE);
	lookstrafe = Cvar_Get("lookstrafe", "0", CVAR_ARCHIVE);
	m_forward = Cvar_Get("m_forward", "1", CVAR_ARCHIVE);
	m_pitch = Cvar_Get("m_pitch", "0.022", CVAR_ARCHIVE);
	m_side = Cvar_Get("m_side", "1", CVAR_ARCHIVE);
	m_yaw = Cvar_Get("m_yaw", "0.022", CVAR_ARCHIVE);
	vid_gamma = Cvar_Get("vid_gamma", "1", CVAR_ARCHIVE);
	vid_ref = Cvar_Get("vid_ref", "gl", CVAR_ARCHIVE);
	modelname = Cvar_Get("modelname", "models/global/m_hiro.dkm", CVAR_USERINFO | CVAR_ARCHIVE);
	skinname = Cvar_Get("skinname", "skins/hiro_bod_1.wal", CVAR_USERINFO | CVAR_ARCHIVE);
	skincolor = Cvar_Get("skincolor", "0", CVAR_USERINFO | CVAR_ARCHIVE);
	character = Cvar_Get("character", "0", CVAR_USERINFO | CVAR_ARCHIVE);
}

char *CM_KeyValue(userEpair_t *epair, char *key)
{	//(the map's worldspawn's: its menus only ask before there's one)
	static char empty[1];
	return empty;
}

/*
=====================================================================
its keys (Quake II's numbers) and their bindings: FTE's
=====================================================================
*/
char *keybindings[256];
static void CLDK_KeysRefresh(void)
{
	int k;
	for (k = 0; k < 256; k++)
	{
		const char *b = CLDKF_KeyBinding(k);
		if (!b != !keybindings[k] || (b && strcmp(b, keybindings[k])))
		{
			free(keybindings[k]);
			keybindings[k] = b?strdup(b):NULL;
		}
	}
}
void Key_SetBinding(int keynum, const char *binding)
{
	if (keynum < 0 || keynum >= 256)
		return;
	CLDKF_SetKeyBinding(keynum, binding);
	free(keybindings[keynum]);
	keybindings[keynum] = (binding && *binding)?strdup(binding):NULL;
}
char *Key_KeynumToString(int keynum)
{
	static char s[64];
	snprintf(s, sizeof(s), "%s", CLDKF_KeyName(keynum));
	return s;
}
int Key_IsDown(int key)
{
	return CLDKF_KeyDown(key);
}

/*
=====================================================================
files: the saved games and configs, where FTE writes the game's
=====================================================================
*/
static char *WriteDir(void)
{
	static char dir[MAX_OSPATH];
	snprintf(dir, sizeof(dir), "%s", CLDKF_WriteDir());
	return dir;
}
char *FS_Gamedir()				{return WriteDir();}
char *FS_SaveGameDir(void)		{return WriteDir();}
char *FS_ConfigDir()			{return WriteDir();}
void FS_AddGameDirectory(char *dir)	{}
void FS_CreatePath(char *path)
{	//the directories up to its last /
	char buf[MAX_OSPATH], *p;
	snprintf(buf, sizeof(buf), "%s", path);
	for (p = buf + 1; *p; p++)
		if (*p == '/')
		{
			*p = 0;
#ifdef _WIN32
			_mkdir(buf);
#else
			mkdir(buf, 0777);
#endif
			*p = '/';
		}
}
int FS_LoadFile(const char *path, void **buffer)
{
	int len;
	void *buf = CLDKF_LoadFile(path, &len);
	if (buffer)
		*buffer = buf;
	else if (buf)
		CLDKF_FreeFile(buf);
	return buf?len:-1;
}
void FS_FreeFile(void *buffer)
{
	if (buffer)
		CLDKF_FreeFile(buffer);
}

//its Sys_FindFirst: the entries of a directory that match (its q_shlinux.cpp's, after Windows' attributes)
static char findbase[MAX_OSPATH], findpattern[MAX_OSPATH], findpath[MAX_OSPATH];
#ifndef _WIN32
static DIR *fdir;
#endif
static bool FindAttributes(const char *name, unsigned musthave, unsigned canthave)
{
	struct stat st;
	char path[MAX_OSPATH];
	if (!strcmp(name, ".") || !strcmp(name, ".."))
		return false;
	snprintf(path, sizeof(path), "%s/%s", findbase, name);
	if (stat(path, &st) == -1)
		return false;
	if (S_ISDIR(st.st_mode) && (canthave & SFF_SUBDIR))
		return false;
	if ((musthave & SFF_SUBDIR) && !S_ISDIR(st.st_mode))
		return false;
	return true;
}
static char *FindNext(unsigned musthave, unsigned canthave)
{
#ifndef _WIN32
	struct dirent *d;
	if (!fdir)
		return NULL;
	while ((d = readdir(fdir)) != NULL)
		if ((!strcmp(findpattern, "*.")?!strchr(d->d_name, '.'):!fnmatch(findpattern, d->d_name, 0))	//(Windows' *.: no extension)
			&& FindAttributes(d->d_name, musthave, canthave))
		{
			snprintf(findpath, sizeof(findpath), "%s/%s", findbase, d->d_name);
			return findpath;
		}
#endif
	return NULL;
}
char *Sys_FindFirst(char *path, unsigned musthave, unsigned canthave)
{
	char *p;
	Sys_FindClose();
	snprintf(findbase, sizeof(findbase), "%s", path);
	if ((p = strrchr(findbase, '/')) != NULL)
	{
		*p = 0;
		snprintf(findpattern, sizeof(findpattern), "%s", p + 1);
	}
	else
		strcpy(findpattern, "*");
	if (!*findpattern || !strcmp(findpattern, "*.*"))
		strcpy(findpattern, "*");
#ifndef _WIN32
	if ((fdir = opendir(findbase)) == NULL)
		return NULL;
#endif
	return FindNext(musthave, canthave);
}
char *Sys_FindNext(unsigned musthave, unsigned canthave)
{
	return FindNext(musthave, canthave);
}
void Sys_FindClose(void)
{
#ifndef _WIN32
	if (fdir)
		closedir(fdir);
	fdir = NULL;
#endif
}
void Sys_Delete(char *path)
{
	remove(path);
}
void Sys_DateTime(char *path, char *date, int dateSize, char *timestr, int timeSize)
{
	struct stat st;
	struct tm *t;
	if (date && dateSize)
		*date = 0;
	if (timestr && timeSize)
		*timestr = 0;
	if (stat(path, &st) == -1 || !(t = localtime(&st.st_mtime)))
		return;
	if (date)
		strftime(date, dateSize, "%m/%d/%Y", t);
	if (timestr)
		strftime(timestr, timeSize, "%H:%M:%S", t);
}
int Sys_CompareFileDateTime(char *path1, char *path2)
{
	struct stat s1, s2;
	if (stat(path1, &s1) == -1 || stat(path2, &s2) == -1)
		return 0;
	return (s1.st_mtime < s2.st_mtime)?-1:((s1.st_mtime > s2.st_mtime)?1:0);
}
void Sys_Minimize(void)	{}

//saving: FTE's save command (the game writes its header with the comment: svdk_game.cpp's DK3_WriteGame)
short SV_DoSaveGame(char *dir, char *comment)
{
	char cmd[MAX_OSPATH+16];
	if (!CLDK_CanSave())
		return false;
	CLDKF_CvarSet("dk_savecomment", comment?comment:"");
	snprintf(cmd, sizeof(cmd), "save \"%s\"\n", dir);
	CLDKF_AddCommand(cmd);
	return 1;
}
bool CLDK_CanSave(void)
{	//its SV_CanSave: in a single player game, alive, not in a cinematic
	return !Cvar_VariableValue("deathmatch") && !Cvar_VariableValue("coop") && CLDKF_Stat(STAT_HEALTH) > 0
		&& !(CLDKF_RDFlags() & RDF_LETTERBOX);
}
void CL_WriteConfiguration(char *name)	{CLDKF_AddCommand("cfg_save\n");}	//(FTE's: its writeconfig wasn't)
void CL_SaveLastConfiguration(void)		{}
bool LoadBitmapPalette(const char *file_name, byte palette[128])	{return false;}	//(FTE's true colour)

/*
=====================================================================
video: modes as its menu lists them (FTE's vid_width/vid_height)
=====================================================================
*/
static const struct {int w, h;} dkmodes[] =
{
	{640, 480}, {800, 600}, {1024, 768}, {1280, 720}, {1280, 960}, {1280, 1024}, {1366, 768}, {1600, 900},
	{1600, 1200}, {1920, 1080}, {2560, 1440}, {3840, 2160},
};
int VID_GetModeCount(void)		{return sizeof(dkmodes)/sizeof(dkmodes[0]);}
char *VID_GetModeDescription(int mode)
{
	static char s[32];
	if (mode < 0 || mode >= VID_GetModeCount())
		return (char *)"";
	snprintf(s, sizeof(s), "%i x %i", dkmodes[mode].w, dkmodes[mode].h);
	return s;
}
void VID_RefreshModeList(void)	{}
void VID_SetGamma(float gamma)	{Cvar_SetValue("vid_gamma", gamma);}
float CalcFov(float fov_x, float width, float height)
{
	float a, x;
	if (fov_x < 1 || fov_x > 179)
		fov_x = 90;
	x = width / tan(fov_x / 360 * M_PI);
	a = atan(height / x);
	return a * 360 / M_PI;
}

/*
=====================================================================
addresses, server lists (its multiplayer menu)
=====================================================================
*/
qboolean NET_StringToAdr(char *s, netadr_t *a)
{
	int ip[4], port = 27992;
	memset(a, 0, sizeof(*a));
	if (!strcmp(s, "localhost"))
	{
		a->type = NA_LOOPBACK;
		return true;
	}
	if (sscanf(s, "%i.%i.%i.%i:%i", &ip[0], &ip[1], &ip[2], &ip[3], &port) < 4)
		return false;
	a->type = NA_IP;
	a->ip[0] = ip[0]; a->ip[1] = ip[1]; a->ip[2] = ip[2]; a->ip[3] = ip[3];
	a->port = (unsigned short)(((port & 0xff) << 8) | ((port >> 8) & 0xff));	//(network order)
	return true;
}
char *NET_AdrToString(netadr_t a)
{
	static char s[64];
	if (a.type == NA_LOOPBACK)
		return strcpy(s, "loopback");
	snprintf(s, sizeof(s), "%i.%i.%i.%i:%i", a.ip[0], a.ip[1], a.ip[2], a.ip[3], ((a.port & 0xff) << 8) | ((a.port >> 8) & 0xff));
	return s;
}
qboolean NET_CompareBaseAdr(netadr_t a, netadr_t b)
{
	if (a.type != b.type)
		return false;
	if (a.type == NA_LOOPBACK)
		return true;
	return !memcmp(a.ip, b.ip, 4);
}
qboolean NET_CompareAdr(netadr_t a, netadr_t b)
{
	return NET_CompareBaseAdr(a, b) && (a.type == NA_LOOPBACK || a.port == b.port);
}
void CL_PingServer(char *adrstring)	{}
void CL_PingServers_f(void)			{}
int LaunchBrowser(char *url)		{return 0;}
int LaunchMplayer(void)				{return 0;}

//its GameSpy SDK: no master servers, the lists stay empty
GServerList ServerListNew(char *gamename, char *enginename, char *seckey, int maxconcupdates, void *CallBackFn, int CallBackFnType, void *instance)
{
	static int dummy;
	return (GServerList)&dummy;
}
void ServerListFree(GServerList serverlist)					{}
GError ServerListUpdate(GServerList serverlist, gbool async)	{return GE_NOERROR;}
GError ServerListThink(GServerList serverlist)				{return GE_NOERROR;}
GError ServerListHalt(GServerList serverlist)				{return GE_NOERROR;}
GError ServerListClear(GServerList serverlist)				{return GE_NOERROR;}
GServerListState ServerListState(GServerList serverlist)	{return sl_idle;}
GServer ServerListGetServer(GServerList serverlist, int index)	{return NULL;}
int ServerListCount(GServerList serverlist)					{return 0;}
void ServerListSort(GServerList serverlist, gbool ascending, char *sortkey, GCompareMode comparemode)	{}
int ServerGetPing(GServer server)							{return 0;}
char *ServerGetAddress(GServer server)						{return (char *)"";}
char *ServerGetStringValue(GServer server, char *key, char *sdefault)	{return sdefault;}
int ServerGetIntValue(GServer server, char *key, int idefault)	{return idefault;}

/*
=====================================================================
the rest: input, sounds, the console
=====================================================================
*/
void IN_ActivateMouse(void)		{}
void IN_MenuMouseSetup(void)	{}
void IN_MenuMouseRelease(void)	{}
void Cbuf_Execute(void)			{}
int dk_GetSpecificAnimSequences(int modelindex, char *fName, frameData_t *pFrameData, char *seqName)	{return 0;}

static void CLDK_S_StopAllSounds(void)			{CLDKF_StopAllSounds();}
static void CLDK_S_BeginRegistration(void)		{}
static void CLDK_S_EndRegistration(int seq)		{}
static void CLDK_S_StopMusic(int chan)			{}
void (*S_StopAllSounds)(void) = CLDK_S_StopAllSounds;
void (*S_BeginRegistration)(void) = CLDK_S_BeginRegistration;
void (*S_EndRegistration)(int nFreeSequenceCode) = CLDK_S_EndRegistration;
void (*S_StopMusic)(int chan) = CLDK_S_StopMusic;

void M_ForceMenuOff(void)
{
	DKM_Leave();
	CLDKF_CloseMenu();
}
void M_PopMenu(void)
{
	M_ForceMenuOff();
}

/*
=====================================================================
FTE's menu's
=====================================================================
*/
void CLDK_MenuFrame(void);	//cldk_main.cpp's
void DKM_Init(void);			//menu/dk_menu.cpp's
static bool dkmenuinit;

//IonCommon's allocator (its csv, buffers...: the user module's, physics' memmgr), before the game's set it up:
//FTE's zone, as the game's (svdk_game.cpp's, with its tags past DKMEMTAG_BASE)
#include "memmgrcommon.h"
extern memory_import_t dkph_memmgr;
static void *CLDK_X_Malloc(size_t size, MEM_TAG tag)	{return CLDKF_TagMalloc(size, 0x4b00 + (int)tag);}
static void CLDK_X_Free(void *mem)						{if (mem) CLDKF_TagFree(mem);}

extern "C" void CLDK_MenuOpen(int button)
{
	CLDK_MenuFrame();
	if (!dkph_memmgr.X_Malloc)
	{
		dkph_memmgr.X_Malloc = CLDK_X_Malloc;
		dkph_memmgr.X_Free = CLDK_X_Free;
	}
	if (!dkmenuinit)
	{
		dkmenuinit = true;
		CLDK_MenuCvars();
		DKM_Init();
	}
	CLDK_CvarsRefresh();
	CLDK_KeysRefresh();
	cl_episode_num = CL_Episode();
	if (button >= 0 && button < DKMB_NUM_BUTTONS)
		DKM_Enter((dk_main_button)button);	//(its menu_main_force's)
	else
		DKM_Enter();
}
extern "C" int CLDK_MenuActive(void)
{
	return dkmenuinit && DKM_InMenu();
}
extern "C" void CLDK_MenuDraw(void)
{
	if (!CLDK_MenuActive())
		return;
	CLDK_MenuFrame();
	CLDK_CvarsRefresh();
	CLDK_KeysRefresh();
	DKM_Draw();
}
extern "C" void CLDK_MenuKey(int dkkey, int down)
{
	if (!CLDK_MenuActive())
		return;
	CLDK_MenuFrame();
	if (dkkey < 0)
		DKM_Leave();
	else
		DKM_Key(dkkey, down);
}
extern "C" void CLDK_MenuMouse(int x, int y)
{
	static int lastx = -1, lasty = -1;
	if (!CLDK_MenuActive() || (x == lastx && y == lasty))
		return;
	lastx = x;
	lasty = y;
	DKM_MousePos(x, y);
}
