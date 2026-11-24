/*
The Quake II Rerelease's game library (KEX, GAME_API_VERSION 2023) on FTE's Quake II server.

FTE's server speaks game API 3, the original Quake II's. The rerelease's game has another interface (q2rr_api.h):
other edict, client, entity and player state layouts, configstrings in other places, JSON saves, more functions.
This sits between them, the other way about from Q2REPRO's game3_proxy (which runs game API 3 libraries on an
engine that speaks the rerelease's): FTE gets a game API 3 export table, and its edicts are copies kept in step
with the rerelease game's own:

- the game's edicts are copied to FTE's (game API 3) after every call into the game, and as an entity is linked,
  so what FTE sends, links and traces against is what the game has;
- what FTE works out (links, areas, the packed solid, ping) goes back to the game's edicts;
- the game's calls (game_import_t) are FTE's game API 3 imports, with the rerelease's ones on top: clip,
  BoxEdicts' filters, localised prints (svc_locprint), local sounds, JSON saves, configstring remapping...;
- its messages go out as they are: they are the rerelease's protocol, which FTE's clients speak (Q2EX, 2023).

The game runs at FTE's Quake II tick (10 a second), which the rerelease's game handles (gi.tick_rate).

GPL v2 or later.
*/
#include "quakedef.h"

#ifdef Q2SERVER
#include "q2rr_api.h"

#define RRCS_MODELS_OLD		Q2CS_MODELS

static game_import_t	*fgi;			//FTE's game API 3 imports
static game_export_t	ge3;			//what FTE sees
static rr_game_import_t	rrgi;			//what the game gets
static rr_game_export_t	*rrge;
static q2edict_t		*shadow;		//FTE's (game API 3) copies of the game's edicts
static struct q2gclient_s *shadowcl;	//and of its clients
static int				shadowmax;
static int				rrmaxclients;
static uint32_t			rrframenum;
static qboolean			rrtransition;	//WriteLevel is for a level change

#define RREDICT(i)		((rr_edict_t *)((char *)rrge->edicts + (size_t)(i)*rrge->edict_size))
#define RRNUM(e)		((int)(((char *)(e) - (char *)rrge->edicts) / rrge->edict_size))
#define SHADOWNUM(e)	((int)((q2edict_t *)(e) - shadow))

static q2edict_t *RR_ToShadow(const rr_edict_t *e)
{
	int i;
	if (!e)
		return NULL;
	i = RRNUM(e);
	if (i < 0 || i >= shadowmax)
		return NULL;
	return &shadow[i];
}
static rr_edict_t *RR_FromShadow(const void *e)
{
	int i;
	if (!e)
		return NULL;
	i = SHADOWNUM(e);
	if (i < 0 || i >= shadowmax)
		return NULL;
	return RREDICT(i);
}

/*
=====================================================================
Edicts: the game's to FTE's and back
=====================================================================
*/
static short RR_Short8(float f)
{	//13.3 fixed point
	f *= 8;
	if (f > 32767) f = 32767;
	if (f < -32768) f = -32768;
	return (short)(f < 0 ? f - 0.5f : f + 0.5f);
}

static void RR_PlayerStateToShadow(q2player_state_t *o, const rr_player_state_t *ps)
{
	int i;
	float a, sa;

	switch(ps->pmove.pm_type)
	{
	case RR_PM_NORMAL:
	case RR_PM_GRAPPLE:		o->pmove.pm_type = Q2PM_NORMAL;		break;
	case RR_PM_NOCLIP:
	case RR_PM_SPECTATOR:	o->pmove.pm_type = Q2PM_SPECTATOR;	break;
	case RR_PM_DEAD:		o->pmove.pm_type = Q2PM_DEAD;		break;
	case RR_PM_GIB:			o->pmove.pm_type = Q2PM_GIB;		break;
	default:				o->pmove.pm_type = Q2PM_FREEZE;		break;
	}
	for (i = 0; i < 3; i++)
	{
		o->pmove.origin[i] = RR_Short8(ps->pmove.origin[i]);
		o->pmove.velocity[i] = RR_Short8(ps->pmove.velocity[i]);
		o->pmove.delta_angles[i] = ANGLE2SHORT(ps->pmove.delta_angles[i]);
	}
	o->pmove.pm_flags = ps->pmove.pm_flags & 0x7f;
	o->pmove.pm_time = min(255, ps->pmove.pm_time/8);
	o->pmove.gravity = ps->pmove.gravity;

	VectorCopy(ps->viewangles, o->viewangles);
	VectorCopy(ps->viewoffset, o->viewoffset);
	o->viewoffset[2] += ps->pmove.viewheight;	//the rerelease keeps the view height apart
	VectorCopy(ps->kick_angles, o->kick_angles);
	VectorCopy(ps->gunangles, o->gunangles);
	VectorCopy(ps->gunoffset, o->gunoffset);
	o->gunindex = ps->gunindex;
	o->gunframe = ps->gunframe;

	//one blend: the damage's over the screen's
	a = ps->damage_blend[3];
	sa = ps->screen_blend[3];
	if (a > 0)
	{
		float out = a + sa*(1-a);
		for (i = 0; i < 3; i++)
			o->blend[i] = out?(ps->damage_blend[i]*a + ps->screen_blend[i]*sa*(1-a))/out:0;
		o->blend[3] = out;
	}
	else
		Vector4Copy(ps->screen_blend, o->blend);

	o->fov = ps->fov;
	o->rdflags = ps->rdflags;
	for (i = 0; i < Q2MAX_STATS; i++)
		o->stats[i] = ps->stats[i];
}

static void RR_EdictToShadow(int i)
{
	rr_edict_t *re = RREDICT(i);
	q2edict_t *se = &shadow[i];

	if (re->client && i >= 1 && i <= rrmaxclients)
	{
		se->client = &shadowcl[i-1];
		RR_PlayerStateToShadow(&se->client->ps, &re->client->ps);
	}
	else
		se->client = NULL;

	se->inuse = re->inuse;
	if (!re->inuse && !se->inuse)
		return;

	se->s.number = re->s.number;
	VectorCopy(re->s.origin, se->s.origin);
	VectorCopy(re->s.angles, se->s.angles);
	VectorCopy(re->s.old_origin, se->s.old_origin);
	se->s.modelindex = re->s.modelindex;
	se->s.modelindex2 = re->s.modelindex2;
	se->s.modelindex3 = re->s.modelindex3;
	se->s.modelindex4 = re->s.modelindex4;
	se->s.frame = re->s.frame;
	se->s.skinnum = re->s.skinnum;
	se->s.effects = (unsigned int)re->s.effects;
	se->s.renderfx = re->s.renderfx;
	se->s.sound = re->s.sound;
	se->s.event = re->s.event;
	//s.solid is FTE's to pack, as it links

	se->svflags = re->svflags;
	VectorCopy(re->mins, se->mins);
	VectorCopy(re->maxs, se->maxs);
	se->solid = re->solid;
	se->clipmask = re->clipmask;
	se->owner = RR_ToShadow(re->owner);
}

//what FTE worked out as it linked the entity
static void RR_ShadowLinkToEdict(int i)
{
	rr_edict_t *re = RREDICT(i);
	q2edict_t *se = &shadow[i];
	VectorCopy(se->absmin, re->absmin);
	VectorCopy(se->absmax, re->absmax);
	VectorCopy(se->size, re->size);
	re->linkcount = se->linkcount;
	re->areanum = se->areanum;
	re->areanum2 = se->areanum2;
	re->s.solid = se->s.solid;
	re->linked = se->area.prev != NULL;
}

static void RR_SyncAll(void)
{
	int i, n = rrge->num_edicts;
	if (n > shadowmax)
		n = shadowmax;
	for (i = 0; i < n; i++)
		RR_EdictToShadow(i);
	for (; i < ge3.num_edicts && i < shadowmax; i++)
		shadow[i].inuse = false;	//the game has fewer now (a loaded level)
	ge3.num_edicts = n;
}
static void RR_SyncOwners(void)
{
	int i, n = min((int)rrge->num_edicts, shadowmax);
	for (i = 0; i < n; i++)
		shadow[i].owner = RR_ToShadow(RREDICT(i)->owner);
}

//FTE's to the game, before calling into it: the clients' pings
static void RR_PreCall(void);

/*
=====================================================================
cvars: the game holds pointers to its own cvar_t, kept in step with FTE's
=====================================================================
*/
typedef struct rrcvar_s
{
	rr_cvar_t rr;
	cvar_t *var;
	int modifiedcount;
	struct rrcvar_s *nextmirror;
} rrcvar_t;
static rrcvar_t *rrcvars;

static void RR_CvarUpdate(rrcvar_t *m)
{
	cvar_t *v = m->var;
	if (m->rr.string == v->string && m->modifiedcount == v->modifiedcount)
		return;
	m->rr.string = v->string;
	m->rr.latched_string = v->latched_string;
	m->rr.value = v->value;
	m->rr.integer = v->ival;
	m->rr.flags = v->flags & (CVAR_ARCHIVE|CVAR_USERINFO|CVAR_SERVERINFO|CVAR_NOSET|CVAR_MAPLATCH);
	if (m->modifiedcount != v->modifiedcount || m->rr.modified_count == 0)
		m->rr.modified_count++;
	if (!m->rr.modified_count)
		m->rr.modified_count = 1;
	m->modifiedcount = v->modifiedcount;
}
static void RR_CvarsUpdate(void)
{
	rrcvar_t *m;
	for (m = rrcvars; m; m = m->nextmirror)
		RR_CvarUpdate(m);
}
static rr_cvar_t *RR_CvarMirror(cvar_t *v)
{
	rrcvar_t *m;
	if (!v)
		return NULL;
	for (m = rrcvars; m; m = m->nextmirror)
		if (m->var == v)
			break;
	if (!m)
	{
		m = Z_Malloc(sizeof(*m));
		m->var = v;
		m->rr.name = v->name;
		m->modifiedcount = v->modifiedcount-1;
		m->nextmirror = rrcvars;
		rrcvars = m;
	}
	RR_CvarUpdate(m);
	return &m->rr;
}
static void RR_CvarsFree(void)
{
	rrcvar_t *m;
	while ((m = rrcvars))
	{
		rrcvars = m->nextmirror;
		Z_Free(m);
	}
}

static rr_cvar_t *RRGI_cvar(const char *name, const char *value, uint32_t flags)
{
	return RR_CvarMirror(fgi->cvar(name, value, flags & (CVAR_ARCHIVE|CVAR_USERINFO|CVAR_SERVERINFO|CVAR_NOSET|CVAR_MAPLATCH)));
}
static rr_cvar_t *RRGI_cvar_set(const char *name, const char *value)
{
	rr_cvar_t *r = RR_CvarMirror(fgi->cvar_set(name, value));
	RR_CvarsUpdate();
	return r;
}
static rr_cvar_t *RRGI_cvar_forceset(const char *name, const char *value)
{
	rr_cvar_t *r = RR_CvarMirror(fgi->cvar_forceset(name, value));
	RR_CvarsUpdate();
	return r;
}

/*
=====================================================================
configstrings: the rerelease's places to game API 3's
=====================================================================
*/
static char *rrextracs[RR_MAX_CONFIGSTRINGS - RR_CS_WHEEL_WEAPONS];	//the ones game API 3 has no room for
static char *rrshadowlights[RR_MAX_SHADOW_LIGHTS];

//game API 3's configstring for the rerelease's, or -1
static int RR_ConfigstringToQ2(int i)
{
	if (i < RR_CS_STATUSBAR)
		return i;						//name, cdtrack, sky...
	if (i < RR_CS_AIRACCEL)
		return (i == RR_CS_STATUSBAR)?Q2CS_STATUSBAR:-1;	//one long string, set at once
	if (i < RR_CS_MODELS)
		return i - RR_CS_AIRACCEL + Q2CS_AIRACCEL;
	if (i >= RR_CS_MODELS && i < RR_CS_SOUNDS)
		return (i - RR_CS_MODELS < Q2MAX_MODELS)?i - RR_CS_MODELS + Q2CS_MODELS:-1;
	if (i >= RR_CS_SOUNDS && i < RR_CS_IMAGES)
		return (i - RR_CS_SOUNDS < Q2MAX_SOUNDS)?i - RR_CS_SOUNDS + Q2CS_SOUNDS:-1;
	if (i >= RR_CS_IMAGES && i < RR_CS_LIGHTS)
		return (i - RR_CS_IMAGES < Q2MAX_IMAGES)?i - RR_CS_IMAGES + Q2CS_IMAGES:-1;
	if (i >= RR_CS_LIGHTS && i < RR_CS_SHADOWLIGHTS)
		return i - RR_CS_LIGHTS + Q2CS_LIGHTS;
	if (i >= RR_CS_ITEMS && i < RR_CS_PLAYERSKINS)
		return i - RR_CS_ITEMS + Q2CS_ITEMS;
	if (i >= RR_CS_PLAYERSKINS && i < RR_CS_GENERAL)
		return (i - RR_CS_PLAYERSKINS < Q2MAX_CLIENTS)?i - RR_CS_PLAYERSKINS + Q2CS_PLAYERSKINS:-1;
	if (i >= RR_CS_GENERAL && i < RR_CS_WHEEL_WEAPONS)
		return (i - RR_CS_GENERAL < Q2MAX_GENERAL)?i - RR_CS_GENERAL + Q2CS_GENERAL:-1;
	return -1;
}

static void RRGI_configstring(int i, const char *val)
{
	int q2 = RR_ConfigstringToQ2(i);
	if (q2 >= 0)
	{
		fgi->configstring(q2, val);
		return;
	}
	if (!val)
		val = "";
	if (i >= RR_CS_SHADOWLIGHTS && i < RR_CS_ITEMS)
	{
		Z_Free(rrshadowlights[i-RR_CS_SHADOWLIGHTS]);
		rrshadowlights[i-RR_CS_SHADOWLIGHTS] = Z_StrDup(val);
		SVQ2_ExtraConfigstring(i-RR_CS_SHADOWLIGHTS+Q2EXCS_RTLIGHTS, val);
	}
	else if (i >= RR_CS_WHEEL_WEAPONS && i < RR_MAX_CONFIGSTRINGS)
	{
		Z_Free(rrextracs[i-RR_CS_WHEEL_WEAPONS]);
		rrextracs[i-RR_CS_WHEEL_WEAPONS] = Z_StrDup(val);
		SVQ2_ExtraConfigstring(i-RR_CS_WHEEL_WEAPONS+Q2ECS_WHEEL_WEAPONS, val);	//the same order in both
	}
	else
		Con_DPrintf("rerelease configstring %i (%s) has no room here\n", i, val);
}
static const char *RRGI_get_configstring(int i)
{
	int q2 = RR_ConfigstringToQ2(i);
	const char *s;
	if (q2 >= 0)
		s = sv.strings.configstring[q2];
	else if (i >= RR_CS_SHADOWLIGHTS && i < RR_CS_ITEMS)
		s = rrshadowlights[i-RR_CS_SHADOWLIGHTS];
	else if (i >= RR_CS_WHEEL_WEAPONS && i < RR_MAX_CONFIGSTRINGS)
		s = rrextracs[i-RR_CS_WHEEL_WEAPONS];
	else
		s = NULL;
	return s?s:"";
}
static void RR_ConfigstringsClear(void)
{
	size_t i;
	for (i = 0; i < countof(rrextracs); i++)
		Z_Free(rrextracs[i]), rrextracs[i] = NULL;
	for (i = 0; i < countof(rrshadowlights); i++)
		Z_Free(rrshadowlights[i]), rrshadowlights[i] = NULL;
}

/*
=====================================================================
prints
=====================================================================
*/
static void RRGI_Com_Print(const char *msg)
{
	fgi->dprintf("%s", msg);
}
static void RRGI_Broadcast_Print(int32_t level, const char *msg)
{
	level &= 7;
	if (level > RR_PRINT_CHAT)
		level = RR_PRINT_HIGH;
	fgi->bprintf(level, "%s", msg);
}
static void RRGI_Client_Print(rr_edict_t *ent, int32_t level, const char *msg)
{
	if (level & RR_PRINT_BROADCAST)
	{
		RRGI_Broadcast_Print(level, msg);
		return;
	}
	if (!ent)
	{
		RRGI_Com_Print(msg);
		return;
	}
	level &= 7;
	if (level == RR_PRINT_CENTER || level == RR_PRINT_TYPEWRITER)
		fgi->centerprintf(RR_ToShadow(ent), "%s", msg);
	else
		fgi->cprintf(RR_ToShadow(ent), (level > RR_PRINT_CHAT)?RR_PRINT_HIGH:level, "%s", msg);
}
static void RRGI_Center_Print(rr_edict_t *ent, const char *msg)
{
	fgi->centerprintf(RR_ToShadow(ent), "%s", msg);
}
//localised: svc_locprint, the client looks the text up (localization/loc_*.txt) and puts the arguments in
static void RRGI_Loc_Print(rr_edict_t *ent, int32_t level, const char *base, const char **args, size_t num_args)
{
	size_t i;
	fgi->WriteByte(svcq2ex_locprint);
	fgi->WriteByte(level);
	fgi->WriteString(base);
	fgi->WriteByte(num_args);
	for (i = 0; i < num_args; i++)
		fgi->WriteString(args[i]?args[i]:"");
	if (ent && !(level & RR_PRINT_BROADCAST))
		fgi->unicast(RR_ToShadow(ent), true);
	else
		fgi->multicast(vec3_origin, MULTICAST_ALL_R);
}
static void RRGI_Com_Error(const char *msg)
{
	fgi->error("%s", msg);
}

/*
=====================================================================
sounds, models
=====================================================================
*/
static void RRGI_sound(rr_edict_t *ent, uint8_t channel, int soundindex, float volume, float attenuation, float timeofs)
{
	if (!ent)
		return;
	RR_EdictToShadow(RRNUM(ent));	//where it is now
	fgi->sound(RR_ToShadow(ent), channel & 31, soundindex, volume, attenuation, timeofs);
}
static void RRGI_positioned_sound(const float *origin, rr_edict_t *ent, uint8_t channel, int soundindex, float volume, float attenuation, float timeofs)
{
	if (ent)
		RR_EdictToShadow(RRNUM(ent));
	fgi->positioned_sound((float*)origin, RR_ToShadow(ent), channel & 31, soundindex, volume, attenuation, timeofs);
}
static void RRGI_local_sound(rr_edict_t *target, const float *origin, rr_edict_t *ent, uint8_t channel, int soundindex, float volume, float attenuation, float timeofs, uint32_t dupe_key)
{	//for the target player only, in the rerelease (splitscreen). everyone hears it here.
	RRGI_positioned_sound(origin, ent, channel, soundindex, volume, attenuation, timeofs);
}
static int RRGI_modelindex(const char *name)
{
	return fgi->modelindex(name);
}
static int RRGI_soundindex(const char *name)
{
	return fgi->soundindex(name);
}
static int RRGI_imageindex(const char *name)
{
	return fgi->imageindex(name);
}
static void RRGI_setmodel(rr_edict_t *ent, const char *name)
{
	int i = RRNUM(ent);
	q2edict_t *se = &shadow[i];
	RR_EdictToShadow(i);
	fgi->setmodel(se, name);
	ent->s.modelindex = se->s.modelindex;
	VectorCopy(se->mins, ent->mins);
	VectorCopy(se->maxs, ent->maxs);
	RR_ShadowLinkToEdict(i);	//brush models are linked as their size is set
}

/*
=====================================================================
collision
=====================================================================
*/
//the game's surfaces have more fields (and are never null): one each for FTE's
static rr_csurface_t rrnullsurface;
static struct {const q2csurface_t *fte; rr_csurface_t *rr;} *rrsurfaces;
static size_t rrnumsurfaces, rrmaxsurfaces;
static rr_csurface_t *RR_Surface(const q2csurface_t *s)
{
	size_t i;
	rr_csurface_t *r;
	if (!s)
		return &rrnullsurface;
	for (i = 0; i < rrnumsurfaces; i++)
		if (rrsurfaces[i].fte == s)
			return rrsurfaces[i].rr;
	if (rrnumsurfaces == rrmaxsurfaces)
		Z_ReallocElements((void**)&rrsurfaces, &rrmaxsurfaces, rrmaxsurfaces+256, sizeof(*rrsurfaces));
	r = Z_Malloc(sizeof(*r));
	Q_strncpyz(r->name, s->name, sizeof(r->name));
	r->flags = s->flags;
	r->value = s->value;
	r->id = rrnumsurfaces+1;
	rrsurfaces[rrnumsurfaces].fte = s;
	rrsurfaces[rrnumsurfaces].rr = r;
	rrnumsurfaces++;
	return r;
}
static void RR_SurfacesClear(void)
{
	size_t i;
	for (i = 0; i < rrnumsurfaces; i++)
		Z_Free(rrsurfaces[i].rr);
	Z_Free(rrsurfaces);
	rrsurfaces = NULL;
	rrnumsurfaces = rrmaxsurfaces = 0;
}

static rr_trace_t RR_Trace(const q2trace_t *t)
{
	rr_trace_t r;
	memset(&r, 0, sizeof(r));
	r.allsolid = !!t->allsolid;
	r.startsolid = !!t->startsolid;
	r.fraction = t->fraction;
	VectorCopy(t->endpos, r.endpos);
	VectorCopy(t->plane.normal, r.plane.normal);
	r.plane.dist = t->plane.dist;
	r.plane.type = t->plane.type;
	r.plane.signbits = t->plane.signbits;
	r.surface = RR_Surface(t->surface);
	r.contents = t->contents;
	r.ent = t->ent?RR_FromShadow(t->ent):RREDICT(0);
	r.plane2 = r.plane;
	r.surface2 = NULL;
	return r;
}
static rr_trace_t RRGI_trace(const float *start, const float *mins, const float *maxs, const float *end, const rr_edict_t *passent, uint32_t contentmask)
{
	q2trace_t t;
	RR_SyncOwners();	//who is whose (missiles pass their owners)
	t = fgi->trace((float*)start, (float*)(mins?mins:vec3_origin), (float*)(maxs?maxs:vec3_origin), (float*)end, RR_ToShadow(passent), contentmask);
	return RR_Trace(&t);
}
static rr_trace_t RRGI_clip(rr_edict_t *entity, const float *start, const float *mins, const float *maxs, const float *end, uint32_t contentmask)
{
	trace_t tr;
	q2trace_t t;
	tr = WorldQ2_ClipToEntity(&sv.world, RR_ToShadow(entity), (float*)start, (float*)(mins?mins:vec3_origin), (float*)(maxs?maxs:vec3_origin), (float*)end, contentmask);
	memset(&t, 0, sizeof(t));
	t.allsolid = tr.allsolid;
	t.startsolid = tr.startsolid;
	t.fraction = tr.fraction;
	VectorCopy(tr.endpos, t.endpos);
	VectorCopy(tr.plane.normal, t.plane.normal);
	t.plane.dist = tr.plane.dist;
	t.surface = tr.surface;
	t.contents = tr.contents;
	t.ent = RR_ToShadow(entity);
	return RR_Trace(&t);
}
static uint32_t RRGI_pointcontents(const float *p)
{
	return fgi->pointcontents((float*)p);
}
static rr_bool RRGI_inPVS(const float *p1, const float *p2, rr_bool portals)
{
	return !!fgi->inPVS((float*)p1, (float*)p2);
}
static rr_bool RRGI_inPHS(const float *p1, const float *p2, rr_bool portals)
{
	return !!fgi->inPHS((float*)p1, (float*)p2);
}
static void RRGI_SetAreaPortalState(int portalnum, rr_bool open)
{
	fgi->SetAreaPortalState(portalnum, open);
}
static rr_bool RRGI_AreasConnected(int a1, int a2)
{
	return !!fgi->AreasConnected(a1, a2);
}
static void RRGI_linkentity(rr_edict_t *ent)
{
	int i = RRNUM(ent);
	if (i < 0 || i >= shadowmax)
		return;
	RR_EdictToShadow(i);
	shadow[i].inuse = true;	//the game links an entity before saying it's in use, at times
	fgi->linkentity(&shadow[i]);
	RR_ShadowLinkToEdict(i);
}
static void RRGI_unlinkentity(rr_edict_t *ent)
{
	int i = RRNUM(ent);
	if (i < 0 || i >= shadowmax)
		return;
	fgi->unlinkentity(&shadow[i]);
	ent->linked = false;
}
static size_t RRGI_BoxEdicts(const float *mins, const float *maxs, rr_edict_t **list, size_t maxcount, int32_t areatype, rr_BoxEdictsFilter_t filter, void *filter_data)
{
	static q2edict_t *found[MAX_Q2EDICTS];
	int num = fgi->BoxEdicts((float*)mins, (float*)maxs, found, countof(found), areatype), i;
	size_t count = 0;
	for (i = 0; i < num; i++)
	{
		rr_edict_t *e = RR_FromShadow(found[i]);
		int32_t r = filter?filter(e, filter_data):RR_BOXEDICTS_KEEP;
		if (!(r & RR_BOXEDICTS_SKIP))
		{
			if (count < maxcount)
				list[count] = e;
			count++;
		}
		if (r & RR_BOXEDICTS_END)
			break;
	}
	if (maxcount && count > maxcount)
		count = maxcount;
	return count;
}

/*
=====================================================================
messages
=====================================================================
*/
static void RRGI_multicast(const float *origin, int32_t to, rr_bool reliable)
{
	fgi->multicast((float*)origin, (multicast_t)(to + (reliable?MULTICAST_ALL_R:MULTICAST_ALL)));
}
static void RRGI_unicast(rr_edict_t *ent, rr_bool reliable, uint32_t dupe_key)
{
	fgi->unicast(RR_ToShadow(ent), reliable);
}
static void RRGI_WriteChar(int c)				{fgi->WriteChar(c);}
static void RRGI_WriteByte(int c)				{fgi->WriteByte(c);}
static void RRGI_WriteShort(int c)				{fgi->WriteShort(c);}
static void RRGI_WriteLong(int c)				{fgi->WriteLong(c);}
static void RRGI_WriteFloat(float f)			{fgi->WriteFloat(f);}
static void RRGI_WriteString(const char *s)		{fgi->WriteString(s);}
static void RRGI_WritePosition(const float *p)	{fgi->WritePosition((float*)p);}
static void RRGI_WriteDir(const float *p)		{fgi->WriteDir((float*)p);}
static void RRGI_WriteAngle(float f)			{fgi->WriteByte((int)(f*256/360) & 255);}	//the legacy 8 bits, always
static void RRGI_WriteEntity(const rr_edict_t *e)	{fgi->WriteShort(e?RRNUM(e):0);}

/*
=====================================================================
the rest
=====================================================================
*/
static void *RRGI_TagMalloc(size_t size, int tag)	{return fgi->TagMalloc((int)size, tag);}
static void RRGI_TagFree(void *block)				{fgi->TagFree(block);}
static void RRGI_FreeTags(int tag)					{fgi->FreeTags(tag);}
static int RRGI_argc(void)							{return fgi->argc();}
static const char *RRGI_argv(int n)					{return fgi->argv(n);}
static const char *RRGI_args(void)					{return fgi->args();}
static void RRGI_AddCommandString(const char *t)	{fgi->AddCommandString(t);}
static void RRGI_DebugGraph(float value, int color)	{}
static void *RRGI_GetExtension(const char *name)	{return NULL;}
static void RRGI_Bot_RegisterEdict(const rr_edict_t *e)		{RRNav_RegisterEdict(e);}
static void RRGI_Bot_UnRegisterEdict(const rr_edict_t *e)	{RRNav_UnRegisterEdict(e);}
static int32_t RRGI_Bot_MoveToPoint(const rr_edict_t *bot, const float *point, const float tol)	{return 0;}
static int32_t RRGI_Bot_FollowActor(const rr_edict_t *bot, const rr_edict_t *actor)		{return 0;}
static rr_bool RRGI_GetPathToGoal(const rr_PathRequest *request, rr_PathInfo *info)
{	//the map's navigation mesh (svq2_rrnav.c)
	return RRNav_GetPathToGoal(request, info);
}
static void RRGI_Draw_Line(const float *a, const float *b, const rr_rgba_t *c, const float l, const rr_bool d) {}
static void RRGI_Draw_Point(const float *a, const float s, const rr_rgba_t *c, const float l, const rr_bool d) {}
static void RRGI_Draw_Circle(const float *a, const float r, const rr_rgba_t *c, const float l, const rr_bool d) {}
static void RRGI_Draw_Bounds(const float *a, const float *b, const rr_rgba_t *c, const float l, const rr_bool d) {}
static void RRGI_Draw_Sphere(const float *a, const float r, const rr_rgba_t *c, const float l, const rr_bool d) {}
static void RRGI_Draw_OrientedWorldText(const float *a, const char *t, const rr_rgba_t *c, const float s, const float l, const rr_bool d) {}
static void RRGI_Draw_StaticWorldText(const float *a, const float *an, const char *t, const rr_rgba_t *c, const float s, const float l, const rr_bool d) {}
static void RRGI_Draw_Cylinder(const float *a, const float h, const float r, const rr_rgba_t *c, const float l, const rr_bool d) {}
static void RRGI_Draw_Ray(const float *a, const float *dir, const float len, const float s, const rr_rgba_t *c, const float l, const rr_bool d) {}
static void RRGI_Draw_Arrow(const float *a, const float *b, const float s, const rr_rgba_t *c, const rr_rgba_t *c2, const float l, const rr_bool d) {}
static void RRGI_ReportMatchDetails_Multicast(rr_bool is_end) {}
static uint32_t RRGI_ServerFrame(void)			{return rrframenum;}
static void RRGI_SendToClipBoard(const char *t)	{}
static size_t RRGI_Info_ValueForKey(const char *s, const char *key, char *buffer, size_t len)
{
	const char *v = Info_ValueForKey(s, key);
	if (len)
		Q_strncpyz(buffer, v, len);
	return strlen(v);
}
static rr_bool RRGI_Info_RemoveKey(char *s, const char *key)
{
	Info_RemoveKey(s, key);
	return true;
}
static rr_bool RRGI_Info_SetValueForKey(char *s, const char *key, const char *value)
{
	Info_SetValueForKey(s, key, value, 2048);	//the rerelease's MAX_INFO_STRING
	return true;
}

/*
=====================================================================
game API 3 exports, as FTE calls them
=====================================================================
*/
static void RR_PreCall(void)
{
	int i;
	RR_CvarsUpdate();
	for (i = 1; i <= rrmaxclients && i < shadowmax; i++)
	{
		rr_edict_t *re = RREDICT(i);
		if (re->client)
			re->client->ping = shadowcl[i-1].ping;
	}
}

static void VARGS RRGE_Init(void)
{
	cvar_t *v;
	//FTE's Quake II server has room for this many
	v = Cvar_Get("maxentities", STRINGIFY(MAX_Q2EDICTS), CVAR_MAPLATCH, "Quake2 game variables");
	if (v && (v->ival > MAX_Q2EDICTS || v->ival <= 0))
		Cvar_ForceSet(v, STRINGIFY(MAX_Q2EDICTS));
	//lag compensation rewinds by the client's server_frame, which isn't the rerelease's here
	Cvar_Get("g_lag_compensation", "0", 0, "Quake2 game variables");

	RRNav_Init();
	rrge->PreInit();
	rrge->Init();

	rrmaxclients = svq2_maxclients;
	shadowmax = rrge->max_edicts;
	if (shadowmax > MAX_Q2EDICTS)
		shadowmax = MAX_Q2EDICTS;
	shadow = Z_Malloc(sizeof(*shadow) * shadowmax);
	shadowcl = Z_Malloc(sizeof(*shadowcl) * max(1, rrmaxclients));
	ge3.edicts = shadow;
	ge3.edict_size = sizeof(*shadow);
	ge3.max_edicts = shadowmax;
	ge3.num_edicts = 0;
	RR_SyncAll();
}
static void VARGS RRGE_Shutdown(void)
{
	if (rrge)
		rrge->Shutdown();
	RRNav_Unload();
	Z_Free(shadow);
	Z_Free(shadowcl);
	shadow = NULL;
	shadowcl = NULL;
	shadowmax = 0;
	ge3.edicts = NULL;
	ge3.num_edicts = ge3.max_edicts = 0;
	RR_CvarsFree();
	RR_SurfacesClear();
	RR_ConfigstringsClear();
	SVQ2_ExtraConfigstringsClear();
	rrge = NULL;
}
static void VARGS RRGE_SpawnEntities(const char *mapname, const char *entstring, const char *spawnpoint)
{
	RR_SurfacesClear();	//a new map's
	RR_ConfigstringsClear();
	SVQ2_ExtraConfigstringsClear();
	RRNav_Unload();
	RR_PreCall();
	rrge->SpawnEntities(mapname, entstring, spawnpoint);
	RR_SyncAll();
}

static char *RR_LoadFile(const char *filename)
{
	FILE *f = fopen(filename, "rb");
	long len;
	char *s;
	if (!f)
	{
		Con_Printf(CON_ERROR"couldn't open %s\n", filename);
		return NULL;
	}
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	s = BZ_Malloc(len+1);
	if (fread(s, 1, len, f) != (size_t)len)
		len = 0;
	s[len] = 0;
	fclose(f);
	return s;
}
static void RR_SaveFile(const char *filename, char *json, size_t size)
{
	FILE *f;
	if (!json)
		return;
	f = fopen(filename, "wb");
	if (f)
	{
		fwrite(json, 1, size, f);
		fclose(f);
	}
	else
		Con_Printf(CON_ERROR"couldn't write %s\n", filename);
	fgi->TagFree(json);
}
static void VARGS RRGE_WriteGame(const char *filename, qboolean autosave)
{
	size_t size = 0;
	char *json;
	RR_PreCall();
	json = rrge->WriteGameJson(!!autosave, &size);
	RR_SaveFile(filename, json, size);
}
static void VARGS RRGE_ReadGame(const char *filename)
{
	char *json = RR_LoadFile(filename);
	if (!json)
		return;
	RR_PreCall();
	rrge->ReadGameJson(json);
	BZ_Free(json);
	RR_SyncAll();
}
static void VARGS RRGE_WriteLevel(const char *filename)
{
	size_t size = 0;
	char *json;
	RR_PreCall();
	json = rrge->WriteLevelJson(rrtransition, &size);
	RR_SaveFile(filename, json, size);
}
static void VARGS RRGE_ReadLevel(const char *filename)
{
	char *json = RR_LoadFile(filename);
	int i;
	if (!json)
		return;
	RRNav_Unload();
	RR_PreCall();
	rrge->ReadLevelJson(json);
	BZ_Free(json);
	//its entities come back linked as the game's: link FTE's too
	RR_SyncAll();
	for (i = 0; i < ge3.num_edicts; i++)
	{
		rr_edict_t *re = RREDICT(i);
		if (re->inuse && re->linked)
		{
			fgi->linkentity(&shadow[i]);
			RR_ShadowLinkToEdict(i);
		}
	}
}

/*
The rerelease's client always sends these; the game takes no fov for a fov of 1 (where the original game took 90).
*/
static void RR_UserinfoDefaults(char *info, size_t size)
{
	if (!*Info_ValueForKey(info, "fov"))
		Info_SetValueForKey(info, "fov", "90", size);
}
static qboolean VARGS RRGE_ClientConnect(q2edict_t *ent, char *userinfo)
{
	char info[2048];
	rr_bool r;
	RR_PreCall();
	Q_strncpyz(info, userinfo, sizeof(info));
	RR_UserinfoDefaults(info, sizeof(info));
	r = rrge->ClientConnect(RR_FromShadow(ent), info, "", false);
	if (!r)
		Q_strncpyz(userinfo, info, 512);	//its rejmsg. game API 3's room.
	RR_SyncAll();
	return !!r;
}
static void VARGS RRGE_ClientBegin(q2edict_t *ent)
{
	RR_PreCall();
	rrge->ClientBegin(RR_FromShadow(ent));
	RR_SyncAll();
}
static void VARGS RRGE_ClientUserinfoChanged(q2edict_t *ent, char *userinfo)
{
	char info[2048];
	RR_PreCall();
	Q_strncpyz(info, userinfo, sizeof(info));
	RR_UserinfoDefaults(info, sizeof(info));
	rrge->ClientUserinfoChanged(RR_FromShadow(ent), info);
	RR_SyncAll();
}
static void VARGS RRGE_ClientDisconnect(q2edict_t *ent)
{
	RR_PreCall();
	rrge->ClientDisconnect(RR_FromShadow(ent));
	RR_SyncAll();
}
static void VARGS RRGE_ClientCommand(q2edict_t *ent)
{
	RR_PreCall();
	rrge->ClientCommand(RR_FromShadow(ent));
	RR_SyncAll();
}
static void VARGS RRGE_ClientThink(q2edict_t *ent, q2usercmd_t *cmd)
{
	rr_usercmd_t c;
	int i;
	memset(&c, 0, sizeof(c));
	c.msec = cmd->msec;
	c.buttons = cmd->buttons & (RR_BUTTON_ATTACK|RR_BUTTON_USE|RR_BUTTON_HOLSTER|RR_BUTTON_JUMP|RR_BUTTON_CROUCH|RR_BUTTON_ANY);
	if (cmd->upmove > 0)
		c.buttons |= RR_BUTTON_JUMP;	//the rerelease jumps and crouches with buttons
	else if (cmd->upmove < 0)
		c.buttons |= RR_BUTTON_CROUCH;
	for (i = 0; i < 3; i++)
		c.angles[i] = SHORT2ANGLE(cmd->angles[i]);
	c.forwardmove = cmd->forwardmove;
	c.sidemove = cmd->sidemove;
	c.server_frame = rrframenum;
	RR_PreCall();
	rrge->ClientThink(RR_FromShadow(ent), &c);
	RR_SyncAll();
}
static void VARGS RRGE_RunFrame(void)
{
	RR_PreCall();
	rrframenum++;
	RRNav_Frame(svs.name, &rrgi, rrge);	//(loads the map's mesh first time), its conditional nodes
	rrge->RunFrame(true);
	RR_SyncAll();
}
static void VARGS RRGE_ServerCommand(void)
{
	RR_PreCall();
	rrge->ServerCommand();
	RR_SyncAll();
}

//FTE sent the frame: the events go (the rerelease's PrepFrame does that, and its other per-frame clearing)
void SVQ2RR_PrepFrame(void)
{
	if (!rrge || !shadow)
		return;
	rrge->PrepFrame();
	RR_SyncAll();
}
//WriteLevel is for a level change (the clients are left out, to spawn anew)
void SVQ2RR_SetLevelTransition(qboolean transition)
{
	rrtransition = transition;
}
qboolean SVQ2RR_Active(void)
{
	return rrge != NULL;
}
//the rerelease's stats past game API 3's 32 (keys, health bars, the weapon wheel's...), for Q2EX clients
void SVQ2RR_ExtendedStats(q2edict_t *ent, short *out32)
{
	rr_edict_t *re = (rrge && shadow)?RR_FromShadow(ent):NULL;
	int i;
	if (!re || !re->client)
	{
		memset(out32, 0, sizeof(*out32)*32);
		return;
	}
	for (i = 0; i < 32; i++)
		out32[i] = re->client->ps.stats[32+i];
}
//the rerelease's pmove_state_t as it is (game API 3's is rounded), for Q2EX clients to predict with the rerelease's own pmove
void SVQ2RR_ExtendedPmove(q2edict_t *ent, q2expmove_state_t *out)
{
	rr_edict_t *re = (rrge && shadow)?RR_FromShadow(ent):NULL;
	const rr_pmove_state_t *pm;
	memset(out, 0, sizeof(*out));
	if (!re || !re->client)
		return;
	pm = &re->client->ps.pmove;
	out->valid = true;
	out->pm_type = pm->pm_type;
	out->viewheight = pm->viewheight;
	out->pm_flags = pm->pm_flags;
	out->pm_time = pm->pm_time;
	out->gravity = pm->gravity;
	VectorCopy(pm->origin, out->origin);
	VectorCopy(pm->velocity, out->velocity);
	VectorCopy(pm->delta_angles, out->delta_angles);
}

/*
The rerelease's GetGameAPI, from its library: FTE gets game API 3 exports for it.
*/
void *SVQ2RR_GetGameAPI(void *GetGameAPIptr, void *fteimport)
{
	rr_game_export_t *(*GetGameAPI)(rr_game_import_t *import) = (rr_game_export_t *(*)(rr_game_import_t *))GetGameAPIptr;
	rr_game_export_t *e;

	fgi = fteimport;
	memset(&rrgi, 0, sizeof(rrgi));
	rrgi.tick_rate = 10;			//FTE's Quake II tick
	rrgi.frame_time_s = 0.1;
	rrgi.frame_time_ms = 100;
	rrgi.Broadcast_Print = RRGI_Broadcast_Print;
	rrgi.Com_Print = RRGI_Com_Print;
	rrgi.Client_Print = RRGI_Client_Print;
	rrgi.Center_Print = RRGI_Center_Print;
	rrgi.sound = RRGI_sound;
	rrgi.positioned_sound = RRGI_positioned_sound;
	rrgi.local_sound = RRGI_local_sound;
	rrgi.configstring = RRGI_configstring;
	rrgi.get_configstring = RRGI_get_configstring;
	rrgi.Com_Error = RRGI_Com_Error;
	rrgi.modelindex = RRGI_modelindex;
	rrgi.soundindex = RRGI_soundindex;
	rrgi.imageindex = RRGI_imageindex;
	rrgi.setmodel = RRGI_setmodel;
	rrgi.trace = RRGI_trace;
	rrgi.clip = RRGI_clip;
	rrgi.pointcontents = RRGI_pointcontents;
	rrgi.inPVS = RRGI_inPVS;
	rrgi.inPHS = RRGI_inPHS;
	rrgi.SetAreaPortalState = RRGI_SetAreaPortalState;
	rrgi.AreasConnected = RRGI_AreasConnected;
	rrgi.linkentity = RRGI_linkentity;
	rrgi.unlinkentity = RRGI_unlinkentity;
	rrgi.BoxEdicts = RRGI_BoxEdicts;
	rrgi.multicast = RRGI_multicast;
	rrgi.unicast = RRGI_unicast;
	rrgi.WriteChar = RRGI_WriteChar;
	rrgi.WriteByte = RRGI_WriteByte;
	rrgi.WriteShort = RRGI_WriteShort;
	rrgi.WriteLong = RRGI_WriteLong;
	rrgi.WriteFloat = RRGI_WriteFloat;
	rrgi.WriteString = RRGI_WriteString;
	rrgi.WritePosition = RRGI_WritePosition;
	rrgi.WriteDir = RRGI_WriteDir;
	rrgi.WriteAngle = RRGI_WriteAngle;
	rrgi.WriteEntity = RRGI_WriteEntity;
	rrgi.TagMalloc = RRGI_TagMalloc;
	rrgi.TagFree = RRGI_TagFree;
	rrgi.FreeTags = RRGI_FreeTags;
	rrgi.cvar = RRGI_cvar;
	rrgi.cvar_set = RRGI_cvar_set;
	rrgi.cvar_forceset = RRGI_cvar_forceset;
	rrgi.argc = RRGI_argc;
	rrgi.argv = RRGI_argv;
	rrgi.args = RRGI_args;
	rrgi.AddCommandString = RRGI_AddCommandString;
	rrgi.DebugGraph = RRGI_DebugGraph;
	rrgi.GetExtension = RRGI_GetExtension;
	rrgi.Bot_RegisterEdict = RRGI_Bot_RegisterEdict;
	rrgi.Bot_UnRegisterEdict = RRGI_Bot_UnRegisterEdict;
	rrgi.Bot_MoveToPoint = RRGI_Bot_MoveToPoint;
	rrgi.Bot_FollowActor = RRGI_Bot_FollowActor;
	rrgi.GetPathToGoal = RRGI_GetPathToGoal;
	rrgi.Loc_Print = RRGI_Loc_Print;
	rrgi.Draw_Line = RRGI_Draw_Line;
	rrgi.Draw_Point = RRGI_Draw_Point;
	rrgi.Draw_Circle = RRGI_Draw_Circle;
	rrgi.Draw_Bounds = RRGI_Draw_Bounds;
	rrgi.Draw_Sphere = RRGI_Draw_Sphere;
	rrgi.Draw_OrientedWorldText = RRGI_Draw_OrientedWorldText;
	rrgi.Draw_StaticWorldText = RRGI_Draw_StaticWorldText;
	rrgi.Draw_Cylinder = RRGI_Draw_Cylinder;
	rrgi.Draw_Ray = RRGI_Draw_Ray;
	rrgi.Draw_Arrow = RRGI_Draw_Arrow;
	rrgi.ReportMatchDetails_Multicast = RRGI_ReportMatchDetails_Multicast;
	rrgi.ServerFrame = RRGI_ServerFrame;
	rrgi.SendToClipBoard = RRGI_SendToClipBoard;
	rrgi.Info_ValueForKey = RRGI_Info_ValueForKey;
	rrgi.Info_RemoveKey = RRGI_Info_RemoveKey;
	rrgi.Info_SetValueForKey = RRGI_Info_SetValueForKey;

	e = GetGameAPI(&rrgi);
	if (!e)
		return NULL;
	if (e->apiversion != RR_GAME_API_VERSION)
	{
		Con_Printf(CON_ERROR "rerelease game is version %i, not %i\n", e->apiversion, RR_GAME_API_VERSION);
		return NULL;
	}
	rrge = e;
	rrframenum = 0;

	memset(&ge3, 0, sizeof(ge3));
	ge3.apiversion = Q2GAME_API_VERSION;
	ge3.Init = RRGE_Init;
	ge3.Shutdown = RRGE_Shutdown;
	ge3.SpawnEntities = RRGE_SpawnEntities;
	ge3.WriteGame = RRGE_WriteGame;
	ge3.ReadGame = RRGE_ReadGame;
	ge3.WriteLevel = RRGE_WriteLevel;
	ge3.ReadLevel = RRGE_ReadLevel;
	ge3.ClientConnect = RRGE_ClientConnect;
	ge3.ClientBegin = RRGE_ClientBegin;
	ge3.ClientUserinfoChanged = RRGE_ClientUserinfoChanged;
	ge3.ClientDisconnect = RRGE_ClientDisconnect;
	ge3.ClientCommand = RRGE_ClientCommand;
	ge3.ClientThink = RRGE_ClientThink;
	ge3.RunFrame = RRGE_RunFrame;
	ge3.ServerCommand = RRGE_ServerCommand;
	Con_Printf("Quake II Rerelease game (API %i)\n", e->apiversion);
	return &ge3;
}
#endif
