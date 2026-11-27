#include "quakedef.h"
#include "particles.h"

#ifdef Q2CLIENT
#include "shader.h"
#include "com_bih.h"	//q2mapsurface_t: a surface's full texture name (for the rerelease's step materials)

//q2pro's framerate scaling runs the entire server at a higher rate (including gamecode).
//this allows lower latency on player movements without breaking things too much
//animations are still assumed to run at 10fps, so those need some fixup too
//events are keyed to not renew twice within the same 10fps window, unless the entity was actually updated.

extern cvar_t r_drawviewmodel;

extern cvar_t cl_nopred;
typedef enum
{
	Q2EV_NONE,
	Q2EV_ITEM_RESPAWN,
	Q2EV_FOOTSTEP,
	Q2EV_FALLSHORT,
	Q2EV_FALL,
	Q2EV_FALLFAR,
	Q2EV_PLAYER_TELEPORT,
	Q2EV_OTHER_TELEPORT,
	Q2EXEV_OTHER_FOOTSTEP,
	Q2EXEV_LADDER_STEP,
} q2entity_event_t;

#define Q2PMF_NO_PREDICTION	64	// temporarily disables prediction (used for grappling hook)

float LerpAngle (float a2, float a1, float frac)
{
	if (a1 - a2 > 180)
		a1 -= 360;
	if (a1 - a2 < -180)
		a1 += 360;
	return a2 + frac * (a1 - a2);
}

// entity_state_t->effects
// Effects are things handled on the client side (lights, particles, frame animations)
// that happen constantly on the given entity.
// An entity that has effects will be sent to the client
// even if it has a zero index model.
#define	Q2EF_ROTATE				0x00000001		// rotate (bonus items)
#define	Q2EF_GIB				0x00000002		// leave a trail
#define	Q2EF_BLASTER			0x00000008		// redlight + trail
#define	Q2EF_ROCKET				0x00000010		// redlight + trail
#define	Q2EF_GRENADE			0x00000020
#define	Q2EF_HYPERBLASTER		0x00000040
#define	Q2EF_BFG				0x00000080
#define Q2EF_COLOR_SHELL		0x00000100
#define Q2EF_POWERSCREEN		0x00000200
#define	Q2EF_ANIM01				0x00000400		// automatically cycle between frames 0 and 1 at 2 hz
#define	Q2EF_ANIM23				0x00000800		// automatically cycle between frames 2 and 3 at 2 hz
#define Q2EF_ANIM_ALL			0x00001000		// automatically cycle through all frames at 2hz
#define Q2EF_ANIM_ALLFAST		0x00002000		// automatically cycle through all frames at 10hz
#define	Q2EF_FLIES				0x00004000
#define	Q2EF_QUAD				0x00008000
#define	Q2EF_PENT				0x00010000
#define	Q2EF_TELEPORTER			0x00020000		// particle fountain
#define Q2EF_FLAG1				0x00040000
#define Q2EF_FLAG2				0x00080000
// RAFAEL
#define Q2EF_IONRIPPER			0x00100000
#define Q2EF_GREENGIB			0x00200000
#define	Q2EF_BLUEHYPERBLASTER	0x00400000
#define Q2EF_SPINNINGLIGHTS		0x00800000
#define Q2EF_PLASMA				0x01000000
#define Q2EF_TRAP				0x02000000

//ROGUE
#define Q2EF_TRACKER			0x04000000
#define	Q2EF_DOUBLE				0x08000000
#define	Q2EF_SPHERETRANS		0x10000000
#define Q2EF_TAGTRAIL			0x20000000
#define Q2EF_HALF_DAMAGE		0x40000000
#define Q2EF_TRACKERTRAIL		0x80000000
//ROGUE




#define Q2MAX_STATS	32

//the rerelease's effects 32-63 (entity_state_t's u.q2.effects2)
#define Q2EXEF2_DUALFIRE			(1u<<0)		//EF_DUALFIRE: a light green shell
#define Q2EXEF2_HOLOGRAM			(1u<<1)		//EF_HOLOGRAM: the N64 hologram's particles
#define Q2EXEF2_FLASHLIGHT			(1u<<2)		//EF_FLASHLIGHT: lights where the player looks
#define Q2EXEF2_BARREL_EXPLODING	(1u<<3)		//EF_BARREL_EXPLODING: smoke
#define Q2EXEF2_TELEPORTER2			(1u<<4)		//EF_TELEPORTER2: the N64 teleporter's particles
#define Q2EXEF2_GRENADE_LIGHT		(1u<<5)		//EF_GRENADE_LIGHT: a yellow light
#define Q2EXRF_STAIRSTEP			(1u<<26)	//RF_STAIR_STEP: a monster stepped up (or down) a stair, smooth it out

typedef struct q2centity_s
{
	entity_state_t	baseline;		// delta from this if not from a previous frame
	entity_state_t	current;
	entity_state_t	prev;			// will always be valid, but might just be a copy of current

	int			serverframe;		// if not current, this ent isn't in the frame
	int			serverframe_created;	// the frame it (re)appeared in: lasers lerp from the second one on (yquake2)

	trailkey_t trailstate;
	trailkey_t emitstate;
//	float		trailcount;			// for diminishing grenade trails
	vec3_t		lerp_origin;		// for trails (variable hz)
	int			oldframe;
	float		oldframetime;
	float		stair_height;		// the rerelease's RF_STAIR_STEP: the step still to smooth out
	double		stair_time;
	float		flashlightfrac;		// EF_FLASHLIGHT: how far its light is, smoothed
	float		flarefrac;			// RF_FLARE: how much of it shows (its origin seen from the eye, faded in and out)
	double		flaretime;			// RF_FLARE: cl.time flarefrac is from

	float		fly_stoptime;
} q2centity_t;

static void Q2S_StartSound(vec3_t origin, int entnum, int entchannel, sfx_t *sfx, float fvol, float attenuation, float delay)
{
	S_StartSound(entnum, entchannel, sfx, origin, NULL, fvol, attenuation, -delay, 0, 0);
}
sfx_t *S_PrecacheSexedSound(int entnum, const char *soundname)
{
	if (soundname[0] == '*')
	{	//a 'sexed' sound
		if (entnum > 0 && entnum <= MAX_CLIENTS)
		{
			char *model = InfoBuf_ValueForKey(&cl.players[entnum-1].userinfo, "skin");
			char *skin;
			skin = strchr(model, '/');
			if (skin)
				*skin = '\0';
			if (*model)
			{
				sfx_t *sfx = S_PrecacheSound(va("players/%s/%s", model, soundname+1));
				if (sfx && sfx->loadstate != SLS_FAILED)	//warning: the sound might still be loading (and later fail).
					return sfx;
			}
		}
		return S_PrecacheSound(va("players/male/%s", soundname+1));
	}
	return S_PrecacheSound(soundname);
}


void CLQ2_EntityEvent(entity_state_t *es)
{
	switch (es->u.q2.event)
	{
	case Q2EV_NONE:
	case Q2EV_OTHER_TELEPORT:	//inihibits interpolation. not an event in itself.
		break;
	case Q2EV_ITEM_RESPAWN:
		pe->RunParticleEffectState(es->origin, NULL, 1, pt_q2[Q2PT_RESPAWN], NULL);
		break;
	case Q2EV_PLAYER_TELEPORT:
		pe->RunParticleEffectState(es->origin, NULL, 1, pt_q2[Q2PT_PLAYER_TELEPORT], NULL);
		break;
	case Q2EV_FOOTSTEP:
		pe->RunParticleEffectState(es->origin, NULL, 1, pt_q2[Q2PT_FOOTSTEP], NULL);
		break;
	case Q2EV_FALLSHORT:
		Q2S_StartSound (NULL, es->number, CHAN_AUTO, S_PrecacheSound ("player/land1.wav"), 1, ATTN_NORM, 0);
		break;
	case Q2EV_FALL:
		Q2S_StartSound (NULL, es->number, CHAN_AUTO, S_PrecacheSexedSound (es->number, "*fall2.wav"), 1, ATTN_NORM, 0);
		break;
	case Q2EV_FALLFAR:
		Q2S_StartSound (NULL, es->number, CHAN_AUTO, S_PrecacheSexedSound (es->number, "*fall1.wav"), 1, ATTN_NORM, 0);
		break;
	case Q2EXEV_OTHER_FOOTSTEP:
		pe->RunParticleEffectState(es->origin, NULL, 1, pt_q2[Q2PT_OTHER_FOOTSTEP], NULL);
		break;
	case Q2EXEV_LADDER_STEP:
		pe->RunParticleEffectState(es->origin, NULL, 1, pt_q2[Q2PT_LADDER_STEP], NULL);
		break;
	default:
		Con_Printf("event %u not supported\n", es->u.q2.event);
		break;
	}
};
void CLQ2_TeleporterParticles(entity_state_t *es){};

/*these are emissive effects (ie: emitted each frame), but they're also mutually exclusive, so sharing emitstate is fine*/
void CLQ2_Tracker_Shell(q2centity_t *ent, vec3_t org)
{
	P_EmitEffect (org, NULL, 0, pt_q2[Q2PT_TRACKERSHELL], &(ent->emitstate));
};
void CLQ2_BfgParticles(q2centity_t *ent, vec3_t org)
{
	P_EmitEffect (org, NULL, 0, pt_q2[Q2PT_BFGPARTICLES], &(ent->emitstate));
};
void CLQ2_FlyEffect(q2centity_t *ent, vec3_t org)
{
	float starttime, n;
	float cltime = cl.time;
	int count;
	if (cl.paused)
		return;

	//q2 ramps up to 162 within the first 20 secs, sits at 162 for the next 20, then ramps back down for the 20 after that.
	//I have no idea how pvs issues are handled.

	if (ent->fly_stoptime < cltime)
	{
		starttime = cltime;
		ent->fly_stoptime = cltime + 60;
	}
	else
	{
		starttime = ent->fly_stoptime - 60;
	}

	n = cltime - starttime;
	if (n < 20)
		count = n * 162 / 20.0;
	else
	{
		n = ent->fly_stoptime - cltime;
		if (n < 20)
			count = n * 162 / 20.0;
		else
			count = 162;
	}
	if (count < 0)
		return;

	//these are assumed to be spawned anew 
	pe->RunParticleEffectState(org, NULL, count, pt_q2[Q2PT_FLIES], &(ent->emitstate));
};


#define MAX_Q2EDICTS 1024
#define	MAX_PARSE_ENTITIES	Q2MAX_PARSE_ENTITIES


static q2centity_t cl_entities[MAX_Q2EDICTS];
entity_state_t	clq2_parse_entities[MAX_PARSE_ENTITIES];

//where a client entity is now, for yquake2's effects (muzzle flashes come from their entity)
qboolean CLQ2_GetEntityOrigin(int entnum, vec3_t origin, vec3_t angles)
{
	if (entnum < 0 || entnum >= MAX_Q2EDICTS)
		return false;
	VectorCopy(cl_entities[entnum].current.origin, origin);
	VectorCopy(cl_entities[entnum].current.angles, angles);
	return true;
}

//a client entity as the last frame has it, for Daikatana's client code (cldk_fte.c): false if it isn't in it
qboolean CLQ2_GetEntityState(int entnum, float *origin, float *angles, float *lerporigin, int *modelindex, int *frame)
{
	q2centity_t *cent;
	if (entnum < 0 || entnum >= MAX_Q2EDICTS)
		return false;
	cent = &cl_entities[entnum];
	VectorCopy(cent->current.origin, origin);
	VectorCopy(cent->current.angles, angles);
	VectorCopy(cent->lerp_origin, lerporigin);
	*modelindex = cent->current.modelindex;
	*frame = cent->current.frame;
	return cent->serverframe == cl.q2frame.serverframe;
}

//the last frame's entities, their last two states, for Daikatana's client code to draw them itself (cldk_fte.c)
int CLQ2_NumFrameEntities(void)
{
	return cl.q2frame.valid?cl.q2frame.num_entities:0;
}
int CLQ2_FrameEntity(int i, entity_state_t **current, entity_state_t **prev, qboolean *isnew)
{	//its number; 0 if there's none
	entity_state_t *s;
	q2centity_t *cent;
	if (i < 0 || i >= cl.q2frame.num_entities)
		return 0;
	s = &clq2_parse_entities[(cl.q2frame.parse_entities+i)&(MAX_PARSE_ENTITIES-1)];
	if (s->number <= 0 || s->number >= MAX_Q2EDICTS)
		return 0;
	cent = &cl_entities[s->number];
	*current = &cent->current;
	*prev = &cent->prev;
	*isnew = cent->serverframe_created == cl.q2frame.serverframe;	//(prev's a copy, with the old origin)
	return s->number;
}
//where its entities are this frame, for the sounds of the map's own brush models (CLQ2_BSPSoundOrigin)
static void CLQ2_LerpOrigins (q2frame_t *frame)
{
	int pnum, k;
	for (pnum = 0; pnum < frame->num_entities; pnum++)
	{
		entity_state_t *s1 = &clq2_parse_entities[(frame->parse_entities+pnum)&(MAX_PARSE_ENTITIES-1)];
		q2centity_t *cent = &cl_entities[s1->number];
		for (k = 0; k < 3; k++)
			cent->lerp_origin[k] = cent->prev.origin[k] + cl.lerpfrac * (cent->current.origin[k] - cent->prev.origin[k]);
	}
}

void CL_SmokeAndFlash(vec3_t origin);

void CL_GetNumberedEntityInfo (int num, float *org, float *ang)
{
	q2centity_t	*ent;

	if (num < 0 || num >= MAX_Q2EDICTS)
		Host_EndGame ("CL_GetNumberedEntityInfo: bad ent");
	ent = &cl_entities[num];

	if (org)
		VectorCopy (ent->current.origin, org);
	if (ang)
		VectorCopy (ent->current.angles, ang);


	// FIXME: bmodel issues...
}

#ifdef Q2SERVER
void CLQ2_WriteDemoBaselines(sizebuf_t *buf)
{
	int i;
	q2entity_state_t	nullstate = {0};
	for (i = 0; i < MAX_Q2EDICTS; i++)
	{
		q2entity_state_t	es;
		entity_state_t		*base = &cl_entities[i].baseline;
		if (!base->modelindex)
			continue;

		//I brought these copies on myself...
		es.number = i;
		VectorCopy(base->origin, es.origin);
		VectorCopy(base->angles, es.angles);
		VectorCopy(base->u.q2.old_origin, es.old_origin);
		es.modelindex = base->modelindex;
		es.modelindex2 = base->modelindex2;
		es.modelindex3 = base->u.q2.modelindex3;
		es.modelindex4 = base->u.q2.modelindex4;
		es.frame = base->frame;
		es.skinnum = base->skinnum;
		es.effects = base->effects;
		es.renderfx = base->u.q2.renderfx;
		es.solid = base->solidsize;
		es.sound = base->u.q2.sound;
		es.event = base->u.q2.event;

		if (buf->cursize > buf->maxsize/2)
		{
			CL_WriteRecordQ2DemoMessage (buf);
			SZ_Clear (buf);
		}

		MSG_WriteByte (buf, svcq2_spawnbaseline);
		MSGQ2_WriteDeltaEntity(&nullstate, &es, NULL, NULL, buf, true, true, cls.protocol_q2==PROTOCOL_VERSION_Q2EX);
	}
}
#endif


/*
=====================================================================
The rerelease's monster muzzle flashes past the original's 210 (and its mods': REBLIVION's Oblivion monsters), as
KEX and Q2REPRO's CL_MuzzleFlash2 have them: a light, a sound, sometimes particles and smoke. Where is the cgame's
to say (GetMonsterFlashOffset: a mod's cgame knows its own monsters'), scaled with the entity.
=====================================================================
*/
static const struct
{
	unsigned short first, last;
	float r, g, b;
	const char *sound;
	qbyte fx;			//1: particles, 2: smoke and flash
} q2exmz2[] =
{
	{211, 218,	1, 0.5, 0.5,	"weapons/rippfire.wav", 0},		//MZ2_SOLDIER_RIPPER_1-8
	{219, 226,	0, 0, 1,		"weapons/hyprbf1a.wav", 0},		//MZ2_SOLDIER_HYPERGUN_1-8
	{227, 227,	1, 1, 0,		"weapons/hyprbf1a.wav", 0},		//MZ2_GUARDIAN_BLASTER
	{228, 231,	0.5, 0.5, 1,	NULL, 0},						//MZ2_ARACHNID_RAIL*
	{232, 239,	1, 1, 0,		"infantry/infatck1.wav", 3},	//MZ2_INFANTRY_MACHINEGUN_14-21
	{240, 241,	0, 0, 1,		"guncmdr/gcdratck2.wav", 0},	//MZ2_GUNCMDR_CHAINGUN_*
	{242, 250,	1, 0.5, 0,		"guncmdr/gcdratck3.wav", 0},	//MZ2_GUNCMDR_GRENADE_*
	{251, 251,	1, 1, 0,		"soldier/solatck2.wav", 0},		//MZ2_SOLDIER_BLASTER_9
	{252, 252,	1, 1, 0,		"soldier/solatck1.wav", 2},		//MZ2_SOLDIER_SHOTGUN_9
	{253, 253,	1, 1, 0,		"soldier/solatck3.wav", 3},		//MZ2_SOLDIER_MACHINEGUN_9
	{254, 254,	1, 0.5, 0.5,	"weapons/rippfire.wav", 0},		//MZ2_SOLDIER_RIPPER_9
	{255, 255,	0, 0, 1,		"weapons/hyprbf1a.wav", 0},		//MZ2_SOLDIER_HYPERGUN_9
	{256, 259,	1, 0.5, 0,		"gunner/gunatck3.wav", 0},		//MZ2_GUNNER_GRENADE2_*
	{260, 260,	1, 1, 0,		"infantry/infatck1.wav", 3},	//MZ2_INFANTRY_MACHINEGUN_22
	{261, 262,	1, 0.5, 0,		"gunner/gunatck3.wav", 0},		//MZ2_SUPERTANK_GRENADE_*
	{263, 263,	1, 1, 0,		"hover/hovatck1.wav", 0},		//MZ2_HOVER_BLASTER_2
	{264, 264,	0, 1, 0,		"tank/tnkatck3.wav", 0},		//MZ2_DAEDALUS_BLASTER_2
	{265, 276,	1, 1, 0,		"medic/medatck1.wav", 0},		//MZ2_MEDIC_HYPERBLASTER1_*
	{277, 288,	0, 1, 0,		"tank/tnkatck3.wav", 0},		//MZ2_MEDIC_HYPERBLASTER2_*
	//REBLIVION's (Oblivion's monsters). KEX doesn't know them; the game plays the kigrax's sound itself
	{289, 289,	0.5, 0.5, 1,	NULL, 0},						//MZ2_KIGRAX_PLASMA
	{290, 291,	1, 0.5, 0.2,	"tank/rocket.wav", 0},			//MZ2_SPIDER_ROCKET_*
	{292, 293,	0.5, 1, 0.5,	NULL, 0},						//MZ2_CYBORG_DEATOM_*
	{294, 295,	1, 0.5, 0.2,	"tank/tnkatck1.wav", 0},		//MZ2_BADASS_ROCKET_*
};
void CLQ2EX_MuzzleFlash2(int ent, int flash_number)
{
	vec3_t origin, offset = {0,0,0}, forward, right;
	entity_state_t *es;
	dlight_t *dl;
	float scale;
	size_t i;

	if (ent < 1 || ent >= MAX_Q2EDICTS)
		return;
	for (i = 0; i < countof(q2exmz2); i++)
		if (flash_number >= q2exmz2[i].first && flash_number <= q2exmz2[i].last)
			break;
	if (i == countof(q2exmz2))
		return;	//not one anyone knows

	es = &cl_entities[ent].current;
	CLQ2CG_MonsterFlashOffset(flash_number, offset);	//(stays zero without the cgame)
	scale = es->scale?es->scale/16.f:1;
	VectorScale(offset, scale, offset);
	AngleVectors(es->angles, forward, right, NULL);
	origin[0] = es->origin[0] + forward[0] * offset[0] + right[0] * offset[1];
	origin[1] = es->origin[1] + forward[1] * offset[0] + right[1] * offset[1];
	origin[2] = es->origin[2] + forward[2] * offset[0] + right[2] * offset[1] + offset[2];

	dl = CL_AllocDlight (ent);
	VectorCopy (origin, dl->origin);
	dl->radius = 200 + (rand()&31);
	dl->die = cl.time + 0.1;
	dl->color[0] = q2exmz2[i].r;
	dl->color[1] = q2exmz2[i].g;
	dl->color[2] = q2exmz2[i].b;

	if (q2exmz2[i].fx & 1)
		P_RunParticleEffect (origin, vec3_origin, 0, 40);
	if (q2exmz2[i].fx & 2)
		CL_SmokeAndFlash(origin);
	if (q2exmz2[i].sound)
		Q2S_StartSound (NULL, ent, CHAN_WEAPON, S_PrecacheSound(q2exmz2[i].sound), 1, ATTN_NORM, 0);
}

/*
=====================================================================
The rerelease's footsteps (KEX's, Q2REPRO's tent.c): textures/<texture>.mat names what a texture is made of, and a
step on it plays player/steps/<material>N.wav (a ladder's player/steps/ladderN.wav); without one, the original's
player/stepN.wav. Q2EX's servers send monsters' steps and ladder steps too.
=====================================================================
*/
#define Q2STEP_MAXSFX	16
typedef struct
{
	char material[16];		//"": the original's
	int num;
	sfx_t *sfx[Q2STEP_MAXSFX];
} q2stepset_t;
static struct
{
	model_t *world;
	q2stepset_t *sets;
	size_t numsets;
	struct
	{
		char texture[32];
		int set;
	} *tex;
	size_t numtex;
	sfx_t *last;
} q2steps;

static int CLQ2_StepSet(const char *material)
{
	q2stepset_t *s;
	char name[MAX_QPATH];
	size_t i;
	const char *m = material?material:"";

	for (i = 0; i < q2steps.numsets; i++)
		if (!Q_strcasecmp(q2steps.sets[i].material, m))
			return i;
	q2steps.sets = BZ_Realloc(q2steps.sets, sizeof(*q2steps.sets) * (q2steps.numsets+1));
	s = &q2steps.sets[q2steps.numsets];
	memset(s, 0, sizeof(*s));
	Q_strncpyz(s->material, m, sizeof(s->material));
	for (s->num = 0; s->num < Q2STEP_MAXSFX; s->num++)
	{
		if (*m)
			Q_snprintfz(name, sizeof(name), "player/steps/%s%i.wav", m, s->num+1);
		else
			Q_snprintfz(name, sizeof(name), "player/step%i.wav", s->num+1);
		if (!COM_FCheckExists(va("sound/%s", name)))
			break;
		s->sfx[s->num] = S_PrecacheSound(name);
	}
	return q2steps.numsets++;
}
static void CLQ2_StepsReset(void)
{
	BZ_Free(q2steps.sets);
	BZ_Free(q2steps.tex);
	memset(&q2steps, 0, sizeof(q2steps));
	q2steps.world = cl.worldmodel;
	CLQ2_StepSet(NULL);	//0: the original's, for everything else
}
static int CLQ2_TextureStepSet(const char *texture)
{
	char material[16], *file, *s;
	size_t i, size, len;
	int set = 0;

	for (i = 0; i < q2steps.numtex; i++)
		if (!Q_strcasecmp(q2steps.tex[i].texture, texture))
			return q2steps.tex[i].set;

	file = FS_LoadMallocFile(va("textures/%s.mat", texture), &size);
	if (file)
	{	//one word: the material
		for (s = file; s < file+size && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n'); s++)
			;
		for (len = 0; s+len < file+size && len < sizeof(material)-1 && ((s[len] >= 'a' && s[len] <= 'z') || (s[len] >= 'A' && s[len] <= 'Z') || (s[len] >= '0' && s[len] <= '9') || s[len] == '_' || s[len] == '-'); len++)
			material[len] = s[len];
		material[len] = 0;
		BZ_Free(file);
		if (*material && Q_strcasecmp(material, "default"))
			set = CLQ2_StepSet(material);
		if (!q2steps.sets[set].num)
			set = 0;	//no sounds for it
	}

	q2steps.tex = BZ_Realloc(q2steps.tex, sizeof(*q2steps.tex) * (q2steps.numtex+1));
	Q_strncpyz(q2steps.tex[q2steps.numtex].texture, texture, sizeof(q2steps.tex[q2steps.numtex].texture));
	q2steps.tex[q2steps.numtex].set = set;
	q2steps.numtex++;
	return set;
}
//what the entity stands on: half a step down from its feet (as Q2REPRO)
static int CLQ2_StepSurface(entity_state_t *s1)
{
	extern int predignoreentitynum;
	q2trace_t VARGS CLQ2_PMTrace (vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end);
	vec3_t start, end, mins = {0,0,0}, maxs = {0,0,0}, bmins, bmaxs;
	q2trace_t tr;
	int ignore = predignoreentitynum;

	VectorCopy(s1->origin, start);
	start[2] += 1;
	VectorCopy(start, end);
	end[2] -= 18/2;
	if (s1->solidsize && s1->solidsize != ES_SOLID_BSP)
	{
		COM_DecodeSize(s1->solidsize, bmins, bmaxs);
		mins[0] = bmins[0];	mins[1] = bmins[1];
		maxs[0] = bmaxs[0];	maxs[1] = bmaxs[1];
		end[2] += bmins[2];
	}
	else
		end[2] -= 66;	//as big as the biggest monster

	predignoreentitynum = s1->number;
	tr = CLQ2_PMTrace(start, mins, maxs, end);
	predignoreentitynum = ignore;
	if (tr.fraction == 1 || !tr.surface)
		return 0;
	return CLQ2_TextureStepSet(((q2mapsurface_t*)tr.surface)->rname);	//(the full name: game API 3's has 15 characters of it)
}
//Q2EX's step events: true when played (or not, by cl_footsteps)
static qboolean CLQ2_StepEvent(entity_state_t *s1)
{
	float vol = 1, attn = ATTN_NORM;
	q2stepset_t *set;
	sfx_t *sfx;
	int n;

	if (cls.protocol_q2 != PROTOCOL_VERSION_Q2EX || !cl.worldmodel || cl.worldmodel->fromgame != fg_quake2)
		return false;
	switch (s1->u.q2.event)
	{
	case Q2EV_FOOTSTEP:
		break;
	case Q2EXEV_OTHER_FOOTSTEP:	//monsters'
	case Q2EXEV_LADDER_STEP:
		vol = 0.5;
		attn = 2;	//ATTN_IDLE
		break;
	default:
		return false;
	}
	if (!CLQ2FX_FootstepsEnabled())
		return true;

	if (q2steps.world != cl.worldmodel)
		CLQ2_StepsReset();
	n = (s1->u.q2.event == Q2EXEV_LADDER_STEP)?CLQ2_StepSet("ladder"):CLQ2_StepSurface(s1);	//(may grow q2steps.sets)
	set = &q2steps.sets[n];
	if (!set->num)
		set = &q2steps.sets[0];
	if (!set->num)
		return true;

	//not the same one twice in a row
	n = rand() % set->num;
	sfx = set->sfx[n];
	if (sfx == q2steps.last)
		sfx = set->sfx[(n+1) % set->num];
	Q2S_StartSound (NULL, s1->number, CHAN_BODY, sfx, vol, attn, 0);
	q2steps.last = sfx;
	return true;
}

void CLQ2_ClearState(void)
{
	int i;
	memset(cl_entities, 0, sizeof(cl_entities));
	for (i = 0; i < MAX_Q2EDICTS; i++)
		cl_entities[i].baseline = nullentitystate;
}

#include "q2m_flash.c"
void CLQ2_RunMuzzleFlash2 (int ent, int flash_number)
{
	vec3_t		origin;
	dlight_t	*dl;
	vec3_t		forward, right, up;
	char		soundname[64];
	int ef;

	if (flash_number < 0 || flash_number >= sizeof(monster_flash_offset)/sizeof(monster_flash_offset[0]))
		return;

	// locate the origin
	AngleVectors (cl_entities[ent].current.angles, forward, right, up);
	origin[0] = cl_entities[ent].current.origin[0] + forward[0] * monster_flash_offset[flash_number].offset[0] + right[0] * monster_flash_offset[flash_number].offset[1];
	origin[1] = cl_entities[ent].current.origin[1] + forward[1] * monster_flash_offset[flash_number].offset[0] + right[1] * monster_flash_offset[flash_number].offset[1];
	origin[2] = cl_entities[ent].current.origin[2] + forward[2] * monster_flash_offset[flash_number].offset[0] + right[2] * monster_flash_offset[flash_number].offset[1] + monster_flash_offset[flash_number].offset[2];

	ef = P_FindParticleType(monster_flash_offset[flash_number].name);
	if (ef != P_INVALID)
	{
		P_RunParticleEffectType(origin, NULL, 1, ef);
		return;
	}

	//the rest of the function is legacy code.

	dl = CL_AllocDlight (ent);
	VectorCopy (origin,  dl->origin);
	dl->radius = 200 + (rand()&31);
//	dl->minlight = 32;
	dl->die = cl.time + 0.1;

	switch (flash_number)
	{
	case Q2MZ2_INFANTRY_MACHINEGUN_1:
	case Q2MZ2_INFANTRY_MACHINEGUN_2:
	case Q2MZ2_INFANTRY_MACHINEGUN_3:
	case Q2MZ2_INFANTRY_MACHINEGUN_4:
	case Q2MZ2_INFANTRY_MACHINEGUN_5:
	case Q2MZ2_INFANTRY_MACHINEGUN_6:
	case Q2MZ2_INFANTRY_MACHINEGUN_7:
	case Q2MZ2_INFANTRY_MACHINEGUN_8:
	case Q2MZ2_INFANTRY_MACHINEGUN_9:
	case Q2MZ2_INFANTRY_MACHINEGUN_10:
	case Q2MZ2_INFANTRY_MACHINEGUN_11:
	case Q2MZ2_INFANTRY_MACHINEGUN_12:
	case Q2MZ2_INFANTRY_MACHINEGUN_13:
		dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
		P_RunParticleEffect (origin, vec3_origin, 0, 40);
		CL_SmokeAndFlash(origin);
		Q2S_StartSound (NULL, ent, CHAN_WEAPON, S_PrecacheSound("infantry/infatck1.wav"), 1, ATTN_NORM, 0);
		break;

	case Q2MZ2_SOLDIER_MACHINEGUN_1:
	case Q2MZ2_SOLDIER_MACHINEGUN_2:
	case Q2MZ2_SOLDIER_MACHINEGUN_3:
	case Q2MZ2_SOLDIER_MACHINEGUN_4:
	case Q2MZ2_SOLDIER_MACHINEGUN_5:
	case Q2MZ2_SOLDIER_MACHINEGUN_6:
	case Q2MZ2_SOLDIER_MACHINEGUN_7:
	case Q2MZ2_SOLDIER_MACHINEGUN_8:
		dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
		P_RunParticleEffect (origin, vec3_origin, 0, 40);
		CL_SmokeAndFlash(origin);
		Q2S_StartSound (NULL, ent, CHAN_WEAPON, S_PrecacheSound("soldier/solatck3.wav"), 1, ATTN_NORM, 0);
		break;

	case Q2MZ2_GUNNER_MACHINEGUN_1:
	case Q2MZ2_GUNNER_MACHINEGUN_2:
	case Q2MZ2_GUNNER_MACHINEGUN_3:
	case Q2MZ2_GUNNER_MACHINEGUN_4:
	case Q2MZ2_GUNNER_MACHINEGUN_5:
	case Q2MZ2_GUNNER_MACHINEGUN_6:
	case Q2MZ2_GUNNER_MACHINEGUN_7:
	case Q2MZ2_GUNNER_MACHINEGUN_8:
		dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
		P_RunParticleEffect (origin, vec3_origin, 0, 40);
		CL_SmokeAndFlash(origin);
		Q2S_StartSound (NULL, ent, CHAN_WEAPON, S_PrecacheSound("gunner/gunatck2.wav"), 1, ATTN_NORM, 0);
		break;

	case Q2MZ2_ACTOR_MACHINEGUN_1:
	case Q2MZ2_SUPERTANK_MACHINEGUN_1:
	case Q2MZ2_SUPERTANK_MACHINEGUN_2:
	case Q2MZ2_SUPERTANK_MACHINEGUN_3:
	case Q2MZ2_SUPERTANK_MACHINEGUN_4:
	case Q2MZ2_SUPERTANK_MACHINEGUN_5:
	case Q2MZ2_SUPERTANK_MACHINEGUN_6:
	case Q2MZ2_TURRET_MACHINEGUN:			// PGM
		dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;

		P_RunParticleEffect (origin, vec3_origin, 0, 40);
		CL_SmokeAndFlash(origin);
		Q2S_StartSound (NULL, ent, CHAN_WEAPON, S_PrecacheSound("infantry/infatck1.wav"), 1, ATTN_NORM, 0);
		break;

	case Q2MZ2_BOSS2_MACHINEGUN_L1:
	case Q2MZ2_BOSS2_MACHINEGUN_L2:
	case Q2MZ2_BOSS2_MACHINEGUN_L3:
	case Q2MZ2_BOSS2_MACHINEGUN_L4:
	case Q2MZ2_BOSS2_MACHINEGUN_L5:
	case Q2MZ2_CARRIER_MACHINEGUN_L1:		// PMM
	case Q2MZ2_CARRIER_MACHINEGUN_L2:		// PMM
		dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;

		P_RunParticleEffect (origin, vec3_origin, 0, 40);
		CL_SmokeAndFlash(origin);
		Q2S_StartSound (NULL, ent, CHAN_WEAPON, S_PrecacheSound("infantry/infatck1.wav"), 1, ATTN_NONE, 0);
		break;

	case Q2MZ2_SOLDIER_BLASTER_1:
	case Q2MZ2_SOLDIER_BLASTER_2:
	case Q2MZ2_SOLDIER_BLASTER_3:
	case Q2MZ2_SOLDIER_BLASTER_4:
	case Q2MZ2_SOLDIER_BLASTER_5:
	case Q2MZ2_SOLDIER_BLASTER_6:
	case Q2MZ2_SOLDIER_BLASTER_7:
	case Q2MZ2_SOLDIER_BLASTER_8:
	case Q2MZ2_TURRET_BLASTER:			// PGM
		dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
		Q2S_StartSound (NULL, ent, CHAN_WEAPON, S_PrecacheSound("soldier/solatck2.wav"), 1, ATTN_NORM, 0);
		break;

	case Q2MZ2_FLYER_BLASTER_1:
	case Q2MZ2_FLYER_BLASTER_2:
		dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
		Q2S_StartSound (NULL, ent, CHAN_WEAPON, S_PrecacheSound("flyer/flyatck3.wav"), 1, ATTN_NORM, 0);
		break;

	case Q2MZ2_MEDIC_BLASTER_1:
		dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
		Q2S_StartSound (NULL, ent, CHAN_WEAPON, S_PrecacheSound("medic/medatck1.wav"), 1, ATTN_NORM, 0);
		break;

	case Q2MZ2_HOVER_BLASTER_1:
		dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
		Q2S_StartSound (NULL, ent, CHAN_WEAPON, S_PrecacheSound("hover/hovatck1.wav"), 1, ATTN_NORM, 0);
		break;

	case Q2MZ2_FLOAT_BLASTER_1:
		dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
		Q2S_StartSound (NULL, ent, CHAN_WEAPON, S_PrecacheSound("floater/fltatck1.wav"), 1, ATTN_NORM, 0);
		break;

	case Q2MZ2_SOLDIER_SHOTGUN_1:
	case Q2MZ2_SOLDIER_SHOTGUN_2:
	case Q2MZ2_SOLDIER_SHOTGUN_3:
	case Q2MZ2_SOLDIER_SHOTGUN_4:
	case Q2MZ2_SOLDIER_SHOTGUN_5:
	case Q2MZ2_SOLDIER_SHOTGUN_6:
	case Q2MZ2_SOLDIER_SHOTGUN_7:
	case Q2MZ2_SOLDIER_SHOTGUN_8:
		dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
		CL_SmokeAndFlash(origin);
		Q2S_StartSound (NULL, ent, CHAN_WEAPON, S_PrecacheSound("soldier/solatck1.wav"), 1, ATTN_NORM, 0);
		break;

	case Q2MZ2_TANK_BLASTER_1:
	case Q2MZ2_TANK_BLASTER_2:
	case Q2MZ2_TANK_BLASTER_3:
		dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
		Q2S_StartSound (NULL, ent, CHAN_WEAPON, S_PrecacheSound("tank/tnkatck3.wav"), 1, ATTN_NORM, 0);
		break;

	case Q2MZ2_TANK_MACHINEGUN_1:
	case Q2MZ2_TANK_MACHINEGUN_2:
	case Q2MZ2_TANK_MACHINEGUN_3:
	case Q2MZ2_TANK_MACHINEGUN_4:
	case Q2MZ2_TANK_MACHINEGUN_5:
	case Q2MZ2_TANK_MACHINEGUN_6:
	case Q2MZ2_TANK_MACHINEGUN_7:
	case Q2MZ2_TANK_MACHINEGUN_8:
	case Q2MZ2_TANK_MACHINEGUN_9:
	case Q2MZ2_TANK_MACHINEGUN_10:
	case Q2MZ2_TANK_MACHINEGUN_11:
	case Q2MZ2_TANK_MACHINEGUN_12:
	case Q2MZ2_TANK_MACHINEGUN_13:
	case Q2MZ2_TANK_MACHINEGUN_14:
	case Q2MZ2_TANK_MACHINEGUN_15:
	case Q2MZ2_TANK_MACHINEGUN_16:
	case Q2MZ2_TANK_MACHINEGUN_17:
	case Q2MZ2_TANK_MACHINEGUN_18:
	case Q2MZ2_TANK_MACHINEGUN_19:
		dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
		P_RunParticleEffect (origin, vec3_origin, 0, 40);
		CL_SmokeAndFlash(origin);
		snprintf(soundname, sizeof(soundname), "tank/tnkatk2%c.wav", 'a' + rand() % 5);
		Q2S_StartSound (NULL, ent, CHAN_WEAPON, S_PrecacheSound(soundname), 1, ATTN_NORM, 0);
		break;

	case Q2MZ2_CHICK_ROCKET_1:
	case Q2MZ2_TURRET_ROCKET:			// PGM
		dl->color[0] = 1;dl->color[1] = 0.5;dl->color[2] = 0.2;
		Q2S_StartSound (NULL, ent, CHAN_WEAPON, S_PrecacheSound("chick/chkatck2.wav"), 1, ATTN_NORM, 0);
		break;

	case Q2MZ2_TANK_ROCKET_1:
	case Q2MZ2_TANK_ROCKET_2:
	case Q2MZ2_TANK_ROCKET_3:
		dl->color[0] = 1;dl->color[1] = 0.5;dl->color[2] = 0.2;
		Q2S_StartSound (NULL, ent, CHAN_WEAPON, S_PrecacheSound("tank/tnkatck1.wav"), 1, ATTN_NORM, 0);
		break;

	case Q2MZ2_SUPERTANK_ROCKET_1:
	case Q2MZ2_SUPERTANK_ROCKET_2:
	case Q2MZ2_SUPERTANK_ROCKET_3:
	case Q2MZ2_BOSS2_ROCKET_1:
	case Q2MZ2_BOSS2_ROCKET_2:
	case Q2MZ2_BOSS2_ROCKET_3:
	case Q2MZ2_BOSS2_ROCKET_4:
	case Q2MZ2_CARRIER_ROCKET_1:
//	case Q2MZ2_CARRIER_ROCKET_2:
//	case Q2MZ2_CARRIER_ROCKET_3:
//	case Q2MZ2_CARRIER_ROCKET_4:
		dl->color[0] = 1;dl->color[1] = 0.5;dl->color[2] = 0.2;
		Q2S_StartSound (NULL, ent, CHAN_WEAPON, S_PrecacheSound("tank/rocket.wav"), 1, ATTN_NORM, 0);
		break;

	case Q2MZ2_GUNNER_GRENADE_1:
	case Q2MZ2_GUNNER_GRENADE_2:
	case Q2MZ2_GUNNER_GRENADE_3:
	case Q2MZ2_GUNNER_GRENADE_4:
		dl->color[0] = 1;dl->color[1] = 0.5;dl->color[2] = 0;
		Q2S_StartSound (NULL, ent, CHAN_WEAPON, S_PrecacheSound("gunner/gunatck3.wav"), 1, ATTN_NORM, 0);
		break;

	case Q2MZ2_GLADIATOR_RAILGUN_1:
	// PMM
	case Q2MZ2_CARRIER_RAILGUN:
	case Q2MZ2_WIDOW_RAIL:
	// pmm
		dl->color[0] = 0.5;dl->color[1] = 0.5;dl->color[2] = 1.0;
		break;

// --- Xian's shit starts ---
	case Q2MZ2_MAKRON_BFG:
		dl->color[0] = 0.5;dl->color[1] = 1 ;dl->color[2] = 0.5;
		//Q2S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("makron/bfg_fire.wav"), 1, ATTN_NORM, 0);
		break;

	case Q2MZ2_MAKRON_BLASTER_1:
	case Q2MZ2_MAKRON_BLASTER_2:
	case Q2MZ2_MAKRON_BLASTER_3:
	case Q2MZ2_MAKRON_BLASTER_4:
	case Q2MZ2_MAKRON_BLASTER_5:
	case Q2MZ2_MAKRON_BLASTER_6:
	case Q2MZ2_MAKRON_BLASTER_7:
	case Q2MZ2_MAKRON_BLASTER_8:
	case Q2MZ2_MAKRON_BLASTER_9:
	case Q2MZ2_MAKRON_BLASTER_10:
	case Q2MZ2_MAKRON_BLASTER_11:
	case Q2MZ2_MAKRON_BLASTER_12:
	case Q2MZ2_MAKRON_BLASTER_13:
	case Q2MZ2_MAKRON_BLASTER_14:
	case Q2MZ2_MAKRON_BLASTER_15:
	case Q2MZ2_MAKRON_BLASTER_16:
	case Q2MZ2_MAKRON_BLASTER_17:
		dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
		Q2S_StartSound (NULL, ent, CHAN_WEAPON, S_PrecacheSound("makron/blaster.wav"), 1, ATTN_NORM, 0);
		break;
	
	case Q2MZ2_JORG_MACHINEGUN_L1:
	case Q2MZ2_JORG_MACHINEGUN_L2:
	case Q2MZ2_JORG_MACHINEGUN_L3:
	case Q2MZ2_JORG_MACHINEGUN_L4:
	case Q2MZ2_JORG_MACHINEGUN_L5:
	case Q2MZ2_JORG_MACHINEGUN_L6:
		dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
		P_RunParticleEffect (origin, vec3_origin, 0, 40);
		CL_SmokeAndFlash(origin);
		Q2S_StartSound (NULL, ent, CHAN_WEAPON, S_PrecacheSound("boss3/xfire.wav"), 1, ATTN_NORM, 0);
		break;

	case Q2MZ2_JORG_MACHINEGUN_R1:
	case Q2MZ2_JORG_MACHINEGUN_R2:
	case Q2MZ2_JORG_MACHINEGUN_R3:
	case Q2MZ2_JORG_MACHINEGUN_R4:
	case Q2MZ2_JORG_MACHINEGUN_R5:
	case Q2MZ2_JORG_MACHINEGUN_R6:
		dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
		P_RunParticleEffect (origin, vec3_origin, 0, 40);
		CL_SmokeAndFlash(origin);
		break;

	case Q2MZ2_JORG_BFG_1:
		dl->color[0] = 0.5;dl->color[1] = 1 ;dl->color[2] = 0.5;
		break;

	case Q2MZ2_BOSS2_MACHINEGUN_R1:
	case Q2MZ2_BOSS2_MACHINEGUN_R2:
	case Q2MZ2_BOSS2_MACHINEGUN_R3:
	case Q2MZ2_BOSS2_MACHINEGUN_R4:
	case Q2MZ2_BOSS2_MACHINEGUN_R5:
	case Q2MZ2_CARRIER_MACHINEGUN_R1:			// PMM
	case Q2MZ2_CARRIER_MACHINEGUN_R2:			// PMM

		dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;

		P_RunParticleEffect (origin, vec3_origin, 0, 40);
		CL_SmokeAndFlash(origin);
		break;

// ======
// ROGUE
	case Q2MZ2_STALKER_BLASTER:
	case Q2MZ2_DAEDALUS_BLASTER:
	case Q2MZ2_MEDIC_BLASTER_2:
	case Q2MZ2_WIDOW_BLASTER:
	case Q2MZ2_WIDOW_BLASTER_SWEEP1:
	case Q2MZ2_WIDOW_BLASTER_SWEEP2:
	case Q2MZ2_WIDOW_BLASTER_SWEEP3:
	case Q2MZ2_WIDOW_BLASTER_SWEEP4:
	case Q2MZ2_WIDOW_BLASTER_SWEEP5:
	case Q2MZ2_WIDOW_BLASTER_SWEEP6:
	case Q2MZ2_WIDOW_BLASTER_SWEEP7:
	case Q2MZ2_WIDOW_BLASTER_SWEEP8:
	case Q2MZ2_WIDOW_BLASTER_SWEEP9:
	case Q2MZ2_WIDOW_BLASTER_100:
	case Q2MZ2_WIDOW_BLASTER_90:
	case Q2MZ2_WIDOW_BLASTER_80:
	case Q2MZ2_WIDOW_BLASTER_70:
	case Q2MZ2_WIDOW_BLASTER_60:
	case Q2MZ2_WIDOW_BLASTER_50:
	case Q2MZ2_WIDOW_BLASTER_40:
	case Q2MZ2_WIDOW_BLASTER_30:
	case Q2MZ2_WIDOW_BLASTER_20:
	case Q2MZ2_WIDOW_BLASTER_10:
	case Q2MZ2_WIDOW_BLASTER_0:
	case Q2MZ2_WIDOW_BLASTER_10L:
	case Q2MZ2_WIDOW_BLASTER_20L:
	case Q2MZ2_WIDOW_BLASTER_30L:
	case Q2MZ2_WIDOW_BLASTER_40L:
	case Q2MZ2_WIDOW_BLASTER_50L:
	case Q2MZ2_WIDOW_BLASTER_60L:
	case Q2MZ2_WIDOW_BLASTER_70L:
	case Q2MZ2_WIDOW_RUN_1:
	case Q2MZ2_WIDOW_RUN_2:
	case Q2MZ2_WIDOW_RUN_3:
	case Q2MZ2_WIDOW_RUN_4:
	case Q2MZ2_WIDOW_RUN_5:
	case Q2MZ2_WIDOW_RUN_6:
	case Q2MZ2_WIDOW_RUN_7:
	case Q2MZ2_WIDOW_RUN_8:
		dl->color[0] = 0;dl->color[1] = 1;dl->color[2] = 0;
		Q2S_StartSound (NULL, ent, CHAN_WEAPON, S_PrecacheSound("tank/tnkatck3.wav"), 1, ATTN_NORM, 0);
		break;

	case Q2MZ2_WIDOW_DISRUPTOR:
		dl->color[0] = -1;dl->color[1] = -1;dl->color[2] = -1;
		Q2S_StartSound (NULL, ent, CHAN_WEAPON, S_PrecacheSound("weapons/disint2.wav"), 1, ATTN_NORM, 0);
		break;

	case Q2MZ2_WIDOW_PLASMABEAM:
	case Q2MZ2_WIDOW2_BEAMER_1:
	case Q2MZ2_WIDOW2_BEAMER_2:
	case Q2MZ2_WIDOW2_BEAMER_3:
	case Q2MZ2_WIDOW2_BEAMER_4:
	case Q2MZ2_WIDOW2_BEAMER_5:
	case Q2MZ2_WIDOW2_BEAM_SWEEP_1:
	case Q2MZ2_WIDOW2_BEAM_SWEEP_2:
	case Q2MZ2_WIDOW2_BEAM_SWEEP_3:
	case Q2MZ2_WIDOW2_BEAM_SWEEP_4:
	case Q2MZ2_WIDOW2_BEAM_SWEEP_5:
	case Q2MZ2_WIDOW2_BEAM_SWEEP_6:
	case Q2MZ2_WIDOW2_BEAM_SWEEP_7:
	case Q2MZ2_WIDOW2_BEAM_SWEEP_8:
	case Q2MZ2_WIDOW2_BEAM_SWEEP_9:
	case Q2MZ2_WIDOW2_BEAM_SWEEP_10:
	case Q2MZ2_WIDOW2_BEAM_SWEEP_11:
		dl->radius = 300 + (rand()&100);
		dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
		dl->die = cl.time + 200;
		break;
// ROGUE
// ======

// --- Xian's shit ends ---

  //hmm... he must take AGES on the loo.... :p

	default:
		Con_Printf(CON_WARNING"CLQ2_RunMuzzleFlash2: %i not implemented\n", flash_number);
		break;
	}
}

void CLQ2_ParseMuzzleFlash (void)
{
	vec3_t		fv, rv, dummy;
	dlight_t	*dl;
	int			i, weapon;
	vec3_t		org, ang;
	int			silenced;
	float		volume;
	char		soundname[64];

	i = (unsigned short)(short)MSG_ReadShort ();
	if (i < 1 || i >= Q2MAX_EDICTS)
		Host_Error ("CL_ParseMuzzleFlash: bad entity");

	weapon = MSG_ReadByte ();
	silenced = weapon & Q2MZ_SILENCED;
	weapon &= ~Q2MZ_SILENCED;

	CL_GetNumberedEntityInfo(i, org, ang);

	dl = CL_AllocDlight (i);
	VectorCopy (org,  dl->origin);
	AngleVectors (ang, fv, rv, dummy);
	VectorMA (dl->origin, 18, fv, dl->origin);
	VectorMA (dl->origin, 16, rv, dl->origin);
	if (silenced)
		dl->radius = 100 + (rand()&31);
	else
		dl->radius = 200 + (rand()&31);
	dl->minlight = 32;
	dl->die = cl.time+0.05; //+ 0.1;
	dl->decay = 1;

	dl->channelfade[0] = 2;
	dl->channelfade[1] = 2;
	dl->channelfade[2] = 2;

	if (silenced)
		volume = 0.2;
	else
		volume = 1;


	switch (weapon)
	{
	case Q2MZ_BLASTER:
		dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
		Q2S_StartSound (NULL, i, CHAN_WEAPON, S_PrecacheSound("weapons/blastf1a.wav"), volume, ATTN_NORM, 0);
		break;
	case Q2MZ_BLUEHYPERBLASTER:
		dl->color[0] = 0;dl->color[1] = 0;dl->color[2] = 1;
		Q2S_StartSound (NULL, i, CHAN_WEAPON, S_PrecacheSound("weapons/hyprbf1a.wav"), volume, ATTN_NORM, 0);
		break;
	case Q2MZ_HYPERBLASTER:
		dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
		Q2S_StartSound (NULL, i, CHAN_WEAPON, S_PrecacheSound("weapons/hyprbf1a.wav"), volume, ATTN_NORM, 0);
		break;
	case Q2MZ_MACHINEGUN:
		dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
		Q_snprintfz(soundname, sizeof(soundname), "weapons/machgf%ib.wav", (rand() % 5) + 1);
		Q2S_StartSound (NULL, i, CHAN_WEAPON, S_PrecacheSound(soundname), volume, ATTN_NORM, 0);
		break;

	case Q2MZ_SHOTGUN:
		dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
		Q2S_StartSound (NULL, i, CHAN_WEAPON, S_PrecacheSound("weapons/shotgf1b.wav"), volume, ATTN_NORM, 0);
		Q2S_StartSound (NULL, i, CHAN_AUTO,   S_PrecacheSound("weapons/shotgr1b.wav"), volume, ATTN_NORM, 0.1);
		break;
	case Q2MZ_SSHOTGUN:
		dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
		Q2S_StartSound (NULL, i, CHAN_WEAPON, S_PrecacheSound("weapons/sshotf1b.wav"), volume, ATTN_NORM, 0);
		break;
	case Q2MZ_CHAINGUN1:
		dl->radius = 200 + (rand()&31);
		dl->color[0] = 1;dl->color[1] = 0.25;dl->color[2] = 0;
		Q_snprintfz(soundname, sizeof(soundname), "weapons/machgf%ib.wav", (rand() % 5) + 1);
		Q2S_StartSound (NULL, i, CHAN_WEAPON, S_PrecacheSound(soundname), volume, ATTN_NORM, 0);
		break;
	case Q2MZ_CHAINGUN2:
		dl->radius = 225 + (rand()&31);
		dl->color[0] = 1;dl->color[1] = 0.5;dl->color[2] = 0;
		dl->die = cl.time  + 0.1;	// long delay
		Q_snprintfz(soundname, sizeof(soundname), "weapons/machgf%ib.wav", (rand() % 5) + 1);
		Q2S_StartSound (NULL, i, CHAN_WEAPON, S_PrecacheSound(soundname), volume, ATTN_NORM, 0);
		Q_snprintfz(soundname, sizeof(soundname), "weapons/machgf%ib.wav", (rand() % 5) + 1);
		Q2S_StartSound (NULL, i, CHAN_AUTO, S_PrecacheSound(soundname), volume, ATTN_NORM, 0.05);
		break;
	case Q2MZ_CHAINGUN3:
		dl->radius = 250 + (rand()&31);
		dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
		dl->die = cl.time  + 0.1;	// long delay
		Q_snprintfz(soundname, sizeof(soundname), "weapons/machgf%ib.wav", (rand() % 5) + 1);
		Q2S_StartSound (NULL, i, CHAN_WEAPON, S_PrecacheSound(soundname), volume, ATTN_NORM, 0);
		Q_snprintfz(soundname, sizeof(soundname), "weapons/machgf%ib.wav", (rand() % 5) + 1);
		Q2S_StartSound (NULL, i, CHAN_AUTO, S_PrecacheSound(soundname), volume, ATTN_NORM, 0.033);
		Q_snprintfz(soundname, sizeof(soundname), "weapons/machgf%ib.wav", (rand() % 5) + 1);
		Q2S_StartSound (NULL, i, CHAN_AUTO, S_PrecacheSound(soundname), volume, ATTN_NORM, 0.066);
		break;

	case Q2MZ_RAILGUN:
		dl->color[0] = 0.5;dl->color[1] = 0.5;dl->color[2] = 1;
		Q2S_StartSound (NULL, i, CHAN_WEAPON, S_PrecacheSound("weapons/railgf1a.wav"), volume, ATTN_NORM, 0);
		break;
	case Q2MZ_ROCKET:
		dl->color[0] = 1;dl->color[1] = 0.5;dl->color[2] = 0.2;
		Q2S_StartSound (NULL, i, CHAN_WEAPON, S_PrecacheSound("weapons/rocklf1a.wav"), volume, ATTN_NORM, 0);
		Q2S_StartSound (NULL, i, CHAN_AUTO,   S_PrecacheSound("weapons/rocklr1b.wav"), volume, ATTN_NORM, 0.1);
		break;
	case Q2MZ_GRENADE:
		dl->color[0] = 1;dl->color[1] = 0.5;dl->color[2] = 0;
		Q2S_StartSound (NULL, i, CHAN_WEAPON, S_PrecacheSound("weapons/grenlf1a.wav"), volume, ATTN_NORM, 0);
		Q2S_StartSound (NULL, i, CHAN_AUTO,   S_PrecacheSound("weapons/grenlr1b.wav"), volume, ATTN_NORM, 0.1);
		break;
	case Q2MZ_BFG:
		dl->color[0] = 0;dl->color[1] = 1;dl->color[2] = 0;
		Q2S_StartSound (NULL, i, CHAN_WEAPON, S_PrecacheSound("weapons/bfg__f1y.wav"), volume, ATTN_NORM, 0);
		break;

	case Q2MZ_LOGIN:
		dl->color[0] = 0;dl->color[1] = 1; dl->color[2] = 0;
		dl->die = cl.time + 1.0;
		Q2S_StartSound (NULL, i, CHAN_WEAPON, S_PrecacheSound("weapons/grenlf1a.wav"), 1, ATTN_NORM, 0);
//		CL_LogoutEffect (pl->current.origin, weapon);
		break;
	case Q2MZ_LOGOUT:
		dl->color[0] = 1;dl->color[1] = 0; dl->color[2] = 0;
		dl->die = cl.time + 1.0;
		Q2S_StartSound (NULL, i, CHAN_WEAPON, S_PrecacheSound("weapons/grenlf1a.wav"), 1, ATTN_NORM, 0);
//		CL_LogoutEffect (pl->current.origin, weapon);
		break;
	case Q2MZ_RESPAWN:
		dl->color[0] = 1;dl->color[1] = 1; dl->color[2] = 0;
		dl->die = cl.time + 1.0;
		Q2S_StartSound (NULL, i, CHAN_WEAPON, S_PrecacheSound("weapons/grenlf1a.wav"), 1, ATTN_NORM, 0);
//		CL_LogoutEffect (pl->current.origin, weapon);
		break;
	// RAFAEL
	case Q2MZ_PHALANX:
		dl->color[0] = 1;dl->color[1] = 0.5; dl->color[2] = 0.5;
		Q2S_StartSound (NULL, i, CHAN_WEAPON, S_PrecacheSound("weapons/plasshot.wav"), volume, ATTN_NORM, 0);
		break;
	// RAFAEL
	case Q2MZ_IONRIPPER:
		dl->color[0] = 1;dl->color[1] = 0.5; dl->color[2] = 0.5;
		Q2S_StartSound (NULL, i, CHAN_WEAPON, S_PrecacheSound("weapons/rippfire.wav"), volume, ATTN_NORM, 0);
		break;

// ======================
// PGM
	case Q2MZ_ETF_RIFLE:
		dl->color[0] = 0.9;dl->color[1] = 0.7;dl->color[2] = 0;
		Q2S_StartSound (NULL, i, CHAN_WEAPON, S_PrecacheSound("weapons/nail1.wav"), volume, ATTN_NORM, 0);
		break;
	case Q2MZ_SHOTGUN2:
		dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
		Q2S_StartSound (NULL, i, CHAN_WEAPON, S_PrecacheSound("weapons/shotg2.wav"), volume, ATTN_NORM, 0);
		break;
	case Q2MZ_HEATBEAM:
		dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
		dl->die = cl.time + 100;
	//	Q2S_StartSound (NULL, i, CHAN_WEAPON, S_PrecacheSound("weapons/bfg__l1a.wav"), volume, ATTN_NORM, 0);
		break;
	case Q2MZ_BLASTER2:
		dl->color[0] = 0;dl->color[1] = 1;dl->color[2] = 0;
		// FIXME - different sound for blaster2 ??
		Q2S_StartSound (NULL, i, CHAN_WEAPON, S_PrecacheSound("weapons/blastf1a.wav"), volume, ATTN_NORM, 0);
		break;
	case Q2MZ_TRACKER:
		// negative flashes handled the same in gl/soft until CL_AddDLights
		dl->color[0] = -1;dl->color[1] = -1;dl->color[2] = -1;
		Q2S_StartSound (NULL, i, CHAN_WEAPON, S_PrecacheSound("weapons/disint2.wav"), volume, ATTN_NORM, 0);
		break;
	case Q2MZ_NUKE1:
		dl->color[0] = 1;dl->color[1] = 0;dl->color[2] = 0;
		dl->die = cl.time + 100;
		break;
	case Q2MZ_NUKE2:
		dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
		dl->die = cl.time + 100;
		break;
	case Q2MZ_NUKE4:
		dl->color[0] = 0;dl->color[1] = 0;dl->color[2] = 1;
		dl->die = cl.time + 100;
		break;
	case Q2MZ_NUKE8:
		dl->color[0] = 0;dl->color[1] = 1;dl->color[2] = 1;
		dl->die = cl.time + 100;
		break;
// PGM
// ======================

	case Q2EXMZ_BFG2:
	case Q2EXMZ_PHALANX2:
	case Q2EXMZ_PROX:
	//case Q2EXMZ_ETF_RIFLE_2:	//erk?
		Con_Printf(CON_WARNING"CLQ2_ParseMuzzleFlash: %i not implemented\n", weapon);
		break;
	default:
		Host_EndGame ("CL_ParseMuzzleFlash: bad effect index");
	}
}

void CLQ2_ParseMuzzleFlash2 (void)
{
	int			ent;
	int			flash_number;

	ent = (unsigned short)(short)MSG_ReadShort ();
	if (ent < 1 || ent >= Q2MAX_EDICTS)
		Host_EndGame ("CL_ParseMuzzleFlash2: bad entity");

	flash_number = MSG_ReadByte ();

	CLQ2_RunMuzzleFlash2(ent, flash_number);
}
void CLQ2EX_ParseMuzzleFlash3 (void)
{	//same as 2, but with a short for more numbers.
	int			ent;
	int			flash_number;

	ent = (unsigned short)(short)MSG_ReadShort ();
	if (ent < 1 || ent >= Q2MAX_EDICTS)
		Host_EndGame ("CL_ParseMuzzleFlash3: bad entity");

	flash_number = MSG_ReadUInt16 ();

	if (flash_number > 210)
		CLQ2EX_MuzzleFlash2(ent, flash_number);	//the rerelease's
	else
		CLQ2_RunMuzzleFlash2(ent, flash_number);
}

void CLQ2_ParseInventory (int seat)
{
	unsigned int		i;
	for (i=0 ; i<Q2MAX_ITEMS ; i++)
		cl.inventory[seat][i] = MSG_ReadShort ();
}

/*
=========================================================================

FRAME PARSING

=========================================================================
*/

/*
=================
CL_ParseEntityBits

Returns the entity number and the header bits
=================
*/
//static int	bitcounts[32];	/// just for protocol profiling
static int CLQ2_ParseEntityBits (quint64_t *bits)
{
	quint64_t	b, total;
//	int			i;
	int			number;

	total = MSG_ReadByte ();
	if (total & Q2U_MOREBITS1)
	{
		b = MSG_ReadByte ();
		total |= b<<8;
	}
	if (total & Q2U_MOREBITS2)
	{
		b = MSG_ReadByte ();
		total |= b<<16;
	}
	if (total & Q2U_MOREBITS3)
	{
		b = MSG_ReadByte ();
		total |= b<<24;
	}

	if (total & Q2UEX_MOREBITS4)
	{
		b = MSG_ReadByte ();
		total |= b<<32;
	}

	// count the bits for net profiling
/*	for (i=0 ; i<32 ; i++)
		if (total&(1<<i))
			bitcounts[i]++;
*/
	if (total & Q2U_NUMBER16)
		number = (unsigned short)MSG_ReadShort ();
	else
		number = MSG_ReadByte ();

	*bits = total;

	return number;
}

static unsigned int MSG_ReadSizeQ2E (void)
{
	unsigned int solid = MSG_ReadLong();
	if (solid != ES_SOLID_BSP && solid != ES_SOLID_NOT)
		solid =	(((solid>>0)&0xff)<<0)
			|	(((solid>>16)&0xff)<<8)
			|	((((solid>>24)&0xff)+32768-32)<<16);	/*up can be negative*/
	return solid;
}

/*
==================
CL_ParseDelta

Can go from either a baseline or a previous packet_entity
==================
*/
static void CLQ2_ParseDelta (entity_state_t *from, entity_state_t *to, int number, quint64_t bits)
{
	// set everything to the state we are delta'ing from
	*to = *from;

	VectorCopy (from->origin, to->u.q2.old_origin);
	to->number = number;

	if (bits & Q2U_MODEL)
	{
		if (bits & Q2UX_INDEX16)
			to->modelindex = MSG_ReadShort();
		else
			to->modelindex = MSG_ReadByte ();
	}
	if (bits & Q2U_MODEL2)
	{
		if (bits & Q2UX_INDEX16)
			to->modelindex2 = MSG_ReadShort();
		else
			to->modelindex2 = MSG_ReadByte ();
	}
	if (bits & Q2U_MODEL3)
	{
		if (bits & Q2UX_INDEX16)
			to->u.q2.modelindex3 = MSG_ReadShort();
		else
			to->u.q2.modelindex3 = MSG_ReadByte ();
	}
	if (bits & Q2U_MODEL4)
	{
		if (bits & Q2UX_INDEX16)
			to->u.q2.modelindex4 = MSG_ReadShort();
		else
			to->u.q2.modelindex4 = MSG_ReadByte ();
	}
		
	if (bits & Q2U_FRAME8)
		to->frame = MSG_ReadByte ();
	if (bits & Q2U_FRAME16)
		to->frame = MSG_ReadUInt16();
	if ((bits & Q2U_FRAME8) && (bits & Q2U_FRAME16))
		Con_Printf("\t8bit AND 16bit frame?\n");

	if ((bits & Q2U_SKIN8) && (bits & Q2U_SKIN16))		//used for laser colors
		to->skinnum = MSG_ReadLong();
	else if (bits & Q2U_SKIN8)
		to->skinnum = MSG_ReadByte();
	else if (bits & Q2U_SKIN16)
		to->skinnum = MSG_ReadUInt16();

	if (bits & (Q2U_EFFECTS8|Q2U_EFFECTS16|Q2UEX_EFFECTS64))
	{
		unsigned int lo = 0, v;
		if (bits&Q2UEX_EFFECTS64)
			lo = MSG_ReadLong();

		if ( (bits & (Q2U_EFFECTS8|Q2U_EFFECTS16)) == (Q2U_EFFECTS8|Q2U_EFFECTS16) )
			v = MSG_ReadLong();
		else if (bits & Q2U_EFFECTS16)
			v = MSG_ReadUInt16();
		else
			v = MSG_ReadByte();

		if (bits&Q2UEX_EFFECTS64)
		{	//the low 32 bits came first, these are the high ones
			to->effects = lo;
			to->u.q2.effects2 = v;
		}
		else
		{
			to->effects = v;
			to->u.q2.effects2 = 0;
		}
	}

	if ( (bits & (Q2U_RENDERFX8|Q2U_RENDERFX16)) == (Q2U_RENDERFX8|Q2U_RENDERFX16) )
		to->u.q2.renderfx = MSG_ReadLong() & 0x07ffffff;	//only the standard ones actually supported by vanilla q2 without corrupting fte's own ones.
	else if (bits & Q2U_RENDERFX8)
		to->u.q2.renderfx = MSG_ReadByte();
	else if (bits & Q2U_RENDERFX16)
		to->u.q2.renderfx = MSG_ReadUInt16();

	if (cls.protocol_q2 == PROTOCOL_VERSION_Q2EX)
	{
		if (bits & Q2U_SOLID)
			to->solidsize = MSG_ReadSizeQ2E();

		if (!to->solidsize)
		{	//demos reportedly compress these.
			if (bits & Q2U_ORIGIN1)
				to->origin[0] = MSG_ReadCoord ();
			if (bits & Q2U_ORIGIN2)
				to->origin[1] = MSG_ReadCoord ();
			if (bits & Q2U_ORIGIN3)
				to->origin[2] = MSG_ReadCoord ();

			if (bits & Q2U_OLDORIGIN)
			{
				to->u.q2.old_origin[0] = MSG_ReadCoord();
				to->u.q2.old_origin[1] = MSG_ReadCoord();
				to->u.q2.old_origin[2] = MSG_ReadCoord();
			}
		}
		else
		{
			if (bits & Q2U_ORIGIN1)
				to->origin[0] = MSG_ReadFloat ();
			if (bits & Q2U_ORIGIN2)
				to->origin[1] = MSG_ReadFloat ();
			if (bits & Q2U_ORIGIN3)
				to->origin[2] = MSG_ReadFloat ();

			if (bits & Q2U_OLDORIGIN)
			{
				to->u.q2.old_origin[0] = MSG_ReadFloat();
				to->u.q2.old_origin[1] = MSG_ReadFloat();
				to->u.q2.old_origin[2] = MSG_ReadFloat();
			}
		}

		if (bits & Q2U_ANGLE1)
			to->angles[0] = MSG_ReadFloat();
		if (bits & Q2U_ANGLE2)
			to->angles[1] = MSG_ReadFloat();
		if (bits & Q2U_ANGLE3)
			to->angles[2] = MSG_ReadFloat();
	}
	else
	{
		if (bits & Q2U_ORIGIN1)
			to->origin[0] = MSG_ReadCoord ();
		if (bits & Q2U_ORIGIN2)
			to->origin[1] = MSG_ReadCoord ();
		if (bits & Q2U_ORIGIN3)
			to->origin[2] = MSG_ReadCoord ();

		if ((bits & Q2UX_ANGLE16) && (net_message.prim.flags & NPQ2_ANG16))
		{
			if (bits & Q2U_ANGLE1)
				to->angles[0] = MSG_ReadAngle16();
			if (bits & Q2U_ANGLE2)
				to->angles[1] = MSG_ReadAngle16();
			if (bits & Q2U_ANGLE3)
				to->angles[2] = MSG_ReadAngle16();
		}
		else
		{
			if (bits & Q2U_ANGLE1)
				to->angles[0] = MSG_ReadAngle();
			if (bits & Q2U_ANGLE2)
				to->angles[1] = MSG_ReadAngle();
			if (bits & Q2U_ANGLE3)
				to->angles[2] = MSG_ReadAngle();
		}

		if (bits & Q2U_OLDORIGIN)
			MSG_ReadPos (to->u.q2.old_origin);
	}

	if (bits & Q2U_SOUND)
	{
		if (cls.protocol_q2 == PROTOCOL_VERSION_Q2EX)
		{
			to->u.q2.sound = MSG_ReadUInt16();
			/*to->u.q2.soundvol =*/ (to->u.q2.sound&0x4000)?MSG_ReadByte():255;
			/*to->u.q2.soundattn =*/ (to->u.q2.sound&0x8000)?MSG_ReadByte():3;
			to->u.q2.sound &= 0x3fff;
		}
		else if (bits & Q2UX_INDEX16)
			to->u.q2.sound = MSG_ReadUInt16();
		else
			to->u.q2.sound = MSG_ReadByte ();
	}

	if (bits & Q2U_EVENT)
		to->u.q2.event = MSG_ReadByte ();
	else
		to->u.q2.event = 0;

	if (cls.protocol_q2 != PROTOCOL_VERSION_Q2EX)
	{
		if (bits & Q2U_SOLID)
		{
			if (net_message.prim.flags & NPQ2_SOLID32)
				to->solidsize = MSG_ReadLong();
			else
				to->solidsize = MSG_ReadSize16 (&net_message);
		}
	}

	if (bits & Q2UEX_ALPHA)
	{	//255ths, with 0 the rerelease's default (opaque)
		to->trans = MSG_ReadByte();
		if (!to->trans)
			to->trans = 255;
	}
	if (bits & Q2UEX_SCALE)
	{	//16ths, with 0 the rerelease's default (1)
		to->scale = MSG_ReadByte();
		if (!to->scale)
			to->scale = 16;
	}
	if (bits & Q2UEX_INSTANCE)
		to->u.q2.instance = MSG_ReadByte();
	if (bits & Q2UEX_OWNER)
		to->u.q2.owner = MSG_ReadShort();
	if (bits & Q2UEX_OLDFRAME)
		to->u.q2.oldframe = MSG_ReadUInt16();
}

void CLQ2_ClearParticleState(void)
{
	int i;
	for (i = 0; i < MAX_Q2EDICTS; i++)
	{
		P_DelinkTrailstate(&cl_entities[i].trailstate);
	}
}

/*
==================
CL_DeltaEntity

Parses deltas from the given base and adds the resulting entity
to the current frame
==================
*/
static void CLQ2_DeltaEntity (q2frame_t *frame, int newnum, entity_state_t *old, uint64_t bits)
{
	q2centity_t	*ent;
	entity_state_t	*state;

	ent = &cl_entities[newnum];

	state = &clq2_parse_entities[cl.parse_entities & (MAX_PARSE_ENTITIES-1)];
	cl.parse_entities++;
	frame->num_entities++;

	CLQ2_ParseDelta (old, state, newnum, bits);

	// some data changes will force no lerping
	if (state->modelindex != ent->current.modelindex
		|| state->modelindex2 != ent->current.modelindex2
		|| state->u.q2.modelindex3 != ent->current.u.q2.modelindex3
		|| state->u.q2.modelindex4 != ent->current.u.q2.modelindex4
		|| fabs(state->origin[0] - ent->current.origin[0]) > 512
		|| fabs(state->origin[1] - ent->current.origin[1]) > 512
		|| fabs(state->origin[2] - ent->current.origin[2]) > 512
		|| state->u.q2.event == Q2EV_PLAYER_TELEPORT
		|| state->u.q2.event == Q2EV_OTHER_TELEPORT
		)
	{
		ent->serverframe = -99;
	}

	if (ent->serverframe != cl.q2frame.serverframe - 1)
	{	// wasn't in last update, so initialize some things
		// clear trailstate
		P_DelinkTrailstate(&ent->trailstate);

		// duplicate the current state so lerping doesn't hurt anything
		ent->prev = *state;
		ent->serverframe_created = cl.q2frame.serverframe;
		if (state->u.q2.event == Q2EV_OTHER_TELEPORT)
		{
			VectorCopy (state->origin, ent->prev.origin);
			VectorCopy (state->origin, ent->lerp_origin);
		}
		else
		{
			VectorCopy (state->u.q2.old_origin, ent->prev.origin);
			VectorCopy (state->u.q2.old_origin, ent->lerp_origin);
		}
		CLQ2FX_EntityReset(state->number, ent->lerp_origin);	//yquake2's trails start over from here

		ent->oldframe = state->frame;	//nothing to lerp from
		ent->oldframetime = cl.oldgametime;
		ent->stair_height = 0;
		ent->stair_time = 0;
	}
	else
	{	// shuffle the last state to previous
		ent->prev = ent->current;

		if (cl.q2svnetrate > 10 && (state->u.q2.renderfx & Q2EXRF_STAIRSTEP))
		{	//the rerelease's monsters, stepping faster than 10 a second: smooth the step out over 100ms (as Q2REPRO, after Q3A)
			float step = state->origin[2] - ent->current.origin[2];
			double delta = realtime - ent->stair_time;
			float old_step = (delta < 0.1)?ent->stair_height * (0.1 - delta) / 0.1:0;
			ent->stair_height = bound(-32, old_step + step, 32);
			ent->stair_time = realtime;
		}

		if (old->frame != state->frame)
		{
			ent->oldframe = old->frame;
			ent->oldframetime = cl.oldgametime;
		}
	}

	ent->serverframe = cl.q2frame.serverframe;
	ent->current = *state;
}

/*
==================
CL_ParsePacketEntities

An svc_packetentities has just been parsed, deal with the
rest of the data stream.
==================
*/
static void CLQ2_ParsePacketEntities (q2frame_t *oldframe, q2frame_t *newframe)
{
	unsigned int		newnum;
	quint64_t			bits;
	entity_state_t	*oldstate=NULL;
	unsigned int		oldindex, oldnum;

	cl.validsequence = cls.netchan.incoming_sequence;
	cl.ackedmovesequence = cls.netchan.incoming_acknowledged;

	cl.outframes[cl.ackedmovesequence&UPDATE_MASK].latency = realtime - cl.outframes[cl.ackedmovesequence&UPDATE_MASK].senttime;

	newframe->parse_entities = cl.parse_entities;
	newframe->num_entities = 0;

	// delta from the entities present in oldframe
	oldindex = 0;
	if (!oldframe)
		oldnum = 99999;
	else
	{
		if (oldindex >= oldframe->num_entities)
			oldnum = 99999;
		else
		{
			oldstate = &clq2_parse_entities[(oldframe->parse_entities+oldindex) & (MAX_PARSE_ENTITIES-1)];
			oldnum = oldstate->number;
		}
	}

	while (1)
	{
		newnum = CLQ2_ParseEntityBits (&bits);
		if (newnum >= MAX_Q2EDICTS)
			Host_EndGame ("CL_ParsePacketEntities: bad number:%i", newnum);

		if (MSG_GetReadCount() > net_message.cursize)
			Host_EndGame ("CL_ParsePacketEntities: end of message");

		if (!newnum)
			break;

		while (oldnum < newnum)
		{	// one or more entities from the old packet are unchanged
			if (cl_shownet.ival == 3)
				Con_Printf ("%i:   unchanged: %i\n", MSG_GetReadCount(), oldnum);
			CLQ2_DeltaEntity (newframe, oldnum, oldstate, 0);
			
			oldindex++;

			if (!oldframe || oldindex >= oldframe->num_entities)
				oldnum = 99999;
			else
			{
				oldstate = &clq2_parse_entities[(oldframe->parse_entities+oldindex) & (MAX_PARSE_ENTITIES-1)];
				oldnum = oldstate->number;
			}
		}

		if (bits & Q2U_REMOVE)
		{	// the entity present in oldframe is not in the current frame
			if (cl_shownet.ival == 3)
				Con_Printf ("%3i:   remove: %i\n", MSG_GetReadCount(), newnum);
			if (oldnum != newnum)
				Con_Printf ("U_REMOVE: oldnum != newnum\n");

			oldindex++;

			if (!oldframe || oldindex >= oldframe->num_entities)
				oldnum = 99999;
			else
			{
				oldstate = &clq2_parse_entities[(oldframe->parse_entities+oldindex) & (MAX_PARSE_ENTITIES-1)];
				oldnum = oldstate->number;
			}
			continue;
		}

		if (oldnum == newnum)
		{	// delta from previous state
			if (cl_shownet.ival == 3)
				Con_Printf ("%3i:   delta: %i\n", MSG_GetReadCount(), newnum);
			CLQ2_DeltaEntity (newframe, newnum, oldstate, bits);

			oldindex++;

			if (oldindex >= oldframe->num_entities)
				oldnum = 99999;
			else
			{
				oldstate = &clq2_parse_entities[(oldframe->parse_entities+oldindex) & (MAX_PARSE_ENTITIES-1)];
				oldnum = oldstate->number;
			}
			continue;
		}

		if (oldnum > newnum)
		{	// delta from baseline
			if (cl_shownet.ival == 3)
				Con_Printf ("%3i:   baseline: %i\n", MSG_GetReadCount(), newnum);
			CLQ2_DeltaEntity (newframe, newnum, &cl_entities[newnum].baseline, bits);
			continue;
		}

	}

	// any remaining entities in the old frame are copied over
	while (oldnum != 99999)
	{	// one or more entities from the old packet are unchanged
		if (cl_shownet.ival == 3)
			Con_Printf ("%3i:   unchanged: %i\n", MSG_GetReadCount(), oldnum);
		CLQ2_DeltaEntity (newframe, oldnum, oldstate, 0);
		
		oldindex++;

		if (oldindex >= oldframe->num_entities)
			oldnum = 99999;
		else
		{
			oldstate = &clq2_parse_entities[(oldframe->parse_entities+oldindex) & (MAX_PARSE_ENTITIES-1)];
			oldnum = oldstate->number;
		}
	}
}

void CLQ2_ParseBaseline (void)
{
	entity_state_t	*es;
	quint64_t				bits;
	int				newnum;

	newnum = CLQ2_ParseEntityBits (&bits);
	es = &cl_entities[newnum].baseline;
	CLQ2_ParseDelta (&nullentitystate, es, newnum, bits);
}


/*
===================
CL_ParsePlayerstate
===================
*/
void CLQ2_ParsePlayerstate (int seat, q2frame_t *oldframe, q2frame_t *newframe, int extflags)
{
	int			flags;
	q2player_state_t	*state;
	int			i;
	int			statbits;
	qboolean q2e = cls.protocol_q2==PROTOCOL_VERSION_Q2EX;
	q2expmove_state_t *expm;	//Q2EX's pmove as it comes, for the rerelease's cgame

	state = &newframe->seat[seat].playerstate;
	expm = &newframe->seat[seat].expmove;

	// clear to old value before delta parsing
	if (oldframe)
	{
		*state = oldframe->seat[seat].playerstate;
		memcpy(newframe->seat[seat].exstats, oldframe->seat[seat].exstats, sizeof(newframe->seat[seat].exstats));
		*expm = oldframe->seat[seat].expmove;
		newframe->seat[seat].clientnum = oldframe->seat[seat].clientnum;
	}
	else
	{
		memset (state, 0, sizeof(*state));
		memset(newframe->seat[seat].exstats, 0, sizeof(newframe->seat[seat].exstats));
		memset(expm, 0, sizeof(*expm));
		newframe->seat[seat].clientnum = cl.playerview[seat].playernum;
	}
	expm->valid = q2e;

	flags = MSG_ReadUInt16 ();
	if (flags & Q2PS_EXTRABITS)
		flags |= (q2e?MSG_ReadUInt16():MSG_ReadByte())<<16;

	//
	// parse the pmove_state_t
	//
	if (flags & Q2PS_M_TYPE)
	{
		state->pmove.pm_type = MSG_ReadByte ();
		if (q2e)
		{	//sigh... q2e added some extra pmove types that we don't support.
			expm->pm_type = state->pmove.pm_type;
			switch((int)state->pmove.pm_type)
			{
			case Q2EPM_NORMAL: state->pmove.pm_type = Q2PM_NORMAL; break;
			case Q2EPM_GRAPPLE: state->pmove.pm_type = Q2PM_NORMAL; break;	//FIXME: not supported
			case Q2EPM_SPECTATOR: state->pmove.pm_type = Q2PM_SPECTATOR; break;
			case Q2EPM_SPECTATOR2: state->pmove.pm_type = Q2PM_SPECTATOR; break;	//FIXME: not supported.
			case Q2EPM_DEAD: state->pmove.pm_type = Q2PM_DEAD; break;
			case Q2EPM_GIB: state->pmove.pm_type = Q2PM_GIB; break;
			case Q2EPM_FREEZE: state->pmove.pm_type = Q2PM_FREEZE; break;
			}
		}
	}

	if (flags & Q2PS_M_ORIGIN)
	{
		if (q2e)
		{
			state->pmove.origin[0] = (expm->origin[0] = MSG_ReadFloat ())*8;
			state->pmove.origin[1] = (expm->origin[1] = MSG_ReadFloat ())*8;
			if (extflags & Q2PSX_OLD)
				state->pmove.origin[2] = (expm->origin[2] = MSG_ReadFloat ())*8;
		}
		else
		{
			state->pmove.origin[0] = MSG_ReadShort ();
			state->pmove.origin[1] = MSG_ReadShort ();
			if (extflags & Q2PSX_OLD)
				state->pmove.origin[2] = MSG_ReadShort ();
		}
	}
	if (extflags & Q2PSX_M_ORIGIN2)
		state->pmove.origin[2] = MSG_ReadShort ();

	if (flags & Q2PS_M_VELOCITY)
	{
		if (q2e)
		{
			state->pmove.velocity[0] = (expm->velocity[0] = MSG_ReadFloat ())*8;
			state->pmove.velocity[1] = (expm->velocity[1] = MSG_ReadFloat ())*8;
			if (extflags & Q2PSX_OLD)
				state->pmove.velocity[2] = (expm->velocity[2] = MSG_ReadFloat ())*8;
		}
		else
		{
			state->pmove.velocity[0] = MSG_ReadShort ();
			state->pmove.velocity[1] = MSG_ReadShort ();
			if (extflags & Q2PSX_OLD)
				state->pmove.velocity[2] = MSG_ReadShort ();
		}
	}
	if (extflags & Q2PSX_M_VELOCITY2)
		state->pmove.velocity[2] = MSG_ReadShort ();

	if (flags & Q2PS_M_TIME)
	{
		if (q2e)
		{	//Q2EX's are in ms, the original's in 8ms units
			expm->pm_time = MSG_ReadUInt16();
			state->pmove.pm_time = min(255, expm->pm_time/8);
		}
		else
			state->pmove.pm_time = MSG_ReadByte ();
	}

	if (flags & Q2PS_M_FLAGS)
	{
		if (q2e)
			state->pmove.pm_flags = expm->pm_flags = MSG_ReadUInt16();
		else
			state->pmove.pm_flags = MSG_ReadByte ();
	}

	if (flags & Q2PS_M_GRAVITY)
		state->pmove.gravity = expm->gravity = MSG_ReadShort ();

	if (flags & Q2PS_M_DELTA_ANGLES)
	{
		if (q2e)
		{
			state->pmove.delta_angles[0] = ANGLE2SHORT(expm->delta_angles[0] = MSG_ReadFloat ());
			state->pmove.delta_angles[1] = ANGLE2SHORT(expm->delta_angles[1] = MSG_ReadFloat ());
			state->pmove.delta_angles[2] = ANGLE2SHORT(expm->delta_angles[2] = MSG_ReadFloat ());
		}
		else
		{
			state->pmove.delta_angles[0] = MSG_ReadShort ();
			state->pmove.delta_angles[1] = MSG_ReadShort ();
			state->pmove.delta_angles[2] = MSG_ReadShort ();
		}
	}

//	if (cl.attractloop)
//		state->pmove.pm_type = Q2PM_FREEZE;		// demo playback

	//
	// parse the rest of the player_state_t
	//
	if (flags & Q2PS_VIEWOFFSET)
	{
		if (q2e)
		{
			state->viewoffset[0] = MSG_ReadShort () * (1/16.f);
			state->viewoffset[1] = MSG_ReadShort () * (1/16.f);
			state->viewoffset[2] = MSG_ReadShort () * (1/16.f);
			expm->viewheight = MSG_ReadChar();
			state->viewoffset[2] += expm->viewheight;
		}
		else
		{
			state->viewoffset[0] = MSG_ReadChar () * 0.25;
			state->viewoffset[1] = MSG_ReadChar () * 0.25;
			state->viewoffset[2] = MSG_ReadChar () * 0.25;
		}
	}

	if (flags & Q2PS_VIEWANGLES)
	{
		if (q2e)
		{
			state->viewangles[0] = MSG_ReadFloat ();
			state->viewangles[1] = MSG_ReadFloat ();
			if (extflags & Q2PSX_OLD)
				state->viewangles[2] = MSG_ReadFloat ();
		}
		else
		{
			state->viewangles[0] = MSG_ReadAngle16 ();
			state->viewangles[1] = MSG_ReadAngle16 ();
			if (extflags & Q2PSX_OLD)
				state->viewangles[2] = MSG_ReadAngle16 ();
		}
	}
	if (extflags & Q2PSX_VIEWANGLE2)
		state->viewangles[2] = MSG_ReadAngle16 ();

	if (flags & Q2PS_KICKANGLES)
	{
		if (q2e)
		{
			state->kick_angles[0] = MSG_ReadShort () * (1/1024.f);
			state->kick_angles[1] = MSG_ReadShort () * (1/1024.f);
			state->kick_angles[2] = MSG_ReadShort () * (1/1024.f);
		}
		else
		{
			state->kick_angles[0] = MSG_ReadChar () * 0.25;
			state->kick_angles[1] = MSG_ReadChar () * 0.25;
			state->kick_angles[2] = MSG_ReadChar () * 0.25;
		}
	}

	if (flags & Q2PS_WEAPONINDEX)
	{
		if (q2e)
		{
			state->gunindex = MSG_ReadUInt16 ();
			//state->gunskin = state->gunindex>>13
			state->gunindex &= 0x1fff;
		}
		else
		{
			if (flags & Q2FTEPS_INDEX16)
				state->gunindex = MSG_ReadShort ();
			else
				state->gunindex = MSG_ReadByte ();
		}
	}

	if (flags & Q2PS_WEAPONFRAME)
	{
		if (q2e)
		{
			unsigned int fl = MSG_ReadUInt16 ();
			state->gunframe = fl & 0x1ff;
			if (fl & (1<< 9)) state->gunoffset[0] = MSG_ReadFloat ();
			if (fl & (1<<10)) state->gunoffset[1] = MSG_ReadFloat ();
			if (fl & (1<<11)) state->gunoffset[2] = MSG_ReadFloat ();
			if (fl & (1<<12)) state->gunangles[0] = MSG_ReadFloat ();
			if (fl & (1<<13)) state->gunangles[1] = MSG_ReadFloat ();
			if (fl & (1<<14)) state->gunangles[2] = MSG_ReadFloat ();
			if (fl & (1<<15)) /*state->gunangles[2] =*/ MSG_ReadByte ();
		}
		else
		{
			if (flags & Q2FTEPS_INDEX16)
				state->gunframe = MSG_ReadShort ();
			else
				state->gunframe = MSG_ReadByte ();
			if (extflags & Q2PSX_OLD)
			{
				state->gunoffset[0] = MSG_ReadChar ()*0.25;
				state->gunoffset[1] = MSG_ReadChar ()*0.25;
				state->gunoffset[2] = MSG_ReadChar ()*0.25;
				state->gunangles[0] = MSG_ReadChar ()*0.25;
				state->gunangles[1] = MSG_ReadChar ()*0.25;
				state->gunangles[2] = MSG_ReadChar ()*0.25;
			}
		}
	}
	if (extflags & Q2PSX_GUNOFFSET)
	{
		state->gunoffset[0] = MSG_ReadChar ()*0.25;
		state->gunoffset[1] = MSG_ReadChar ()*0.25;
		state->gunoffset[2] = MSG_ReadChar ()*0.25;
	}
	if (extflags & Q2PSX_GUNANGLES)
	{
		state->gunangles[0] = MSG_ReadChar ()*0.25;
		state->gunangles[1] = MSG_ReadChar ()*0.25;
		state->gunangles[2] = MSG_ReadChar ()*0.25;
	}

	if (flags & Q2PS_BLEND)
	{
		state->blend[0] = MSG_ReadByte ()/255.0;
		state->blend[1] = MSG_ReadByte ()/255.0;
		state->blend[2] = MSG_ReadByte ()/255.0;
		state->blend[3] = MSG_ReadByte ()/255.0;
	}

	if (flags & Q2PS_FOV)
		state->fov = MSG_ReadByte ();

	if (flags & Q2PS_RDFLAGS)
		state->rdflags = MSG_ReadByte ();

	// parse stats
	if (extflags & (Q2PSX_OLD|Q2PSX_STATS))
		statbits = MSG_ReadLong ();
	else
		statbits = 0;
	if (statbits)
	{
		for (i=0 ; i<min(32,Q2MAX_STATS) ; i++)
			if (statbits & (1u<<i) )
				state->stats[i] = MSG_ReadShort();
	}
	if (q2e)
	{
		// parse stats
		statbits = MSG_ReadLong ();
		if (statbits)
		{	//stats 32-63
			for (i=0 ; i<32 ; i++)
				if (statbits & (1u<<i) )
					newframe->seat[seat].exstats[i] = MSG_ReadShort();
		}

		if (flags & Q2EXPS_DAMAGEBLEND)
		{
			/*state->damageblend[0] = (1/255.f) */ MSG_ReadByte ();
			/*state->damageblend[1] = (1/255.f) */ MSG_ReadByte ();
			/*state->damageblend[2] = (1/255.f) */ MSG_ReadByte ();
			/*state->damageblend[3] = (1/255.f) */ MSG_ReadByte ();
		}
		if (flags & Q2EXPS_TEAMID)
			MSG_ReadByte();
	}
	else
	{
		if ((extflags & Q2PSX_CLIENTNUM) || (flags & Q2FTEPS_CLIENTNUM))
			newframe->seat[seat].clientnum = MSG_ReadByte();
	}
}


/*
==================
CL_FireEntityEvents

==================
*/
void CLQ2_FireEntityEvents (q2frame_t *frame)
{
	entity_state_t		*s1;
	int					pnum, num;

	for (pnum = 0 ; pnum<frame->num_entities ; pnum++)
	{
		num = (frame->parse_entities + pnum)&(MAX_PARSE_ENTITIES-1);
		s1 = &clq2_parse_entities[num];
		//yquake2's (clq2_fx.c): footsteps, falls, teleports, item respawns
		if (s1->u.q2.event && !CLQ2_StepEvent(s1))	//(the rerelease's steps: on what)
			CLQ2FX_EntityEvent (s1->number, s1->u.q2.event, s1->origin);

		// EF_TELEPORTER acts like an event, but is not cleared each frame
		if (s1->effects & Q2EF_TELEPORTER)
			CLQ2FX_TeleporterParticles (s1->origin);
		// the rerelease's two more of those
		if (s1->u.q2.effects2 & Q2EXEF2_TELEPORTER2)
			CLQ2FX_RereleaseParticles (0, s1->origin);
		if (s1->u.q2.effects2 & Q2EXEF2_BARREL_EXPLODING)
			CLQ2FX_RereleaseParticles (2, s1->origin);
	}
}

void CLR1Q2_ParsePlayerUpdate(void)
{
	unsigned int framenum = MSG_ReadLong();
	q2frame_t	*frame = &cl.q2frames[framenum & Q2UPDATE_MASK];
	int seat;
	if (frame->serverframe != framenum)
		Con_DPrintf("svcr1q2_playerupdate: stale frame\n");
	else if (!frame->valid)
		Con_DPrintf("svcr1q2_playerupdate: invalid frame\n");	//excrement happens.
	else
	{
		int pnum;
		vec3_t neworg;
		entity_state_t *st;
		for (pnum = 0; pnum < frame->num_entities; pnum++)
		{
			st = &clq2_parse_entities[(frame->parse_entities+pnum) & (MAX_PARSE_ENTITIES-1)];

			//I don't like how r1q2 does its maxclients, so I'm just going to go on message size instead
			if (MSG_GetReadCount() == net_message.cursize)
				break;

			//the local client(s) is not included, thanks to prediction covering that.
			for (seat = 0; seat < cl.splitclients; seat++)
			{
				if (st->number == cl.playerview[0].playernum+1)
					break;
			}
			if (seat != cl.splitclients)
				continue;

			if (st->number != 1)
				continue;

			//FIXME: handle this, with lerping and stuff.
			MSG_ReadPos(neworg);
		}

		//just for sanity's sake
		if (MSG_GetReadCount() != net_message.cursize)
			msg_badread = true;
	}
	//this should be the only/last thing in these packets, because if it isn't then we're screwed when a packet got lost
	net_message.currentbit = net_message.cursize<<3;
}

#define SHOWNET(x) if(cl_shownet.value>=2)Con_Printf ("%3i:%s\n", MSG_GetReadCount()-1, x);
/*
================
CL_ParseFrame
================
*/
void CLQ2_ParseFrame (int extrabits)
{
	int			cmd;
	int			len;
	q2frame_t		*old;
	int i,j, chokecount;
	static float throttle;

	memset (&cl.q2frame, 0, sizeof(cl.q2frame));

#if 0
	CLQ2_ClearProjectiles(); // clear projectiles for new frame
#endif

	if (cls.protocol_q2 == PROTOCOL_VERSION_R1Q2 || cls.protocol_q2 == PROTOCOL_VERSION_Q2PRO)
	{
		unsigned int bits = MSG_ReadLong();
		cl.q2frame.serverframe = bits & 0x07ffffff;
		i = bits >> 27;
		if (i == 31)
			cl.q2frame.deltaframe = -1;
		else
			cl.q2frame.deltaframe = cl.q2frame.serverframe - i;
		bits = MSG_ReadByte();
		chokecount = bits & 0xf;
		extrabits = (extrabits<<4) | (bits>>4);
	}
	else
	{
		cl.q2frame.serverframe = MSG_ReadLong ();
		cl.q2frame.deltaframe = MSG_ReadLong ();
		if (cls.protocol_q2 > 26)
			chokecount = MSG_ReadByte ();
		else
			chokecount = 0;

		extrabits = Q2PSX_OLD;
	}

	cl.q2frame.servertime = cl.q2frame.serverframe*(1000/cl.q2svnetrate);

	cl.oldgametime = cl.gametime;
	cl.oldgametimemark = cl.gametimemark;
	cl.gametime = cl.q2frame.servertime/1000.;
	cl.gametimemark = realtime;

	for (j=0 ; j<chokecount ; j++)
		cl.outframes[ (cls.netchan.incoming_acknowledged-1-j)&UPDATE_MASK ].latency = -2;

	if (cl_shownet.value == 3)
		Con_Printf ("   frame:%i  delta:%i\n", cl.q2frame.serverframe, cl.q2frame.deltaframe);

	// If the frame is delta compressed from data that we
	// no longer have available, we must suck up the rest of
	// the frame, but not use it, then ask for a non-compressed
	// message 
	if (cl.q2frame.deltaframe <= 0)
	{
		cl.q2frame.valid = true;		// uncompressed frame
		old = NULL;
		cls.demohadkeyframe = true;	//yay! all is right with the world!
	}
	else
	{
		old = &cl.q2frames[cl.q2frame.deltaframe & Q2UPDATE_MASK];
		if (!old->valid)
		{	// should never happen
			Con_ThrottlePrintf (&throttle, 0, CON_WARNING"Delta from invalid frame (not supposed to happen!).\n");
			old = NULL;
		}
		else if (old->serverframe != cl.q2frame.deltaframe)
		{	// The frame that the server did the delta from
			// is too old, so we can't reconstruct it properly.
			Con_ThrottlePrintf (&throttle, 0, CON_WARNING"Delta frame too old.\n");
		}
		else if (cl.parse_entities - old->parse_entities > MAX_PARSE_ENTITIES-128)
		{
			Con_ThrottlePrintf (&throttle, 0, CON_WARNING"Delta parse_entities too old.\n");
		}
		else
			cl.q2frame.valid = true;	// valid delta parse
	}

	// clamp time 
	if (cl.time > cl.q2frame.servertime/1000.0)
		cl.time = cl.q2frame.servertime/1000.0;
	else if (cl.time < (cl.q2frame.servertime - 1000/cl.q2svnetrate)/1000.0)
		cl.time = (cl.q2frame.servertime - 1000/cl.q2svnetrate)/1000.0;

	if (cls.protocol_q2 == PROTOCOL_VERSION_Q2EX)
	{
		for (i = 0; i<cl.splitclients; i++)
		{
			len = MSG_ReadByte ();
			if (len > countof(cl.q2frame.seat[i].areabits))
				Host_EndGame ("CL_ParseFrame: too many areas");
			MSG_ReadData (&cl.q2frame.seat[i].areabits, len);

			cmd = MSG_ReadByte ();
			if (cmd != svcq2_playerinfo)
				Host_EndGame ("CL_ParseFrame: not playerinfo");
			SHOWNET("svcq2_playerinfo");
			CLQ2_ParsePlayerstate (i, old, &cl.q2frame, extrabits);
		}
		cmd = MSG_ReadByte ();
//		SHOWNET(svc_strings[cmd]);
		if (cmd != svcq2_packetentities)
			Host_EndGame ("CL_ParseFrame: not packetentities");
		SHOWNET("svcq2_packetentities");
	}
	else
	{
		// read areabits
		len = MSG_ReadByte ();
		if (len > MAX_Q2MAP_AREAS/8)
			Host_EndGame ("CL_ParseFrame: too many areas");
		MSG_ReadData (&cl.q2frame.seat[0].areabits, len);

		// normally playerstate then packet entities
		//in splitscreen we may have multiple player states, one per player.
		if (cls.protocol_q2 != PROTOCOL_VERSION_R1Q2 && cls.protocol_q2 != PROTOCOL_VERSION_Q2PRO)
		{
			for (cl.splitclients = 0; ; )
			{
				cmd = MSG_ReadByte ();
	//			SHOWNET(svc_strings[cmd]);
				if (cmd == svcq2_playerinfo && cl.splitclients < MAX_SPLITS)
				{
					if (cl.splitclients != 0)
						memcpy(cl.q2frame.seat[cl.splitclients].areabits, cl.q2frame.seat[0].areabits, len);
					SHOWNET("svcq2_playerinfo");
					CLQ2_ParsePlayerstate (cl.splitclients++, old, &cl.q2frame, extrabits);
				}
				else
					break;
			}
			if (!cl.splitclients)
				Host_EndGame ("CL_ParseFrame: no playerinfo");
			if (cmd != svcq2_packetentities)
				Host_EndGame ("CL_ParseFrame: not packetentities");
			SHOWNET("svcq2_packetentities");
		}
		else
		{
			cl.splitclients = 1;
			CLQ2_ParsePlayerstate (0, old, &cl.q2frame, extrabits);
		}
	}
	CLQ2_ParsePacketEntities (old, &cl.q2frame);

	for (cmd = 0; cmd < MAX_SPLITS; cmd++)
		cl.playerview[cmd].viewentity = cl.q2frame.seat[cmd].clientnum+1;

	// save the frame off in the backup array for later delta comparisons
	cl.q2frames[cl.q2frame.serverframe & Q2UPDATE_MASK] = cl.q2frame;

	cl.parsecount = cl.q2frame.serverframe;
	if (cl.q2frame.valid)
	{
		// getting a valid frame message ends the connection process
		if (cls.state != ca_active)
		{
			CL_MakeActive("Quake2");

//			cl.force_refdef = true;
//			cl.predicted_origin[0] = cl.q2frame.playerstate[0].pmove.origin[0]*0.125;
//			cl.predicted_origin[1] = cl.q2frame.playerstate[0].pmove.origin[1]*0.125;
//			cl.predicted_origin[2] = cl.q2frame.playerstate[0].pmove.origin[2]*0.125;
//			VectorCopy (cl.q2frame.playerstate[0].viewangles, cl.predicted_angles);
//			if (cls.disable_servercount != cl.servercount
//				&& cl.refresh_prepped)
				SCR_EndLoadingPlaque ();	// get rid of loading plaque
		}
//		cl.sound_prepped = true;	// can start mixing ambient sounds
	
		// fire entity events
		CLQ2_FireEntityEvents (&cl.q2frame);
#ifdef Q2BSPS
		CLQ2_CheckPredictionError ();
#endif

		cl.validsequence = cl.q2frame.serverframe;
	}
}

/*
==========================================================================

INTERPOLATE BETWEEN FRAMES TO GET RENDERING PARMS

==========================================================================
*/
/*
struct model_s *S_RegisterSexedModel (entity_state_t *ent, char *base)
{
	int				n;
	char			*p;
	struct model_s	*mdl;
	char			model[MAX_QPATH];
	char			buffer[MAX_QPATH];

	// determine what model the client is using
	model[0] = 0;
	n = CS_PLAYERSKINS + ent->number - 1;
	if (cl.configstrings[n][0])
	{
		p = strchr(cl.configstrings[n], '\\');
		if (p)
		{
			p += 1;
			strcpy(model, p);
			p = strchr(model, '/');
			if (p)
				*p = 0;
		}
	}
	// if we can't figure it out, they're male
	if (!model[0])
		strcpy(model, "male");

	Com_sprintf (buffer, sizeof(buffer), "players/%s/%s", model, base+1);
	mdl = re.RegisterModel(buffer);
	if (!mdl) {
		// not found, try default weapon model
		Com_sprintf (buffer, sizeof(buffer), "players/%s/weapon.md2", model);
		mdl = re.RegisterModel(buffer);
		if (!mdl) {
			// no, revert to the male model
			Com_sprintf (buffer, sizeof(buffer), "players/%s/%s", "male", base+1);
			mdl = re.RegisterModel(buffer);
			if (!mdl) {
				// last try, default male weapon.md2
				Com_sprintf (buffer, sizeof(buffer), "players/male/weapon.md2");
				mdl = re.RegisterModel(buffer);
			}
		} 
	}

	return mdl;
}

*/

/*
yquake2's s_bsp_soundpos: the sounds of a brush model (a door, a lift, a train) come from the middle of it (1)
or from its point nearest the listener (2), not from the entity's origin -- which for most of them is the
world's origin, so their loop sounds played from the middle of the map (0, as vanilla Quake 2 does).
*/
cvar_t s_bsp_soundpos = CVARD("s_bsp_soundpos", "1", "Quake 2: where a brush model's (a door's, a lift's) sounds come from: 0 its origin (vanilla, usually the middle of the map), 1 its middle, 2 its point nearest the listener.");
qboolean CLQ2_BSPSoundOrigin(int entnum, vec3_t org)
{
	q2centity_t *ent;
	model_t *mod;
	int mi, i;
	vec3_t mins, maxs;
	float *listener = cl.playerview[0].audio.origin;

	if (!s_bsp_soundpos.ival || entnum <= 0 || entnum >= MAX_Q2EDICTS)
		return false;
	ent = &cl_entities[entnum];
	mi = ent->current.modelindex;
	mod = (mi > 0 && mi < MAX_PRECACHE_MODELS) ? cl.model_precache[mi] : NULL;
	if (!mod || *mod->name != '*' || mod->loadstate != MLS_LOADED)
		return false;	//only the map's own brush models, as yquake2's cl.model_clip

	VectorAdd(mod->mins, ent->lerp_origin, mins);
	VectorAdd(mod->maxs, ent->lerp_origin, maxs);
	for (i = 0; i < 3; i++)
	{
		if (s_bsp_soundpos.ival < 2)
			org[i] = (mins[i] + maxs[i]) * 0.5;
		else	//ClosestPointOnBounds
			org[i] = bound(mins[i], listener[i], maxs[i]);
	}
	return true;
}

//returns a list of all the ents currently trying to play a sound (volumes: 0-1, attenuations: inaudible at 1000/it,
//from distmins: full volume to there).
unsigned int CLQ2_GatherSounds(vec3_t *positions, unsigned int *entnums, sfx_t **sounds, float *volumes, float *attenuations, float *distmins, unsigned int max)
{
	entity_state_t		*s1;
	sfx_t *sfx;
	unsigned int pnum;
	unsigned int count = 0;
	q2frame_t *frame = &cl.q2frame;
	for (pnum = 0 ; pnum<frame->num_entities ; pnum++)
	{
		s1 = &clq2_parse_entities[(frame->parse_entities+pnum)&(MAX_PARSE_ENTITIES-1)];
		if (s1->u.q2.sound > 0 && s1->u.q2.sound < MAX_PRECACHE_SOUNDS)
		{
			sfx = cl.sound_precache[s1->u.q2.sound];
			if (sfx)
			{
				if (count == max)
				{
					Con_DPrintf("Exceeded limit of %d looped sounds\n", max);
					break;
				}
				//fixme: sexed sounds
				entnums[count] = s1->number;
				if (!CLQ2_BSPSoundOrigin(s1->number, positions[count]))
					VectorCopy(s1->origin, positions[count]);
				sounds[count] = sfx;
				volumes[count] = 1;
				attenuations[count] = 3;	//ATTN_STATIC
				distmins[count] = 0;
#ifdef DAIKATANA
				if (CLDK_IsDaikatana())	//its loops have their own volume and distances
					CLDK_LoopSoundParams(s1->number, &volumes[count], &attenuations[count], &distmins[count]);
#endif
				count++;
			}
		}
	}
	return count;
}


static struct q2ex_rtlight_s
{
	qboolean dirty;
	int key, type, res, style;
	float radius, intensity, fade[2], cone, conedir[3];
	vec3_t axis[3];
	vec3_t angles;
	unsigned int rgba;	//its entity's skinnum: the rerelease's game puts the light's _color there (0: white)
	int seen;			//q2ex_rtlight_frame its entity was last in
} q2ex_rtlight[Q2EXMAX_RTLIGHTS];
static int q2ex_rtlight_frame;	//counts CLQ2_AddPacketEntities: a light whose entity is not in the frame is off
void CLQ2EX_ParseLightConfigString(int i, const char *s)
{
	struct q2ex_rtlight_s *l = &q2ex_rtlight[i];
	l->dirty = true;
	s = COM_ParseToken(s, ";");	l->key			= atoi(com_token);	s = COM_ParseToken(s, ";");
	s = COM_ParseToken(s, ";");	l->type			= atoi(com_token);	s = COM_ParseToken(s, ";");
	s = COM_ParseToken(s, ";");	l->radius		= atof(com_token);	s = COM_ParseToken(s, ";");
	s = COM_ParseToken(s, ";");	l->res			= atoi(com_token);	s = COM_ParseToken(s, ";");
	s = COM_ParseToken(s, ";");	l->intensity	= atof(com_token);	s = COM_ParseToken(s, ";");
	s = COM_ParseToken(s, ";");	l->fade[0]		= atof(com_token);	s = COM_ParseToken(s, ";");
	s = COM_ParseToken(s, ";");	l->fade[1]		= atof(com_token);	s = COM_ParseToken(s, ";");
	s = COM_ParseToken(s, ";");	l->style		= atoi(com_token);	s = COM_ParseToken(s, ";");
	s = COM_ParseToken(s, ";");	l->cone			= atof(com_token);	s = COM_ParseToken(s, ";");
	s = COM_ParseToken(s, ";");	l->axis[0][0]	= atof(com_token);	s = COM_ParseToken(s, ";");
	s = COM_ParseToken(s, ";");	l->axis[0][1]	= atof(com_token);	s = COM_ParseToken(s, ";");
	s = COM_ParseToken(s, ";");	l->axis[0][2]	= atof(com_token);	s = COM_ParseToken(s, ";");

	//faff around with angles and stuff.
	VectorVectors(l->axis[0], l->axis[1], l->axis[2]);
	VectorNegate(l->axis[1], l->axis[1]);
	VectorAngles(l->axis[0], l->axis[2], l->angles, false);
}
static dlight_t *CLQ2EX_FindRTLight(int key)
{
	int i;
	dlight_t *dl = cl_dlights+rtlights_first;
	for (i=rtlights_first ; i<rtlights_max ; i++, dl++)
	{
		if (dl->key == key)
			return dl;
	}
	return NULL;
}
//a shadow light's entity in this frame (the rerelease's dynamic_light: RF_CASTSHADOW, CS_SHADOWLIGHTS): its light, coloured
//by the entity's skinnum (RGBA, as Q2REPRO's CL_AddShadowLights)
static void CLQ2EX_UpdateRTLight(int key, vec3_t org, unsigned int rgba)
{
	int			i;
	dlight_t	*dl;
	struct q2ex_rtlight_s *l;

	for (i = 0, l = q2ex_rtlight; i < countof(q2ex_rtlight); i++, l++)
	{
		if (l->key == key)
		{	//okay, we found the configstring entry...

			//see if its current or not...
			dl = CLQ2EX_FindRTLight(key);
			if (!dl)
			{
				dl = CL_AllocSlight();
				dl->key = key;
				l->dirty = true;
			}
			l->seen = q2ex_rtlight_frame;
			dl->flags |= LFLAG_NORMALMODE|LFLAG_REALTIMEMODE|LFLAG_LINEARFALLOFF;	//(on again if its entity was hidden: the game's use toggles it; KEX's linear falloff)
			if (l->rgba != rgba)
			{
				l->rgba = rgba;
				l->dirty = true;
			}

			if (!l->dirty)
			{
				if (!VectorCompare(org, dl->origin))
				{	//it moved :( keep the light tracking the ent and invalidate the cache.
					VectorCopy(org, dl->origin);
					dl->rebuildcache = true;
				}
			}
			else
			{	//LOTS of stuff changed.
				vec3_t rgb = {1,1,1};
				l->dirty = false;
				dl->flags |= LFLAG_FORCECACHE;

				//ent info
				VectorCopy(org, dl->origin);

				//config string info
				dl->radius = l->radius;
//FIXME: we're not using the suggested resolution.
				if (rgba)
					VectorSet(rgb, ((rgba>>24)&0xff)/255.0, ((rgba>>16)&0xff)/255.0, ((rgba>>8)&0xff)/255.0);
				VectorScale(rgb, l->intensity, dl->color);
				dl->fade[0] = l->fade[0];
				dl->fade[1] = l->fade[1];
				dl->style = l->style;

				if (l->type)
					dl->fov = l->cone*2;
				VectorCopy(l->angles, dl->angles);
				VectorCopy(l->axis[0], dl->axis[0]);
				VectorCopy(l->axis[1], dl->axis[1]);
				VectorCopy(l->axis[2], dl->axis[2]);

				dl->corona = 0;
				dl->rebuildcache = true;
			}
			break;
		}
	}
}
//shadow lights whose entity was not in this frame (hidden by the game, or out of the PHS) are off until it is again
static void CLQ2EX_HideUnseenRTLights(void)
{
	int i;
	dlight_t *dl;
	struct q2ex_rtlight_s *l;
	for (i = 0, l = q2ex_rtlight; i < countof(q2ex_rtlight); i++, l++)
	{
		if (l->key > 0 && l->seen != q2ex_rtlight_frame && (dl = CLQ2EX_FindRTLight(l->key)))
			dl->flags &= ~(LFLAG_NORMALMODE|LFLAG_REALTIMEMODE);
	}
}

//whether the eye sees a point: the world's windows and the surfaces it can be seen through (alpha tested grates,
//translucent glass) don't hide it, though a solid trace stops at them
static qboolean CLQ2EX_PointSeen(model_t *mod, const float *eye, const float *org)
{
	trace_t tr;
	vec3_t start, dir;
	int i;
	VectorCopy(eye, start);
	VectorSubtract(org, eye, dir);
	VectorNormalize(dir);
	for (i = 0; i < 8; i++)
	{
		memset(&tr, 0, sizeof(tr));
		mod->funcs.NativeTrace(mod, 0, NULLFRAMESTATE, NULL, start, org, vec3_origin, vec3_origin, false, FTECONTENTS_SOLID|Q2CONTENTS_WINDOW, &tr);
		if (tr.fraction >= 1)
			return true;
		if (!(tr.contents & Q2CONTENTS_WINDOW) &&
			!(tr.surface && (tr.surface->flags & (TI_TRANS33|TI_TRANS66|TI_Q2EX_ALPHATEST|TI_KINGPIN_ALPHATEST))))
			return false;
		VectorMA(tr.endpos, 1, dir, start);	//on, from inside it: a trace leaving a brush isn't stopped by it
	}
	return false;
}
//whether a flare shows, as Q2REPRO's occlusion query of it (GL_OccludeFlares): a little square facing the eye, 2.5 units
//from its middle and more with distance, moved 5 units towards the eye from inside solid (a flare's origin is often in
//its lamp), shows if any of it is seen - here its middle or a corner
static qboolean CLQ2EX_FlareVisible(const float *eye, const float *org)
{
	model_t *mod = cl.worldmodel;
	vec3_t dir, right, up, mid, p;
	float dist, size;
	int i;
	if (!mod || mod->loadstate != MLS_LOADED || !mod->funcs.NativeTrace)
		return true;
	VectorSubtract(org, eye, dir);
	dist = VectorNormalize(dir);
	if (dist <= 0)
		return true;
	VectorCopy(org, mid);
	if (mod->funcs.PointContents && (mod->funcs.PointContents(mod, NULL, org) & FTECONTENTS_SOLID))
		VectorMA(org, -5, dir, mid);
	if (CLQ2EX_PointSeen(mod, eye, mid))
		return true;
	size = 2.5 + ((dist > 20) ? dist * 0.004 : 0);
	VectorVectors(dir, right, up);
	for (i = 0; i < 4; i++)
	{
		VectorMA(mid, (i&1)?size:-size, right, p);
		VectorMA(p, (i&2)?size:-size, up, p);
		if (CLQ2EX_PointSeen(mod, eye, p))
			return true;
	}
	return false;
}

//the rerelease's flares (misc_flare, RF_FLARE): a sprite over the world while its origin can be seen, fading in and out
//(Q2REPRO's GL_DrawFlares: occlusion queries, there; a trace from the eye, here), and faded out near the viewer
static shader_t *defaultflare;	//freed with the renderer (CLQ2_RendererRestarted)

//vid_reload (a graphics preset, the software look...) freed the models and shaders; cached ones are found again
void CLQ2_RendererRestarted(void)
{
	defaultflare = NULL;
	CLQ2FX_RendererRestarted();
}

static void CLQ2EX_AddFlare(const entity_t *src, const entity_state_t *s1, q2centity_t *cent, unsigned int renderfx)
{
	shader_t *shader = NULL;
	qboolean def = false;
	float d, alpha, frac, s, visible;
	float fade_start = s1->modelindex2, fade_end = s1->u.q2.modelindex3;
	unsigned int rgba = s1->skinnum;
	const float *eye = cl.playerview[0].simorg;	//(as the effects' view, CLQ2FX_BeginFrame)
	const char *image;
	entity_t ent = *src;	//(the caller's is reused for the next entity)

	//how much of it shows: towards whether the eye sees its origin, 8 per second (gl_flarespeed's default); one that
	//was not drawn for a while starts as it is
	visible = CLQ2EX_FlareVisible(eye, ent.origin);
	frac = cent->flarefrac;
	if (cent->flaretime && cl.time >= cent->flaretime && cl.time - cent->flaretime < 0.5)
	{
		float step = (cl.time - cent->flaretime) * 8;
		frac = (frac < visible) ? min(visible, frac + step) : max(visible, frac - step);
	}
	else
		frac = visible;
	cent->flarefrac = frac;
	cent->flaretime = cl.time;
	if (frac <= 0)
		return;

	{
		vec3_t delta;
		VectorSubtract(ent.origin, eye, delta);
		d = VectorLength(delta);
	}
	if (d < fade_start)
		return;
	if (d > fade_end)
		alpha = 1;
	else
		alpha = (d - fade_start) / (fade_end - fade_start);

	if ((renderfx & Q2RF_CUSTOMSKIN) && (unsigned int)s1->frame < countof(cl.image_name) && (image = cl.image_name[s1->frame]) && *image)
		shader = R_RegisterShader(va("q2exflare/%s", image), SUF_NONE, va(
			"{\n"
				"surfaceparm nodlight\n"
				"surfaceparm noshadows\n"
				"sort additive\n"
				"{\n"
					"map \"%s\"\n"
					"blendfunc GL_SRC_ALPHA GL_ONE\n"
					"rgbgen vertex\n"
					"alphagen vertex\n"
				"}\n"
			"}\n", image));
	if (!shader || R_GetShaderSizes(shader, NULL, NULL, true) <= 0)
	{
		if (!defaultflare)
			defaultflare = R_RegisterShader("q2exflare/misc/flare", SUF_NONE,
				"{\n"
					"surfaceparm nodlight\n"
					"surfaceparm noshadows\n"
					"sort additive\n"
					"{\n"
						"map \"misc/flare.tga\"\n"
						"blendfunc GL_SRC_ALPHA GL_ONE\n"
						"rgbgen vertex\n"
						"alphagen vertex\n"
					"}\n"
				"}\n");
		shader = defaultflare;
		def = true;
	}

	s = ent.scale ? ent.scale : 1;
	ent.rtype = (renderfx & 1/*RF_FLARE_LOCK_ANGLE: the rerelease's RF_MINLIGHT*/) ? RT_Q2EXFLARE_LOCKED : RT_Q2EXFLARE;
	ent.model = NULL;
	ent.forcedshader = shader;
	ent.scale = (def ? 50 : 25) * s * frac;	//Q2REPRO's quad: 25 units from its middle per unit of scale (50 with the default image)
	ent.flags = RF_NODEPTHTEST | RF_ADDITIVE;
	if (rgba)
	{
		ent.shaderRGBAf[0] = ((rgba>>24)&0xff)/255.0;
		ent.shaderRGBAf[1] = ((rgba>>16)&0xff)/255.0;
		ent.shaderRGBAf[2] = ((rgba>> 8)&0xff)/255.0;
	}
	else
		VectorSet(ent.shaderRGBAf, 1, 1, 1);
	ent.shaderRGBAf[3] = ((def ? 160 : 128) / 255.0) * alpha * frac;
	//the edge: its RF_SHELL_* rim's colour (each one full), else its own
	if (renderfx & (Q2RF_SHELL_RED|Q2RF_SHELL_GREEN|Q2RF_SHELL_BLUE))
		VectorSet(ent.glowmod, (renderfx & Q2RF_SHELL_RED)?1:0, (renderfx & Q2RF_SHELL_GREEN)?1:0, (renderfx & Q2RF_SHELL_BLUE)?1:0);
	else
		VectorCopy(ent.shaderRGBAf, ent.glowmod);
	V_AddEntity(&ent);
}

/*
===============
CL_AddPacketEntities

===============
*/
void R_AddItemTimer(vec3_t shadoworg, float yaw, float radius, float percent, vec3_t rgb);
static void CLQ2_AddPacketEntities (q2frame_t *frame)
{
	entity_t			ent;
	entity_state_t		*s1;
	float				autorotate;
	int					i;
	int					pnum;
	q2centity_t			*cent;
	int					autoanim;
//	q2clientinfo_t		*ci;
	player_info_t		*player;
	unsigned int		effects, renderfx;
	qboolean			dualfire;
	float back, fwds;
	struct itemtimer_s **timerlink, *timer;

	// bonus items rotate at a fixed rate
	autorotate = anglemod(cl.time*100);

	// brush models can auto animate their frames
	autoanim = 2*cl.time;

	memset (&ent, 0, sizeof(ent));
	q2ex_rtlight_frame++;

	for (timerlink = &cl.itemtimers; (timer=*timerlink); )
	{
		if (cl.time > timer->end)
		{
			*timerlink = timer->next;
			Z_Free(timer);
		}
		else
		{
			timerlink = &(*timerlink)->next;
			/*if (timer->entnum)
			{
				if (timer->entnum >= cl.maxlerpents)
					continue;
				le = &cl.lerpents[timer->entnum];
				if (le->sequence != cl.lerpentssequence)
					continue;
//				VectorCopy(le->origin, timer->origin);
			}*/
			R_AddItemTimer(timer->origin, cl.time*90 + timer->origin[0] + timer->origin[1] + timer->origin[2], timer->radius, (cl.time - timer->start) / timer->duration, timer->rgb);
		}
	}

	for (pnum = 0 ; pnum<frame->num_entities ; pnum++)
	{
		s1 = &clq2_parse_entities[(frame->parse_entities+pnum)&(MAX_PARSE_ENTITIES-1)];

		cent = &cl_entities[s1->number];

		effects = s1->effects;
		renderfx = s1->u.q2.renderfx;

		ent.rtype = RT_MODEL;
		ent.keynum = s1->number;

		ent.scale = s1->scale/16.f;
		ent.shaderRGBAf[0] = 1;
		ent.shaderRGBAf[1] = 1;
		ent.shaderRGBAf[2] = 1;
		ent.shaderRGBAf[3] = 1;
		ent.glowmod[0] = 1;
		ent.glowmod[1] = 1;
		ent.glowmod[2] = 1;
		ent.fatness = 0;
		ent.topcolour = 1;
		ent.bottomcolour = 1;
#ifdef HEXEN2
		ent.h2playerclass = 0;
#endif
		ent.playerindex = -1;
		ent.customskin = 0;

		// set frame
		if (effects & Q2EF_ANIM01)
			ent.framestate.g[FS_REG].frame[0] = autoanim & 1;
		else if (effects & Q2EF_ANIM23)
			ent.framestate.g[FS_REG].frame[0] = 2 + (autoanim & 1);
		else if (effects & Q2EF_ANIM_ALL)
			ent.framestate.g[FS_REG].frame[0] = autoanim;
		else if (effects & Q2EF_ANIM_ALLFAST)
			ent.framestate.g[FS_REG].frame[0] = cl.time * 10;	//yquake2's cl.time/100 counts milliseconds: ten frames a second
		else
			ent.framestate.g[FS_REG].frame[0] = s1->frame;

		// quad and pent can do different things on client
		if (effects & Q2EF_PENT)
		{
			effects &= ~Q2EF_PENT;
			effects |= Q2EF_COLOR_SHELL;
			renderfx |= Q2RF_SHELL_RED;
		}

		if (effects & Q2EF_QUAD)
		{
			effects &= ~Q2EF_QUAD;
			effects |= Q2EF_COLOR_SHELL;
			renderfx |= Q2RF_SHELL_BLUE;
		}
//======
// PMM
		if (effects & Q2EF_DOUBLE)
		{
			effects &= ~Q2EF_DOUBLE;
			effects |= Q2EF_COLOR_SHELL;
			renderfx |= Q2RF_SHELL_DOUBLE;
		}

		if (effects & Q2EF_HALF_DAMAGE)
		{
			effects &= ~Q2EF_HALF_DAMAGE;
			effects |= Q2EF_COLOR_SHELL;
			renderfx |= Q2RF_SHELL_HALF_DAM;
		}
// pmm
//======
		dualfire = !!(s1->u.q2.effects2 & Q2EXEF2_DUALFIRE);
		if (dualfire)	//the rerelease's: a light green shell
			effects |= Q2EF_COLOR_SHELL;

		ent.framestate.g[FS_REG].frame[1] = cent->prev.frame;
		ent.framestate.g[FS_REG].lerpweight[0] = cl.lerpfrac;
		if (cl.q2svnetrate > 10 && cent->oldframe != s1->frame && ent.framestate.g[FS_REG].frame[0] == s1->frame)
		{	//ticks faster than the models animate (the rerelease's 40 a second, its models' 10): a frame change lerps
			//over the 100ms from the tick before the one it came in, not over the tick (as Q2REPRO does for KEX)
			float frac = (cl.time - cent->oldframetime) / 0.1;
			ent.framestate.g[FS_REG].frame[1] = cent->oldframe;
			ent.framestate.g[FS_REG].lerpweight[0] = bound(0, frac, 1);
		}

		if ((renderfx & Q2RF_BEAM) && cl.q2frame.serverframe - cent->serverframe_created >= 2)
		{	//yquake2: a laser that has been there for two frames moves smoothly, both of its ends
			for (i=0 ; i<3 ; i++)
			{
				ent.origin[i] = cent->prev.origin[i] + cl.lerpfrac * (cent->current.origin[i] - cent->prev.origin[i]);
				ent.oldorigin[i] = cent->prev.u.q2.old_origin[i] + cl.lerpfrac * (cent->current.u.q2.old_origin[i] - cent->prev.u.q2.old_origin[i]);
			}
		}
		else if (renderfx & (Q2RF_FRAMELERP|Q2RF_BEAM))
		{	// step origin discretely, because the frames
			// do the animation properly
			VectorCopy (cent->current.origin, ent.origin);
			VectorCopy (cent->current.u.q2.old_origin, ent.oldorigin);
		}
		else
		{	// interpolate origin
			for (i=0 ; i<3 ; i++)
			{
				ent.origin[i] = ent.oldorigin[i] = cent->prev.origin[i] + cl.lerpfrac *
					(cent->current.origin[i] - cent->prev.origin[i]);
			}

			if (cent->stair_height && realtime - cent->stair_time < 0.1)
			{	//the rerelease's RF_STAIR_STEP: the step's height comes over 100ms
				ent.origin[2] = ent.oldorigin[2] = cent->current.origin[2] - cent->stair_height * (0.1 - (realtime - cent->stair_time)) / 0.1;
			}
		}

		//as yquake2: the previous server frame's frame, weighted by the frame's lerpfrac. A second clock used to
		//take over whenever the frame changed, lerping over 100ms from when the change was SEEN.
		ent.framestate.g[FS_REG].lerpweight[1] = 1-ent.framestate.g[FS_REG].lerpweight[0];

		if (renderfx & Q2EXRF_FLARE)
		{
/*This flag marks an entity as being rendered with a flare instead of the usual entity rendering. Flares overload some fields:
* `s.renderfx & RF_SHELL_RED` causes the flare to have an outer red rim.
* `s.renderfx & RF_SHELL_GREEN` causes the flare to have an outer green rim.
* `s.renderfx & RF_SHELL_BLUE` causes the flare to have an outer blue rim.
* `s.renderfx & RF_FLARE_LOCK_ANGLE` causes the flare to not rotate towards the viewer.
* `s.renderfx & RF_CUSTOMSKIN` causes the flare to use the custom image index in `s.frame`.
* `s.modelindex2` is the start distance of fading the flare out.
* `s.modelindex3` is the end distance of fading the flare out.
* `s.skinnum` is the RGBA of the flare.*/
			CLQ2EX_AddFlare(&ent, s1, cent, renderfx);
			continue;
		}
		if (renderfx & Q2EXRF_CUSTOM_LIGHT)
		{	//0xRRGGBBXX apparently
			V_AddLight (ent.keynum, ent.origin,
						ent.framestate.g[FS_REG].frame[0]/*radius*/,
						((ent.skinnum >> 24) & 0xFF)/255.0/*r*/,
						((ent.skinnum >> 16) & 0xFF)/255.0/*g*/,
						((ent.skinnum >>  8) & 0xFF)/255.0/*b*/);
			continue;	//probably modelindex 1. we don't want to actually draw that. but probably do want to lerp it.
		}
		if (renderfx & Q2REX_CASTSHADOW)
		{
			CLQ2EX_UpdateRTLight(ent.keynum, ent.origin, s1->skinnum);
			continue;
		}

		// create a new entity
	
		// tweak the color of beams
		if ( renderfx & Q2RF_BEAM )
		{	// the four beam colors are encoded in 32 bits of skinnum (hack)
			ent.skinnum = (s1->skinnum >> ((rand() % 4)*8)) & 0xff;
			ent.shaderRGBAf[0] = ((d_8to24srgbtable[ent.skinnum & 0xFF] >>  0) & 0xFF)/255.0;
			ent.shaderRGBAf[1] = ((d_8to24srgbtable[ent.skinnum & 0xFF] >>  8) & 0xFF)/255.0;
			ent.shaderRGBAf[2] = ((d_8to24srgbtable[ent.skinnum & 0xFF] >> 16) & 0xFF)/255.0;
			ent.shaderRGBAf[3] = 0.30;
			ent.model = NULL;
			ent.framestate.g[FS_REG].lerpweight[0] = 1;
			ent.framestate.g[FS_REG].lerpweight[1] = 0;
			ent.rtype = RT_BEAM;
		}
		else
		{
			// set skin
			if (s1->modelindex == 255)
			{	// use custom player skin
				ent.skinnum = 0;

				player = &cl.players[(s1->skinnum&0xff)%cl.allocated_client_slots];
				ent.model = player->model;
				if (!ent.model || ent.model->loadstate != MLS_LOADED)	//we need to do better than this
				{
					ent.model = Mod_ForName("players/male/tris.md2", MLV_SILENT);
					ent.customskin = Mod_RegisterSkinFile("players/male/grunt.skin");
					if (!ent.customskin)
						ent.customskin = Mod_ReadSkinFile("players/male/grunt.skin", "replace \"\" \"players/male/grunt.pcx\"");
				}
				else
					ent.customskin = player->skinid;
				ent.playerindex = (s1->skinnum&0xff)%cl.allocated_client_slots;
/*				ci = &cl.clientinfo[s1->skinnum & 0xff];
//				ent.skin = ci->skin;
				ent.model = ci->model;
				if (!ent.skin || !ent.model)
				{
					ent.skin = cl.baseclientinfo.skin;
					ent.model = cl.baseclientinfo.model;
				}

//============
//PGM
				if (renderfx & Q2RF_USE_DISGUISE)
				{
					if(!strncmp((char *)ent.skin, "players/male", 12))
					{
						ent.skin = re.RegisterSkin ("players/male/disguise.pcx");
						ent.model = re.RegisterModel ("players/male/tris.md2");
					}
					else if(!strncmp((char *)ent.skin, "players/female", 14))
					{
						ent.skin = re.RegisterSkin ("players/female/disguise.pcx");
						ent.model = re.RegisterModel ("players/female/tris.md2");
					}
					else if(!strncmp((char *)ent.skin, "players/cyborg", 14))
					{
						ent.skin = re.RegisterSkin ("players/cyborg/disguise.pcx");
						ent.model = re.RegisterModel ("players/cyborg/tris.md2");
					}
				}*/
//PGM
//============
			}
			else
			{
				ent.skinnum = s1->skinnum;
//				ent.skin = NULL;
				ent.model = cl.model_precache[s1->modelindex];
			}
		}

		// only used for black hole model right now, FIXME: do better
		if ((renderfx & RF_TRANSLUCENT) && !(renderfx & Q2RF_BEAM))	//yquake2 tests the bit: == missed any combination
			ent.shaderRGBAf[3] = 0.70;
		else if (s1->trans != 255)
		{
			renderfx |= RF_TRANSLUCENT;
			ent.shaderRGBAf[3] = s1->trans/255.0f;
		}
		else
			ent.shaderRGBAf[3] = 1;

		// render effects (fullbright, translucent, etc)
		if ((effects & Q2EF_COLOR_SHELL))
			ent.flags = 0;	// renderfx go on color shell entity
		else
			ent.flags = renderfx;

		// calculate angles
		if (effects & Q2EF_ROTATE)
		{	// some bonus items auto-rotate
			ent.angles[0] = 0;
			ent.angles[1] = autorotate;
			ent.angles[2] = 0;
		}
		// RAFAEL
		else if (effects & Q2EF_SPINNINGLIGHTS)
		{
			ent.angles[0] = 0;
			ent.angles[1] = anglemod(cl.time*500) + s1->angles[1];	//yquake2's cl.time/2 is milliseconds: 500 degrees a second
			ent.angles[2] = 180;
			{
				vec3_t forward;
				vec3_t start;

				AngleVectors (ent.angles, forward, NULL, NULL);
				VectorMA (ent.origin, 64, forward, start);
				V_AddLight (ent.keynum, start, 100, 0.2, 0, 0);
			}
		}
		else
		{	// interpolate angles
			float	a1, a2;

			for (i=0 ; i<3 ; i++)
			{
				a1 = cent->current.angles[i];
				a2 = cent->prev.angles[i];
				ent.angles[i] = LerpAngle (a2, a1, cl.lerpfrac);
			}
		}

		ent.angles[0]*=r_meshpitch.value;	//q2 has it fixed. consistency...
		ent.angles[2]*=r_meshroll.value;	//h2 doesn't. consistency...

		if (s1->number == cl.playerview[0].playernum+1)	//woo! this is us!
		{
//			VectorCopy(cl.predicted_origin, ent.origin);
//			VectorCopy(cl.predicted_origin, ent.oldorigin);
			ent.flags |= RF_EXTERNALMODEL;	// only draw from mirrors
			renderfx |= RF_EXTERNALMODEL;

			if (effects & Q2EF_FLAG1)
				V_AddLight (ent.keynum, ent.origin, 225, 0.2, 0.05, 0.05);
			else if (effects & Q2EF_FLAG2)
				V_AddLight (ent.keynum, ent.origin, 225, 0.05, 0.05, 0.2);
			else if (effects & Q2EF_TAGTRAIL)						//PGM
				V_AddLight (ent.keynum, ent.origin, 225, 0.2, 0.2, 0.0);	//PGM
			else if (effects & Q2EF_TRACKERTRAIL)					//PGM
				V_AddLight (ent.keynum, ent.origin, 225, -0.2, -0.2, -0.2);	//PGM
		}

		if (s1->u.q2.effects2 & Q2EXEF2_FLASHLIGHT)
		{	//the rerelease's flashlight: a light where it shines, 256 units out at most (Q2REPRO's, without per-pixel lights)
			vec3_t start, end, forward;
			trace_t tr;
			if (s1->number == cl.playerview[0].playernum+1)
			{	//ours: from the view
				VectorCopy(cl.playerview[0].simorg, start);
				AngleVectors(cl.playerview[0].simangles, forward, NULL, NULL);
			}
			else
			{
				VectorCopy(ent.origin, start);
				AngleVectors(ent.angles, forward, NULL, NULL);
			}
			VectorMA(start, 256, forward, end);
			cl.worldmodel->funcs.NativeTrace(cl.worldmodel, 0, NULLFRAMESTATE, NULL, start, end, vec3_origin, vec3_origin, false, FTECONTENTS_SOLID, &tr);
			//smooth out its jumps in distance
			cent->flashlightfrac += bound(-host_frametime, tr.fraction - cent->flashlightfrac, host_frametime);
			VectorMA(start, 256*cent->flashlightfrac, forward, end);
			V_AddLight (0, end, 256, 0.2, 0.2, 0.2);
		}
		if (s1->u.q2.effects2 & Q2EXEF2_GRENADE_LIGHT)
			V_AddLight (0, ent.origin, 100, 0.2, 0.2, 0);
		if (s1->u.q2.effects2 & Q2EXEF2_HOLOGRAM)
			CLQ2FX_RereleaseParticles (1, ent.origin);

		// if set to invisible, skip
		if (!s1->modelindex)
			continue;

		if (effects & Q2EF_BFG)
		{
			ent.flags |= RF_TRANSLUCENT;
			ent.shaderRGBAf[3] = 0.30;
		}

		// RAFAEL
		if (effects & Q2EF_PLASMA)
		{
			ent.flags |= RF_TRANSLUCENT;
			ent.shaderRGBAf[3] = 0.6;
		}

		if (effects & Q2EF_SPHERETRANS)
		{
			ent.flags |= RF_TRANSLUCENT;
			// PMM - *sigh*  yet more EF overloading
			if (effects & Q2EF_TRACKERTRAIL)
				ent.shaderRGBAf[3] = 0.6;
			else
				ent.shaderRGBAf[3] = 0.3;
		}
//pmm

		/*lerp the ent now*/
		fwds = ent.framestate.g[FS_REG].lerpweight[0];
		back = ent.framestate.g[FS_REG].lerpweight[1];
		for (i = 0; i < 3; i++)
		{
			ent.origin[i] = ent.origin[i]*fwds + ent.oldorigin[i]*back;
		}

		if ((renderfx & Q2RF_IR_VISIBLE) && (r_refdef.flags & Q2RDF_IRGOGGLES))
		{
			//IR googles make ir visible ents visible in pure red.
			ent.shaderRGBAf[0] = 1;
			ent.shaderRGBAf[1] = 0;
			ent.shaderRGBAf[2] = 0;
			//bypasses world lighting
			ent.light_known = true;
			VectorSet(ent.light_avg, 1, 1, 1);
			VectorSet(ent.light_range, 0, 0, 0);
			//(yes, its a bit shit. not even a post-process thing)
		}

		// add to refresh list
		V_AddEntity (&ent);
		ent.light_known = false;
		ent.customskin = 0;


		// color shells generate a seperate entity for the main model
		if (effects & Q2EF_COLOR_SHELL)
		{
			// PMM - at this point, all of the shells have been handled
			// if we're in the rogue pack, set up the custom mixing, otherwise just
			// keep going

			// all of the solo colors are fine.  we need to catch any of the combinations that look bad
			// (double & half) and turn them into the appropriate color, and make double/quad something special
			if (renderfx & Q2RF_SHELL_HALF_DAM)
			{
				
				{
					// ditch the half damage shell if any of red, blue, or double are on
					if (renderfx & (Q2RF_SHELL_RED|Q2RF_SHELL_BLUE|Q2RF_SHELL_DOUBLE))
						renderfx &= ~Q2RF_SHELL_HALF_DAM;
				}
			}

			if (renderfx & Q2RF_SHELL_DOUBLE)
			{

				{
					// lose the yellow shell if we have a red, blue, or green shell
					if (renderfx & (Q2RF_SHELL_RED|Q2RF_SHELL_BLUE|Q2RF_SHELL_GREEN))
						renderfx &= ~Q2RF_SHELL_DOUBLE;
					// if we have a red shell, turn it to purple by adding blue
					if (renderfx & Q2RF_SHELL_RED)
						renderfx |= Q2RF_SHELL_BLUE;
					// if we have a blue shell (and not a red shell), turn it to cyan by adding green
					else if (renderfx & Q2RF_SHELL_BLUE)
					{
						// go to green if it's on already, otherwise do cyan (flash green)
						if (renderfx & Q2RF_SHELL_GREEN)
							renderfx &= ~Q2RF_SHELL_BLUE;
						else
							renderfx |= Q2RF_SHELL_GREEN;
					}
				}
			}
			// pmm
			ent.flags = renderfx;
			ent.shaderRGBAf[3] = 0.30;	//yquake2's shell alpha
			//the colours as the renderers (id's, yquake2's, Q2REPRO's) mix them
			VectorClear(ent.shaderRGBAf);
			if (dualfire)
				VectorSet(ent.shaderRGBAf, 0.56, 0.93, 0.56);
			if (renderfx & Q2RF_SHELL_HALF_DAM)
				VectorSet(ent.shaderRGBAf, 0.56, 0.59, 0.45);
			if (renderfx & Q2RF_SHELL_DOUBLE)
			{
				ent.shaderRGBAf[0] = 0.9;
				ent.shaderRGBAf[1] = 0.7;
			}
			if (renderfx & Q2RF_SHELL_RED)
				ent.shaderRGBAf[0] = 1;
			if (renderfx & Q2RF_SHELL_GREEN)
				ent.shaderRGBAf[1] = 1;
			if (renderfx & Q2RF_SHELL_BLUE)
				ent.shaderRGBAf[2] = 1;
			ent.forcedshader = R_RegisterCustom(NULL, "q2/shell", SUF_NONE, Shader_DefaultSkinShell, NULL);
			ent.fatness = 2;
			V_AddEntity (&ent);
			ent.fatness = 0;
		}
		ent.forcedshader = NULL;

//		ent.skin = NULL;		// never use a custom skin on others
		ent.skinnum = 0;
		ent.flags &= RF_EXTERNALMODEL;
		ent.shaderRGBAf[3] = 1;

		// duplicate for linked models
		if (s1->modelindex2)
		{
			if (s1->modelindex2 == 255)
			{	// custom weapon
				char *modelname;
				char *skin;
				ent.model=NULL;

				player = &cl.players[(s1->skinnum&0xff)%MAX_CLIENTS];
				modelname = InfoBuf_ValueForKey(&player->userinfo, "skin");
				if (!modelname[0])
					modelname = "male";
				skin = strchr(modelname, '/');
				if (skin) *skin = '\0';

				i = (s1->skinnum >> 8); // 0 is default weapon model
				if (i < 0 || i >= cl.numq2visibleweapons)
					i = 0;	//0 is always valid
				ent.model = Mod_ForName(va("players/%s/%s", modelname, cl.q2visibleweapons[i]), MLV_WARN);
				if (ent.model->loadstate == MLS_FAILED && i)
				{
					i = 0;
					ent.model = Mod_ForName(va("players/%s/%s", modelname, cl.q2visibleweapons[i]), MLV_WARN);
				}
				if (ent.model->loadstate == MLS_FAILED && strcmp(modelname, "male"))
					ent.model = Mod_ForName(va("players/%s/%s", "male", cl.q2visibleweapons[i]), MLV_WARN);
			}
			else
				ent.model = cl.model_precache[s1->modelindex2];

			// PMM - check for the defender sphere shell .. make it translucent
			// replaces the previous version which used the high bit on modelindex2 to determine transparency
/*			if (!Q_strcasecmp (cl.model_name[(s1->modelindex2)], "models/items/shell/tris.md2"))
			{
				ent.alpha = 0.32;
				ent.flags |= Q2RF_TRANSLUCENT;

				V_AddEntity (&ent);

				// make sure these get reset.
				ent.flags &= RF_EXTERNALMODEL;
				ent.shaderRGBAf[3] = 1;
			}
			else
*/			// pmm

			V_AddEntity (&ent);
		}
		if (s1->u.q2.modelindex3)
		{
			ent.model = cl.model_precache[s1->u.q2.modelindex3];
			V_AddEntity (&ent);
		}
		if (s1->u.q2.modelindex4)
		{
			ent.model = cl.model_precache[s1->u.q2.modelindex4];
			V_AddEntity (&ent);
		}

		if ( effects & Q2EF_POWERSCREEN )
		{
			ent.model = Mod_ForName("models/items/armor/effect/tris.md2", MLV_WARN);
			ent.framestate.g[FS_REG].frame[0] = 0;
			ent.framestate.g[FS_REG].frame[0] = 0;
			ent.flags |= (RF_TRANSLUCENT | Q2RF_SHELL_GREEN);
			ent.shaderRGBAf[3] = 0.30;
			V_AddEntity (&ent);
		}

		// add automatic particle trails: yquake2's (clq2_fx.c), with their lights. It keeps where each entity
		// was for them itself. Not for us: yquake2 skips the player's own entity before its trails.
		if (s1->number != cl.playerview[0].playernum+1)
			CLQ2FX_EntityEffects(s1->number, effects, s1->frame, ent.origin);

		VectorCopy (ent.origin, cent->lerp_origin);
	}

	CLQ2EX_HideUnseenRTLights();
}



/*
==============
CL_AddViewWeapon
==============
*/
static void CLQ2_AddViewWeapon (int seat, q2player_state_t *ps, q2player_state_t *ops)
{
	playerview_t *pv = &cl.playerview[seat];
	int i;

	pv->vm.oldmodel = NULL;

	// allow the gun to be completely removed
	if (!r_drawviewmodel.value)
		return;

	if (!Cam_DrawViewModel(pv))
		return;

	//no wide angle check: yquake2 draws the gun at any fov (its cl_gun defaults to 2)

	pv->vm.oldmodel = cl.model_precache[ps->gunindex];
	if (!pv->vm.oldmodel)
		return;

	/*
	As yquake2's CL_AddViewWeapon: the gun shows the server's frames, lerped between the last two player
	states like every other entity, and is placed at the view plus the gunoffset/gunangles the game sends
	(the sway and the kick of firing). This used to lerp on its own 10 Hz clock from the time a frame
	change was SEEN, and never applied gunoffset or gunangles at all. CL_LinkViewModel draws it.
	*/
	pv->vm.prevframe = ps->gunframe;
	pv->vm.oldframe = ps->gunframe ? ops->gunframe : 0;	//frame 0: just changed weapons, nothing to lerp from
	pv->vm.q2lerp = cl.lerpfrac;
	for (i = 0; i < 3; i++)
	{
		pv->vm.q2offset[i] = ops->gunoffset[i] + cl.lerpfrac * (ps->gunoffset[i] - ops->gunoffset[i]);
		pv->vm.q2angles[i] = LerpAngle(ops->gunangles[i], ps->gunangles[i], cl.lerpfrac);
	}
}


/*
===============
CL_CalcViewValues

Sets r_refdef view values
===============
*/
void CLQ2_CalcViewValues (int seat)
{
	int			i;
	float		lerp, backlerp;
	q2frame_t		*oldframe;
	q2player_state_t	*ps, *ops;
	playerview_t *pv = &cl.playerview[seat];

	r_refdef.areabitsknown = true;
	memcpy(r_refdef.areabits, cl.q2frame.seat[seat].areabits, sizeof(r_refdef.areabits));

	r_refdef.useperspective = true;
	r_refdef.mindist = bound(0.1, gl_mindist.value, 4);
	r_refdef.maxdist = gl_maxdist.value;

	// find the previous frame to interpolate from
	ps = &cl.q2frame.seat[seat].playerstate;
	i = (cl.q2frame.serverframe - 1) & Q2UPDATE_MASK;
	oldframe = &cl.q2frames[i];
	if (oldframe->serverframe != cl.q2frame.serverframe-1 || !oldframe->valid)
		oldframe = &cl.q2frame;		// previous frame was dropped or involid
	ops = &oldframe->seat[seat].playerstate;

	// see if the player entity was teleported this frame
	if ( abs(ops->pmove.origin[0] - ps->pmove.origin[0]) > 256*8
		|| abs(ops->pmove.origin[1] - ps->pmove.origin[1]) > 256*8
		|| abs(ops->pmove.origin[2] - ps->pmove.origin[2]) > 256*8)
		ops = ps;		// don't interpolate

	lerp = cl.lerpfrac;

	// calculate the origin
	if (cl.worldmodel && (!cl_nopred.value) && !(cl.q2frame.seat[seat].playerstate.pmove.pm_flags & Q2PMF_NO_PREDICTION) && !cls.demoplayback)
	{	// use predicted values
		float	delta;

		backlerp = 1.0 - lerp;
		for (i=0 ; i<3 ; i++)
		{
			pv->simorg[i] = pv->predicted_origin[i] + ops->viewoffset[i] 
				+ lerp * (ps->viewoffset[i] - ops->viewoffset[i])
				- backlerp * pv->prediction_error[i];
		}

		// smooth out stair climbing
		delta = realtime - pv->predicted_step_time;
		if (delta < 0.1)
			pv->simorg[2] -= pv->predicted_step * (0.1 - delta)*10;
	}
	else
	{	// just use interpolated values
		for (i=0 ; i<3 ; i++)
			pv->simorg[i] =  ops->pmove.origin[i]*0.125 + ops->viewoffset[i]
				+ lerp *	( ps->pmove.origin[i]*0.125 +  ps->viewoffset[i]
				-			(ops->pmove.origin[i]*0.125 + ops->viewoffset[i]) );
	}

	// if not running a demo or on a locked frame, add the local angle movement
	if (cl.worldmodel && ps->pmove.pm_type < Q2PM_DEAD && !cls.demoplayback)
	{	// use predicted values
		for (i=0 ; i<3 ; i++)
			pv->simangles[i] = pv->predicted_angles[i];
	}
	else
	{	// just use interpolated values
		for (i=0 ; i<3 ; i++)
			pv->simangles[i] = LerpAngle (ops->viewangles[i], ps->viewangles[i], lerp);
	}

	for (i=0 ; i<3 ; i++)
		pv->simangles[i] += v_gunkick_q2.value * LerpAngle (ops->kick_angles[i], ps->kick_angles[i], lerp);

//	VectorCopy(r_refdef.viewangles, cl.viewangles);

//	AngleVectors (r_refdef.viewangles, v_forward, v_right, v_up);

	// interpolate field of view
	pv->forcefov = ops->fov + lerp * (ps->fov - ops->fov);
	pv->handedness = atoi(InfoBuf_ValueForKey(&cls.userinfo[seat], "hand"));

	// don't interpolate blend color (nor its alpha: yquake2 shows the server's blend as it is)
	for (i=0 ; i<3 ; i++)
		pv->screentint[i] = ps->blend[i];
	pv->screentint[3] = ps->blend[3]*gl_cshiftenabled.value;

	//underwater warp and the ir goggles come from the server; V_CalcRefdef applies them
	pv->q2rdflags = ps->rdflags;
#ifdef DAIKATANA
	CLDK_WaterTint(pv->screentint);	//its client's own, in its water
#endif

	// add the weapon
	CLQ2_AddViewWeapon (seat, ps, ops);
}

/*
===============
CL_AddEntities

Emits all entities, particles, and lights to the refresh
===============
*/
void CLQ2_AddEntities (void)
{
	int seat;
	if (cls.state != ca_active)
		return;

	cl.lerpfrac = 1.0 - (cl.q2frame.servertime/1000.0 - cl.time) * cl.q2svnetrate;
//	Con_Printf("%g: %g\n", cl.q2frame.servertime - (cl.time*1000), cl.lerpfrac);
	cl.lerpfrac = bound(0, cl.lerpfrac, 1);

	for (seat = 0; seat < cl.splitclients; seat++)
		CLQ2_CalcViewValues (seat);
	CLQ2FX_BeginFrame ();	//yquake2's effects: the time and view the trails are stamped with
#ifdef DAIKATANA
	if (CLDK_IsDaikatana())
	{
		CLQ2_LerpOrigins (&cl.q2frame);
		CLDK_AddScene ();	//Daikatana's client code draws its entities itself (daikatana/client), and its effects
	}
	else
#endif
	CLQ2_AddPacketEntities (&cl.q2frame);
#if 0
	CLQ2_AddProjectiles ();
#endif
	CLQ2FX_AddEntities ();	//yquake2's temp entities, explosions, beams, lasers, particles, dlights
	CL_UpdateTEnts ();

#ifdef _DEBUG
	if (chase_active.ival)
	{
		playerview_t *pv = &cl.playerview[0];
		vec3_t axis[3];
		vec3_t camorg;
//		trace_t tr;
		AngleVectors(r_refdef.viewangles, axis[0], axis[1], axis[2]);
		VectorMA(r_refdef.vieworg, -chase_back.value, axis[0], camorg);
		VectorMA(camorg, -chase_up.value, pv->gravitydir, camorg);
//		if (cl.worldmodel && cl.worldmodel->funcs.NativeTrace(cl.worldmodel, 0, NULLFRAMESTATE, NULL, r_refdef.vieworg, camorg, vec3_origin, vec3_origin, true, MASK_WORLDSOLID, &tr))
		VectorCopy(camorg, r_refdef.vieworg);

		V_EditExternalModels(0, NULL, 0);
	}
#endif

#ifdef RTLIGHTS
	R_EditLights_DrawLights();
#endif
}

#endif
