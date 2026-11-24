/*
Daikatana's entities on FTE's client: its client draws them itself (cl_ents.cpp's CL_AddPacketEntities, its view
weapon, its light flares) from what FTE's client has of them (Quake II's entity state, which carries its renderfx
and effects as they are) and what its server sends past that (svcdk_fte_entstate, dk_fteproto.h); it animates them
itself, from their frameInfo, as its client did.

GPL v2 or later.
*/
#include "cldk_local.h"
#include "../fte/dk_fteproto.h"

entity_state_t	cl_parse_entities[MAX_PARSE_ENTITIES];

static cvar_t	cv_showflares = {(char *)"cl_showflares"}, cv_flarecount = {(char *)"cl_flarecount"};
static cvar_t	cv_antiskate = {(char *)"cl_antiskate"}, cv_showsync = {(char *)"showsync"}, cv_showclamp = {(char *)"cl_showclamp"};
cvar_t			*cl_showflares = &cv_showflares;
cvar_t			*cl_flarecount = &cv_flarecount;
cvar_t			*cl_antiskate = &cv_antiskate;
cvar_t			*showsync = &cv_showsync;
cvar_t			*cl_showclamp = &cv_showclamp;

int				cldk_pmflags, cldk_pmtype;	//its pmove's (svcdk_fte_stats)

//cl_ents.cpp's
extern int		backframe[MAX_PARSE_ENTITIES];
extern int		frontframe[MAX_PARSE_ENTITIES];
void	CLDK_AddPacketEntities(void);
void	CL_AddLightFlares(frame_t *frame);
void	CL_AddWeaponModels(void);
void	CLDK_SetNextFrame(int entnum, int frame);

void	CL_InitClientSideParticleVolume(entity_state_t *ent);	//cl_pv.cpp's

//each entity's state as its client keeps it: the server's, and its animation's
static entity_state_t	dkstate[MAX_EDICTS];
static int				dkfteframe[MAX_EDICTS];	//the frame Quake II's state had last
static bool				dkpvolume[MAX_EDICTS];	//its particle volume's been set up (CL_ParseDelta did it as the bounds came)

void CLDK_EntitiesReset(void)
{
	memset(dkstate, 0, sizeof(dkstate));
	memset(dkfteframe, 0, sizeof(dkfteframe));
	memset(dkpvolume, 0, sizeof(dkpvolume));
	memset(cl_parse_entities, 0, sizeof(cl_parse_entities));
	memset(backframe, 0, sizeof(backframe));
	memset(frontframe, 0, sizeof(frontframe));
	cldk_pmflags = cldk_pmtype = 0;
}

static void ReadVec(CVector &v)
{
	v.x = MSG_ReadFloat(&net_message);
	v.y = MSG_ReadFloat(&net_message);
	v.z = MSG_ReadFloat(&net_message);
}

//svcdk_fte_entstate
void CLDK_ParseEntState(void)
{
	int num = MSG_ReadShort(&net_message);
	int bits = MSG_ReadShort(&net_message) & 0xffff;
	entity_state_t tmp, *s;
	int k;
	if (num <= 0 || num >= MAX_EDICTS)
		s = &tmp;	//(read, for what's after it)
	else
		s = &dkstate[num];
	if (bits & DKX_RESET)
		memset(s, 0, sizeof(*s));
	if (bits & DKX_MODEL2)		s->modelindex2 = MSG_ReadLong(&net_message);
	if (bits & DKX_MODEL3)		s->modelindex3 = MSG_ReadLong(&net_message);
	if (bits & DKX_MODEL4)		s->modelindex4 = MSG_ReadLong(&net_message);
	if (bits & DKX_EFFECTS2)	s->effects2 = MSG_ReadLong(&net_message);
	if (bits & DKX_FLAGS)		s->flags = MSG_ReadLong(&net_message);
	if (bits & DKX_ALPHA)		s->alpha = MSG_ReadFloat(&net_message);
	if (bits & DKX_COLOR)		ReadVec(s->color);
	if (bits & DKX_SCALE)		ReadVec(s->render_scale);
	if (bits & DKX_MINSMAXS)
	{
		ReadVec(s->mins);
		ReadVec(s->maxs);
	}
	if (bits & DKX_ANGLEDELTA)	ReadVec(s->angle_delta);
	if (bits & DKX_ORIGINHACK)	ReadVec(s->origin_hack);
	if (bits & DKX_FRAMEINFO)
	{
		frameInfo_t *fi = &s->frameInfo;
		fi->frameFlags = MSG_ReadShort(&net_message);
		fi->startFrame = MSG_ReadShort(&net_message);
		fi->endFrame = MSG_ReadShort(&net_message);
		fi->startLoop = MSG_ReadShort(&net_message);
		fi->endLoop = MSG_ReadShort(&net_message);
		fi->loopCount = MSG_ReadShort(&net_message);
		fi->sound1Frame = MSG_ReadShort(&net_message);
		fi->sound2Frame = MSG_ReadShort(&net_message);
		fi->sound1Index = MSG_ReadByte(&net_message);
		fi->sound2Index = MSG_ReadByte(&net_message);
		fi->frameInc = (char)MSG_ReadByte(&net_message);
		fi->modelAnimSpeed = MSG_ReadFloat(&net_message);
		//as its client set the frame rate from the flags
		if (fi->frameFlags & FRAME_FPS10)
			fi->frameTime = FRAMETIME_FPS10;
		else if (fi->frameFlags & FRAME_FPS30)
			fi->frameTime = FRAMETIME_FPS30;
		else
			fi->frameTime = FRAMETIME_FPS20;
	}
	if (bits & DKX_SYNC)
	{
		s->frame = MSG_ReadShort(&net_message);
		if (s != &tmp)
			CLDK_SetNextFrame(num, s->frame);
	}
	if (bits & DKX_CLUSTERS)
	{
		s->numClusters = MSG_ReadByte(&net_message);
		for (k = 0; k < s->numClusters; k++)
		{
			int idx = MSG_ReadByte(&net_message);
			CVector a;
			ReadVec(a);
			if (k < MAX_MESH_CLUSTERS)
			{
				s->cDef[k].clusterIdx = idx;
				s->cDef[k].angles = a;
			}
		}
		if (s->numClusters > MAX_MESH_CLUSTERS)
			s->numClusters = MAX_MESH_CLUSTERS;
	}
	if (bits & DKX_SOUND)
	{
		s->snd_flags = MSG_ReadByte(&net_message);
		s->volume = MSG_ReadFloat(&net_message);
		s->dist_min = MSG_ReadFloat(&net_message);
		s->dist_max = MSG_ReadFloat(&net_message);
	}
}

//its entity's looping sound (its audio's autosounds): its volume, full to dist_min, nothing past dist_max (FTE's
//attenuation fades from the entity to 1000/attenuation, so to dist_max)
extern "C" void CLDK_LoopSoundParams(int entnum, float *volume, float *attenuation)
{
	const entity_state_t *s;
	float dmax;
	if (entnum <= 0 || entnum >= MAX_EDICTS)
		return;
	s = &dkstate[entnum];
	*volume = s->volume?s->volume:1.0f;
	dmax = s->dist_max?s->dist_max:ATTN_NORM_MAX;
	*attenuation = (dmax > 0)?1000.0f/dmax:0;
	if (*attenuation > 4)
		*attenuation = 4;
}

static void SetState(entity_state_t *s, const cldk_estate_t *e)
{
	s->origin.Set(e->origin[0], e->origin[1], e->origin[2]);
	s->angles.Set(e->angles[0], e->angles[1], e->angles[2]);
	s->old_origin.Set(e->old_origin[0], e->old_origin[1], e->old_origin[2]);
	s->modelindex = e->modelindex;
	s->skinnum = e->skinnum;
	s->effects = e->effects;
	s->renderfx = e->renderfx;
	s->sound = e->sound;
	s->event = e->event;
}

//its client's frame (cl.frame, cl_entities, cl_parse_entities) from FTE's
static void CLDK_EntitiesFrame(void)
{
	int i, n = CLDKF_NumFrameEntities();
	int serverframe = cl.frame.serverframe;
	float vel[3];

	cl.frame.parse_entities = 0;
	cl.frame.num_entities = 0;
	cl.frame.playerstate.pmove.pm_flags = cldk_pmflags;
	cl.frame.playerstate.pmove.pm_type = (pmtype_t)cldk_pmtype;
	CLDKF_PlayerVelocity(vel);
	for (i = 0; i < 3; i++)
		cl.frame.playerstate.pmove.velocity[i] = (short)(vel[i]*8);

	for (i = 0; i < n && cl.frame.num_entities < MAX_PARSE_ENTITIES; i++)
	{
		cldk_estate_t cur, prev;
		int isnew, num = CLDKF_FrameEntity(i, &cur, &prev, &isnew);
		entity_state_t *s;
		centity_t *cent;
		if (num <= 0 || num >= MAX_EDICTS)
			continue;
		s = &dkstate[num];
		cent = &cl_entities[num];
		s->number = num;
		SetState(s, &cur);
		if ((s->renderfx & RF_PARTICLEVOLUME) && !dkpvolume[num] && s->effects2 && s->maxs.z > s->mins.z)
		{	//rain, snow, drips in its bounds (cl_pv.cpp)
			dkpvolume[num] = true;
			CL_InitClientSideParticleVolume(s);
		}

		//the frame: its animation's, synced to the server's as it was (as it comes into view, as the server
		//changes the frame of what isn't animated)
		if (cent->serverframe != serverframe)
		{
			if (isnew)
			{
				s->frame = cur.frame;
				CLDK_SetNextFrame(num, cur.frame);
				backframe[num] = frontframe[num] = cur.frame;
				s->frameInfo.next_frameTime = 0;
			}
			else if (cur.frame != dkfteframe[num] && (!s->frameInfo.frameFlags || (s->frameInfo.frameFlags & FRAME_STATIC)))
				CLDK_SetNextFrame(num, cur.frame);
			dkfteframe[num] = cur.frame;

			if (isnew)
			{	//as its CL_DeltaEntity had it
				cent->trailcount = 1024;
				cent->prev = *s;
				cent->prev.origin.Set(prev.origin[0], prev.origin[1], prev.origin[2]);
				cent->lerp_origin = cent->prev.origin;
			}
			else
				cent->prev = cent->current;
			cent->serverframe = serverframe;
		}
		cent->current = *s;
		cent->prev.origin.Set(prev.origin[0], prev.origin[1], prev.origin[2]);
		cent->prev.angles.Set(prev.angles[0], prev.angles[1], prev.angles[2]);

		cl_parse_entities[cl.frame.num_entities++] = *s;
	}
}

void CLDK_AddEntities(void)
{
	int i;
	cv_showflares.value = CLDKF_CvarValue("cl_showflares", "1");
	cv_flarecount.value = CLDKF_CvarValue("cl_flarecount", "0");
	cv_antiskate.value = CLDKF_CvarValue("cl_antiskate", "1");
	cv_showsync.value = CLDKF_CvarValue("showsync", "0");

	CLDK_EntitiesFrame();
	if (CLDKF_CvarValue("cl_dkshowents", "0"))
	{	//debugging: the frame's entities as its client has them, once
		char line[256];
		CLDKF_CvarSet("cl_dkshowents", "0");
		snprintf(line, sizeof(line), "%i entities, player %i, pmflags %x, rdflags %x, view %.0f %.0f %.0f\n", cl.frame.num_entities,
			cl.playernum, cldk_pmflags, CLDKF_RDFlags(), cl.refdef.vieworg.x, cl.refdef.vieworg.y, cl.refdef.vieworg.z);
		CLDKF_Print(line);
		for (i = 0; i < cl.frame.num_entities; i++)
		{
			entity_state_t *s = &cl_parse_entities[i];
			snprintf(line, sizeof(line), "%4i (%.0f %.0f %.0f) m%i:%s f%i rf %08x ef %08x ef2 %08x m2 %x m3 %x ff %x %i-%i a %.2f sc %.2f\n",
				s->number, s->origin.x, s->origin.y, s->origin.z, s->modelindex, CLDKF_ModelName(s->modelindex), s->frame, s->renderfx, s->effects, s->effects2,
				s->modelindex2, s->modelindex3, s->frameInfo.frameFlags, s->frameInfo.startFrame, s->frameInfo.endFrame,
				s->alpha, s->render_scale.x);
			CLDKF_Print(line);
		}
	}
	CLDK_AddPacketEntities();
	for (i = 0; i < cl.frame.num_entities; i++)
	{	//what its animation's made of their states, for the next
		entity_state_t *s = &cl_parse_entities[i];
		dkstate[s->number] = *s;
	}
	CL_AddLightFlares(&cl.frame);
	CL_AddWeaponModels();
}
