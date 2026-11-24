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
//how many more back buffers a client (0-based) has room for (what's more than they have room for drops them)
int SVDK_ReliableRoom(int playernum)
{
	client_t *cl;
	if (playernum < 0 || playernum >= svs.allocated_client_slots)
		return 0;
	cl = &svs.clients[playernum];
	if (cl->state < cs_connected || cl->protocol == SCP_BAD || cl->controller)
		return 0;
	return MAX_BACK_BUFFERS - cl->num_backbuf;
}

//which of the map's planes a trace hit (traces have copies of them): by a hash of them, made as the map changes
#define DKPLANEHASH 4096
static struct
{
	model_t *model;
	mplane_t *planes;
	int *first;		//[DKPLANEHASH]
	int *next;		//[numplanes]
} dkplanes;
static unsigned int SVDK_PlaneHash(const float *normal, float dist)
{
	int n[4] = {(int)floor(normal[0]*64+0.5), (int)floor(normal[1]*64+0.5), (int)floor(normal[2]*64+0.5), (int)floor(dist+0.5)};
	return ((unsigned)n[0]*73856093u ^ (unsigned)n[1]*19349663u ^ (unsigned)n[2]*83492791u ^ (unsigned)n[3]*2654435761u) % DKPLANEHASH;
}
int SVDK_PlaneIndex(const float *normal, float dist)
{
	model_t *m = sv.world.worldmodel;
	int i;
	if (!m || !m->planes || m->numplanes <= 0)
		return -1;
	if (dkplanes.model != m || dkplanes.planes != m->planes)
	{
		BZ_Free(dkplanes.first);
		BZ_Free(dkplanes.next);
		dkplanes.model = m;
		dkplanes.planes = m->planes;
		dkplanes.first = BZ_Malloc(sizeof(int)*DKPLANEHASH);
		dkplanes.next = BZ_Malloc(sizeof(int)*m->numplanes);
		for (i = 0; i < DKPLANEHASH; i++)
			dkplanes.first[i] = -1;
		for (i = m->numplanes; i-- > 0; )
		{
			unsigned int h = SVDK_PlaneHash(m->planes[i].normal, m->planes[i].dist);
			dkplanes.next[i] = dkplanes.first[h];
			dkplanes.first[h] = i;
		}
	}
	for (i = dkplanes.first[SVDK_PlaneHash(normal, dist)]; i >= 0; i = dkplanes.next[i])
	{
		const mplane_t *p = &m->planes[i];
		if (fabs(p->dist - dist) < 0.01 && fabs(p->normal[0] - normal[0]) < 0.001
			&& fabs(p->normal[1] - normal[1]) < 0.001 && fabs(p->normal[2] - normal[2]) < 0.001)
			return i;
	}
	return -1;
}

//the game to run, by the files there are
qboolean SVDK_IsDaikatana(void)
{
	return !strcmp(com_protocolname.string, "FTE-Daikatana");
}
#endif
