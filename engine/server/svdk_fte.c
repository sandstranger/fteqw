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

//reliable data for a client (0-based), or everyone connected (-1), through the back buffers when there's much (its
//entities' state for a client that's begun is more than a message has room for)
void SVDK_ReliableWrite(int playernum, const void *data, int len)
{
	int i;
	for (i = 0; i < svs.allocated_client_slots; i++)
	{
		client_t *cl = &svs.clients[i];
		sizebuf_t *msg;
		if (playernum >= 0 && i != playernum)
			continue;
		if (cl->state < cs_connected || cl->protocol == SCP_BAD || cl->controller)
			continue;
		msg = ClientReliable_StartWrite(cl, len);
		SZ_Write(msg, data, len);
		ClientReliable_FinishWrite(cl);
	}
}

//the game to run, by the files there are
qboolean SVDK_IsDaikatana(void)
{
	return !strcmp(com_protocolname.string, "FTE-Daikatana");
}
#endif
