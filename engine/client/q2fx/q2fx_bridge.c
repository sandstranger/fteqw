/*
yquake2's client, as far as its effects need one around them (see q2fx.h): the client state they read,
the refresh and sound calls they make, network reads, cvars -- all of it forwarded to FTE through
../q2fx_sys.h -- and the entry points FTE calls (Q2FX_*).

Q2FX_EntityEffects is yquake2's automatic particle trails block from CL_AddPacketEntities (cl_entities.c).

Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) yquake2 contributors.
GPL v2 or later.
*/
#include "q2fx.h"
#include <stdarg.h>

q2fx_client_t q2fx_cl;
q2fx_static_t q2fx_cls;
struct sizebuf_s { int unused; };
sizebuf_t net_message;
int num_power_sounds;

/* yquake2's cvars the effects read; FTE passes their values in (Q2FX_SetFrame) */
static cvar_t cv_hand = {.name = "hand", .string = "0"};
static cvar_t cv_laseralpha = {.name = "cl_laseralpha", .string = "0.3", .value = 0.3f};
static cvar_t cv_limitsparksounds = {.name = "cl_limitsparksounds", .string = "0"};
static cvar_t cv_footsteps = {.name = "cl_footsteps", .string = "1", .value = 1};
static cvar_t cv_r1q2_lightstyle = {.name = "cl_r1q2_lightstyle", .string = "1", .value = 1};
cvar_t *hand = &cv_hand;
cvar_t *cl_laseralpha = &cv_laseralpha;
cvar_t *cl_limitsparksounds = &cv_limitsparksounds;
cvar_t *cl_footsteps = &cv_footsteps;
cvar_t *cl_r1q2_lightstyle = &cv_r1q2_lightstyle;

/* --- refresh and sound --- */

void V_AddEntity(entity_t *ent)
{
	Q2FXSYS_AddEntity(ent->model, ent->origin, ent->angles, ent->oldorigin, ent->frame, ent->oldframe,
			ent->backlerp, ent->skinnum, ent->alpha, ent->flags);
}
void V_AddParticle(vec3_t org, unsigned int color, float alpha)
{
	Q2FXSYS_AddParticle(org, color, alpha);
}
void V_AddLight(vec3_t org, float intensity, float r, float g, float b)
{
	Q2FXSYS_AddLight(org, intensity, r, g, b);
}
void V_AddLightStyle(int style, float r, float g, float b)
{	/* FTE runs Quake 2's lightstyles itself */
}
struct model_s *R_RegisterModel(const char *name)
{
	return Q2FXSYS_RegisterModel(name);
}
struct image_s *Draw_FindPic(const char *name)
{	/* only used to precache the HUD's pics */
	Q2FXSYS_PrecachePic(name);
	return NULL;
}
struct sfx_s *S_RegisterSound(char *name)
{
	return Q2FXSYS_RegisterSound(name);
}
void S_StartSound(vec3_t origin, int entnum, int entchannel, struct sfx_s *sfx, float fvol, float attenuation, float timeofs)
{
	if (sfx)
		Q2FXSYS_StartSound(origin, entnum, entchannel, sfx, fvol, attenuation, timeofs);
}

/* --- network --- */

int MSG_ReadByte(sizebuf_t *sb)		{ return Q2FXSYS_ReadByte(); }
int MSG_ReadShort(sizebuf_t *sb)	{ return Q2FXSYS_ReadShort(); }
int MSG_ReadLong(sizebuf_t *sb)		{ return Q2FXSYS_ReadLong(); }
void MSG_ReadPos(sizebuf_t *sb, vec3_t pos)	{ Q2FXSYS_ReadPos(pos); }
void MSG_ReadDir(sizebuf_t *sb, vec3_t dir)	{ Q2FXSYS_ReadDir(dir); }

/* --- messages --- */

void Com_Printf(const char *fmt, ...)
{
	char text[1024];
	va_list argptr;
	va_start(argptr, fmt);
	vsnprintf(text, sizeof(text), fmt, argptr);
	va_end(argptr);
	Q2FXSYS_Print(0, text);
}
void Com_DPrintf(const char *fmt, ...)
{
	char text[1024];
	va_list argptr;
	va_start(argptr, fmt);
	vsnprintf(text, sizeof(text), fmt, argptr);
	va_end(argptr);
	Q2FXSYS_Print(1, text);
}
void Com_Error(int code, const char *fmt, ...)
{
	char text[1024];
	va_list argptr;
	va_start(argptr, fmt);
	vsnprintf(text, sizeof(text), fmt, argptr);
	va_end(argptr);
	Q2FXSYS_Error(text);
}

/* --- client entities: what the trails remember between frames, and where FTE says an entity is --- */

static centity_t *q2fx_entities;
static int q2fx_numentities;

centity_t *CL_AllocEntity(int entnum)
{
	centity_t *e;
	if (entnum < 0 || entnum > MAX_CL_ENTNUM)
		return NULL;
	if (entnum >= q2fx_numentities)
	{
		int n = q2fx_numentities ? q2fx_numentities : 1024;
		centity_t *grown;
		while (n <= entnum)
			n *= 2;
		grown = realloc(q2fx_entities, sizeof(*q2fx_entities) * n);
		if (!grown)
			return NULL;
		memset(grown + q2fx_numentities, 0, sizeof(*grown) * (n - q2fx_numentities));
		q2fx_entities = grown;
		q2fx_numentities = n;
	}
	e = &q2fx_entities[entnum];
	e->current.number = entnum;
	Q2FXSYS_GetEntity(entnum, e->current.origin, e->current.angles);	/* muzzle flashes read these */
	return e;
}

/* --- what FTE calls --- */

void Q2FX_ClearState(void)
{
	CL_ClearEffects();	/* particles, dlights, lightstyles */
	CL_ClearTEnts();	/* explosions, beams, lasers, sustains */
	if (q2fx_entities)
		memset(q2fx_entities, 0, sizeof(*q2fx_entities) * q2fx_numentities);
	num_power_sounds = 0;
}

void Q2FX_RegisterMedia(void)
{
	CL_RegisterTEntSounds();
	CL_RegisterTEntModels();
}

void Q2FX_SetFrame(int time_ms, int servertime_ms, int serverframe, int playernum, float lerpfrac,
		const float *vieworg, const float *forward, const float *right, const float *up,
		const float *gunoffset, const float *oldgunoffset, int handvalue,
		float laseralpha, int limitsparksounds, int footsteps)
{
	q2fx_frame_t *old;

	if (time_ms - cl.time > 0)
		cls.rframetime = (time_ms - cl.time) * 0.001f;
	else
		cls.rframetime = 0;
	cl.time = time_ms;
	cls.realtime = time_ms;
	cl.lerpfrac = lerpfrac;
	cl.playernum = playernum;
	VectorCopy(vieworg, cl.refdef.vieworg);
	VectorCopy(forward, cl.v_forward);
	VectorCopy(right, cl.v_right);
	VectorCopy(up, cl.v_up);

	/* the frame and the one before, for the gun offset of the player's own beams */
	cl.frame.valid = true;
	cl.frame.serverframe = serverframe;
	cl.frame.servertime = servertime_ms;
	VectorCopy(gunoffset, cl.frame.playerstate.gunoffset);
	old = &cl.frames[(serverframe - 1) & UPDATE_MASK];
	*old = cl.frame;
	old->serverframe = serverframe - 1;
	VectorCopy(oldgunoffset, old->playerstate.gunoffset);

	hand->value = handvalue;
	cl_laseralpha->value = laseralpha;
	cl_limitsparksounds->value = limitsparksounds;
	cl_footsteps->value = footsteps;
}

void Q2FX_ParseTEnt(void)			{ CL_ParseTEnt(); }
void Q2FX_ParseMuzzleFlash(void)	{ CL_AddMuzzleFlash(); }
void Q2FX_ParseMuzzleFlash2(void)	{ CL_AddMuzzleFlash2(); }

void Q2FX_EntityEvent(int number, int event, const float *origin)
{
	entity_state_t es;
	memset(&es, 0, sizeof(es));
	es.number = number;
	es.event = event;
	VectorCopy(origin, es.origin);
	CL_EntityEvent(&es);
}
void Q2FX_TeleporterParticles(const float *origin)
{
	entity_state_t es;
	memset(&es, 0, sizeof(es));
	VectorCopy(origin, es.origin);
	CL_TeleporterParticles(&es);
}

void Q2FX_EntityReset(int entnum, const float *origin)
{	/* yquake2's CL_DeltaEntity, for an entity that wasn't in the last frame */
	centity_t *cent = CL_AllocEntity(entnum);
	if (!cent)
		return;
	cent->trailcount = 1024;	/* for diminishing rocket / grenade trails */
	VectorCopy(origin, cent->lerp_origin);
}

void Q2FX_EntityEffects(int entnum, int effects, int frame, const float *origin)
{	/* yquake2's CL_AddPacketEntities, "add automatic particle trails" */
	entity_t ent;
	centity_t *cent = CL_AllocEntity(entnum);
	int i;

	if (!cent)
		return;
	memset(&ent, 0, sizeof(ent));
	VectorCopy(origin, ent.origin);

	if ((effects & ~EF_ROTATE))
	{
		if (effects & EF_ROCKET)
		{
			CL_RocketTrail(cent->lerp_origin, ent.origin, cent);

			if (cl_r1q2_lightstyle->value)
			{
				V_AddLight(ent.origin, 200, 1, 0.23f, 0);
			}
			else
			{
				V_AddLight(ent.origin, 200, 1, 1, 0);
			}
		}

		/* Do not reorder EF_BLASTER and EF_HYPERBLASTER.
		   EF_BLASTER | EF_TRACKER is a special case for
		   EF_BLASTER2 */
		else if (effects & EF_BLASTER)
		{
			if (effects & EF_TRACKER)
			{
				CL_BlasterTrail2(cent->lerp_origin, ent.origin);
				V_AddLight(ent.origin, 200, 0, 1, 0);
			}
			else
			{
				CL_BlasterTrail(cent->lerp_origin, ent.origin);
				V_AddLight(ent.origin, 200, 1, 1, 0);
			}
		}
		else if (effects & EF_HYPERBLASTER)
		{
			if (effects & EF_TRACKER)
			{
				V_AddLight(ent.origin, 200, 0, 1, 0);
			}
			else
			{
				V_AddLight(ent.origin, 200, 1, 1, 0);
			}
		}
		else if (effects & EF_GIB)
		{
			CL_DiminishingTrail(cent->lerp_origin, ent.origin,
					cent, effects);
		}
		else if (effects & EF_GRENADE)
		{
			CL_DiminishingTrail(cent->lerp_origin, ent.origin,
					cent, effects);
		}
		else if (effects & EF_FLIES)
		{
			CL_FlyEffect(cent, ent.origin);
		}
		else if (effects & EF_BFG)
		{
			static int bfg_lightramp[6] = {300, 400, 600, 300, 150, 75};

			if (effects & EF_ANIM_ALLFAST)
			{
				CL_BfgParticles(&ent);
				i = 200;
			}
			else
			{
				i = bfg_lightramp[(unsigned)frame % 6];
			}

			V_AddLight(ent.origin, i, 0, 1, 0);
		}
		else if (effects & EF_TRAP)
		{
			ent.origin[2] += 32;
			CL_TrapParticles(&ent);
			i = (randk() % 100) + 100;
			V_AddLight(ent.origin, i, 1, 0.8f, 0.1f);
		}
		else if (effects & EF_FLAG1)
		{
			CL_FlagTrail(cent->lerp_origin, ent.origin, 242);
			V_AddLight(ent.origin, 225, 1, 0.1f, 0.1f);
		}
		else if (effects & EF_FLAG2)
		{
			CL_FlagTrail(cent->lerp_origin, ent.origin, 115);
			V_AddLight(ent.origin, 225, 0.1f, 0.1f, 1);
		}
		else if (effects & EF_TAGTRAIL)
		{
			CL_TagTrail(cent->lerp_origin, ent.origin, 220);
			V_AddLight(ent.origin, 225, 1.0, 1.0, 0.0);
		}
		else if (effects & EF_TRACKERTRAIL)
		{
			if (effects & EF_TRACKER)
			{
				float intensity;

				intensity = 50 + (500 * ((float)sin(cl.time / 500.0f) + 1.0f));
				V_AddLight(ent.origin, intensity, -1.0, -1.0, -1.0);
			}
			else
			{
				CL_Tracker_Shell(cent->lerp_origin);
				V_AddLight(ent.origin, 155, -1.0, -1.0, -1.0);
			}
		}
		else if (effects & EF_TRACKER)
		{
			CL_TrackerTrail(cent->lerp_origin, ent.origin, 0);
			V_AddLight(ent.origin, 200, -1, -1, -1);
		}
		else if (effects & EF_IONRIPPER)
		{
			CL_IonripperTrail(cent->lerp_origin, ent.origin);
			V_AddLight(ent.origin, 100, 1, 0.5, 0.5);
		}
		else if (effects & EF_BLUEHYPERBLASTER)
		{
			V_AddLight(ent.origin, 200, 0, 0, 1);
		}
		else if (effects & EF_PLASMA)
		{
			if (effects & EF_ANIM_ALLFAST)
			{
				CL_BlasterTrail(cent->lerp_origin, ent.origin);
			}

			V_AddLight(ent.origin, 130, 1, 0.5, 0.5);
		}
	}

	VectorCopy(ent.origin, cent->lerp_origin);
}

void Q2FX_AddEntities(void)
{	/* yquake2's CL_Frame runs the dlights, CL_AddEntities adds the rest after the packet entities */
	CL_RunDLights();
	CL_AddTEnts();
	CL_AddParticles();
	CL_AddDLights();
	num_power_sounds = 0;	/* yquake2 resets it every frame */
}
