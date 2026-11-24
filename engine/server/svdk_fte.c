/*
What Daikatana's game (daikatana/fte/svdk_game.cpp) needs of FTE that Quake II's game API 3 hasn't. The game is
C++ with headers that clash with FTE's, so it gets these as plain functions.

GPL v2 or later.
*/
#include "quakedef.h"

#if defined(Q2SERVER) && defined(DAIKATANA)

void SVDK_CvarRead(cvar_t *var, const char **string, const char **latched, float *value, int *flags, int *modified)
{
	*string = var->string;
	*latched = var->latched_string;
	*value = var->value;
	*flags = var->flags;
	*modified = var->modifiedcount;
}

const char *SVDK_Gamedir(void)
{
	return FS_GetGamedir(true);
}

void *SVDK_LoadFile(const char *path, int *length)
{
	size_t len;
	void *buf;
	if (!path || !*path)
		return NULL;
	buf = FS_LoadMallocFile(path, &len);
	if (!buf)
		return NULL;
	*length = (int)len;
	return buf;
}
void SVDK_FreeFile(void *buf)
{
	BZ_Free(buf);
}

int SVDK_Milliseconds(void)
{
	return (int)Sys_Milliseconds();
}

int SVDK_ExeArgc(void)
{
	return com_argc;
}
const char *SVDK_ExeArgv(int i)
{
	if (i < 0 || i >= com_argc)
		return "";
	return com_argv[i];
}

int SVDK_IsLocalClient(int playernum)
{
	client_t *cl;
	if (playernum < 0 || playernum >= svs.allocated_client_slots)
		return -1;
	cl = &svs.clients[playernum];
	if (cl->state == cs_free)
		return -1;
	return cl->netchan.remote_address.type == NA_LOOPBACK;
}

//the game to run, by the files there are
qboolean SVDK_IsDaikatana(void)
{
	return !strcmp(com_protocolname.string, "FTE-Daikatana");
}
#endif
