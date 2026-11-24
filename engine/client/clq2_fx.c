/*
FTE's side of yquake2's client effects (engine/client/q2fx, see q2fx_sys.h): particles, trails, temp
entities, explosions, beams, lasers, sustains, muzzle flashes and dlights for Quake 2 come from id's code
as yquake2 keeps it, instead of FTE's particle scripts (q2part.cfg).

This file gives that code what it asks for -- models, sounds, entities, lights and particles -- in FTE's
terms, and tells it the client state it reads. Particles are drawn the way id's ref_gl drew them: one
triangle with a round dot per particle, 1.5 units across up close, growing by 0.4% per unit of distance,
coloured from the palette.
*/
#include "quakedef.h"
#include "shader.h"

#ifdef Q2CLIENT
#include "q2fx_sys.h"

extern cvar_t r_meshpitch, r_meshroll;
mpic_t *Sbar_Q2CachePic(const char *name);
sfx_t *S_PrecacheSexedSound(int entnum, const char *soundname);
qboolean CLQ2_GetEntityOrigin(int entnum, vec3_t origin, vec3_t angles);

static cvar_t cl_laseralpha = CVARFD("cl_laseralpha", "0.3", CVAR_ARCHIVE, "Quake 2: the opacity of lasers (yquake2's).");
static cvar_t cl_limitsparksounds = CVARFD("cl_limitsparksounds", "0", CVAR_ARCHIVE, "Quake 2: play at most 16 power screen/shield hit sounds a frame (yquake2's).");
static cvar_t cl_footsteps = CVARFD("cl_footsteps", "1", CVAR_ARCHIVE, "Quake 2: play the footstep sounds the server's events ask for.");

static int fx_pendingbyte = -1;	//the temp entity type, read by FTE to choose, handed back to the effects

void CLQ2FX_Init(void)
{
	Cvar_Register(&cl_laseralpha, "Quake 2 effects");
	Cvar_Register(&cl_limitsparksounds, "Quake 2 effects");
	Cvar_Register(&cl_footsteps, "Quake 2 effects");
}

/*
=====================================================================
The client state the effects read
=====================================================================
*/
static void CLQ2FX_UpdateFrame(void)
{
	playerview_t *pv = &cl.playerview[0];
	q2player_state_t *ps = &cl.q2frame.seat[0].playerstate;
	q2frame_t *oldframe = &cl.q2frames[(cl.q2frame.serverframe - 1) & Q2UPDATE_MASK];
	q2player_state_t *ops = ps;
	vec3_t forward, right, up;

	if (oldframe->serverframe == cl.q2frame.serverframe-1 && oldframe->valid)
		ops = &oldframe->seat[0].playerstate;
	AngleVectors(pv->simangles, forward, right, up);

	Q2FX_SetFrame(cl.time*1000, cl.q2frame.servertime, cl.q2frame.serverframe, cl.q2frame.seat[0].clientnum, cl.lerpfrac,
			pv->simorg, forward, right, up, ps->gunoffset, ops->gunoffset,
			atoi(InfoBuf_ValueForKey(&cls.userinfo[0], "hand")),
			cl_laseralpha.value, cl_limitsparksounds.ival, cl_footsteps.ival);
}

/*
=====================================================================
Network messages
=====================================================================
*/

//svc_temp_entity, type already read: true when it is a vanilla one the effects took
qboolean CLQ2FX_ParseTEnt(int type)
{
	if (type < 0 || type > Q2TE_FLECHETTE)
		return false;	//the rerelease's and others' additions stay with FTE's own code
	CLQ2FX_UpdateFrame();
	fx_pendingbyte = type;	//CL_ParseTEnt reads the type itself
	Q2FX_ParseTEnt();
	fx_pendingbyte = -1;
	return true;
}
void CLQ2FX_ParseMuzzleFlash(void)
{
	CLQ2FX_UpdateFrame();
	Q2FX_ParseMuzzleFlash();
}
void CLQ2FX_ParseMuzzleFlash2(void)
{
	CLQ2FX_UpdateFrame();
	Q2FX_ParseMuzzleFlash2();
}

int Q2FXSYS_ReadByte(void)
{
	if (fx_pendingbyte >= 0)
	{
		int b = fx_pendingbyte;
		fx_pendingbyte = -1;
		return b;
	}
	return MSG_ReadByte();
}
int Q2FXSYS_ReadShort(void)		{ return MSG_ReadShort(); }
int Q2FXSYS_ReadLong(void)		{ return MSG_ReadLong(); }
void Q2FXSYS_ReadPos(float *pos)	{ MSG_ReadPos(pos); }
void Q2FXSYS_ReadDir(float *dir)	{ MSG_ReadDir(dir); }

int Q2FXSYS_GetEntity(int entnum, float *origin, float *angles)
{
	if (CLQ2_GetEntityOrigin(entnum, origin, angles))
		return true;
	VectorClear(origin);
	VectorClear(angles);
	return false;
}

void Q2FXSYS_Print(int developer, const char *text)
{
	if (developer)
		Con_DPrintf("%s", text);
	else
		Con_Printf("%s", text);
}
void Q2FXSYS_Error(const char *text)
{
	Host_EndGame("%s", text);
}

/*
=====================================================================
Media and sound
=====================================================================
*/
void *Q2FXSYS_RegisterModel(const char *name)
{
	model_t *mod = Mod_ForName(name, MLV_SILENT);
	if (mod && mod->loadstate == MLS_FAILED)
		return NULL;
	return mod;
}
void *Q2FXSYS_RegisterSound(const char *name)
{
	return S_PrecacheSound(name);
}
void Q2FXSYS_PrecachePic(const char *name)
{
	Sbar_Q2CachePic(name);
}
void Q2FXSYS_StartSound(const float *origin, int entnum, int channel, void *sfxp, float volume, float attenuation, float timeofs)
{
	sfx_t *sfx = sfxp;
	if (*sfx->name == '*')	//a 'sexed' sound: the entity's own model's
		sfx = S_PrecacheSexedSound(entnum, sfx->name);
	S_StartSound(entnum, channel, sfx, (float*)origin, NULL, volume, attenuation, -timeofs, 0, 0);
}

/*
=====================================================================
Entities, lights, particles
=====================================================================
*/
void Q2FXSYS_AddEntity(void *model, const float *origin, const float *angles, const float *oldorigin,
		int frame, int oldframe, float backlerp, int skinnum, float alpha, int flags)
{
	entity_t ent;
	V_ClearEntity(&ent);

	VectorCopy(origin, ent.origin);
	VectorCopy(oldorigin, ent.oldorigin);
	VectorCopy(angles, ent.angles);
	ent.angles[0] *= r_meshpitch.value;
	ent.angles[2] *= r_meshroll.value;
	ent.framestate.g[FS_REG].frame[0] = frame;
	ent.framestate.g[FS_REG].frame[1] = oldframe;
	ent.framestate.g[FS_REG].lerpweight[0] = 1 - backlerp;
	ent.framestate.g[FS_REG].lerpweight[1] = backlerp;
	ent.skinnum = skinnum;
	ent.flags = flags;	//Quake 2's renderfx bits are FTE's
	ent.shaderRGBAf[0] = ent.shaderRGBAf[1] = ent.shaderRGBAf[2] = 1;
	ent.shaderRGBAf[3] = (flags & RF_TRANSLUCENT) ? alpha : 1;
	ent.glowmod[0] = ent.glowmod[1] = ent.glowmod[2] = 1;

	if (flags & Q2RF_BEAM)
	{	//a laser: frame is its diameter, the low byte of skinnum its palette colour
		unsigned int c = d_8to24srgbtable[skinnum & 0xff];
		ent.shaderRGBAf[0] = ((c >>  0) & 0xFF)/255.0;
		ent.shaderRGBAf[1] = ((c >>  8) & 0xFF)/255.0;
		ent.shaderRGBAf[2] = ((c >> 16) & 0xFF)/255.0;
		ent.model = NULL;
		ent.framestate.g[FS_REG].lerpweight[0] = 1;
		ent.framestate.g[FS_REG].lerpweight[1] = 0;
		ent.rtype = RT_BEAM;
	}
	else
	{
		ent.model = model;
		if (!ent.model)
			return;
	}
	V_AddEntity(&ent);
}

void Q2FXSYS_AddLight(const float *org, float intensity, float r, float g, float b)
{	//FTE's V_AddLight scales colours by 5
	V_AddLight(0, (float*)org, intensity, r*0.2, g*0.2, b*0.2);
}

static shader_t *fx_particleshader;
static int fx_particletris = -1;	//index: cl_stris may be reallocated by others later in the frame
static int fx_particleframe = -1;
static vec3_t fx_up, fx_right, fx_forward, fx_vieworg;

void Q2FXSYS_AddParticle(const float *org, unsigned int color, float alpha)
{
	unsigned int c;
	float scale;
	scenetris_t *t;

	if (fx_particleframe != cl_framecount || fx_particletris < 0)
	{	//one batch a frame, after the scene's other triangles were cleared
		if (!fx_particleshader)
			fx_particleshader = R_RegisterShader("particles_q2", SUF_NONE,
				"{\n"
					"program defaultsprite\n"
					"nomipmaps\n"
					"surfaceparm nodlight\n"
					"{\n"
						"clampmap classicparticle\n"
						"rgbgen vertex\n"
						"alphagen vertex\n"
						"blendfunc blend\n"
					"}\n"
				"}\n");
		if (cl_numstris == cl_maxstris)
		{
			cl_maxstris += 8;
			cl_stris = BZ_Realloc(cl_stris, sizeof(*cl_stris)*cl_maxstris);
		}
		fx_particletris = cl_numstris++;
		t = &cl_stris[fx_particletris];
		memset(t, 0, sizeof(*t));
		t->shader = fx_particleshader;
		t->flags = BEF_NODLIGHT|BEF_NOSHADOWS;
		t->firstidx = cl_numstrisidx;
		t->firstvert = cl_numstrisvert;
		fx_particleframe = cl_framecount;
	}
	t = &cl_stris[fx_particletris];

	//as id's GL_DrawParticles
	scale = (org[0] - fx_vieworg[0]) * fx_forward[0] + (org[1] - fx_vieworg[1]) * fx_forward[1] + (org[2] - fx_vieworg[2]) * fx_forward[2];
	if (scale < 20)
		scale = 1;
	else
		scale = 1 + scale * 0.004;

	if (cl_numstrisvert+3 > cl_maxstrisvert)
		cl_stris_ExpandVerts(cl_numstrisvert+1024*3);
	if (cl_numstrisidx+3 > cl_maxstrisidx)
	{
		cl_maxstrisidx = cl_numstrisidx+1024*3;
		cl_strisidx = BZ_Realloc(cl_strisidx, sizeof(*cl_strisidx)*cl_maxstrisidx);
	}

	c = d_8to24srgbtable[color & 0xff];
	Vector4Set(cl_strisvertc[cl_numstrisvert+0], ((c>>0)&0xff)/255.0, ((c>>8)&0xff)/255.0, ((c>>16)&0xff)/255.0, bound(0, alpha, 1));
	Vector4Copy(cl_strisvertc[cl_numstrisvert+0], cl_strisvertc[cl_numstrisvert+1]);
	Vector4Copy(cl_strisvertc[cl_numstrisvert+0], cl_strisvertc[cl_numstrisvert+2]);

	Vector2Set(cl_strisvertt[cl_numstrisvert+0], 0, 0);
	Vector2Set(cl_strisvertt[cl_numstrisvert+1], 1, 0);
	Vector2Set(cl_strisvertt[cl_numstrisvert+2], 0, 1);

	VectorCopy(org, cl_strisvertv[cl_numstrisvert+0]);
	VectorMA(org, scale, fx_up, cl_strisvertv[cl_numstrisvert+1]);
	VectorMA(org, scale, fx_right, cl_strisvertv[cl_numstrisvert+2]);

	cl_strisidx[cl_numstrisidx++] = (cl_numstrisvert - t->firstvert) + 0;
	cl_strisidx[cl_numstrisidx++] = (cl_numstrisvert - t->firstvert) + 1;
	cl_strisidx[cl_numstrisidx++] = (cl_numstrisvert - t->firstvert) + 2;
	cl_numstrisvert += 3;
	t->numvert += 3;
	t->numidx += 3;
}

/*
=====================================================================
What the rest of the Quake 2 client calls
=====================================================================
*/

//the trails start over where an entity (re)appears
void CLQ2FX_EntityReset(int entnum, const vec3_t origin)
{
	Q2FX_EntityReset(entnum, origin);
}
void CLQ2FX_EntityEffects(int entnum, int effects, int frame, const vec3_t origin)
{
	Q2FX_EntityEffects(entnum, effects, frame, origin);
}
void CLQ2FX_EntityEvent(int entnum, int event, const vec3_t origin)
{
	CLQ2FX_UpdateFrame();
	Q2FX_EntityEvent(entnum, event, origin);
}
void CLQ2FX_TeleporterParticles(const vec3_t origin)
{
	Q2FX_TeleporterParticles(origin);
}
//the rerelease's effects past bit 31: 0 EF_TELEPORTER2, 1 EF_HOLOGRAM, 2 EF_BARREL_EXPLODING
void CLQ2FX_RereleaseParticles(int effect, const vec3_t origin)
{
	Q2FX_RereleaseParticles(effect, origin);
}

//before the packet entities: the time and view the trails they leave are stamped with
void CLQ2FX_BeginFrame(void)
{
	playerview_t *pv = &cl.playerview[0];
	vec3_t right, up;

	CLQ2FX_UpdateFrame();

	//id's particles: 1.5 units of the view's up and right
	AngleVectors(pv->simangles, fx_forward, right, up);
	VectorScale(up, 1.5, fx_up);
	VectorScale(right, 1.5, fx_right);
	VectorCopy(pv->simorg, fx_vieworg);
	fx_particletris = -1;
}
//after the packet entities: temp entities, particles and dlights
void CLQ2FX_AddEntities(void)
{
	Q2FX_AddEntities();
}

void CLQ2FX_ClearState(void)
{
	Q2FX_ClearState();
	fx_particletris = -1;
	fx_pendingbyte = -1;
}
void CLQ2FX_RegisterMedia(void)
{
	Q2FX_RegisterMedia();
}

#endif
