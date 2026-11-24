/*
Daikatana's game (physics, which runs world and weapons) on FTE's Quake II server.

FTE's server speaks Quake II's game API 3. Daikatana's physics has another interface (user/physics.h): an edict
whose entity_state_t has much more in it (mins/maxs among it), CVectors, its own pmove and player state, traces
that report its surfaces, X_Malloc, FILE* file access, its own protocol for what it writes. This sits between
them the way server/svq2_rerelease.c does for the Quake II rerelease's game: FTE gets a game API 3 export table
and edicts that are copies kept in step with the game's own,

- the game's edicts are copied to FTE's after every call into the game, and as an entity is linked, so what FTE
  sends, links and traces against is what the game has; what FTE works out as it links goes back to the game;
- the game's calls are FTE's game API 3 imports, with Daikatana's on top: its pmove (dk_pmove.cpp), model frame
  names and animation sequences read from the models, files through FTE's filesystem, client-language strings
  formatted here;
- what it writes for its clients is Daikatana's protocol: what Quake II's has too goes out as that, the rest is
  dropped (for now).

GPL v2 or later.
*/
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dk_shared.h"
#include "qfiles.h"
#include "physics.h"		//the engine's view: edict_t is the part of userEntity_t it knows
#include "client_string.h"
#include "l__language.h"
#include "vertnormals.h"
#include "svdk_q2.h"
#include "dk_fteproto.h"
#include "dk_cin_types.h"
#include "dk_cin_playback.h"	//its engine's cinematics (cin_main.cpp)

extern "C" game_export_t *GetGameAPI (game_import_t *import);	//physics' (p_main.cpp)
extern edict_t *g_edicts;									//physics' too: freed here between levels
void DKPM_Pmove (pmove_t *pmove);							//dk_pmove.cpp
float DKPH_EdictMaxSpeed(struct edict_s *e);					//physics/dk_static.cpp

#define DKMEMTAG_BASE		0x4b00	//Daikatana's MEM_TAG_*s, among FTE's tags

static fq2::game_import_t	*fgi;			//FTE's game API 3 imports
static fq2::game_export_t	ge3;			//what FTE sees
static game_import_t		dkgi;			//what the game gets
static game_export_t		*dkge;
static fq2::edict_t			*shadow;		//FTE's copies of the game's edicts
static fq2::gclient_t		*shadowcl;		//and of its clients
static int					shadowmax;
static int					dkmaxclients;
static bool					dklevelloaded;	//a level's been spawned (so the next needs the last's cleared away)
static bool					dkloadinggame;	//a saved game's been read: its level's to spawn as such (then read)
static unsigned char		dkbegun[256];	//which clients the game's begun (only those does it disconnect)
static bool					dksentvalid[256];	//DK_SendStats has sent them their stats

#define DKEDICT(i)		((edict_t *)((char *)dkge->edicts + (size_t)(i)*dkge->edict_size))
#define DKNUM(e)		((int)(((char *)(e) - (char *)dkge->edicts) / dkge->edict_size))
#define SHADOWNUM(e)	((int)((fq2::edict_t *)(e) - shadow))

static fq2::edict_t *DK_ToShadow(const edict_t *e)
{
	int i;
	if (!e || !dkge)
		return NULL;
	i = DKNUM(e);
	if (i < 0 || i >= shadowmax)
		return NULL;
	return &shadow[i];
}
static edict_t *DK_FromShadow(const void *e)
{
	int i;
	if (!e)
		return NULL;
	i = SHADOWNUM(e);
	if (i < 0 || i >= shadowmax)
		return NULL;
	return DKEDICT(i);
}

static void DK_Printf(const char *fmt, ...)
{
	va_list argptr;
	char text[2048];
	va_start(argptr, fmt);
	vsnprintf(text, sizeof(text), fmt, argptr);
	va_end(argptr);
	fgi->dprintf("%s", text);
}
static cvar_t *DKGI_cvar(const char *name, const char *value, int flags);
void DKCIN_Printf(const char *fmt, ...)
{	//(for developers)
	va_list argptr;
	char text[2048];
	cvar_t *dev = DKGI_cvar("developer", "0", 0);
	if (!dev || !dev->value)
		return;
	va_start(argptr, fmt);
	vsnprintf(text, sizeof(text), fmt, argptr);
	va_end(argptr);
	fgi->dprintf("%s", text);
}

/*
=====================================================================
Edicts: the game's to FTE's and back
=====================================================================
*/
static short DK_Short8(float f)
{	//13.3 fixed point
	f *= 8;
	if (f > 32767) f = 32767;
	if (f < -32768) f = -32768;
	return (short)(f < 0 ? f - 0.5f : f + 0.5f);
}

static void DK_PlayerStateToShadow(fq2::player_state_t *o, const player_state_t *ps)
{
	int i;

	switch(ps->pmove.pm_type)
	{
	case PM_NORMAL:		o->pmove.pm_type = fq2::PM_NORMAL;		break;
	case PM_SPECTATOR:	o->pmove.pm_type = fq2::PM_SPECTATOR;	break;
	case PM_DEAD:		o->pmove.pm_type = fq2::PM_DEAD;		break;
	case PM_GIB:		o->pmove.pm_type = fq2::PM_GIB;			break;
	default:			o->pmove.pm_type = fq2::PM_FREEZE;		break;	//freeze, no input...
	}
	for (i = 0; i < 3; i++)
	{
		o->pmove.origin[i] = ps->pmove.origin[i];
		o->pmove.velocity[i] = ps->pmove.velocity[i];
		o->pmove.delta_angles[i] = ps->pmove.delta_angles[i];
	}
	if (ps->view_entity > 0 && ps->view_entity < shadowmax && DKEDICT(ps->view_entity)->inuse)
	{	//its engine's server sent where the view entity (a camera) is as the player's, still (sv_ents.cpp)
		const edict_t *v = DKEDICT(ps->view_entity);
		for (i = 0; i < 3; i++)
		{
			o->pmove.origin[i] = (short)(v->s.origin[i]*8);
			o->pmove.velocity[i] = 0;
		}
	}
	if (ps->pmove.pm_flags & PMF_CAMERAMODE)
		o->pmove.pm_type = fq2::PM_FREEZE;	//the view's the server's (its client didn't predict the angles)
	//its flags Quake II's have (FTE's client predicts with its pmove: daikatana/client/cldk_pred.cpp); not from
	//a view entity
	o->pmove.pm_flags = ps->pmove.pm_flags & 0x7f;
	if (ps->view_entity > 0)
		o->pmove.pm_flags |= PMF_NO_PREDICTION;
	o->pmove.pm_time = ps->pmove.pm_time;
	o->pmove.gravity = ps->pmove.gravity;

	for (i = 0; i < 3; i++)
	{
		o->viewangles[i] = ps->viewangles[i];
		o->viewoffset[i] = ps->viewoffset[i];
		o->kick_angles[i] = ps->kick_angles[i];
		o->gunangles[i] = ps->gunangles[i];
		o->gunoffset[i] = ps->gunoffset[i];
	}
	//the view model is an entity of its own, its client draws (RF_WEAPONMODEL)
	o->gunindex = o->gunframe = 0;

	for (i = 0; i < 4; i++)
		o->blend[i] = ps->blend[i];
	o->fov = ps->fov;
	o->rdflags = ps->rdflags;
	if (CIN_Running())
	{	//its cinematics: letterboxed (its client did it), the script's fov and fades
		o->rdflags |= RDF_LETTERBOX;
		if (CIN_RunningWithCamera())
		{
			o->fov = ScriptFOVCurrent();
			if (ScriptColorFading())
			{
				o->blend[0] = ScriptRCurrent()/SCRIPT_COLOR_MAX;
				o->blend[1] = ScriptGCurrent()/SCRIPT_COLOR_MAX;
				o->blend[2] = ScriptBCurrent()/SCRIPT_COLOR_MAX;
				o->blend[3] = ScriptACurrent()/SCRIPT_COLOR_MAX;
			}
		}
	}
	for (i = 0; i < fq2::Q2MAX_STATS; i++)
	{
		long v = ps->stats[i];
		o->stats[i] = (short)((v > 32767)?32767:((v < -32768)?-32768:v));
	}
}

//its renderfx and effects go as they are (FTE's client hands them to Daikatana's client code, which draws the
//entities: daikatana/client/cldk_ents.cpp); its beams have Quake II's RF_BEAM too (which is its RF_DEPTHHACK,
//nothing to a beam), for FTE's server to send their ends (old_origin) as they move
#define DK_RF_BEAMS		(RF_BEAM|RF_LIGHTRAY|RF_LIGHTNING|RF_BEAM_MOVING|RF_TRACER)
#define Q2RF_BEAM		0x00000080

static void DK_EdictToShadow(int i)
{
	edict_t *de = DKEDICT(i);
	fq2::edict_t *se = &shadow[i];
	int k;

	if (de->client && i >= 1 && i <= dkmaxclients)
	{
		se->client = &shadowcl[i-1];
		DK_PlayerStateToShadow(&se->client->ps, &de->client->ps);
	}
	else
		se->client = NULL;

	se->inuse = de->inuse;
	if (!de->inuse && !se->inuse)
		return;

	if (de->inuse)
		de->s.number = i;	//its engine's, as it sent them (the game tags a weapon with its owner's)
	se->s.number = i;
	for (k = 0; k < 3; k++)
	{
		se->s.origin[k] = de->s.origin[k];
		se->s.angles[k] = de->s.angles[k];
		se->s.old_origin[k] = de->s.old_origin[k];
		se->mins[k] = de->s.mins[k];
		se->maxs[k] = de->s.maxs[k];
	}
	se->s.modelindex = de->s.modelindex;
	se->s.modelindex2 = se->s.modelindex3 = se->s.modelindex4 = 0;	//(its, past Quake II's ranges: DK_SendEntX's)
	se->s.frame = de->s.frame;
	se->s.skinnum = de->s.skinnum;
	se->s.effects = de->s.effects;
	se->s.renderfx = de->s.renderfx | ((de->s.renderfx & DK_RF_BEAMS)?Q2RF_BEAM:0);
	se->s.sound = de->s.sound;
	se->s.event = 0;	//Daikatana's events aren't Quake II's
	//s.solid is FTE's to pack, as it links

	se->svflags = de->svflags & (SVF_NOCLIENT|SVF_DEADMONSTER|SVF_MONSTER);
	se->solid = de->solid;
	se->clipmask = de->clipmask;
	se->owner = DK_ToShadow(de->owner);
}

//what FTE worked out as it linked the entity
static void DK_ShadowLinkToEdict(int i)
{
	edict_t *de = DKEDICT(i);
	fq2::edict_t *se = &shadow[i];
	int k;
	for (k = 0; k < 3; k++)
	{
		de->absmin[k] = se->absmin[k];
		de->absmax[k] = se->absmax[k];
		de->size[k] = se->size[k];
	}
	de->linkcount = se->linkcount;
	de->areanum = se->areanum;
	de->areanum2 = se->areanum2;
	de->num_clusters = se->num_clusters;
	de->headnode = se->headnode;
	de->s.solid = se->s.solid;
	//area: the game only looks at whether it's linked
	de->area.prev = (link_t *)se->area.prev;
	de->area.next = (link_t *)se->area.next;
}

static void DK_SyncAll(void)
{
	int i, n;
	if (!dkge || !dkge->edicts)
		return;
	n = dkge->num_edicts;
	if (n > shadowmax)
		n = shadowmax;
	for (i = 0; i < n; i++)
		DK_EdictToShadow(i);
	for (; i < ge3.num_edicts && i < shadowmax; i++)
		shadow[i].inuse = false;	//the game has fewer now (a new level)
	ge3.num_edicts = n;
}

//FTE's to the game, before calling into it
static void DK_CvarsUpdate(void);
static void DK_PreCall(void)
{
	int i;
	DK_CvarsUpdate();
	if (!dkge || !dkge->edicts)
		return;
	for (i = 1; i <= dkmaxclients && i < shadowmax; i++)
	{
		edict_t *de = DKEDICT(i);
		de->s.number = i;	//its engine's, from the start (ClientConnect gives the weapons their owner's)
		if (de->client && shadow[i].client)
			de->client->ping = shadow[i].client->ping;
	}
}

/*
=====================================================================
cvars: the game holds pointers to its own cvar_t, kept in step with FTE's
=====================================================================
*/
typedef struct dkcvar_s
{
	cvar_t dk;
	fq2::cvar_t *var;
	int modifiedcount;
	struct dkcvar_s *nextmirror;
} dkcvar_t;
static dkcvar_t *dkcvars;

static void DK_CvarUpdate(dkcvar_t *m)
{
	const char *string, *latched;
	float value;
	int flags, modified;
	SVDK_CvarRead(m->var, &string, &latched, &value, &flags, &modified);
	m->dk.string = (char *)string;
	m->dk.latched_string = (char *)latched;
	m->dk.value = value;
	m->dk.flags = flags & (CVAR_ARCHIVE|CVAR_USERINFO|CVAR_SERVERINFO|CVAR_NOSET|CVAR_LATCH);
	if (m->modifiedcount != modified)
	{
		m->modifiedcount = modified;
		m->dk.modified = true;
	}
}
static void DK_CvarsUpdate(void)
{
	dkcvar_t *m;
	for (m = dkcvars; m; m = m->nextmirror)
		DK_CvarUpdate(m);
}
static cvar_t *DK_CvarMirror(fq2::cvar_t *v)
{
	dkcvar_t *m;
	if (!v)
		return NULL;
	for (m = dkcvars; m; m = m->nextmirror)
		if (m->var == v)
		{
			DK_CvarUpdate(m);
			return &m->dk;
		}
	m = (dkcvar_t *)calloc(1, sizeof(*m));
	m->var = v;
	m->modifiedcount = -1;
	m->nextmirror = dkcvars;
	dkcvars = m;
	DK_CvarUpdate(m);
	m->dk.modified = true;
	return &m->dk;
}
static void DK_CvarsFree(void)
{
	dkcvar_t *m;
	while ((m = dkcvars))
	{
		dkcvars = m->nextmirror;
		free(m->dk.name);
		free(m);
	}
}
static cvar_t *DKGI_cvar(const char *name, const char *value, int flags)
{
	cvar_t *c = DK_CvarMirror(fgi->cvar(name, value, flags));
	if (c && !c->name)
		c->name = strdup(name);
	return c;
}
//its engine's Cvar_Set and Cvar_ForceSet made the cvar if there wasn't one; FTE's game API 3 ones don't
static cvar_t *DKGI_cvar_set(const char *name, const char *value)
{
	cvar_t *c;
	fgi->cvar(name, value, 0);
	c = DK_CvarMirror(fgi->cvar_set(name, value));
	if (c && !c->name)
		c->name = strdup(name);
	return c;
}
static cvar_t *DKGI_cvar_forceset(const char *name, const char *value)
{
	cvar_t *c;
	fgi->cvar(name, value, 0);
	c = DK_CvarMirror(fgi->cvar_forceset(name, value));
	if (c && !c->name)
		c->name = strdup(name);
	return c;
}

/*
=====================================================================
configstrings: Daikatana's are where Quake II's are, with more models and sounds (512), and some of its own
=====================================================================
*/
static char *dkconfigstrings[MAX_CONFIGSTRINGS];

//FTE's (game API 3's) configstring for Daikatana's, or -1
static int DK_ConfigstringToQ2(int i)
{
	if (i == CS_NAME || i == CS_CDTRACK || i == CS_SKY || i == CS_MAXCLIENTS || i == CS_MAPCHECKSUM)
		return i;
	if (i == CS_STATUSBAR)
		return fq2::Q2CS_STATUSBAR;
	if (i >= CS_MODELS && i < CS_SOUNDS)
		return (i - CS_MODELS < fq2::Q2MAX_MODELS)?i - CS_MODELS + fq2::Q2CS_MODELS:-1;
	if (i >= CS_SOUNDS && i < CS_IMAGES)
		return (i - CS_SOUNDS < fq2::Q2MAX_SOUNDS)?i - CS_SOUNDS + fq2::Q2CS_SOUNDS:-1;
	if (i >= CS_IMAGES && i < CS_LIGHTS)
		return (i - CS_IMAGES < fq2::Q2MAX_IMAGES)?i - CS_IMAGES + fq2::Q2CS_IMAGES:-1;
	if (i >= CS_LIGHTS && i < CS_ITEMS)
		return (i - CS_LIGHTS < fq2::Q2MAX_LIGHTSTYLES)?i - CS_LIGHTS + fq2::Q2CS_LIGHTS:-1;
	if (i >= CS_ITEMS && i < CS_PLAYERSKINS)
		return (i - CS_ITEMS < fq2::Q2MAX_ITEMS)?i - CS_ITEMS + fq2::Q2CS_ITEMS:-1;
	if (i >= CS_PLAYERSKINS && i < MAX_CONFIGSTRINGS)
		return (i - CS_PLAYERSKINS < fq2::Q2MAX_CLIENTS)?i - CS_PLAYERSKINS + fq2::Q2CS_PLAYERSKINS:-1;
	return -1;
}
static void DK_SetConfigstring(int i, const char *val)
{
	if (i < 0 || i >= MAX_CONFIGSTRINGS)
		return;
	free(dkconfigstrings[i]);
	dkconfigstrings[i] = (val && *val)?strdup(val):NULL;
}
static void DKGI_configstring(int i, char *val)
{
	int q2 = DK_ConfigstringToQ2(i);
	DK_SetConfigstring(i, val);
	if (q2 >= 0)
		fgi->configstring(q2, val?val:"");
}
static char *DKGI_GetConfigString(int i)
{
	if (i < 0 || i >= MAX_CONFIGSTRINGS || !dkconfigstrings[i])
		return (char *)"";
	return dkconfigstrings[i];
}
static void DK_ConfigstringsClear(void)
{
	int i;
	for (i = 0; i < MAX_CONFIGSTRINGS; i++)
		free(dkconfigstrings[i]), dkconfigstrings[i] = NULL;
}
static int DKGI_modelindex(const char *name)
{
	int i;
	if (!name || !*name)
		return 0;
	i = fgi->modelindex(name);
	if (i > 0)
		DK_SetConfigstring(CS_MODELS+i, name);
	return i;
}
static int DKGI_soundindex(const char *name)
{
	int i;
	if (!name || !*name)
		return 0;
	i = fgi->soundindex(name);
	if (i > 0)
		DK_SetConfigstring(CS_SOUNDS+i, name);
	return i;
}
static int DKGI_imageindex(const char *name)
{
	int i;
	if (!name || !*name)
		return 0;
	i = fgi->imageindex(name);
	if (i > 0)
		DK_SetConfigstring(CS_IMAGES+i, name);
	return i;
}

/*
=====================================================================
prints
=====================================================================
*/
static void DKGI_bprintf(int level, char *fmt, ...)
{
	va_list argptr;
	char text[2048];
	va_start(argptr, fmt);
	vsnprintf(text, sizeof(text), fmt, argptr);
	va_end(argptr);
	fgi->bprintf(level, "%s", text);
}
static void DKGI_Con_Dprintf(char *fmt, ...)
{
	va_list argptr;
	char text[2048];
	va_start(argptr, fmt);
	vsnprintf(text, sizeof(text), fmt, argptr);
	va_end(argptr);
	fgi->dprintf("%s", text);
}
static void DKGI_cprintf(edict_t *ent, int level, char *fmt, ...)
{
	va_list argptr;
	char text[2048];
	va_start(argptr, fmt);
	vsnprintf(text, sizeof(text), fmt, argptr);
	va_end(argptr);
	fgi->cprintf(DK_ToShadow(ent), level, "%s", text);
}
static void DKGI_centerprintf(edict_t *ent, char *fmt, ...)
{
	va_list argptr;
	char text[2048];
	va_start(argptr, fmt);
	vsnprintf(text, sizeof(text), fmt, argptr);
	va_end(argptr);
	if (ent)
		fgi->centerprintf(DK_ToShadow(ent), "%s", text);
}
static void DKGI_centerprint(edict_t *ent, float msg_time, char *fmt, ...)
{
	va_list argptr;
	char text[2048];
	va_start(argptr, fmt);
	vsnprintf(text, sizeof(text), fmt, argptr);
	va_end(argptr);
	if (ent)
		fgi->centerprintf(DK_ToShadow(ent), "%s", text);
}
static void DKGI_Error(char *fmt, ...)
{
	va_list argptr;
	char text[2048];
	va_start(argptr, fmt);
	vsnprintf(text, sizeof(text), fmt, argptr);
	va_end(argptr);
	fgi->error("%s", text);
}
static void DKGI_printxy(int x, int y, char *fmt, ...) {}
static void DKGI_clearxy(void) {}

/*
=====================================================================
sounds, music
=====================================================================
*/
//Quake II's attenuation from Daikatana's distances: inaudible at 1000/attenuation
static float DK_Attenuation(float dist_min, float dist_max)
{
	float attn;
	if (dist_max <= 0)
		return 1;	//ATTN_NORM
	attn = 1000 / dist_max;
	if (attn > 3.98f)
		attn = 3.98f;
	if (attn < 0.0f)
		attn = 0;
	return attn;
}
static void DKGI_StartSound(const CVector &origin, edict_t *ent, int channel, int soundindex, float volume, float dist_min, float dist_max, float timeofs)
{
	float attn = DK_Attenuation(dist_min, dist_max);
	//FTE stops on what's out of Quake II's range
	if (volume > 1)
		volume = 1;
	if (!(volume >= 0))
		volume = 0;
	if (!(timeofs >= 0))
		timeofs = 0;
	if (timeofs > 0.255f)
		timeofs = 0.255f;
	if (soundindex <= 0)
		return;
	if (ent && !origin.x && !origin.y && !origin.z)
		fgi->sound(DK_ToShadow(ent), channel & 7, soundindex, volume, attn, timeofs);
	else
	{
		float o[3] = {origin.x, origin.y, origin.z};
		fgi->positioned_sound(o, ent?DK_ToShadow(ent):&shadow[0], channel & 7, soundindex, volume, attn, timeofs);
	}
}
static void DKGI_set_reverb_preset(int preset) {}
static void DKGI_streamed_sound(const char *name, float vol) {}
//its music and voices, as its engine's server sent them (sv_send.cpp: reliably here, they're not to miss)
static void DKGI_WriteByte(int c);
static void DKGI_WriteShort(int c);
static void DKGI_WriteFloat(float f);
static void DKGI_WriteString(char *s);
static void DKGI_multicast(const CVector &origin, multicast_t to);
static void DKGI_unicast(edict_t *ent, qboolean reliable);
static void DKGI_StartMusic(const char *name, int chan, edict_t *ent)
{
	if (!name)
		return;
	DKGI_WriteByte(SVC_START_MUSIC);
	DKGI_WriteString((char *)name);
	DKGI_WriteShort(chan);
	if (ent)
		DKGI_unicast(ent, true);
	else
		DKGI_multicast(CVector(0, 0, 0), MULTICAST_ALL_R);
}
static void DKGI_StartMP3(const char *name, int chan, float fvol, int play_count, qboolean bRestart, edict_t *ent)
{
	if (!name)
		return;
	DKGI_WriteByte(SVC_START_MP3);
	DKGI_WriteString((char *)name);
	DKGI_WriteShort(chan);
	DKGI_WriteFloat(fvol);
	DKGI_WriteShort(play_count);
	DKGI_WriteByte(bRestart);
	if (ent)
		DKGI_unicast(ent, true);
	else
		DKGI_multicast(CVector(0, 0, 0), MULTICAST_ALL_R);
}
static void DKGI_StopMP3(int chan, edict_t *ent)
{
	DKGI_WriteByte(SVC_STOP_MP3);
	DKGI_WriteShort(chan);
	if (ent)
		DKGI_unicast(ent, true);
	else
		DKGI_multicast(CVector(0, 0, 0), MULTICAST_ALL_R);
}

/*
=====================================================================
collision
=====================================================================
*/
//Daikatana's surfaces have more in them (an index, a colour): these are FTE's, with that left at 0
typedef struct dksurf_s
{
	const fq2::csurface_t *q2;
	csurface_t dk;
	struct dksurf_s *next;
} dksurf_t;
static dksurf_t *dksurfs[256];
static csurface_t dknullsurf;
static csurface_t *DK_Surface(const fq2::csurface_t *s)
{
	dksurf_t *d;
	size_t h;
	if (!s)
		return &dknullsurf;
	h = (((size_t)s) >> 4) & 255;
	for (d = dksurfs[h]; d; d = d->next)
		if (d->q2 == s)
			return &d->dk;
	d = (dksurf_t *)calloc(1, sizeof(*d));
	d->q2 = s;
	memcpy(d->dk.name, s->name, sizeof(d->dk.name));
	d->dk.flags = s->flags;
	d->dk.value = s->value;
	d->next = dksurfs[h];
	dksurfs[h] = d;
	return &d->dk;
}
static void DK_SurfacesClear(void)
{
	int h;
	dksurf_t *d;
	for (h = 0; h < 256; h++)
		while ((d = dksurfs[h]))
		{
			dksurfs[h] = d->next;
			free(d);
		}
}

static trace_t DK_Trace(const fq2::trace_t &t)
{
	trace_t o;
	memset(&o, 0, sizeof(o));
	o.allsolid = t.allsolid;
	o.startsolid = t.startsolid;
	o.fraction = t.fraction;
	o.endpos.Set(t.endpos[0], t.endpos[1], t.endpos[2]);
	o.plane.normal.Set(t.plane.normal[0], t.plane.normal[1], t.plane.normal[2]);
	o.plane.dist = t.plane.dist;
	o.plane.type = t.plane.type;
	o.plane.signbits = t.plane.signbits;
	if (t.fraction < 1)
	{	//its engine's: which of the map's planes it hit (its client puts the surface sprites the server tags on it)
		int pi = SVDK_PlaneIndex(t.plane.normal, t.plane.dist);
		o.plane.planeIndex = (pi < 0)?0xffff:pi;
	}
	o.surface = DK_Surface(t.surface);
	o.contents = t.contents;
	o.ent = DK_FromShadow(t.ent);
	return o;
}
static trace_t DKGI_TraceBox(const CVector &start, const CVector &mins, const CVector &maxs, const CVector &end, edict_t *passent, int contentmask)
{
	float s[3] = {start.x, start.y, start.z};
	float mn[3] = {mins.x, mins.y, mins.z};
	float mx[3] = {maxs.x, maxs.y, maxs.z};
	float e[3] = {end.x, end.y, end.z};
	return DK_Trace(fgi->trace(s, mn, mx, e, DK_ToShadow(passent), contentmask));
}
static trace_t DKGI_TraceLine(const CVector &start, const CVector &end, edict_t *passent, int contentmask)
{
	float s[3] = {start.x, start.y, start.z};
	float z[3] = {0, 0, 0};
	float e[3] = {end.x, end.y, end.z};
	return DK_Trace(fgi->trace(s, z, z, e, DK_ToShadow(passent), contentmask));
}
//a trace that hits triggers (with nAcceptFlag in their svflags) too: the closest of a line against the world
//and anything solid, and the triggers in the way
static trace_t DKGI_TraceLineTrigger(const CVector &start, const CVector &end, edict_t *passent, int contentmask, unsigned int nAcceptFlag)
{
	trace_t tr = DKGI_TraceLine(start, end, passent, contentmask);
	fq2::edict_t *list[256];
	float mins[3], maxs[3];
	int i, n, k;
	for (k = 0; k < 3; k++)
	{
		mins[k] = ((start[k] < tr.endpos[k])?start[k]:tr.endpos[k]) - 1;
		maxs[k] = ((start[k] > tr.endpos[k])?start[k]:tr.endpos[k]) + 1;
	}
	n = fgi->BoxEdicts(mins, maxs, list, 256, 2);	//AREA_TRIGGERS
	for (i = 0; i < n; i++)
	{
		edict_t *de = DK_FromShadow(list[i]);
		float t0 = 0, t1 = tr.fraction, d;
		CVector dir = end - start;
		if (!de || de == passent || !(de->svflags & nAcceptFlag))
			continue;
		//the line against the trigger's box
		for (k = 0; k < 3 && t0 <= t1; k++)
		{
			if (!dir[k])
			{
				if (start[k] < de->absmin[k] || start[k] > de->absmax[k])
					t0 = 2;
				continue;
			}
			float a = (de->absmin[k] - start[k]) / dir[k];
			float b = (de->absmax[k] - start[k]) / dir[k];
			if (a > b)
				d = a, a = b, b = d;
			if (a > t0)
				t0 = a;
			if (b < t1)
				t1 = b;
		}
		if (t0 <= t1 && t0 < tr.fraction)
		{
			tr.fraction = t0;
			tr.endpos = start + dir*t0;
			tr.ent = de;
		}
	}
	return tr;
}
static int dktracecount;
static int DKGI_GetTraceCount(void)
{
	return dktracecount;
}
static int DKGI_pointcontents(const CVector &p)
{
	float o[3] = {p.x, p.y, p.z};
	return fgi->pointcontents(o);
}
static qboolean DKGI_inPVS(const CVector &p1, const CVector &p2)
{
	float a[3] = {p1.x, p1.y, p1.z}, b[3] = {p2.x, p2.y, p2.z};
	return fgi->inPVS(a, b);
}
static qboolean DKGI_inPHS(const CVector &p1, const CVector &p2)
{
	float a[3] = {p1.x, p1.y, p1.z}, b[3] = {p2.x, p2.y, p2.z};
	return fgi->inPHS(a, b);
}
static void DKGI_SetAreaPortalState(int portalnum, qboolean open)
{
	fgi->SetAreaPortalState(portalnum, open);
}
static qboolean DKGI_AreasConnected(int a1, int a2)
{
	return fgi->AreasConnected(a1, a2);
}
static void DKGI_linkentity(edict_t *ent)
{
	int i = DKNUM(ent);
	if (i < 0 || i >= shadowmax)
		return;
	if (i >= ge3.num_edicts)
		ge3.num_edicts = i+1;
	DK_EdictToShadow(i);
	fgi->linkentity(&shadow[i]);
	DK_ShadowLinkToEdict(i);
}
static void DKGI_unlinkentity(edict_t *ent)
{
	int i = DKNUM(ent);
	if (i < 0 || i >= shadowmax)
		return;
	fgi->unlinkentity(&shadow[i]);
	DK_ShadowLinkToEdict(i);
}
static int DKGI_BoxEdicts(const CVector &mins, const CVector &maxs, edict_t **list, int maxcount, int areatype)
{
	fq2::edict_t *q2list[1024];
	float mn[3] = {mins.x, mins.y, mins.z}, mx[3] = {maxs.x, maxs.y, maxs.z};
	int i, n;
	if (maxcount > 1024)
		maxcount = 1024;
	n = fgi->BoxEdicts(mn, mx, q2list, maxcount, areatype);
	for (i = 0; i < n; i++)
		list[i] = DK_FromShadow(q2list[i]);
	return n;
}
static void DKGI_setmodel(edict_t *ent, char *name)
{
	int i;
	if (!name)
	{
		ent->s.modelindex = 0;
		return;
	}
	i = DKGI_modelindex(name);
	ent->s.modelindex = i;
	if (name[0] == '*')
	{	//a brush model's size: FTE has it
		fq2::edict_t *se = DK_ToShadow(ent);
		int k;
		if (!se)
			return;
		fgi->setmodel(se, name);
		for (k = 0; k < 3; k++)
		{
			ent->s.mins[k] = se->mins[k];
			ent->s.maxs[k] = se->maxs[k];
		}
		DKGI_linkentity(ent);
	}
}
static void DKGI_Pmove(pmove_t *pm)
{
	DKPM_Pmove(pm);
}

/*
=====================================================================
messages: what the game writes is Daikatana's protocol, Quake II's where it has the same
=====================================================================
*/
static unsigned char dkmsg[8192];
static int dkmsglen;
static bool dkmsgoverflow;
static int dkdropped[64];

static void DK_MsgWrite(const void *data, int len)
{
	if (dkmsglen + len > (int)sizeof(dkmsg))
	{
		dkmsgoverflow = true;
		return;
	}
	memcpy(dkmsg + dkmsglen, data, len);
	dkmsglen += len;
}
static void DKGI_WriteChar(int c)	{signed char b = c; DK_MsgWrite(&b, 1);}
static void DKGI_WriteByte(int c)	{unsigned char b = c; DK_MsgWrite(&b, 1);}
static void DKGI_WriteShort(int c)	{unsigned char b[2] = {(unsigned char)(c&0xff), (unsigned char)((c>>8)&0xff)}; DK_MsgWrite(b, 2);}
static void DKGI_WriteLong(int c)	{unsigned char b[4] = {(unsigned char)(c&0xff), (unsigned char)((c>>8)&0xff), (unsigned char)((c>>16)&0xff), (unsigned char)((c>>24)&0xff)}; DK_MsgWrite(b, 4);}
static void DKGI_WriteFloat(float f)	{union {float f; int i;} u; u.f = f; DKGI_WriteLong(u.i);}
static void DKGI_WriteString(char *s)	{if (!s) s = (char *)""; DK_MsgWrite(s, (int)strlen(s)+1);}
static void DKGI_WritePosition(const CVector &p)	{DKGI_WriteShort((int)(p.x*8)); DKGI_WriteShort((int)(p.y*8)); DKGI_WriteShort((int)(p.z*8));}
static CVertexNormals dkvertexnormals;	//its own 255 (MSG_WriteDir's)
static void DKGI_WriteDir(const CVector &p)
{
	int best;
	if (!p.x && !p.y && !p.z)
		best = NUMVERTEXNORMALS;
	else
		best = dkvertexnormals.GetClosestVertNormal(p);
	DKGI_WriteByte(best);
}
static void DKGI_WriteAngle(float f)	{DKGI_WriteShort((int)(f*65536/360) & 65535);}	//MSG_WriteAngle16

//a reader of what's been written
typedef struct
{
	const unsigned char *data;
	int len, pos;
	bool bad;
} dkreader_t;
static int DK_ReadByte(dkreader_t *r)
{
	if (r->pos + 1 > r->len)
		return r->bad = true, -1;
	return r->data[r->pos++];
}
static int DK_ReadShort(dkreader_t *r)
{
	int v;
	if (r->pos + 2 > r->len)
		return r->bad = true, -1;
	v = (short)(r->data[r->pos] | (r->data[r->pos+1]<<8));
	r->pos += 2;
	return v;
}
static int DK_ReadLong(dkreader_t *r)
{
	int v;
	if (r->pos + 4 > r->len)
		return r->bad = true, -1;
	v = r->data[r->pos] | (r->data[r->pos+1]<<8) | (r->data[r->pos+2]<<16) | (r->data[r->pos+3]<<24);
	r->pos += 4;
	return v;
}
static float DK_ReadFloat(dkreader_t *r)
{
	union {float f; int i;} u;
	u.i = DK_ReadLong(r);
	return u.f;
}
static const char *DK_ReadString(dkreader_t *r)
{
	const char *s = (const char *)r->data + r->pos;
	while (r->pos < r->len && r->data[r->pos])
		r->pos++;
	if (r->pos >= r->len)
		return r->bad = true, "";
	r->pos++;
	return s;
}

//what of the message Quake II's clients can have, written to FTE; false if nothing
static bool DK_TranslateMessage(void)
{
	dkreader_t r = {dkmsg, dkmsglen, 0, false};
	bool any = false;
	if (dkmsgoverflow)
	{
		DK_Printf("Daikatana: message overflowed\n");
		return false;
	}
	while (r.pos < r.len && !r.bad)
	{
		int start = r.pos;
		int svc = DK_ReadByte(&r);
		switch(svc)
		{
		case SVC_LAYOUT:
			fgi->WriteByte(fq2::svc_layout);
			fgi->WriteString(DK_ReadString(&r));
			any = true;
			break;
		case SVC_PRINT:
			{
				int level = DK_ReadByte(&r);
				const char *s = DK_ReadString(&r);
				fgi->WriteByte(fq2::svc_print);
				fgi->WriteByte(level);
				fgi->WriteString(s);
				any = true;
			}
			break;
		case SVC_STUFFTEXT:
			fgi->WriteByte(fq2::svc_stufftext);
			fgi->WriteString(DK_ReadString(&r));
			any = true;
			break;
		case SVC_CENTERPRINT:
			fgi->WriteByte(fq2::svc_centerprint);
			fgi->WriteString(DK_ReadString(&r));
			DK_ReadShort(&r);	//how long, in 8ths of a second
			any = true;
			break;
		case SVC_NOP:
			break;
		default:
			//Daikatana's own, all of the message from here on: as it is, for FTE's clients' Daikatana code
			//(daikatana/client), in an svc of FTE's own with its length
			fgi->WriteByte(svcq2dk_message);
			fgi->WriteShort(r.len - start);
			for (; start < r.len; start++)
				fgi->WriteByte(r.data[start]);
			return true;
		}
	}
	return any;
}

//what Quake II's player state has no room for: its stats are longs (experience points...), its pmove's flags
//(the camera's...); sent when they change
static long dksentstats[256][MAX_STATS];
static int dksentpmflags[256], dksentpmtype[256];
static float dksentmaxspeed[256];
static void DK_SendStats(void)
{
	int i, k;
	for (i = 1; i <= dkmaxclients && i < 256 && i < shadowmax; i++)
	{
		edict_t *de = DKEDICT(i);
		if (!de->inuse || !de->client || !dkbegun[i])
		{
			dksentvalid[i] = false;
			continue;
		}
		if (dksentvalid[i] && !memcmp(dksentstats[i], de->client->ps.stats, sizeof(dksentstats[i]))
			&& dksentpmflags[i] == de->client->ps.pmove.pm_flags && dksentpmtype[i] == (int)de->client->ps.pmove.pm_type
			&& dksentmaxspeed[i] == DKPH_EdictMaxSpeed(de))
			continue;
		memcpy(dksentstats[i], de->client->ps.stats, sizeof(dksentstats[i]));
		dksentpmflags[i] = de->client->ps.pmove.pm_flags;
		dksentpmtype[i] = (int)de->client->ps.pmove.pm_type;
		dksentmaxspeed[i] = DKPH_EdictMaxSpeed(de);
		dksentvalid[i] = true;
		fgi->WriteByte(svcq2dk_message);
		fgi->WriteShort(1 + MAX_STATS*4 + 4 + 1 + 4);
		fgi->WriteByte(svcdk_fte_stats);
		for (k = 0; k < MAX_STATS; k++)
			fgi->WriteLong((int)de->client->ps.stats[k]);
		fgi->WriteLong(dksentpmflags[i]);
		fgi->WriteByte(dksentpmtype[i]);
		{	//its pmove's max_speed (the player's: its skills, boosts), for its client's prediction
			union {float f; int i;} u;
			u.f = dksentmaxspeed[i];
			fgi->WriteLong(u.i);
		}
		fgi->unicast(&shadow[i], true);
	}
}

/*
=====================================================================
what of its entities' state Quake II's has no room for (its client draws them, and animates them itself:
daikatana/client/cldk_ents.cpp): sent reliably to everyone as it changes, all of it to a client as it begins
=====================================================================
*/
typedef struct
{
	int				modelindex2, modelindex3, modelindex4;
	unsigned int	effects2, flags;
	float			alpha;
	float			color[3], render_scale[3], mins[3], maxs[3], angle_delta[3], origin_hack[3];
	short			frameFlags, startFrame, endFrame, startLoop, endLoop, loopCount, sound1Frame, sound2Frame;
	unsigned char	sound1Index, sound2Index, frameInc;
	float			modelAnimSpeed;
	int				numClusters;
	int				clusterIdx[MAX_MESH_CLUSTERS];
	float			clusterAngles[MAX_MESH_CLUSTERS][3];
	unsigned char	snd_flags;
	float			volume, dist_min, dist_max;
} dkentx_t;
static dkentx_t	*dkentx;		//[shadowmax]: what everyone has
static const dkentx_t dkentxnull = {0};

static void DK_EntXFrom(dkentx_t *x, const edict_t *de)
{
	const entity_state_t *s = &de->s;
	const frameInfo_t *fi = &s->frameInfo;
	int k;
	memset(x, 0, sizeof(*x));
	if (!de->inuse)
		return;
	x->modelindex2 = s->modelindex2;
	x->modelindex3 = s->modelindex3;
	x->modelindex4 = s->modelindex4;
	x->effects2 = s->effects2;
	x->flags = s->flags;
	x->alpha = s->alpha;
	for (k = 0; k < 3; k++)
	{
		x->color[k] = s->color[k];
		x->render_scale[k] = s->render_scale[k];
		x->mins[k] = s->mins[k];
		x->maxs[k] = s->maxs[k];
		x->angle_delta[k] = s->angle_delta[k];
		x->origin_hack[k] = s->origin_hack[k];
	}
	//its engine's: the frame rate as flags, what was forced not
	x->frameFlags = fi->frameFlags & ~(FRAME_FPS10|FRAME_FPS30|FRAME_FORCEINDEX);
	if (fi->frameTime >= FRAMETIME_FPS10)
		x->frameFlags |= FRAME_FPS10;
	else if (fi->frameTime <= FRAMETIME_FPS30 && fi->frameTime > 0)
		x->frameFlags |= FRAME_FPS30;
	x->startFrame = fi->startFrame;
	x->endFrame = fi->endFrame;
	x->startLoop = fi->startLoop;
	x->endLoop = fi->endLoop;
	x->loopCount = fi->loopCount;
	x->sound1Frame = fi->sound1Frame;
	x->sound2Frame = fi->sound2Frame;
	x->sound1Index = (unsigned char)fi->sound1Index;
	x->sound2Index = (unsigned char)fi->sound2Index;
	x->frameInc = (unsigned char)fi->frameInc;
	x->modelAnimSpeed = fi->modelAnimSpeed;
	x->numClusters = (s->numClusters < 0)?0:((s->numClusters > MAX_MESH_CLUSTERS)?MAX_MESH_CLUSTERS:s->numClusters);
	for (k = 0; k < x->numClusters; k++)
	{
		x->clusterIdx[k] = s->cDef[k].clusterIdx;
		x->clusterAngles[k][0] = s->cDef[k].angles.x;
		x->clusterAngles[k][1] = s->cDef[k].angles.y;
		x->clusterAngles[k][2] = s->cDef[k].angles.z;
	}
	x->snd_flags = s->snd_flags;
	x->volume = s->volume;
	x->dist_min = s->dist_min;
	x->dist_max = s->dist_max;
}

//the ops going out: an svcq2dk_message each so many, through FTE's back buffers (there can be more than a
//message has room for)
static unsigned char	dkxbuf[3+1024];
static int				dkxlen = 3;
static int				dkxto = -1;		//the player; -1: everyone
static void DKX_Flush(void)
{
	if (dkxlen <= 3)
		return;
	dkxbuf[0] = svcq2dk_message;
	dkxbuf[1] = (dkxlen-3) & 0xff;
	dkxbuf[2] = ((dkxlen-3)>>8) & 0xff;
	SVDK_ReliableWrite(dkxto, dkxbuf, dkxlen);
	dkxlen = 3;
}
static void DKX_Byte(int c)		{dkxbuf[dkxlen++] = (unsigned char)c;}
static void DKX_Short(int c)	{DKX_Byte(c); DKX_Byte(c>>8);}
static void DKX_Long(int c)		{DKX_Byte(c); DKX_Byte(c>>8); DKX_Byte(c>>16); DKX_Byte(c>>24);}
static void DKX_Float(float f)	{union {float f; int i;} u; u.f = f; DKX_Long(u.i);}
static void DKX_Floats(const float *f, int n)	{while (n-- > 0) DKX_Float(*f++);}

//an entity's op, from what they have to what it is (and its frame to sync to if sync); nothing if no change
static void DKX_Write(int entnum, const dkentx_t *from, const dkentx_t *to, bool reset, bool sync, int frame)
{
	int bits = 0, k;
	if (reset)
		bits |= DKX_RESET, from = &dkentxnull;
	if (to->modelindex2 != from->modelindex2)	bits |= DKX_MODEL2;
	if (to->modelindex3 != from->modelindex3)	bits |= DKX_MODEL3;
	if (to->modelindex4 != from->modelindex4)	bits |= DKX_MODEL4;
	if (to->effects2 != from->effects2)			bits |= DKX_EFFECTS2;
	if (to->flags != from->flags)				bits |= DKX_FLAGS;
	if (to->alpha != from->alpha)				bits |= DKX_ALPHA;
	if (memcmp(to->color, from->color, sizeof(to->color)))					bits |= DKX_COLOR;
	if (memcmp(to->render_scale, from->render_scale, sizeof(to->render_scale)))	bits |= DKX_SCALE;
	if (memcmp(to->mins, from->mins, sizeof(to->mins)) || memcmp(to->maxs, from->maxs, sizeof(to->maxs)))	bits |= DKX_MINSMAXS;
	if (memcmp(to->angle_delta, from->angle_delta, sizeof(to->angle_delta)))	bits |= DKX_ANGLEDELTA;
	if (memcmp(to->origin_hack, from->origin_hack, sizeof(to->origin_hack)))	bits |= DKX_ORIGINHACK;
	if (to->frameFlags != from->frameFlags || to->startFrame != from->startFrame || to->endFrame != from->endFrame
		|| to->startLoop != from->startLoop || to->endLoop != from->endLoop)
		bits |= DKX_FRAMEINFO, sync = true;		//(its engine synced the frame on these)
	if (to->loopCount != from->loopCount || to->sound1Frame != from->sound1Frame || to->sound2Frame != from->sound2Frame
		|| to->sound1Index != from->sound1Index || to->sound2Index != from->sound2Index || to->frameInc != from->frameInc
		|| to->modelAnimSpeed != from->modelAnimSpeed)
		bits |= DKX_FRAMEINFO;
	if (sync)
		bits |= DKX_SYNC;
	if (to->numClusters != from->numClusters || memcmp(to->clusterIdx, from->clusterIdx, sizeof(to->clusterIdx))
		|| memcmp(to->clusterAngles, from->clusterAngles, sizeof(to->clusterAngles)))
		bits |= DKX_CLUSTERS;
	if (to->snd_flags != from->snd_flags || to->volume != from->volume || to->dist_min != from->dist_min
		|| to->dist_max != from->dist_max)
		bits |= DKX_SOUND;
	if (!bits)
		return;

	if (dkxlen + 200 > (int)sizeof(dkxbuf))
		DKX_Flush();
	DKX_Byte(svcdk_fte_entstate);
	DKX_Short(entnum);
	DKX_Short(bits);
	if (bits & DKX_MODEL2)		DKX_Long(to->modelindex2);
	if (bits & DKX_MODEL3)		DKX_Long(to->modelindex3);
	if (bits & DKX_MODEL4)		DKX_Long(to->modelindex4);
	if (bits & DKX_EFFECTS2)	DKX_Long((int)to->effects2);
	if (bits & DKX_FLAGS)		DKX_Long((int)to->flags);
	if (bits & DKX_ALPHA)		DKX_Float(to->alpha);
	if (bits & DKX_COLOR)		DKX_Floats(to->color, 3);
	if (bits & DKX_SCALE)		DKX_Floats(to->render_scale, 3);
	if (bits & DKX_MINSMAXS)	{DKX_Floats(to->mins, 3); DKX_Floats(to->maxs, 3);}
	if (bits & DKX_ANGLEDELTA)	DKX_Floats(to->angle_delta, 3);
	if (bits & DKX_ORIGINHACK)	DKX_Floats(to->origin_hack, 3);
	if (bits & DKX_FRAMEINFO)
	{
		DKX_Short(to->frameFlags);
		DKX_Short(to->startFrame);
		DKX_Short(to->endFrame);
		DKX_Short(to->startLoop);
		DKX_Short(to->endLoop);
		DKX_Short(to->loopCount);
		DKX_Short(to->sound1Frame);
		DKX_Short(to->sound2Frame);
		DKX_Byte(to->sound1Index);
		DKX_Byte(to->sound2Index);
		DKX_Byte(to->frameInc);
		DKX_Float(to->modelAnimSpeed);
	}
	if (bits & DKX_SYNC)		DKX_Short(frame);
	if (bits & DKX_CLUSTERS)
	{
		DKX_Byte(to->numClusters);
		for (k = 0; k < to->numClusters; k++)
		{
			DKX_Byte(to->clusterIdx[k]);
			DKX_Floats(to->clusterAngles[k], 3);
		}
	}
	if (bits & DKX_SOUND)
	{
		DKX_Byte(to->snd_flags);
		DKX_Float(to->volume);
		DKX_Float(to->dist_min);
		DKX_Float(to->dist_max);
	}
}

//each frame: what's changed, to everyone
static void DK_SendEntX(void)
{
	int i, n;
	dkentx_t x;
	if (!dkge || !dkge->edicts || !dkentx)
		return;
	n = dkge->num_edicts;
	if (n > shadowmax)
		n = shadowmax;
	dkxto = -1;
	for (i = 1; i < n; i++)
	{
		edict_t *de = DKEDICT(i);
		bool force = de->inuse && (de->s.frameInfo.frameFlags & FRAME_FORCEINDEX);
		if (!de->inuse && !memcmp(&dkentx[i], &dkentxnull, sizeof(dkentx[i])))
			continue;
		DK_EntXFrom(&x, de);
		if (!de->inuse)
			DKX_Write(i, &dkentx[i], &x, true, false, 0);
		else
		{
			DKX_Write(i, &dkentx[i], &x, false, force, de->s.frame);
			de->s.frameInfo.frameFlags &= ~FRAME_FORCEINDEX;	//(its engine cleared it as it sent the entity)
		}
		dkentx[i] = x;
	}
	DKX_Flush();
}
//all of it, to a client that's begun (fresh: its client starts over with each level); its client syncs the
//frames of what it hasn't had from the frame Quake II's entity state has. A big level's is more than FTE's back
//buffers have room for at once (it'd drop them), so it goes as they've room, from frame to frame
//(DK_ContinueAllEntX); what changes meanwhile has gone to everyone, and what's sent is what's there then
static int dkallentx[256];	//the next entity a client (0-based) has yet to have it of; 0: none
#define DKX_ROOM	4		//back buffers left them
static void DK_ContinueAllEntX(void)
{
	int p, n;
	if (!dkge || !dkge->edicts || !dkentx)
		return;
	n = dkge->num_edicts;
	if (n > shadowmax)
		n = shadowmax;
	for (p = 0; p < 256; p++)
	{
		if (!dkallentx[p])
			continue;
		dkxto = p;
		while (dkallentx[p] < n && SVDK_ReliableRoom(p) > DKX_ROOM)
		{
			int i = dkallentx[p]++;
			if (DKEDICT(i)->inuse)
				DKX_Write(i, &dkentxnull, &dkentx[i], false, false, 0);
		}
		DKX_Flush();
		dkxto = -1;
		if (dkallentx[p] >= n)
			dkallentx[p] = 0;
	}
}
static void DK_SendAllEntX(int playernum)
{
	if (playernum >= 0 && playernum < 256)
		dkallentx[playernum] = 1;
	DK_ContinueAllEntX();
}

static void DKGI_multicast(const CVector &origin, multicast_t to)
{
	float o[3] = {origin.x, origin.y, origin.z};
	if (DK_TranslateMessage())
		fgi->multicast(o, to);
	dkmsglen = 0;
	dkmsgoverflow = false;
}
static void DKGI_unicast(edict_t *ent, qboolean reliable)
{
	if (ent && DK_TranslateMessage())
		fgi->unicast(DK_ToShadow(ent), reliable);
	dkmsglen = 0;
	dkmsgoverflow = false;
}
static qboolean DKGI_TestMulticast(multicast_t to)			{return true;}
static qboolean DKGI_TestUnicast(edict_t *ent, qboolean reliable)	{return true;}

/*
=====================================================================
client-language strings (Daikatana 1.2): written the way its server did, formatted here as its client did
=====================================================================
*/
static unsigned char csbuf[4096];
static int cslen;
static bool csnocontinue;

static void CS_Write(const void *d, int len)
{
	if (cslen + len <= (int)sizeof(csbuf))
		memcpy(csbuf + cslen, d, len);
	cslen += len;
}
static void CS_WriteByte(int c)		{unsigned char b = c; CS_Write(&b, 1);}
static void CS_WriteLong(int c)		{unsigned char b[4] = {(unsigned char)(c&0xff), (unsigned char)((c>>8)&0xff), (unsigned char)((c>>16)&0xff), (unsigned char)((c>>24)&0xff)}; CS_Write(b, 4);}
static void CS_WriteFloat(float f)	{union {float f; int i;} u; u.f = f; CS_WriteLong(u.i);}
static void CS_WriteString(const char *s)	{if (!s) s = ""; CS_Write(s, (int)strlen(s)+1);}

static char *CS_StringForID(long str_list_index)
{
	unsigned long id = (unsigned long)str_list_index;
	int list = (int)((id >> 16) & 0xffffUL);
	int index = (int)(id & 0xffffUL);
#define CS_ARRAY_ITEM(array) ((index >= 0 && index < (int)(sizeof(array) / sizeof((array)[0]))) ? (array)[index] : NULL)
	switch (list)
	{
	case CS_RES_TONGUE:             return CS_ARRAY_ITEM(tongue);
	case CS_RES_WEAPONS:            return CS_ARRAY_ITEM(tongue_weapons);
	case CS_RES_ARMORS:             return CS_ARRAY_ITEM(tongue_armors);
	case CS_RES_WORLD:              return CS_ARRAY_ITEM(tongue_world);
	case CS_RES_DEATHMSG_DAIKATANA: return CS_ARRAY_ITEM(tongue_deathmsg_daikatana);
	case CS_RES_DEATHMSG_SELF:      return CS_ARRAY_ITEM(tongue_deathmsg_self);
	case CS_RES_DEATHMSG_WEAPON1:   return CS_ARRAY_ITEM(tongue_deathmsg_weapon1);
	case CS_RES_DEATHMSG_WEAPON2:   return CS_ARRAY_ITEM(tongue_deathmsg_weapon2);
	case CS_RES_DEATHMSG_WEAPON3:   return CS_ARRAY_ITEM(tongue_deathmsg_weapon3);
	case CS_RES_DEATHMSG_WEAPON4:   return CS_ARRAY_ITEM(tongue_deathmsg_weapon4);
	case CS_RES_CTF:                return CS_ARRAY_ITEM(tongue_ctf);
	case CS_RES_DEATHTAG:           return CS_ARRAY_ITEM(tongue_deathtag);
	default:						return NULL;
	}
#undef CS_ARRAY_ITEM
}

static void CS_Append(char *result, int size, int *len, const char *text)
{
	int available = size - *len - 1;
	int n = (int)strlen(text);
	if (available <= 0)
		return;
	if (n > available)
		n = available;
	memcpy(result + *len, text, n);
	*len += n;
	result[*len] = 0;
}
//the client's CL_ParseClientString, on what's been written: false if it's malformed
static bool CS_Format(char *result, int resultsize, int *printtype)
{
	dkreader_t r = {csbuf, cslen, 1, false};	//past the svc
	char fmt[2048], temp[1024];
	int datatype, parmcount, i;
	bool more;

	if (cslen > (int)sizeof(csbuf))
		return false;
	*result = 0;
	datatype = DK_ReadByte(&r);
	if (datatype == CS_data_str_resource)
	{
		char *s = CS_StringForID(DK_ReadLong(&r));
		snprintf(fmt, sizeof(fmt), "%s", s?s:"");
	}
	else if (datatype == CS_data_str_literal)
		snprintf(fmt, sizeof(fmt), "%s", DK_ReadString(&r));
	else
		return false;
	do
	{
		int len = 0;
		const char *cursor = fmt;
		*result = 0;
		parmcount = DK_ReadByte(&r);
		if (parmcount < 0)
			return false;
		for (i = 0; i < parmcount; i++)
		{
			const char *spec = strchr(cursor, '%'), *specend;
			char part[64];
			int n;
			if (!spec)
				return false;
			if (spec > cursor)
			{
				char literal[2048];
				n = (int)(spec - cursor);
				if (n >= (int)sizeof(literal))
					n = sizeof(literal)-1;
				memcpy(literal, cursor, n);
				literal[n] = 0;
				CS_Append(result, resultsize, &len, literal);
			}
			for (specend = spec+1; *specend && !strchr("diouxXfFeEgGaAcsp", *specend); specend++)
				;
			if (!*specend)
				return false;
			n = (int)(specend - spec + 1);
			if (n >= (int)sizeof(part))
				n = sizeof(part)-1;
			memcpy(part, spec, n);
			part[n] = 0;
			*temp = 0;
			switch(DK_ReadByte(&r))
			{
			case CS_data_str_resource:
				{
					char *s = CS_StringForID(DK_ReadLong(&r));
					snprintf(temp, sizeof(temp), part, s?s:"");
				}
				break;
			case CS_data_str_literal:	snprintf(temp, sizeof(temp), part, DK_ReadString(&r));	break;
			case CS_data_integer:		snprintf(temp, sizeof(temp), part, DK_ReadLong(&r));	break;
			case CS_data_float:			snprintf(temp, sizeof(temp), part, DK_ReadFloat(&r));	break;
			default:					return false;
			}
			CS_Append(result, resultsize, &len, temp);
			cursor = specend+1;
		}
		CS_Append(result, resultsize, &len, cursor);
		more = DK_ReadByte(&r) > 0;
		if (more)
			snprintf(fmt, sizeof(fmt), "%s", result);
	} while (more && !r.bad);
	*printtype = DK_ReadByte(&r);
	return !r.bad;
}

static void CS_BeginSendString(void)		{cslen = 0; csnocontinue = false; CS_WriteByte(SVC_CLIENT_STRING);}
static void CS_EndSendString(void)			{cslen = 0;}
static void CS_Continue(qboolean b)			{if (!b) csnocontinue = true; CS_WriteByte(b?1:0);}
static void CS_SendCount(byte count)		{CS_WriteByte(count);}
static void CS_SendStringID(long id)		{CS_WriteByte(CS_data_str_resource); CS_WriteLong((int)id);}
static void CS_SendString(char *s)			{CS_WriteByte(CS_data_str_literal); CS_WriteString(s);}
static void CS_SendInteger(int v)			{CS_WriteByte(CS_data_integer); CS_WriteLong(v);}
static void CS_SendFloat(float v)			{CS_WriteByte(CS_data_float); CS_WriteFloat(v);}
static void CS_SendSpecifierStr(char *s, byte n)	{CS_SendString(s); CS_SendCount(n);}
static void CS_SendSpecifierID(long id, byte n)		{CS_SendStringID(id); CS_SendCount(n);}

static void CS_Send(edict_t *ent, byte print_type, float msg_time)
{
	char text[2048];
	int printtype;
	int save = cslen;
	bool savenc = csnocontinue;
	if (!csnocontinue)
		CS_Continue(false);
	CS_WriteByte(print_type);
	if (CS_Format(text, sizeof(text), &printtype))
	{
		if (printtype == CS_print_center)
		{
			if (ent)
				fgi->centerprintf(DK_ToShadow(ent), "%s", text);
			else
			{
				int i;
				for (i = 1; i <= dkmaxclients; i++)
					if (shadow[i].inuse && shadow[i].client)
						fgi->centerprintf(&shadow[i], "%s", text);
			}
		}
		else if (ent)
			fgi->cprintf(DK_ToShadow(ent), PRINT_HIGH, "%s", text);
		else
			fgi->bprintf(PRINT_HIGH, "%s", text);
	}
	else
		DK_Printf("Daikatana: client string malformed\n");
	//as the server had it: the same string can go to the next one
	cslen = save;
	csnocontinue = savenc;
}
static void CS_Multicast(byte print_type, float msg_time)			{CS_Send(NULL, print_type, msg_time);}
static void CS_Unicast(edict_t *ent, byte print_type, float msg_time)	{if (ent) CS_Send(ent, print_type, msg_time);}

/*
=====================================================================
memory
=====================================================================
*/
static void *DKGI_X_Malloc(size_t size, MEM_TAG tag)	{return fgi->TagMalloc((int)size, DKMEMTAG_BASE + (int)tag);}
static void DKGI_X_Free(void *mem)						{if (mem) fgi->TagFree(mem);}
static void DKGI_Mem_Free_Tag(MEM_TAG tag)				{fgi->FreeTags(DKMEMTAG_BASE + (int)tag);}
static int DKGI_Mem_Heap_Walk(int stats)				{return 0;}
static void *DKGI_TagMalloc(int size, int tag)			{return fgi->TagMalloc(size, tag);}
static void DKGI_TagFree(void *block)					{fgi->TagFree(block);}
static void DKGI_FreeTags(int tag)						{fgi->FreeTags(tag);}

/*
=====================================================================
files: FILE*s, of a copy of what's in FTE's filesystem
=====================================================================
*/
static const char *DK_FilePath(const char *path)
{
	const char *gamedir = dkgi.dk_gamedir;
	size_t l = strlen(gamedir);
	while (path[0] == '.' && (path[1] == '/' || path[1] == '\\'))
		path += 2;
	if (l && !strncasecmp(path, gamedir, l) && (path[l] == '/' || path[l] == '\\'))
		path += l+1;
	return path;
}
static FILE *DK_OpenCopy(const char *filename, int *length)
{
	int len;
	void *buf = SVDK_LoadFile(DK_FilePath(filename), &len);
	FILE *f;
	if (!buf)
		return NULL;
	f = tmpfile();
	if (f)
	{
		fwrite(buf, 1, len, f);
		rewind(f);
	}
	SVDK_FreeFile(buf);
	*length = len;
	return f;
}
static long dkbytesread;
static int DKGI_FS_Open(const char *filename, FILE **file)
{
	int len = -1;
	*file = DK_OpenCopy(filename, &len);
	return *file?len:-1;
}
static void DKGI_FS_Close(FILE *f)				{if (f) fclose(f);}
static int DKGI_FS_FileLength(FILE *f)
{
	long pos = ftell(f), end;
	fseek(f, 0, SEEK_END);
	end = ftell(f);
	fseek(f, pos, SEEK_SET);
	return (int)end;
}
static void DKGI_FS_Read(void *buffer, int len, FILE *f)	{dkbytesread += (long)fread(buffer, 1, len, f);}
static int DKGI_FS_LoadFile(const char *path, void **buffer)
{
	int len;
	void *b = SVDK_LoadFile(DK_FilePath(path), &len);
	if (!b)
	{
		if (buffer)
			*buffer = NULL;
		return -1;
	}
	if (buffer)
		*buffer = b;
	else
		SVDK_FreeFile(b);
	return len;
}
static void DKGI_FS_FreeFile(void *buffer)		{SVDK_FreeFile(buffer);}
static int DKGI_FS_Seek(FILE *f, long offset, int origin)	{return fseek(f, offset, origin);}
static long DKGI_FS_Tell(FILE *f)				{return ftell(f);}
static int DKGI_FS_Getc(FILE *f)				{return fgetc(f);}
static int DKGI_FS_Ungetc(int c, FILE *f)		{return ungetc(c, f);}
static char *DKGI_FS_Gets(char *s, int n, FILE *f)	{return fgets(s, n, f);}
static long DKGI_FS_GetBytesRead(void)			{return dkbytesread;}

/*
=====================================================================
models: what the game reads of them (frame names, animation sequences)
=====================================================================
*/
static struct
{
	int modelindex;
	int numframes;
	char (*names)[16];
} dkframes = {-1};

static bool DK_LoadFrameNames(int modelindex)
{
	int len, i;
	unsigned char *buf;
	const dmdl_t *h;
	if (dkframes.modelindex == modelindex)
		return dkframes.names != NULL;
	free(dkframes.names);
	dkframes.names = NULL;
	dkframes.numframes = 0;
	dkframes.modelindex = modelindex;
	buf = (unsigned char *)SVDK_LoadFile(DKGI_GetConfigString(CS_MODELS+modelindex), &len);
	if (!buf)
		return false;
	h = (const dmdl_t *)buf;
	if (len < (int)sizeof(*h) || h->ident != IDALIASHEADER || h->num_frames <= 0 || h->ofs_frames < 0 ||
		h->ofs_frames + (size_t)h->num_frames*h->framesize > (size_t)len)
	{
		SVDK_FreeFile(buf);
		return false;
	}
	dkframes.numframes = h->num_frames;
	dkframes.names = (char (*)[16])calloc(h->num_frames, 16);
	for (i = 0; i < h->num_frames; i++)
	{	//the name's after the scale and translate in both versions' frames
		const daliasframe_t *fr = (const daliasframe_t *)(buf + h->ofs_frames + (size_t)i*h->framesize);
		memcpy(dkframes.names[i], fr->name, 16);
		dkframes.names[i][15] = 0;
	}
	SVDK_FreeFile(buf);
	return true;
}
static void DK_FramesClear(void)
{
	free(dkframes.names);
	dkframes.names = NULL;
	dkframes.numframes = 0;
	dkframes.modelindex = -1;
}
static void DKGI_GetFrameName(int modelindex, int frameindex, char *name)
{
	if (!DK_LoadFrameNames(modelindex) || frameindex < 0 || frameindex >= dkframes.numframes)
		*name = 0;
	else
		strcpy(name, dkframes.names[frameindex]);
}
static int DKGI_GetAnimSequences(int modelindex, frameData_t *pFrameData)
{
	int len, i, n;
	unsigned char *buf = (unsigned char *)SVDK_LoadFile(DKGI_GetConfigString(CS_MODELS+modelindex), &len);
	const dmdl_t *h = (const dmdl_t *)buf;
	if (!buf)
		return 0;
	if (len < (int)sizeof(*h) || h->ident != IDALIASHEADER)
	{
		SVDK_FreeFile(buf);
		return 0;
	}
	n = h->num_sequences;
	if (n < 0 || h->ofs_sequences < 0 || h->ofs_sequences + (size_t)n*sizeof(animSeq_t) > (size_t)len)
		n = 0;
	if (pFrameData)
	{
		const animSeq_t *seq = (const animSeq_t *)(buf + h->ofs_sequences);
		for (i = 0; i < n; i++)
		{
			memcpy(pFrameData[i].animation_name, seq[i].seqName, sizeof(pFrameData[i].animation_name));
			pFrameData[i].animation_name[sizeof(pFrameData[i].animation_name)-1] = 0;
			pFrameData[i].first = seq[i].startFrame;
			pFrameData[i].last = seq[i].endFrame;
			pFrameData[i].flags = 0;
			pFrameData[i].soundframe1 = -1;
			pFrameData[i].soundframe2 = -1;
		}
	}
	SVDK_FreeFile(buf);
	return n;
}
static int DKGI_GetModelSkinIndex(int modelIndex)		{return 0;}
static void DKGI_ForceModelLoad(int modelindex, char *name)
{
	if (dkframes.modelindex == modelindex)
		DK_FramesClear();
	DK_SetConfigstring(CS_MODELS+modelindex, name);
}
static void DKGI_PreCacheModel(int modelindex, char *name)	{}
//hierarchical models' vertices: its engine never did these either
static int DKGI_VertInfo(edict_t *ent, int surfIndex, int vertIndex, CVector &vert)	{return 0;}
static int DKGI_SurfIndex(int modelIndex, char *surfName)	{return 0;}
static int DKGI_TriVerts(edict_t *ent, int surfIndex, int vertIndex, CVector &v1, CVector &v2, CVector &v3)	{return 0;}

/*
=====================================================================
the rest
=====================================================================
*/
typedef struct dkcmd_s
{
	char *name;
	void (*function)(struct edict_s *ent);
	struct dkcmd_s *next;
} dkcmd_t;
static dkcmd_t *dkcmds;

static void DKGI_AddCommand(char *name, void (*function)(struct edict_s *ent))
{
	dkcmd_t *c;
	for (c = dkcmds; c; c = c->next)
		if (!strcmp(c->name, name))
		{
			c->function = function;
			return;
		}
	c = (dkcmd_t *)calloc(1, sizeof(*c));
	c->name = strdup(name);
	c->function = function;
	c->next = dkcmds;
	dkcmds = c;
}
static void DKGI_RemoveDLLCommands(void)
{
	dkcmd_t *c;
	while ((c = dkcmds))
	{
		dkcmds = c->next;
		free(c->name);
		free(c);
	}
}
//a command's args: FTE's, or ours as we run the game's commands ourselves (its engine's console did: what its
//cinematics stuff, cin_main.cpp)
static int		dkargc = -1;	//-1: FTE's
static char		dkargv[8][256], dkargs[1024];
static void DK_TokenizeArgs(const char *line)
{
	const char *s = line;
	dkargc = 0;
	dkargs[0] = 0;
	while (*s && dkargc < 8)
	{
		int n = 0;
		while (*s == ' ' || *s == '\t')
			s++;
		if (!*s || *s == '\n' || *s == ';')
			break;
		if (dkargc == 1)
			snprintf(dkargs, sizeof(dkargs), "%s", s);
		if (*s == '\"')
		{
			for (s++; *s && *s != '\"' && *s != '\n'; s++)
				if (n < 255)
					dkargv[dkargc][n++] = *s;
			if (*s == '\"')
				s++;
		}
		else
			for (; *s && *s != ' ' && *s != '\t' && *s != '\n' && *s != ';'; s++)
				if (n < 255)
					dkargv[dkargc][n++] = *s;
		dkargv[dkargc++][n] = 0;
	}
	{	//(the args to the line's end)
		char *e = strpbrk(dkargs, "\n;");
		if (e)
			*e = 0;
	}
}
static int DKGI_GetArgc(void)					{return (dkargc >= 0)?dkargc:fgi->argc();}
static char *DKGI_GetArgv(int n)				{return (dkargc >= 0)?((n >= 0 && n < dkargc)?dkargv[n]:(char *)""):fgi->argv(n);}
static char *DKGI_GetArgs(void)					{return (dkargc >= 0)?dkargs:fgi->args();}
int DKCIN_Argc(void)							{return DKGI_GetArgc();}
const char *DKCIN_Argv(int i)					{return DKGI_GetArgv(i);}
static dkcmd_t *DK_FindCommand(const char *name)
{
	dkcmd_t *c;
	for (c = dkcmds; c; c = c->next)
		if (!strcasecmp(c->name, name))
			return c;
	return NULL;
}

//what the game stuffs: its cinematics' commands are ours (cin_main.cpp), the rest FTE's
static void DKGI_CBuf_AddText(const char *text)
{
	const char *line = text;
	while (line && *line)
	{
		const char *end = strchr(line, '\n');
		char buf[1024];
		int len = end?(int)(end - line):(int)strlen(line);
		if (len >= (int)sizeof(buf))
			len = sizeof(buf)-1;
		memcpy(buf, line, len);
		buf[len] = 0;
		DK_TokenizeArgs(buf);
		if (dkargc > 0 && !strncasecmp(dkargv[0], "cin_", 4) && CIN_Command())
			;
		else if (len)
		{
			strcat(buf, "\n");
			fgi->AddCommandString(buf);
		}
		dkargc = -1;
		line = end?end+1:NULL;
	}
}
static void DKGI_DebugGraph(float value, int color)	{}
static int DKGI_EXEGetArgc(void)				{return SVDK_ExeArgc();}
static char *DKGI_EXEGetArgv(int i)				{return (char *)SVDK_ExeArgv(i);}
static int DKGI_Sys_Milliseconds(void)			{return SVDK_Milliseconds();}
//its cinematics (cin_main.cpp): the camera's where the script has it (the world's camera asks for it into its
//origin and angles)
static void DKGI_CIN_CamLocation(const CVector &pos, const CVector &angles)
{
	CIN_CamLocation(const_cast<CVector &>(pos), const_cast<CVector &>(angles));
}
static void *DKGI_CIN_GCE_Interface(void)		{return NULL;}	//(its editor's)
static void DKGI_CIN_WorldPlayback(void *funcs)	{CIN_WorldImport(funcs);}

//what the cinematics have of its engine
static dkcmd_t *DK_FindCommand(const char *name);
extern "C++" void DKCIN_Command(const char *text)
{	//its own (cin_*), the game's for the player (cam_toggle cin_on), FTE's
	char buf[1024];
	dkcmd_t *c;
	edict_t *player;
	if (!text)
		return;
	snprintf(buf, sizeof(buf), "%s", text);
	DK_TokenizeArgs(buf);
	if (dkargc > 0 && !strncasecmp(dkargv[0], "cin_", 4) && CIN_Command())
		;
	else if (dkargc > 0 && (c = DK_FindCommand(dkargv[0])) && dkge && (player = DKEDICT(1))->inuse && player->client)
	{
		c->function(player);
		DK_SyncAll();
	}
	else if (dkargc > 0)
		fgi->AddCommandString(buf);
	dkargc = -1;
}
extern "C++" FILE *DKCIN_OpenFile(const char *path)
{	//as a FILE it reads with fread: a temporary one with what's in the paks
	int len;
	void *buf = SVDK_LoadFile(DK_FilePath(path), &len);
	FILE *f;
	if (!buf)
		return NULL;
	f = tmpfile();
	if (f)
	{
		if (fwrite(buf, 1, len, f) != (size_t)len)
		{
			fclose(f);
			f = NULL;
		}
		else
			rewind(f);
	}
	SVDK_FreeFile(buf);
	return f;
}
extern "C++" void DKCIN_CloseFile(FILE *f)
{
	if (f)
		fclose(f);
}
extern "C++" void DKCIN_PlaySound(const char *name, int channel, float volume, bool loop)
{	//to the player, heard wherever the camera is (its client played them on the player)
	int idx;
	if (!name || !*name)
		return;
	idx = DKGI_soundindex(name);
	if (channel < 0 || idx <= 0 || !dkge)
		return;	//(precached only)
	fgi->sound(&shadow[1], channel & 7, idx, (volume > 1)?1:((volume < 0)?0:volume), 0/*ATTN_NONE*/, 0);
}
extern "C++" void DKCIN_PlayMP3(const char *name, int channel, float volume, bool loop)
{
	DKGI_StartMP3(name, channel, volume, loop?0:1, TRUE, NULL);
}
extern "C++" void DKCIN_StopMP3(int channel)
{
	DKGI_StopMP3(channel, NULL);
}
extern "C++" int DKCIN_PlayerNum(void)		{return 0;}
extern "C++" float DKCIN_CvarValue(const char *name, const char *defaultvalue)
{
	cvar_t *v = DKGI_cvar(name, defaultvalue, 0);
	return v?v->value:atof(defaultvalue);
}
extern "C++" void DKCIN_CvarSet(const char *name, float value)
{
	char s[64];
	snprintf(s, sizeof(s), "%g", value);
	DKGI_cvar_set(name, s);
}
extern "C++" int DKCIN_Milliseconds(void)	{return SVDK_Milliseconds();}
static void DKGI_SV_EmitSyncEntity(edict_t *client, edict_t *ent)	{}
static short DKGI_IsPlayerLocal(int playernum)	{return SVDK_IsLocalClient(playernum);}
static void DKGI_AutoSave(void)					{}
static char dkpreppedrefresh = 1;

/*
=====================================================================
FTE's game API 3, onto the game
=====================================================================
*/
static void DK_Free(void)
{
	free(shadow);
	shadow = NULL;
	free(shadowcl);
	shadowcl = NULL;
	free(dkentx);
	dkentx = NULL;
	shadowmax = 0;
	ge3.edicts = NULL;
	ge3.num_edicts = ge3.max_edicts = 0;
}

//its engine's server's cvars (sv_main.cpp), which it made before the game's (whose defaults don't count then)
static const struct
{
	const char *name, *value;
	int flags;
} dkenginecvars[] =
{
	{"sv_demomode",			"0",	0},	//(the game's default is 1: no intro cinematics)
	{"sv_cinematics",		"1.0",	CVAR_ARCHIVE},
	{"sv_frictionfactor",	"1.0",	CVAR_ARCHIVE},
	{"sv_noreload",			"0",	0},
	{"sv_mapnum",			"1",	0},
	{"sv_violence",			"0",	CVAR_SERVERINFO|CVAR_LATCH|CVAR_ARCHIVE},
	{"skill",				"1",	0},
	{"ctf",					"0",	CVAR_LATCH},
	{"deathtag",			"0",	CVAR_LATCH},
	{"dm_skill_level",		"0",	CVAR_SERVERINFO|CVAR_LATCH},
	{"dm_max_players",		"0",	CVAR_SERVERINFO|CVAR_LATCH},
	{"dm_weapons_stay",		"0",	CVAR_SERVERINFO|CVAR_LATCH},
	{"dm_spawn_farthest",	"0",	CVAR_SERVERINFO|CVAR_LATCH},
	{"dm_allow_exiting",	"0",	CVAR_SERVERINFO|CVAR_LATCH},
	{"dm_same_map",			"0",	CVAR_SERVERINFO|CVAR_LATCH},
	{"dm_force_respawn",	"0",	CVAR_SERVERINFO|CVAR_LATCH},
	{"dm_falling_damage",	"1",	CVAR_SERVERINFO|CVAR_LATCH},
	{"dm_allow_powerups",	"1",	CVAR_SERVERINFO|CVAR_LATCH},
	{"dm_allow_health",		"1",	CVAR_SERVERINFO|CVAR_LATCH},
	{"dm_allow_armor",		"1",	CVAR_SERVERINFO|CVAR_LATCH},
	{"dm_infinite_ammo",	"0",	CVAR_SERVERINFO|CVAR_LATCH},
	{"dm_fixed_fov",		"0",	CVAR_SERVERINFO|CVAR_LATCH},
	{"dm_teamplay",			"0",	CVAR_SERVERINFO|CVAR_LATCH},
	{"dm_friendly_fire",	"0",	CVAR_SERVERINFO|CVAR_LATCH},
	{"dm_footsteps",		"1",	CVAR_SERVERINFO|CVAR_LATCH},
	{"dm_allow_hook",		"1",	CVAR_SERVERINFO|CVAR_LATCH},
	{"dm_item_respawn",		"1",	CVAR_SERVERINFO|CVAR_LATCH},
	{"dm_use_skill_system",	"1",	CVAR_SERVERINFO|CVAR_LATCH},
	{"dm_instagib",			"0",	CVAR_SERVERINFO|CVAR_LATCH},
};

static void FQ2_VARGS DK3_Init(void)
{
	char maxcl[16];
	size_t i;
	for (i = 0; i < sizeof(dkenginecvars)/sizeof(dkenginecvars[0]); i++)
		DKGI_cvar(dkenginecvars[i].name, dkenginecvars[i].value, dkenginecvars[i].flags);
	dkge->InitDLLs();	//physics' cvars, its edicts (and clients), world's and weapons' init

	dkmaxclients = (int)DKGI_cvar("maxclients", "4", CVAR_SERVERINFO|CVAR_LATCH)->value;
	snprintf(maxcl, sizeof(maxcl), "%i", dkmaxclients);
	DK_Free();
	shadowmax = dkge->max_edicts;
	shadow = (fq2::edict_t *)calloc(shadowmax, sizeof(*shadow));
	shadowcl = (fq2::gclient_t *)calloc(dkmaxclients+1, sizeof(*shadowcl));
	dkentx = (dkentx_t *)calloc(shadowmax, sizeof(*dkentx));
	ge3.edicts = shadow;
	ge3.edict_size = sizeof(*shadow);
	ge3.max_edicts = shadowmax;
	ge3.num_edicts = dkmaxclients+1;
	DK_SyncAll();
}

static void FQ2_VARGS DK3_Shutdown(void)
{
	if (dkge)
	{
		CIN_Stop(NULL);
		CIN_WorldImport(NULL);
		if (dklevelloaded)
			dkge->LevelExit();
		dkge->UnloadDLLs();
	}
	dklevelloaded = false;
	dkge = NULL;
	DK_Free();
	DK_CvarsFree();
	DK_ConfigstringsClear();
	DK_SurfacesClear();
	DK_FramesClear();
	DKGI_RemoveDLLCommands();
}

static void FQ2_VARGS DK3_SpawnEntities(const char *mapname, const char *entstring, const char *spawnpoint)
{
	char map[256];
	char *ents;
	DK_PreCall();
	CIN_Stop(NULL);	//(its client's, changing map)
	memset(dkallentx, 0, sizeof(dkallentx));	//(its clients begin again)
	if (dklevelloaded && !dkloadinggame)
	{	//its engine went through these for each level after the first (not for a saved game's: it's just read)
		dkge->LevelExit();
		DKGI_Mem_Free_Tag(MEM_TAG_GAME);	//the last level's edicts and clients, which physics doesn't
		g_edicts = NULL;
		dkge->InitChangelevel();
		if (dkge->max_edicts > shadowmax)
			fgi->error("Daikatana: maxentities can't change in a game");
	}
	dklevelloaded = true;
	memset(dkbegun, 0, sizeof(dkbegun));	//they begin again in the new level
	if (dkentx)
		memset(dkentx, 0, sizeof(*dkentx)*shadowmax);	//(and their clients start over)
	DK_FramesClear();

	snprintf(map, sizeof(map), "%s", mapname);
	ents = strdup(entstring?entstring:"");
	{	//its engine's: the episode, from the map's name (e<episode>m<map>), for the game's level setup (weapons...)
		char episode[2] = {'0', 0};
		if ((map[0] == 'e' || map[0] == 'E') && map[1] >= '1' && map[1] <= '9')
			episode[0] = map[1];
		DKGI_cvar_forceset("sv_episode", episode);
		DK_CvarsUpdate();
	}
	dkge->RegisterWorldFuncs();
	dkge->LevelLoad();
	dkge->LoadNodes(map);
	dkge->SetServerTime(0.75f);
	dkge->SpawnEntities(map, ents, (char *)(spawnpoint?spawnpoint:""), dkloadinggame);
	free(ents);
	DK_SyncAll();
}

/*
saved games (p_save.cpp's: its functions are saved by their registered indexes, so they load here as anywhere):
FTE's Quake II server reads the game, spawns the level's entities, then reads the level over them, as its engine did
*/
static void FQ2_VARGS DK3_WriteGame(const char *filename, fq2::qboolean autosave)
{
	DK_PreCall();
	if (!autosave && dkge->WriteHeader)
	{	//a save (not a level change's): as its engine's SV_DoSaveGame, it uses a savegem, and writes the header its
		//menus show beside it (info.hdr: the comment, its stats)
		char hdr[MAX_OSPATH], *slash;
		cvar_t *comment = DKGI_cvar("dk_savecomment", "", 0);
		if (DKCIN_CvarValue("unlimited_saves", "0") == 0)
			DKCIN_Command("item_savegem_use\n");
		snprintf(hdr, sizeof(hdr), "%s", filename);
		slash = strrchr(hdr, '/');
		if (slash && (size_t)(slash + 1 - hdr) + 9 < sizeof(hdr))
		{
			strcpy(slash + 1, "info.hdr");
			dkge->WriteHeader(hdr, (char *)((comment && *comment->string)?comment->string:DKGI_GetConfigString(CS_NAME)), false);
		}
		if (comment)
			DKGI_cvar_set("dk_savecomment", "");
	}
	dkge->WriteGame((char *)filename, autosave?1:0);
}
static void FQ2_VARGS DK3_ReadGame(const char *filename)
{
	DK_PreCall();
	CIN_Stop(NULL);
	if (dklevelloaded)
	{	//(its engine loaded the game afresh: FTE keeps it, so the level it's in is left first)
		dkge->LevelExit();
		dklevelloaded = false;
	}
	dkge->ReadGame((char *)filename);	//(its edicts and clients anew)
	dkloadinggame = true;				//the level's spawned for a saved game's (its SpawnEntities' bLoadgame)
	DK_SyncAll();
}
static void FQ2_VARGS DK3_WriteLevel(const char *filename)
{
	DK_PreCall();
	dkge->WriteLevel((char *)filename);
}
static void FQ2_VARGS DK3_ReadLevel(const char *filename)
{
	int i;
	DK_PreCall();
	for (i = 0; i < shadowmax; i++)
		memset(&shadow[i].area, 0, sizeof(shadow[i].area));	//(FTE's cleared its world's links)
	dkge->ReadLevel((char *)filename);
	//its edicts as they were read (their links stale): linked again, as Quake II's game did
	for (i = dkmaxclients+1; i < dkge->num_edicts && i < shadowmax; i++)
	{
		edict_t *de = DKEDICT(i);
		de->area.prev = de->area.next = NULL;
		if (de->inuse)
			DKGI_linkentity(de);
	}
	dkloadinggame = false;
	if (dkentx)
		memset(dkentx, 0, sizeof(*dkentx)*shadowmax);	//(its clients start over)
	DK_SyncAll();
}

static fq2::qboolean FQ2_VARGS DK3_ClientConnect(fq2::edict_t *ent, char *userinfo)
{
	qboolean r;
	DK_PreCall();
	r = dkge->ClientConnect(DK_FromShadow(ent), userinfo, false);
	DK_SyncAll();
	return r;
}
static void FQ2_VARGS DK3_ClientBegin(fq2::edict_t *ent)
{
	int n = SHADOWNUM(ent);
	DK_PreCall();
	dkge->ClientBegin(DK_FromShadow(ent), false);
	if (n > 0 && n < 256)
		dkbegun[n] = true, dksentvalid[n] = false;
	DK_SyncAll();
	DK_SendEntX();			//what's changed, to everyone, then all of it to this one
	if (n > 0)
		DK_SendAllEntX(n-1);
}
static void FQ2_VARGS DK3_ClientUserinfoChanged(fq2::edict_t *ent, char *userinfo)
{
	DK_PreCall();
	dkge->ClientUserinfoChanged(DK_FromShadow(ent), userinfo);
	DK_SyncAll();
}
static void FQ2_VARGS DK3_ClientDisconnect(fq2::edict_t *ent)
{
	int n = SHADOWNUM(ent);
	//its engine only told the game of clients that had begun (spawned): before that they've no hook
	if (n > 0 && n <= 256)
		dkallentx[n-1] = 0;
	if (n <= 0 || n >= 256 || !dkbegun[n])
		return;
	dkbegun[n] = false;
	DK_PreCall();
	dkge->ClientDisconnect(DK_FromShadow(ent));
	DK_SyncAll();
}
static void FQ2_VARGS DK3_ClientCommand(fq2::edict_t *ent)
{
	dkcmd_t *c;
	const char *cmd = fgi->argv(0);
	DK_PreCall();
	if (!strcasecmp(cmd, "cin_skip"))
	{	//its escape key, as a cinematic plays (FTE's client sends it)
		if (SHADOWNUM(ent) == 1)
			CIN_Command();
		DK_SyncAll();
		return;
	}
	for (c = dkcmds; c; c = c->next)
		if (!strcasecmp(c->name, cmd))
		{
			c->function(DK_FromShadow(ent));
			DK_SyncAll();
			return;
		}
	if (!strcasecmp(cmd, "dkents"))
	{	//debugging: what the game has, to dkents.txt in the working directory (too much to print to a client)
		int i;
		const char *filter = fgi->argv(1);
		FILE *f = fopen("dkents.txt", "a");
		if (!f)
			return;
		fprintf(f, "---- dkents %s\n", filter);
		for (i = 0; i < dkge->num_edicts && i < shadowmax; i++)
		{
			edict_t *de = DKEDICT(i);
			if (!de->inuse)
				continue;
			if (*filter && (!de->className || !strstr(de->className, filter)))
				continue;
			fprintf(f, "%4i %-24s %6.0f %6.0f %6.0f model %3i:%-32s frame %3i rf %08x ef %08x sv %x solid %i\n", i,
				de->className?de->className:"?", de->s.origin.x, de->s.origin.y, de->s.origin.z,
				de->s.modelindex, DKGI_GetConfigString(CS_MODELS+de->s.modelindex), de->s.frame,
				de->s.renderfx, de->s.effects, de->svflags, de->solid);
		}
		fclose(f);
		return;
	}
	if (!strcasecmp(cmd, "setpos") && fgi->argc() >= 4)
	{	//the game's own is in its debug builds only
		cvar_t *cheats = DKGI_cvar("sv_cheats", "0", 0);
		edict_t *de = DK_FromShadow(ent);
		int k;
		if (!cheats || !cheats->value || !de || !de->client)
		{
			fgi->cprintf(ent, PRINT_HIGH, "setpos needs sv_cheats\n");
			return;
		}
		for (k = 0; k < 3; k++)
		{
			de->s.origin[k] = atof(fgi->argv(1+k));
			de->client->ps.pmove.origin[k] = (short)(de->s.origin[k]*8);
			de->client->ps.pmove.velocity[k] = 0;
		}
		de->s.old_origin = de->s.origin;
		if (fgi->argc() >= 7)
		{	//the view: the client's angles stay as they are, the delta goes round to these
			for (k = 0; k < 3; k++)
			{
				short want = (short)(atof(fgi->argv(4+k))*65536/360);
				short cmdangle = (short)(((int)(de->client->ps.viewangles[k]*65536/360)) - de->client->ps.pmove.delta_angles[k]);
				de->client->ps.pmove.delta_angles[k] = (short)(want - cmdangle);
				de->client->ps.viewangles[k] = atof(fgi->argv(4+k));
			}
			de->s.angles.Set(0, de->client->ps.viewangles[1], 0);
		}
		DKGI_linkentity(de);
		return;
	}
	fgi->cprintf(ent, PRINT_HIGH, "Unknown command \"%s\"\n", cmd);
}
static void FQ2_VARGS DK3_ClientThink(fq2::edict_t *ent, fq2::usercmd_t *cmd)
{
	usercmd_t dkcmd;
	DK_PreCall();
	memset(&dkcmd, 0, sizeof(dkcmd));
	dkcmd.msec = cmd->msec;
	dkcmd.buttons = cmd->buttons;
	dkcmd.angles[0] = cmd->angles[0];
	dkcmd.angles[1] = cmd->angles[1];
	dkcmd.angles[2] = cmd->angles[2];
	dkcmd.forwardmove = cmd->forwardmove;
	dkcmd.sidemove = cmd->sidemove;
	dkcmd.upmove = cmd->upmove;
	dkcmd.impulse = cmd->impulse;
	dkcmd.lightlevel = cmd->lightlevel;
	dkge->ClientThink(DK_FromShadow(ent), &dkcmd);
	DK_SyncAll();
}
static void FQ2_VARGS DK3_RunFrame(void)
{
	DK_PreCall();
	CIN_Play();		//its cinematics (its engine played them each of its client's frames)
	dkge->RunFrame();
	DK_SyncAll();
	DK_SendStats();
	DK_SendEntX();
	DK_ContinueAllEntX();
}
static void FQ2_VARGS DK3_ServerCommand(void)
{
	if (dkge->ServerCommand)
		dkge->ServerCommand();
}

/*
=====================================================================
the rest of FTE's server
=====================================================================
*/
extern "C" int SVDK_Active(void)
{
	return dkge != NULL;
}
//events last a message, which the game's engine saw to
extern "C" void SVDK_PrepFrame(void)
{
	int i;
	if (!dkge || !dkge->edicts)
		return;
	for (i = 0; i < dkge->num_edicts; i++)
		DKEDICT(i)->s.event = 0;
	for (i = 0; i < ge3.num_edicts; i++)
		shadow[i].s.event = 0;
}

//what Q2EX's entity state has past Quake II's that Daikatana's has too: its translucency, its scale
extern "C" void SVDK_EntityExtras(fq2::edict_t *ent, fq2::entityex_t *out)
{
	edict_t *de = DK_FromShadow(ent);
	float a, s;
	memset(out, 0, sizeof(*out));
	if (!de || !dkge)
		return;
	if (de->s.renderfx & RF_TRANSLUCENT)
	{
		a = de->s.alpha;
		out->alpha = (a <= 0)?1:((a >= 1)?255:(int)(a*255+0.5f));
	}
	s = de->s.render_scale.x;
	if (s > 0 && (s < 0.99f || s > 1.01f))
		out->scale = (s*16 >= 255)?255:((s*16 < 1)?1:(int)(s*16+0.5f));
}

extern "C" void *SVDK_GetGameAPI(void *fteimport)
{
	fgi = (fq2::game_import_t *)fteimport;

	memset(&dkgi, 0, sizeof(dkgi));
	dkgi.bprintf = DKGI_bprintf;
	dkgi.Con_Dprintf = DKGI_Con_Dprintf;
	dkgi.Con_Printf = DKGI_Con_Dprintf;
	dkgi.cprintf = DKGI_cprintf;
	dkgi.centerprintf = DKGI_centerprintf;
	dkgi.centerprint = DKGI_centerprint;
	dkgi.StartSound = DKGI_StartSound;
	dkgi.set_reverb_preset = DKGI_set_reverb_preset;
	dkgi.streamed_sound = DKGI_streamed_sound;
	dkgi.StartMusic = DKGI_StartMusic;
	dkgi.StartMP3 = DKGI_StartMP3;
	dkgi.StopMP3 = DKGI_StopMP3;
	dkgi.configstring = DKGI_configstring;
	dkgi.Error = DKGI_Error;
	dkgi.modelindex = DKGI_modelindex;
	dkgi.soundindex = DKGI_soundindex;
	dkgi.imageindex = DKGI_imageindex;
	dkgi.GetConfigString = DKGI_GetConfigString;
	dkgi.setmodel = DKGI_setmodel;
	dkgi.TraceLine = DKGI_TraceLine;
	dkgi.TraceLineTrigger = DKGI_TraceLineTrigger;
	dkgi.TraceBox = DKGI_TraceBox;
	dkgi.pointcontents = DKGI_pointcontents;
	dkgi.inPVS = DKGI_inPVS;
	dkgi.inPHS = DKGI_inPHS;
	dkgi.SetAreaPortalState = DKGI_SetAreaPortalState;
	dkgi.AreasConnected = DKGI_AreasConnected;
	dkgi.GetTraceCount = DKGI_GetTraceCount;
	dkgi.linkentity = DKGI_linkentity;
	dkgi.unlinkentity = DKGI_unlinkentity;
	dkgi.BoxEdicts = DKGI_BoxEdicts;
	dkgi.Pmove = DKGI_Pmove;
	dkgi.multicast = DKGI_multicast;
	dkgi.unicast = DKGI_unicast;
	dkgi.WriteChar = DKGI_WriteChar;
	dkgi.WriteByte = DKGI_WriteByte;
	dkgi.WriteShort = DKGI_WriteShort;
	dkgi.WriteLong = DKGI_WriteLong;
	dkgi.WriteFloat = DKGI_WriteFloat;
	dkgi.WriteString = DKGI_WriteString;
	dkgi.WritePosition = DKGI_WritePosition;
	dkgi.WriteDir = DKGI_WriteDir;
	dkgi.WriteAngle = DKGI_WriteAngle;
	dkgi.X_Malloc = DKGI_X_Malloc;
	dkgi.X_Free = DKGI_X_Free;
	dkgi.Mem_Free_Tag = DKGI_Mem_Free_Tag;
	dkgi.Mem_Heap_Walk = DKGI_Mem_Heap_Walk;
	dkgi.cvar = DKGI_cvar;
	dkgi.cvar_set = DKGI_cvar_set;
	dkgi.cvar_forceset = DKGI_cvar_forceset;
	dkgi.GetArgc = DKGI_GetArgc;
	dkgi.GetArgv = DKGI_GetArgv;
	dkgi.GetArgs = DKGI_GetArgs;
	dkgi.CBuf_AddText = DKGI_CBuf_AddText;
	dkgi.DebugGraph = DKGI_DebugGraph;
	dkgi.RemoveDLLCommands = DKGI_RemoveDLLCommands;
	dkgi.AddCommand = DKGI_AddCommand;
	dkgi.dk_dlldir = (char *)"dlls";
	dkgi.dk_gamedir = (char *)SVDK_Gamedir();
	dkgi.dk_gamename = (char *)"physics";
	dkgi.EXEGetArgc = DKGI_EXEGetArgc;
	dkgi.EXEGetArgv = DKGI_EXEGetArgv;
	dkgi.printxy = DKGI_printxy;
	dkgi.clearxy = DKGI_clearxy;
	dkgi.dk_GetFrameName = DKGI_GetFrameName;
	dkgi.dk_GetAnimSequences = DKGI_GetAnimSequences;
	dkgi.dk_GetModelSkinIndex = DKGI_GetModelSkinIndex;
	dkgi.sv_prepped_refresh = &dkpreppedrefresh;
	dkgi.dk_ForceModelLoad = DKGI_ForceModelLoad;
	dkgi.dk_PreCacheModel = DKGI_PreCacheModel;
	dkgi.Sys_Milliseconds = DKGI_Sys_Milliseconds;
	dkgi.VertInfo = DKGI_VertInfo;
	dkgi.SurfIndex = DKGI_SurfIndex;
	dkgi.TriVerts = DKGI_TriVerts;
	dkgi.FS_FileLength = DKGI_FS_FileLength;
	dkgi.FS_Close = DKGI_FS_Close;
	dkgi.FS_Open = DKGI_FS_Open;
	dkgi.FS_Read = DKGI_FS_Read;
	dkgi.FS_LoadFile = DKGI_FS_LoadFile;
	dkgi.FS_FreeFile = DKGI_FS_FreeFile;
	dkgi.FS_Seek = DKGI_FS_Seek;
	dkgi.FS_Tell = DKGI_FS_Tell;
	dkgi.FS_Getc = DKGI_FS_Getc;
	dkgi.FS_Ungetc = DKGI_FS_Ungetc;
	dkgi.FS_Gets = DKGI_FS_Gets;
	dkgi.FS_GetBytesRead = DKGI_FS_GetBytesRead;
	dkgi.CIN_CamLocation = DKGI_CIN_CamLocation;
	dkgi.CIN_GCE_Interface = DKGI_CIN_GCE_Interface;
	dkgi.CIN_WorldPlayback = DKGI_CIN_WorldPlayback;
	dkgi.SV_EmitSyncEntity = DKGI_SV_EmitSyncEntity;
	dkgi.TagMalloc = DKGI_TagMalloc;
	dkgi.TagFree = DKGI_TagFree;
	dkgi.FreeTags = DKGI_FreeTags;
	dkgi.IsPlayerLocal = DKGI_IsPlayerLocal;
	dkgi.TestMulticast = DKGI_TestMulticast;
	dkgi.TestUnicast = DKGI_TestUnicast;
	dkgi.AutoSave = DKGI_AutoSave;
	dkgi.cs.BeginSendString = CS_BeginSendString;
	dkgi.cs.EndSendString = CS_EndSendString;
	dkgi.cs.SendStringID = CS_SendStringID;
	dkgi.cs.SendString = CS_SendString;
	dkgi.cs.SendInteger = CS_SendInteger;
	dkgi.cs.SendFloat = CS_SendFloat;
	dkgi.cs.SendSpecifierStr = CS_SendSpecifierStr;
	dkgi.cs.SendSpecifierID = CS_SendSpecifierID;
	dkgi.cs.SendCount = CS_SendCount;
	dkgi.cs.Continue = CS_Continue;
	dkgi.cs.Multicast = CS_Multicast;
	dkgi.cs.Unicast = CS_Unicast;
	dkgi.cs.StringForID = CS_StringForID;

	dkge = GetGameAPI(&dkgi);	//physics, which loads (sets up) world and weapons
	if (!dkge)
		return NULL;

	memset(&ge3, 0, sizeof(ge3));
	ge3.apiversion = 3;
	ge3.Init = DK3_Init;
	ge3.Shutdown = DK3_Shutdown;
	ge3.SpawnEntities = DK3_SpawnEntities;
	ge3.WriteGame = DK3_WriteGame;
	ge3.ReadGame = DK3_ReadGame;
	ge3.WriteLevel = DK3_WriteLevel;
	ge3.ReadLevel = DK3_ReadLevel;
	ge3.ClientConnect = DK3_ClientConnect;
	ge3.ClientBegin = DK3_ClientBegin;
	ge3.ClientUserinfoChanged = DK3_ClientUserinfoChanged;
	ge3.ClientDisconnect = DK3_ClientDisconnect;
	ge3.ClientCommand = DK3_ClientCommand;
	ge3.ClientThink = DK3_ClientThink;
	ge3.RunFrame = DK3_RunFrame;
	ge3.ServerCommand = DK3_ServerCommand;
	return &ge3;
}
