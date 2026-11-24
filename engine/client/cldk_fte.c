/*
What Daikatana's client code (daikatana/client, C++ with headers that clash with FTE's) has of FTE's client, as plain
functions (daikatana/client/cldk_fte.h): pics and drawing on the HUD's area, sounds, cvars, files, music.

GPL v2 or later.
*/
#include "quakedef.h"

#if defined(Q2CLIENT) && defined(DAIKATANA)
#include "shader.h"
#include "../daikatana/client/cldk_fte.h"

static struct
{
	float x, y;		//the area's corner
	float scale;	//its pixels per the HUD's (which needs 480 of them down)
	int w, h;		//in the HUD's
} cldkarea = {0, 0, 1, 640, 480};

qboolean CLDK_IsDaikatana(void)
{
	return !strcmp(com_protocolname.string, "FTE-Daikatana");
}

void *CLDKF_RegisterPic(const char *name, int *width, int *height)
{
	mpic_t *pic = R2D_SafeCachePic(name);
	int w = 0, h = 0;
	if (!pic || R_GetShaderSizes(pic, &w, &h, true) <= 0 || w <= 0 || h <= 0)
	{
		*width = *height = 0;
		return NULL;
	}
	*width = w;
	*height = h;
	return pic;
}
void CLDKF_DrawPic(void *pic, float x, float y, float w, float h, float s1, float t1, float s2, float t2, const float *rgba)
{
	R2D_ImageColours(rgba[0], rgba[1], rgba[2], rgba[3]);
	R2D_Image(cldkarea.x + x*cldkarea.scale, cldkarea.y + y*cldkarea.scale, w*cldkarea.scale, h*cldkarea.scale, s1, t1, s2, t2, pic);
	R2D_ImageColours(1, 1, 1, 1);
}
void CLDKF_DrawFill(float x, float y, float w, float h, const float *rgba)
{
	R2D_ImageColours(rgba[0], rgba[1], rgba[2], rgba[3]);
	R2D_FillBlock(cldkarea.x + x*cldkarea.scale, cldkarea.y + y*cldkarea.scale, w*cldkarea.scale, h*cldkarea.scale);
	R2D_ImageColours(1, 1, 1, 1);
}
void CLDKF_VidSize(int *width, int *height)
{
	*width = cldkarea.w;
	*height = cldkarea.h;
}
int CLDKF_Milliseconds(void)
{
	return (int)(cl.time*1000);
}
float CLDKF_CvarValue(const char *name, const char *defaultvalue)
{
	cvar_t *v = Cvar_FindVar(name);
	if (!v)
		v = Cvar_Get(name, defaultvalue, CVAR_ARCHIVE, "Daikatana");
	return v?v->value:atof(defaultvalue);
}
void CLDKF_CvarSet(const char *name, const char *value)
{
	cvar_t *v = Cvar_FindVar(name);
	if (!v)
		v = Cvar_Get(name, value, 0, "Daikatana");
	if (v)
		Cvar_Set(v, value);
}
void CLDKF_LocalSound(const char *name)
{
	S_LocalSound(name);
}
void CLDKF_AddCommand(const char *text)
{
	Cbuf_AddText(text, RESTRICT_LOCAL);
}
void CLDKF_Print(const char *text)
{
	Con_Printf("%s", text);
}
int CLDKF_Stat(int i)
{
	if (i < 0 || i >= Q2MAX_STATS)
		return 0;
	return cl.q2frame.seat[0].playerstate.stats[i];
}
int CLDKF_RDFlags(void)
{
	return cl.q2frame.seat[0].playerstate.rdflags;
}
const char *CLDKF_MapName(void)
{
	return cl.model_name[1]?COM_SkipPath(cl.model_name[1]):"";
}
int CLDKF_ModelIndex(const char *name)
{
	int i;
	for (i = 1; i < MAX_PRECACHE_MODELS && cl.model_name[i]; i++)
		if (!strcasecmp(cl.model_name[i], name))
			return i;
	return 0;
}
const char *CLDKF_ModelName(int modelindex)
{
	if (modelindex > 0 && modelindex < MAX_PRECACHE_MODELS && cl.model_name[modelindex])
		return cl.model_name[modelindex];
	return "";
}
void *CLDKF_LoadFile(const char *name, int *length)
{
	size_t len;
	void *buf = FS_LoadMallocFile(name, &len);
	*length = buf?(int)len:0;
	return buf;
}
void CLDKF_FreeFile(void *buf)
{
	BZ_Free(buf);
}
void CLDKF_Music(const char *name, int loop)
{
	if (!name)
		Media_NamedTrack(NULL, NULL);
	else
		Media_NamedTrack(name, loop?name:NULL);
}

/*
=====================================================================
its scene: what its client's effects (cl_tent, cl_fx...) add each frame
=====================================================================
*/
qboolean CLQ2_GetEntityState(int entnum, float *origin, float *angles, float *lerporigin, int *modelindex, int *frame);
entity_t *CL_NewTempEntity (void);	//cl_tent.c's

void *CLDKF_RegisterModel(const char *name)
{
	if (!name || !*name)
		return NULL;
	return Mod_ForName(name, MLV_WARN);
}
void *CLDKF_ModelForIndex(int modelindex)
{
	if (modelindex <= 0 || modelindex >= MAX_PRECACHE_MODELS)
		return NULL;
	return cl.model_precache[modelindex];
}
//its renderfx: some Quake II's in other bits
#define DKRF_MINLIGHT		0x00000001
#define DKRF_VIEWERMODEL	0x00000002
#define DKRF_WEAPONMODEL	0x00000004
#define DKRF_FULLBRIGHT		0x00000008
#define DKRF_BEAM			0x00000010
#define DKRF_TRANSLUCENT	0x00000020
#define DKRF_DEPTHHACK		0x00000080
#define DKRF_GLOW			0x00000200
#define DKRF_SPOTLIGHT		0x00008000
#define DKRF_NODRAW			0x00080000
#define DKRF_PARTICLEVOLUME	0x04000000
void CLDKF_AddEntity(void *model, const float *origin, const float *oldorigin, const float *angles, int frame, int oldframe,
	float backlerp, int skinnum, float alpha, float scale, const float *rgb, unsigned int dkrenderfx)
{
	entity_t *ent;
	if (!model || (dkrenderfx & (DKRF_NODRAW|DKRF_SPOTLIGHT|DKRF_PARTICLEVOLUME)))
		return;	//(its renderer drew spotlights as volumes, to render_scale: not yet; particle volumes are its
				//client's rain and snow, in their brush model's bounds, not the brushes)
	ent = CL_NewTempEntity();
	if (!ent)
		return;
	VectorCopy(origin, ent->origin);
	VectorCopy(oldorigin, ent->oldorigin);
	VectorCopy(angles, ent->angles);
	ent->angles[0] *= r_meshpitch.value;
	ent->angles[2] *= r_meshroll.value;
	AngleVectors(ent->angles, ent->axis[0], ent->axis[1], ent->axis[2]);
	VectorInverse(ent->axis[1]);
	ent->model = model;
	ent->framestate.g[FS_REG].frame[0] = frame;
	ent->framestate.g[FS_REG].frame[1] = oldframe;
	ent->framestate.g[FS_REG].lerpweight[0] = 1-backlerp;
	ent->framestate.g[FS_REG].lerpweight[1] = backlerp;
	ent->skinnum = skinnum;
	ent->scale = (scale > 0)?scale:1;
	if (rgb && (rgb[0] || rgb[1] || rgb[2]))
		VectorCopy(rgb, ent->shaderRGBAf);
	ent->flags = dkrenderfx & (DKRF_MINLIGHT|DKRF_VIEWERMODEL|DKRF_WEAPONMODEL|DKRF_FULLBRIGHT|DKRF_TRANSLUCENT|DKRF_GLOW);
	if (dkrenderfx & DKRF_DEPTHHACK)
		ent->flags |= RF_DEPTHHACK;
	if (dkrenderfx & DKRF_WEAPONMODEL)
	{	//its view model's where the view is, in the world (bobbing as its client has it): FTE's are relative to it
		vec3_t d;
		int i;
		//the matrix FTE draws them by is the view (its Quake II gun's sets it, and there's none here)
		AngleVectors(r_refdef.viewangles, r_refdef.weaponmatrix[0], r_refdef.weaponmatrix[1], r_refdef.weaponmatrix[2]);
		VectorInverse(r_refdef.weaponmatrix[1]);
		VectorCopy(r_refdef.vieworg, r_refdef.weaponmatrix[3]);
		memcpy(r_refdef.weaponmatrix_bob, r_refdef.weaponmatrix, sizeof(r_refdef.weaponmatrix_bob));
		VectorSubtract(ent->origin, r_refdef.vieworg, d);
		for (i = 0; i < 3; i++)
			ent->origin[i] = DotProduct(d, r_refdef.weaponmatrix[i]);
		VectorCopy(ent->origin, ent->oldorigin);
		VectorSubtract(angles, r_refdef.viewangles, ent->angles);	//(the view's, for its view model)
		ent->angles[0] *= r_meshpitch.value;
		ent->angles[2] *= r_meshroll.value;
		AngleVectors(ent->angles, ent->axis[0], ent->axis[1], ent->axis[2]);
		VectorInverse(ent->axis[1]);
		ent->flags |= RF_WEAPONMODELNOBOB|RF_NOSHADOW;
	}
	if (dkrenderfx & DKRF_TRANSLUCENT)
		ent->shaderRGBAf[3] = alpha;
	if (dkrenderfx & DKRF_BEAM)
		ent->rtype = RT_BEAM;
	if (cl.worldmodel && !(ent->flags & RF_WEAPONMODEL))
	{	//the leaves it's in, for the visibility culling (as cl_ents.c has them: rotated or scaled, by its radius)
		model_t *mod = model;
		vec3_t absmin, absmax;
		int i;
		float r = 0;
		if (mod->type == mod_brush && (angles[0] || angles[1] || angles[2]))
		{
			for (i = 0; i < 3; i++)
				r = max(r, max(fabs(mod->mins[i]), fabs(mod->maxs[i])));
		}
		else if (mod->type != mod_brush && (angles[0] || angles[1] || angles[2] || ent->scale != 1))
			r = mod->radius * ent->scale;
		for (i = 0; i < 3; i++)
		{
			absmin[i] = ent->origin[i] + (r?-r:mod->mins[i]);
			absmax[i] = ent->origin[i] + (r?r:mod->maxs[i]);
		}
		cl.worldmodel->funcs.FindTouchedLeafs(cl.worldmodel, &ent->pvscache, absmin, absmax);
	}
}
void CLDKF_AddLight(const float *origin, float radius, float r, float g, float b)
{
	vec3_t org;
	VectorCopy(origin, org);
	V_AddLight(0, org, radius, r, g, b);
}
//triangles (numverts a multiple of 3) in the scene with a shader of its own
void CLDKF_AddTriangles(void *shader, int numverts, const float *xyz, const float *st, const float *rgba)
{
	scenetris_t *t;
	int i;
	if (!shader || numverts < 3)
		return;
	if (cl_numstrisvert + numverts > cl_maxstrisvert)
		cl_stris_ExpandVerts(cl_numstrisvert + numverts + 64);
	if (cl_numstrisidx + numverts > cl_maxstrisidx)
	{
		cl_maxstrisidx = cl_numstrisidx + numverts + 64;
		cl_strisidx = BZ_Realloc(cl_strisidx, sizeof(*cl_strisidx)*cl_maxstrisidx);
	}
	if (cl_numstris == cl_maxstris)
	{
		cl_maxstris += 8;
		cl_stris = BZ_Realloc(cl_stris, sizeof(*cl_stris)*cl_maxstris);
	}
	t = &cl_stris[cl_numstris++];
	t->shader = shader;
	t->flags = 0;
	t->lightmap = -1;
	t->firstvert = cl_numstrisvert;
	t->firstidx = cl_numstrisidx;
	for (i = 0; i < numverts; i++)
	{
		VectorCopy(xyz+i*3, cl_strisvertv[cl_numstrisvert+i]);
		Vector2Copy(st+i*2, cl_strisvertt[cl_numstrisvert+i]);
		Vector4Copy(rgba+i*4, cl_strisvertc[cl_numstrisvert+i]);
		cl_strisidx[cl_numstrisidx+i] = cl_numstrisvert+i - t->firstvert;
	}
	cl_numstrisvert += numverts;
	cl_numstrisidx += numverts;
	t->numvert = numverts;
	t->numidx = numverts;
}
void *CLDKF_RegisterParticleShader(void)
{	//its particles' atlas, blended over the scene as its renderer had them
	return R_RegisterShader("dkparticles", SUF_NONE,
		"{\n"
			"nomipmaps\n"
			"{\n"
				"map pics/particles/particles.tga\n"
				"blendfunc blend\n"
				"rgbgen vertex\n"
				"alphagen vertex\n"
			"}\n"
		"}\n");
}
void *CLDKF_RegisterBeamShader(const char *texture)
{	//its beams' (gl_beams.cpp): blended over the scene, coloured and faded by their vertices
	return R_RegisterShader(va("dkbeam:%s", texture?texture:"none"), SUF_NONE,
		va("{\n"
			"cull disable\n"
			"{\n"
				"map %s\n"
				"blendfunc blend\n"
				"rgbgen vertex\n"
				"alphagen vertex\n"
			"}\n"
		"}\n", texture?texture:"$whiteimage"));
}
void *CLDKF_RegisterSound(const char *name)
{
	if (!name || !*name)
		return NULL;
	return S_PrecacheSound(name);
}
void CLDKF_StartSound(void *sfx, const float *origin, int entnum, int channel, float volume, float attenuation, float timeofs)
{
	vec3_t org;
	if (!sfx)
		return;
	if (origin)
		VectorCopy(origin, org);
	S_StartSound(entnum, channel, sfx, origin?org:NULL, NULL, volume, attenuation, timeofs, 0, 0);
}
float CLDKF_Trace(const float *start, const float *end, int passent, int mask, float *endpos, float *normal, int *entnum, int *contents)
{
	vec3_t s, e;
	float frac;
	int ent = 0;
	VectorCopy(start, s);
	VectorCopy(end, e);
	frac = CL_TraceLine(s, e, endpos, normal, &ent);
	*entnum = ent;
	*contents = (frac < 1 && cl.worldmodel)?cl.worldmodel->funcs.PointContents(cl.worldmodel, NULL, endpos):0;
	return frac;
}
int CLDKF_PointContents(const float *point)
{
	vec3_t p;
	if (!cl.worldmodel)
		return 0;
	VectorCopy(point, p);
	return cl.worldmodel->funcs.PointContents(cl.worldmodel, NULL, p);
}
int CLDKF_EntityState(int entnum, float *origin, float *angles, float *lerporigin, int *modelindex, int *frame)
{
	return CLQ2_GetEntityState(entnum, origin, angles, lerporigin, modelindex, frame);
}
int CLQ2_NumFrameEntities(void);
int CLQ2_FrameEntity(int i, entity_state_t **current, entity_state_t **prev, qboolean *isnew);
int CLDKF_NumFrameEntities(void)
{
	return CLQ2_NumFrameEntities();
}
static void CLDK_EState(cldk_estate_t *o, const entity_state_t *s)
{
	VectorCopy(s->origin, o->origin);
	VectorCopy(s->angles, o->angles);
	VectorCopy(s->u.q2.old_origin, o->old_origin);
	o->modelindex = s->modelindex;
	o->frame = s->frame;
	o->skinnum = s->skinnum;
	o->sound = s->u.q2.sound;
	o->event = s->u.q2.event;
	o->effects = s->effects;
	o->renderfx = s->u.q2.renderfx;
}
int CLDKF_FrameEntity(int i, cldk_estate_t *current, cldk_estate_t *prev, int *isnew)
{
	entity_state_t *cur, *old;
	qboolean created;
	int num = CLQ2_FrameEntity(i, &cur, &old, &created);
	if (!num)
		return 0;
	CLDK_EState(current, cur);
	CLDK_EState(prev, old);
	*isnew = created;
	return num;
}
void CLDKF_PlayerVelocity(float *velocity)
{
	int i;
	for (i = 0; i < 3; i++)
		velocity[i] = cl.q2frame.seat[0].playerstate.pmove.velocity[i] * 0.125;
}
void CLDKF_View(float *origin, float *angles)
{
	VectorCopy(r_refdef.vieworg, origin);
	VectorCopy(r_refdef.viewangles, angles);
}
int CLDKF_ServerFrame(void)
{
	return cl.q2frame.serverframe;
}
float CLDKF_LerpFrac(void)
{
	return cl.lerpfrac;
}
float CLDKF_FrameTime(void)
{
	return host_frametime;
}
int CLDKF_PlayerNum(void)
{
	return cl.playerview[0].playernum;
}

//the status bar and the rest of its HUD, for the first seat (it had no splitscreen)
qboolean CLDK_DrawHUDArea(int seat, const vrect_t *area)
{
	if (!CLDK_IsDaikatana())
		return false;
	if (seat)
		return true;
	cldkarea.x = area->x;
	cldkarea.y = area->y;
	cldkarea.scale = (area->height < 480)?area->height/480.0:1;
	cldkarea.w = area->width / cldkarea.scale;
	cldkarea.h = area->height / cldkarea.scale;
	CLDK_DrawHUD();
	return true;
}

void CLDK_ParseServerMessage(void)
{	//svcq2dk_message: [short length][that many bytes of Daikatana's own]
	static qbyte buf[65536];
	int len = MSG_ReadShort();
	if (len < 0 || len > sizeof(buf))
	{
		Host_EndGame("CLDK_ParseServerMessage: bad length %i", len);
		return;
	}
	MSG_ReadData(buf, len);
	if (CLDK_IsDaikatana())
		CLDK_ParseMessage(buf, len);
}
#endif
