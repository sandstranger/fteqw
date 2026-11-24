/*
What yquake2's client effects (cl_effects.c, cl_tempentities.c, cl_particles.c, cl_lights.c, flash.c --
copied from yquake2 with only their #includes changed) expect of the client around them: the bits of
yquake2's client.h and ref.h they use, and a small client state that FTE fills in (q2fx_bridge.c).

None of FTE's headers come in here; see ../q2fx_sys.h for how this code reaches FTE.
*/
#ifndef Q2FX_H
#define Q2FX_H

#include "q2fx_rename.h"	/* q2fx_ names for the globals FTE has too */
#include <limits.h>
#include "q2shared.h"
#include "../q2fx_sys.h"

/* --- yquake2's ref.h --- */
typedef struct entity_s
{
	struct model_s *model;	/* opaque */
	float angles[3];
	float origin[3];	/* also used as RF_BEAM's "from" */
	int frame;			/* also used as RF_BEAM's diameter */
	float oldorigin[3];	/* also used as RF_BEAM's "to" */
	int oldframe;
	float backlerp;		/* 0.0 = current, 1.0 = old */
	int skinnum;		/* also used as RF_BEAM's palette index */
	int lightstyle;
	float alpha;		/* ignore if RF_TRANSLUCENT isn't set */
	struct image_s *skin;
	int flags;
} entity_t;

void V_AddEntity(entity_t *ent);
void V_AddParticle(vec3_t org, unsigned int color, float alpha);
void V_AddLight(vec3_t org, float intensity, float r, float g, float b);
void V_AddLightStyle(int style, float r, float g, float b);
struct model_s *R_RegisterModel(const char *name);
struct image_s *Draw_FindPic(const char *name);

/* --- yquake2's client.h --- */
#define MAX_CL_ENTNUM SHRT_MAX
#define MAX_DLIGHTS 32
#define MAX_SUSTAINS 32
#define MAX_PARTICLES 4096
#define PARTICLE_GRAVITY 40
#define BLASTER_PARTICLE_COLOR 0xe0
#define INSTANT_PARTICLE -10000.0
#define UPDATE_BACKUP 16
#define UPDATE_MASK (UPDATE_BACKUP-1)
#define NUMVERTEXNORMALS 162
extern vec3_t bytedirs[NUMVERTEXNORMALS];	/* FTE's (common.c), the same anorms table */
extern int num_power_sounds;				/* power shield/screen sounds this frame (cl_limitsparksounds) */

void Com_Error(int code, const char *fmt, ...);
void Com_DPrintf(const char *fmt, ...);

typedef struct
{
	entity_state_t baseline;
	entity_state_t current;
	entity_state_t prev;
	int serverframe_created;
	int serverframe;
	int trailcount;		/* for diminishing grenade trails */
	vec3_t lerp_origin;	/* for trails (variable hz) */
	int fly_stoptime;
} centity_t;

typedef struct
{
	int key;
	vec3_t color;
	vec3_t origin;
	float radius;
	float die;
	float decay;
	float minlight;
} cdlight_t;

typedef struct cl_sustain
{
	int id;
	int type;
	int endtime;
	int nextthink;
	int thinkinterval;
	vec3_t org;
	vec3_t dir;
	int color;
	int count;
	int magnitude;
	void (*think)(struct cl_sustain *self);
} cl_sustain_t;

typedef struct particle_s
{
	struct particle_s *next;
	float time;
	vec3_t org;
	vec3_t vel;
	vec3_t accel;
	float color;
	float colorvel;
	float alpha;
	float alphavel;
} cparticle_t;

/* the client state the effects read, as yquake2 names it: FTE fills it in (Q2FX_SetFrame) */
typedef struct
{
	qboolean valid;
	int serverframe;
	int servertime;
	player_state_t playerstate;
} q2fx_frame_t;
typedef struct
{
	int time;		/* milliseconds */
	float lerpfrac;
	int playernum;
	vec3_t v_forward, v_right, v_up;
	struct { vec3_t vieworg; } refdef;
	q2fx_frame_t frame;
	q2fx_frame_t frames[UPDATE_BACKUP];
	char configstrings[MAX_CONFIGSTRINGS][MAX_QPATH];	/* only for cl_lights.c's lightstyles, which FTE does itself */
} q2fx_client_t;
typedef q2fx_frame_t frame_t;
typedef struct
{
	float rframetime;	/* seconds */
	float realtime;
} q2fx_static_t;
extern q2fx_client_t q2fx_cl;
extern q2fx_static_t q2fx_cls;
#define cl q2fx_cl
#define cls q2fx_cls

typedef struct sizebuf_s sizebuf_t;
extern sizebuf_t net_message;
int MSG_ReadByte(sizebuf_t *sb);
int MSG_ReadShort(sizebuf_t *sb);
int MSG_ReadLong(sizebuf_t *sb);
void MSG_ReadPos(sizebuf_t *sb, vec3_t pos);
void MSG_ReadDir(sizebuf_t *sb, vec3_t dir);

struct sfx_s *S_RegisterSound(char *name);
void S_StartSound(vec3_t origin, int entnum, int entchannel, struct sfx_s *sfx, float fvol, float attenuation, float timeofs);

extern cvar_t *hand, *cl_laseralpha, *cl_limitsparksounds, *cl_footsteps, *cl_r1q2_lightstyle;

extern cdlight_t cl_dlights[MAX_DLIGHTS];
centity_t *CL_AllocEntity(int entnum);

/* effects (cl_effects.c, cl_particles.c, cl_tempentities.c, cl_lights.c) */
void CL_ClearEffects(void);
void CL_ClearTEnts(void);
void CL_ClearParticles(void);
void CL_ClearDlights(void);
void CL_RegisterTEntModels(void);
void CL_RegisterTEntSounds(void);
void CL_ParseTEnt(void);
void CL_AddMuzzleFlash(void);
void CL_AddMuzzleFlash2(void);
void CL_AddTEnts(void);
void CL_AddParticles(void);
void CL_AddDLights(void);
void CL_RunDLights(void);
void CL_EntityEvent(entity_state_t *ent);
void CL_TeleporterParticles(entity_state_t *ent);
cdlight_t *CL_AllocDlight(int key);
void CL_ParticleEffect(vec3_t org, vec3_t dir, int color, int count);
void CL_ParticleEffect2(vec3_t org, vec3_t dir, int color, int count);
void CL_ParticleEffect3(vec3_t org, vec3_t dir, int color, int count);
void CL_GenericParticleEffect(vec3_t org, vec3_t dir, int color, int count, int numcolors, int dirspread, float alphavel);
void CL_BlasterTrail(vec3_t start, vec3_t end);
void CL_QuadTrail(vec3_t start, vec3_t end);
void CL_RailTrail(vec3_t start, vec3_t end);
void CL_BubbleTrail(vec3_t start, vec3_t end);
void CL_FlagTrail(vec3_t start, vec3_t end, int color);
void CL_IonripperTrail(vec3_t start, vec3_t end);
void CL_BlasterParticles2(vec3_t org, vec3_t dir, unsigned int color);
void CL_BlasterTrail2(vec3_t start, vec3_t end);
void CL_DebugTrail(vec3_t start, vec3_t end);
void CL_SmokeTrail(vec3_t start, vec3_t end, int colorStart, int colorRun, int spacing);
void CL_Flashlight(int ent, vec3_t pos);
void CL_ForceWall(vec3_t start, vec3_t end, int color);
void CL_BubbleTrail2(vec3_t start, vec3_t end, int dist);
void CL_Heatbeam(vec3_t start, vec3_t end);
void CL_ParticleSteamEffect(vec3_t org, vec3_t dir, int color, int count, int magnitude);
void CL_ParticleSteamEffect2(cl_sustain_t *self);
void CL_TrackerTrail(vec3_t start, vec3_t end, int particleColor);
void CL_Tracker_Explode(vec3_t origin);
void CL_TagTrail(vec3_t start, vec3_t end, int color);
void CL_ColorFlash(vec3_t pos, int ent, float intensity, float r, float g, float b);
void CL_Tracker_Shell(vec3_t origin);
void CL_MonsterPlasma_Shell(vec3_t origin);
void CL_ColorExplosionParticles(vec3_t org, int color, int run);
void CL_ParticleSmokeEffect(vec3_t org, vec3_t dir, int color, int count, int magnitude);
void CL_Widowbeamout(cl_sustain_t *self);
void CL_Nukeblast(cl_sustain_t *self);
void CL_WidowSplash(vec3_t org);
void CL_DiminishingTrail(vec3_t start, vec3_t end, centity_t *old, int flags);
void CL_RocketTrail(vec3_t start, vec3_t end, centity_t *old);
void CL_FlyEffect(centity_t *ent, vec3_t origin);
void CL_BfgParticles(entity_t *ent);
void CL_TrapParticles(entity_t *ent);
void CL_SmokeAndFlash(vec3_t origin);
void CL_BigTeleportParticles(vec3_t org);
struct sfx_s *CL_RandomFootstepSfx(void);
struct model_s *CL_PowerScreenModel(void);

#endif
