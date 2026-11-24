///////////////////////////////////////////////////////////////////////////////
//	dk_static.cpp
//
//	FTE: physics' DLL loader (dk_dll.cpp, dk_so.cpp) for world and weapons
//	linked into the engine. What a DLL exported, and so what GetProcAddress
//	found, is in dk_exports.cpp's tables; its dll_Entry is called the way the
//	loader called it.
///////////////////////////////////////////////////////////////////////////////

#include	<stdlib.h>
#include	<string.h>

#include	"p_global.h"
#include	"p_user.h"
#include	"dk_dll.h"
#include	"../fte/dk_exports.h"

typedef	void	(*dll_ClientDisconnect_t)		(userEntity_t *self);
typedef	void	(*dll_ClientBegin_t)			(userEntity_t *self, int loadgame);
typedef	int		(*dll_ClientConnect_t)			(userEntity_t *self, char *userinfo, int loadgame);
typedef	void	(*dll_ClientUserinfoChanged_t)	(edict_t *ent, char *userinfo);
typedef	void	(*dll_ClientThink_t)			(userEntity_t *ent, usercmd_t *ucmd, pmove_t *pm);
typedef	void	(*dll_ClientBeginServerFrame_t)	(userEntity_t *ent);
typedef	void	(*dll_think_stub_t)				(userEntity_t *ent);
typedef	void	(*dll_SetStats_t)				(userEntity_t *ent);
typedef	void	(*dll_UpdateBoundingBoxes_t)	(userEntity_t *ent);
typedef	void	(*dll_NPCAlert_t)				(userEntity_t *owner, userEntity_t *target);
typedef void	(*dll_BeginIntermission_t)		(char *nextMap);
typedef void	(*dll_LoadNodes_t)				( char *pMapName );
typedef void	(*dll_RegisterWorldFuncs_t)		();
typedef void	(*dll_EntityLoadCleanup_t)		( int nIndex );
typedef int		(*dll_FLAG_GetScores_t)			(teamInfo_t *scorearr, int maxscores );
typedef int		(*dll_FLAG_CheckRules_t)		();
typedef short	(*dll_DT_CanDamage_t)			(userEntity_t *self, userEntity_t *attacker, unsigned long damage_flags);

void	(*dll_ClientDisconnect)		(userEntity_t *self);
void	(*dll_ClientBegin)			(userEntity_t *self, int loadgame);
int		(*dll_ClientConnect)		(userEntity_t *self, char *userinfo, int loadgame);
void	(*dll_ClientUserinfoChanged)(edict_t *ent, char *userinfo);
void	(*dll_ClientThink)			(userEntity_t *ent, usercmd_t *ucmd, pmove_t *pm);
void	(*dll_ClientBeginServerFrame)	(userEntity_t *ent);
void	(*dll_think_stub)			(userEntity_t *ent);
void	(*dll_SetStats)				(userEntity_t *ent);
void	(*dll_UpdateBoundingBoxes)	(userEntity_t *ent);
void	(*dll_NPCAlert)				(userEntity_t *owner, userEntity_t *attacker);
void	(*dll_BeginIntermission)	(char *nextMap);
void	(*dll_LoadNodes)			( char *pMapName );
void	(*dll_RegisterWorldFuncs)	();
void	(*dll_EntityLoadCleanup)	( int nIndex );
int		(*dll_FLAG_GetScores)		(teamInfo_t *scorearr, int maxscores );
int		(*dll_FLAG_CheckRules)		();
short	(*dll_DT_CanDamage)			(userEntity_t *self, userEntity_t *attacker, unsigned long damage_flags);

int		dll_Loading = 0;

typedef struct
{
	const char			*name;
	const dkexport_t	*exports;
	int					numexports;
	dllEntry_t			*dll_Entry;
	int					loaded;
} dll_t;

//	in the order the loader went through them: world first, then the rest of the directory
static dll_t dlls[] =
{
	{"world",	dkexports_world,	0},
	{"weapons",	dkexports_weapons,	0},
};
#define MAX_DLLS (int)(sizeof(dlls)/sizeof(dlls[0]))

static int DLL_CompareExport (const void *key, const void *elem)
{
	return strcmp ((const char *)key, ((const dkexport_t *)elem)->name);
}

static void *DLL_Lookup (dll_t *dll, const char *funcName)
{
	const dkexport_t *e;
	if (!funcName)
		return NULL;
	e = (const dkexport_t *)bsearch (funcName, dll->exports, dll->numexports, sizeof(*dll->exports), DLL_CompareExport);
	return e?e->func:NULL;
}

//	world's own GetProcAddress (spawn.cpp, for ai functions by class name)
void *DLL_FindWorldFunction (const char *funcName)
{
	return DLL_Lookup (&dlls[0], funcName);
}

static void DLL_LoadStatic (dll_t *dll)
{
	int		size;
	char	*desc;

	if (dll->loaded)
		return;
	for (dll->numexports = 0; dll->exports[dll->numexports].name; dll->numexports++)
		;
	dll->dll_Entry = (dllEntry_t *)DLL_Lookup (dll, "dll_Entry");
	if (!dll->dll_Entry)
	{
		gi.Con_Dprintf("Skipping dll %s, no dll_Entry function.\n", dll->name);
		return;
	}
	size = IONSTORM_DLL_INTERFACE_VERSION;
	if (!dll->dll_Entry (NULL, QDLL_VERSION, &size))
	{
		gi.Con_Dprintf("Skipping dll %s, size mismatch\n", dll->name);
		return;
	}
	dll->loaded = 1;

	dll->dll_Entry (NULL, QDLL_SERVER_LOAD, &serverState);
	gi.Con_Dprintf("Physics: loaded %s.\n", dll->name);

	desc = NULL;
	dll->dll_Entry (NULL, QDLL_DESCRIPTION, &desc);
}

void *DLL_FindFunction (char *funcName)
{
	int		i;
	void	*func;

	for (i = 0; i < MAX_DLLS; i++)
	{
		if (dlls[i].loaded)
		{
			func = DLL_Lookup (&dlls[i], funcName);
			if (func)
				return func;
		}
	}
	return NULL;
}

#define REQFUNC(var,name)		var = (var##_t)DLL_FindFunction ((char*)name); if (!var) Sys_Error ("Unable to find " name " in dlls.")
#define OPTFUNC(var,name)		var = (var##_t)DLL_FindFunction ((char*)name)
void DLL_SetReqFunctions (void)
{
	REQFUNC (dll_ClientDisconnect, "dll_ClientDisconnect");
	REQFUNC (dll_ClientConnect, "dll_ClientConnect");
	REQFUNC (dll_ClientBegin, "dll_ClientBegin");
	REQFUNC (dll_ClientUserinfoChanged, "dll_ClientUserinfoChanged");
	REQFUNC (dll_ClientThink, "dll_ClientThink");
	REQFUNC (dll_ClientBeginServerFrame, "dll_ClientBeginServerFrame");
	OPTFUNC (dll_think_stub, "ai_think_stub");
	REQFUNC (dll_SetStats, "dll_SetStats");
	REQFUNC (dll_NPCAlert, "SIDEKICK_Alert");
	OPTFUNC (dll_UpdateBoundingBoxes, "ShowBoundingBoxes");
	REQFUNC (dll_BeginIntermission, "dll_BeginIntermission");
	REQFUNC (dll_LoadNodes, "dll_LoadNodes");
	REQFUNC (dll_EntityLoadCleanup, "dll_EntityLoadCleanup");
	REQFUNC (dll_RegisterWorldFuncs, "dll_RegisterWorldFuncs");
	REQFUNC (dll_FLAG_GetScores, "dll_FLAG_GetScores");
	REQFUNC (dll_FLAG_CheckRules, "dll_FLAG_CheckRules");
	REQFUNC (dll_DT_CanDamage, "dll_DT_CanDamage");
}

void DLL_InitFunctions (void)
{
	int i;
	for (i = 0; i < MAX_DLLS; i++)
	{
		if (dlls[i].loaded)
		{
			if (!dlls[i].dll_Entry (NULL, QDLL_SERVER_INIT, &serverState))
			{
				gi.Con_Dprintf("Unloaded %s\n", dlls[i].name);
				dlls[i].loaded = 0;
			}
		}
	}
}

void DLL_LoadLevelFunctions (void)
{
	int i;
	for (i = 0; i < MAX_DLLS; i++)
		if (dlls[i].loaded)
			dlls[i].dll_Entry (NULL, QDLL_LEVEL_LOAD, &serverState);
}

void DLL_ExitLevelFunctions (void)
{
	int i;
	for (i = 0; i < MAX_DLLS; i++)
		if (dlls[i].loaded)
			dlls[i].dll_Entry (NULL, QDLL_LEVEL_EXIT, &serverState);
}

void DLL_ExitFunctions (void)
{
	int i;
	for (i = 0; i < MAX_DLLS; i++)
		if (dlls[i].loaded)
			dlls[i].dll_Entry (NULL, QDLL_SERVER_KILL, &serverState);
}

void DLL_LoadDLLs (char *dirName)
{
	int i;
	dll_Loading = 1;
	for (i = 0; i < MAX_DLLS; i++)
		DLL_LoadStatic (&dlls[i]);
	DLL_SetReqFunctions ();
	dll_Loading = 0;
}

void DLL_UnloadDLLs (void)
{
	int i;
	DLL_ExitFunctions ();
	for (i = 0; i < MAX_DLLS; i++)
		dlls[i].loaded = 0;
	gi.RemoveDLLCommands ();
}

//	world.cpp's crash handler wants this (DUMPSTAK.CPP, Windows only)
void DumpStack (unsigned long)
{
}
