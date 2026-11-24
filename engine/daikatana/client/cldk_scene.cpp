/*
Daikatana's client's effects (cl_tent, cl_fx, dk_beams, Projectile_fx, Artifact_fx, ported as they are) on FTE's
client: what they have of its client (cl_entities, sounds, traces) and its refresh (V_AddEntity, V_AddLight, the
particles its renderer drew: gl_particle.cpp's, as triangles in FTE's scene) over cldk_fte.h.

GPL v2 or later.
*/
#include "cldk_local.h"
#include "dk_beams.h"

centity_t		cl_entities[MAX_EDICTS];
foginfo_t		r_foginfo;

static cvar_t	cv_cl_bob = {(char *)"cl_bob"}, cv_weapon_visible = {(char *)"weapon_visible"}, cv_sv_violence = {(char *)"sv_violence"};
static cvar_t	cv_fov = {(char *)"fov"};
cvar_t			*fov = &cv_fov;
cvar_t			*cl_bob = &cv_cl_bob;
cvar_t			*weapon_visible = &cv_weapon_visible;
cvar_t			*sv_violence = &cv_sv_violence;

void CL_InitDispatchFunctionList(void);
void CL_RegisterTEntModels(void);
void CL_RegisterTEntSounds(void);
void CL_ClearTEnts(void);
void CL_ClearEffects(void);
void CL_AddTEnts(void);
void CL_AddParticles(void);
void CL_AddComParticles(void);
void CL_AddDLights(void);
void CL_AddTrackEnts(void);
void CLDK_EntitiesReset(void);	//cldk_ents.cpp's
void CLDK_AddEntities(void);
void CLDK_RegisterBeams(void);	//cldk_beams.cpp's
void CLDK_DrawBeams(void);
void CLDK_DrawEntityBeam(entity_t *e);
void CLDK_DrawSpotlight(entity_t *e);
void CLDK_DrawSurfaceSprites(void);	//cldk_surfsprites.cpp's

/*
=====================================================================
its engine's
=====================================================================
*/
void *X_Malloc(size_t size, MEM_TAG tag)
{
	return calloc(1, size);
}
void Com_Error(int code, char *fmt, ...)
{
	va_list argptr;
	char text[1024];
	va_start(argptr, fmt);
	vsnprintf(text, sizeof(text), fmt, argptr);
	va_end(argptr);
	CLDKF_Print("Daikatana: ");
	CLDKF_Print(text);
	CLDKF_Print("\n");
}
void Com_DPrintf(char *fmt, ...)
{
	va_list argptr;
	char text[1024];
	if (!CLDKF_CvarValue("developer", "0"))
		return;
	va_start(argptr, fmt);
	vsnprintf(text, sizeof(text), fmt, argptr);
	va_end(argptr);
	CLDKF_Print(text);
}
int CM_PointContents(CVector &p, int headnode)
{
	float o[3] = {p.x, p.y, p.z};
	return CLDKF_PointContents(o);
}
static csurface_t cltracesurface;
trace_t CL_TraceLine(const CVector &start, const CVector &end, int passent_num, int clip_mask)
{
	trace_t tr;
	float s[3] = {start.x, start.y, start.z}, e[3] = {end.x, end.y, end.z}, endpos[3], normal[3] = {0, 0, 0};
	int ent, contents;
	memset(&tr, 0, sizeof(tr));
	tr.fraction = CLDKF_Trace(s, e, passent_num, clip_mask, endpos, normal, &ent, &contents);
	tr.endpos.Set(endpos[0], endpos[1], endpos[2]);
	tr.plane.normal.Set(normal[0], normal[1], normal[2]);
	tr.contents = contents;
	tr.surface = &cltracesurface;
	tr.ent = (struct edict_s *)(size_t)ent;	//its client's traces had entity numbers there
	return tr;
}
//cl_view.cpp's: the particle volumes (rain, snow, drips: cl_pv.cpp's) its renderer generated particles in, as
//they came into view
int					r_numparticleVolumes;
particleVolume_t	r_particleVolumes[MAX_PARTICLEVOLUMES];
static cvar_t		cv_particlevol = {(char *)"cl_particlevol"};
cvar_t				*cl_particlevol = &cv_particlevol;
void CL_AddParticleVolumes(void);
void CL_GenerateVolumeParticles(uintptr_t key, int type, CVector &mins, CVector &maxs, float dist, int flags);
void CL_RemoveParticleVolume(uintptr_t key);

static void CLDK_ParticleVolumes(void)
{
	int i, k;
	cv_particlevol.value = CLDKF_CvarValue("cl_particlevol", "1");
	CL_AddParticleVolumes();
	for (i = 0; i < r_numparticleVolumes; i++)
	{
		particleVolume_t *pv = &r_particleVolumes[i];
		bool infront = false;
		if (pv->bVisible)
		{	//its renderer's frustum cull, roughly: any corner of its box in front of the view
			for (k = 0; k < 8 && !infront; k++)
			{
				CVector c((k&1)?pv->maxs.x:pv->mins.x, (k&2)?pv->maxs.y:pv->mins.y, (k&4)?pv->maxs.z:pv->mins.z);
				c = c - cl.refdef.vieworg;
				infront = DotProduct(c, cl.v_forward) > 0;
			}
		}
		if (infront)
			CL_GenerateVolumeParticles((uintptr_t)pv, pv->type, pv->mins, pv->maxs, pv->dist, pv->effects);
		else
			CL_RemoveParticleVolume((uintptr_t)pv);
	}
}

/*
=====================================================================
its sound engine's
=====================================================================
*/
static void *CLDK_S_RegisterSound(const char *name)
{
	return CLDKF_RegisterSound(name);
}
static void *CLDK_S_StartSound(const CVector &origin, int entnum, int entchannel, void *stream, float fvol, float dist_min, float dist_max)
{	//its distances as Quake II's attenuation: inaudible at 1000/attenuation
	float o[3] = {origin.x, origin.y, origin.z};
	float attn = (dist_max > 0)?1000/dist_max:1;
	if (attn > 4)
		attn = 4;
	CLDKF_StartSound(stream, (origin.x || origin.y || origin.z)?o:NULL, entnum, entchannel & 7, fvol, attn, 0);
	return NULL;
}
static void *CLDK_S_StartSoundQuake2(const CVector &origin, int entnum, int entchannel, void *stream, float fvol, float attenuation, float timeofs)
{
	float o[3] = {origin.x, origin.y, origin.z};
	CLDKF_StartSound(stream, (origin.x || origin.y || origin.z)?o:NULL, entnum, entchannel & 7, fvol, attenuation, timeofs);
	return NULL;
}
void *(*S_RegisterSound)(const char *pszName) = CLDK_S_RegisterSound;
void *(*S_StartSound)(const CVector &origin, int entnum, int entchannel, void *pAudioStream, float fvol, float dist_min, float dist_max) = CLDK_S_StartSound;
void *(*S_StartSoundQuake2)(const CVector &origin, int entnum, int entchannel, void *pAudioStream, float fvol, float attenuation, float timeofs) = CLDK_S_StartSoundQuake2;

/*
=====================================================================
its refresh
=====================================================================
*/
void V_AddEntity(entity_t *ent)
{
	if (ent->flags & RF_BEAM_MOVING)
	{	//its renderer drew these as beams
		CLDK_DrawEntityBeam(ent);
		return;
	}
	if (ent->flags & RF_SPOTLIGHT)
	{	//and these as volumes of light
		CLDK_DrawSpotlight(ent);
		return;
	}
	float o[3] = {ent->origin.x, ent->origin.y, ent->origin.z};
	float oo[3] = {ent->oldorigin.x, ent->oldorigin.y, ent->oldorigin.z};
	float a[3] = {ent->angles.x, ent->angles.y, ent->angles.z};
	float rgb[3] = {ent->color.x, ent->color.y, ent->color.z};
	float scale = ent->render_scale.x;	//(FTE's are uniform)
	CLDKF_AddEntity(ent->model, o, oo, a, ent->frame, ent->oldframe, ent->backlerp, ent->skinnum, ent->skin, ent->alpha, scale, rgb, ent->flags);
}
void V_AddLight(CVector &org, float intensity, float r, float g, float b)
{
	float o[3] = {org.x, org.y, org.z};
	CLDKF_AddLight(o, intensity, r, g, b);
}
void V_AddLightStyle(int style, float r, float g, float b)	{}
void V_AddFlare(CVector &org, CVector &ang, float intensity, float falloff, float r, float g, float b)	{}

//its particles: gl_particle.cpp's, their places in pics/particles/particles.tga (256x128) and sizes
static particle_def_t	pdefs[NUM_PARTICLETYPES];
static void *pshader;

static void CL_InitParticleDefs(void)
{
	particle_def_t *p;
	int i;
	float w = 256, h = 128;
	memset(pdefs, 0, sizeof(pdefs));
	for (i = 0; i < NUM_PARTICLETYPES; i++)
	{	//(what it didn't set)
		pdefs[i].s1 = 48; pdefs[i].t1 = 48; pdefs[i].s2 = 63; pdefs[i].t2 = 63; pdefs[i].fScale = 1.5;
	}
#define PDEF(t,a,b,c,d,s) p = &pdefs[t]; p->s1 = a; p->t1 = b; p->s2 = c; p->t2 = d; p->fScale = s
	PDEF(PARTICLE_SNOW,			0, 0, 63, 63, 2.3);
	PDEF(PARTICLE_RAIN,			225, 32, 255, 63, 2.0);
	PDEF(PARTICLE_BLOOD,		112, 96, 127, 111, 1.5);
	PDEF(PARTICLE_BLOOD2,		128, 96, 143, 111, 1.5);
	PDEF(PARTICLE_BLOOD3,		144, 96, 159, 111, 1.5);
	PDEF(PARTICLE_BLOOD4,		112, 112, 127, 127, 1.5);
	PDEF(PARTICLE_BLOOD5,		128, 112, 143, 127, 1.5);
	PDEF(PARTICLE_BUBBLE,		0, 64, 31, 95, 1.5);
	PDEF(PARTICLE_SMOKE,		64, 0, 127, 64, 7.0);
	PDEF(PARTICLE_SPARKS,		144, 112, 159, 127, 1.5);
	PDEF(PARTICLE_POISON,		128, 0, 127, 63, 1.5);
	PDEF(PARTICLE_BLUE_SPARKS,	96, 112, 111, 127, 3.0);
	PDEF(PARTICLE_ICE,			192, 64, 255, 127, 3.0);
	PDEF(PARTICLE_SPARKLE1,		32, 96, 63, 127, 3.0);
	PDEF(PARTICLE_SPARKLE2,		64, 96, 95, 127, 3.0);
	PDEF(PARTICLE_DRIP_BLOOD,	96, 96, 111, 111, 3.0);
	PDEF(PARTICLE_SPLASH1,		96, 64, 127, 91, 4.0);
	PDEF(PARTICLE_SPLASH2,		128, 64, 159, 91, 4.0);
	PDEF(PARTICLE_SPLASH3,		160, 64, 191, 91, 4.0);
	PDEF(PARTICLE_FIRE,			192, 0, 255, 63, 3.0);
	PDEF(PARTICLE_CRYOSPRAY,	160, 96, 191, 127, 1.0);
	PDEF(PARTICLE_SPARK_1,		32, 64, 63, 95, 1.5);
	PDEF(PARTICLE_SPARK_2,		64, 64, 95, 95, 1.5);
	PDEF(PARTICLE_SIMPLE,		48, 48, 63, 63, 1.5);
	PDEF(PARTICLE_CP1,			96, 32, 127, 63, 1.5);
	PDEF(PARTICLE_CP2,			160, 32, 191, 63, 1.5);
	PDEF(PARTICLE_CP3,			224, 32, 255, 63, 1.5);
	PDEF(PARTICLE_CP4,			224, 96, 255, 127, 1.5);
#undef PDEF
	pdefs[PARTICLE_BIG_SPARKS]		= pdefs[PARTICLE_SPARKS];
	pdefs[PARTICLE_DRIP_SLUDGE]		= pdefs[PARTICLE_DRIP_BLOOD];
	pdefs[PARTICLE_DRIP_WATER]		= pdefs[PARTICLE_DRIP_BLOOD];
	pdefs[PARTICLE_BEAM_SPARKS]		= pdefs[PARTICLE_SPARKS];
	pdefs[PARTICLE_FIREFLY]			= pdefs[PARTICLE_FIRE];
	pdefs[PARTICLE_BIG_SPARKS].fScale = 3.0;
	pdefs[PARTICLE_FIREFLY].fScale = 3.0;
	pdefs[PARTICLE_SIMPLE].fScale = 1.0;
	for (i = 0; i < NUM_PARTICLETYPES; i++)
	{	//half a texel in, as it had them
		pdefs[i].s1 = pdefs[i].s1/w + 1/(w*2);
		pdefs[i].s2 = pdefs[i].s2/w + 1/(w*2);
		pdefs[i].t1 = pdefs[i].t1/h + 1/(h*2);
		pdefs[i].t2 = pdefs[i].t2/h + 1/(h*2);
	}
}

#define MAX_DKPARTICLES 8192
static int		numparts;
static float	pxyz[MAX_DKPARTICLES*3*3], pst[MAX_DKPARTICLES*3*2], prgba[MAX_DKPARTICLES*3*4];
static CVector	pvieworg, pvpn, pvright, pvup;

static void CL_ParticleTri(const CVector &org, float alpha, particle_type_t type, const CVector &rgb, float pscale)
{
	particle_def_t *d;
	CVector up, right, origin, o;
	float scale;
	int k = numparts*3;
	if (numparts >= MAX_DKPARTICLES || type < 0 || type >= NUM_PARTICLETYPES || alpha <= 0)
		return;
	d = &pdefs[type];
	//GL_DrawComplexParticles': a triangle facing the view, bigger with distance
	scale = (org.x - pvieworg.x) * pvpn.x + (org.y - pvieworg.y) * pvpn.y + (org.z - pvieworg.z) * pvpn.z;
	up = pvup * d->fScale;
	right = pvright * d->fScale;
	if (pscale > 1.0)
		scale = pscale;
	else if (scale < 20)
		scale = 1;
	else
		scale = 1 + scale * 0.004;
	o = (up + right) * scale;
	o.Multiply(0.33);
	origin = org - o;

	pxyz[k*3+0] = origin.x;						pxyz[k*3+1] = origin.y;						pxyz[k*3+2] = origin.z;
	pxyz[k*3+3] = origin.x + up.x*scale;		pxyz[k*3+4] = origin.y + up.y*scale;		pxyz[k*3+5] = origin.z + up.z*scale;
	pxyz[k*3+6] = origin.x + right.x*scale;		pxyz[k*3+7] = origin.y + right.y*scale;		pxyz[k*3+8] = origin.z + right.z*scale;
	pst[k*2+0] = d->s1;	pst[k*2+1] = d->t1;
	pst[k*2+2] = d->s2;	pst[k*2+3] = d->t1;
	pst[k*2+4] = d->s1;	pst[k*2+5] = d->t2;
	for (k = numparts*3*4; k < (numparts+1)*3*4; k += 4)
	{
		prgba[k+0] = rgb.x;
		prgba[k+1] = rgb.y;
		prgba[k+2] = rgb.z;
		prgba[k+3] = (alpha > 1)?1:alpha;
	}
	numparts++;
}

static CVector	ColourOf(int color)
{	//its palette's colours; the particles that come with an index and no rgb are few: grey for them
	return CVector(0.6f, 0.6f, 0.6f);
}
void V_AddParticle(CVector &org, int color, float alpha, particle_type_t type, float fScale)
{
	CL_ParticleTri(org, alpha, type, ColourOf(color), fScale);
}
void V_AddParticleRGB(CVector &org, int color, float alpha, particle_type_t type, CVector &color_rgb, float fScale)
{
	CL_ParticleTri(org, alpha, type, color_rgb, fScale);
}
void V_AddComParticle(CVector &org, int color, float alpha, particle_type_t type, float pscale, short flags, CVector &color_rgb)
{
	CL_ParticleTri(org, alpha, type, color_rgb, pscale);
}
void V_AddComParticle2(cparticle_t *cp)
{
	CL_ParticleTri(cp->org, cp->alpha, (particle_type_t)cp->type, cp->color_rgb, cp->pscale);
}

/*
=====================================================================
each frame
=====================================================================
*/
static int		dkscene_initialised;
static int		dkscene_registered;
static char		dkimagenames[MAX_IMAGES][MAX_QPATH];	//the configstrings CLDK_ConfigStrings had last
static char		dkclientinfos[MAX_CLIENTS][256];

void CLDK_SceneReset(void)
{
	if (!dkscene_initialised)
	{
		dkscene_initialised = true;
		CL_InitDispatchFunctionList();
		CL_InitParticleDefs();
	}
	CL_ClearEffects();
	CL_ClearTEnts();
	memset(cl_entities, 0, sizeof(cl_entities));
	CLDK_EntitiesReset();
	memset(dkimagenames, 0, sizeof(dkimagenames));
	memset(dkclientinfos, 0, sizeof(dkclientinfos));
	memset(cl.image_precache, 0, sizeof(cl.image_precache));
	memset(cl.clientinfo, 0, sizeof(cl.clientinfo));
	dkscene_registered = false;
}

//cl_parse.cpp's CL_LoadClientinfo: "name\model\skin\character"
static void CL_LoadClientinfo(clientinfo_t *ci, const char *s)
{
	char buf[512], *field[4] = {NULL, NULL, NULL, NULL}, *p;
	int n = 0;
	snprintf(buf, sizeof(buf), "%s", s);
	for (p = buf, field[n++] = p; *p && n < 4; p++)
		if (*p == '\\')
		{
			*p = 0;
			field[n++] = p+1;
		}
	memset(ci, 0, sizeof(*ci));
	snprintf(ci->name, sizeof(ci->name), "%s", field[0]);
	if (!field[1] || !*field[1])
		return;	//no skin specified
	ci->model = re.RegisterModel(field[1], RESOURCE_EPISODE);
	if (field[2] && *field[2])
		ci->skin = re.RegisterSkin(field[2], RESOURCE_EPISODE);
	ci->character = field[3]?atoi(field[3]):0;
	if (ci->character < 0 || ci->character > 2)
		ci->character = 0;	//Hiro
}

//what the server's configstrings were to its client: the images entities have as skins (EF2_IMAGESKIN), the
//players' models and skins (CHARACTER_MODEL_SKINNUM's), as they change
static void CLDK_ConfigStrings(void)
{
	int i;
	for (i = 1; i < MAX_IMAGES; i++)
	{
		const char *name = CLDKF_ImageName(i);
		if (!name)
			name = "";
		if (!strcmp(name, dkimagenames[i]))
			continue;
		snprintf(dkimagenames[i], sizeof(dkimagenames[i]), "%s", name);
		cl.image_precache[i] = *name?re.RegisterSkin(dkimagenames[i], RESOURCE_LEVEL):NULL;
	}
	for (i = 0; i < MAX_CLIENTS; i++)
	{
		const char *s = CLDKF_PlayerSkinString(i);
		if (!s)
			s = "";
		if (!strcmp(s, dkclientinfos[i]))
			continue;
		snprintf(dkclientinfos[i], sizeof(dkclientinfos[i]), "%s", s);
		if (*s)
			CL_LoadClientinfo(&cl.clientinfo[i], s);
		else
			memset(&cl.clientinfo[i], 0, sizeof(cl.clientinfo[i]));
	}
}

//its client's frame state, from FTE's (cldk_ents.cpp has the entities)
static void CLDK_SceneFrame(void)
{
	int i;
	float org[3], ang[3];
	int serverframe = CLDKF_ServerFrame();
	CLDK_ConfigStrings();

	cl.frame.serverframe = serverframe;
	cl.frame.valid = true;
	cl.lerpfrac = CLDKF_LerpFrac();
	cls.frametime = CLDKF_FrameTime();
	cls.framecount++;
	cl.playernum = CLDKF_PlayerNum();
	for (i = 1; i < MAX_MODELS; i++)
		cl.model_draw[i] = CLDKF_ModelForIndex(i);

	CLDKF_View(org, ang);
	cl.refdef.vieworg.Set(org[0], org[1], org[2]);
	cl.refdef.viewangles.Set(ang[0], ang[1], ang[2]);
	pvieworg = cl.refdef.vieworg;
	AngleToVectors(cl.refdef.viewangles, pvpn, pvright, pvup);
	cl.v_forward = pvpn;
	cl.v_right = pvright;
	cl.v_up = pvup;

	cv_cl_bob.value = CLDKF_CvarValue("cl_bob", "1");
	cv_weapon_visible.value = CLDKF_CvarValue("weapon_visible", "1");
	cv_sv_violence.value = CLDKF_CvarValue("sv_violence", "0");
	cv_fov.value = CLDKF_CvarValue("fov", "90");
}

extern "C" void CLDK_AddScene(void)
{
	if (!dkscene_initialised)
		CLDK_SceneReset();
	CLDK_SceneFrame();
	if (!dkscene_registered)
	{	//what its client registered as the level's models and sounds came in
		dkscene_registered = true;
		CL_RegisterTEntModels();
		CL_RegisterTEntSounds();
		pshader = CLDKF_RegisterParticleShader();
		CLDK_RegisterBeams();
	}

	numparts = 0;
	beam_InitFrame();
	CLDK_AddEntities();
	CL_AddTEnts();
	CL_AddParticles();
	CL_AddDLights();
	CLDK_ParticleVolumes();
	CL_AddComParticles();
	CL_AddTrackEnts();
	CLDK_DrawBeams();
	CLDK_DrawSurfaceSprites();

	if (numparts)
		CLDKF_AddTriangles(pshader, numparts*3, pxyz, pst, prgba);
}
