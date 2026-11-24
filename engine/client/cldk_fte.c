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

static void CLDK_MenuForce_f(void);
static skinid_t CLDK_ModelSkin(model_t *mod, void *skin);
static void CLDK_ToggleQuickConsole_f(void)
{	//its small console (default_keys.cfg's `), FTE's
	Cbuf_InsertText("toggleconsole\n", Cmd_ExecLevel, false);
}
qboolean CLDK_IsDaikatana(void)
{
	static qboolean commands;
	if (strcmp(com_protocolname.string, "FTE-Daikatana"))
		return false;
	if (!commands)
	{	//its client's commands FTE hasn't
		static const struct {const char *name, *value;} userinfo[] =
		{	//and its userinfo cvars (cl_main.cpp's, its menus'), which its game reads (the player's model in
			//deathmatch's from modelname: without, it had none, nor its weapon)
			{"modelname", "models/global/m_hiro.dkm"}, {"skinname", "skins/hiro_bod_1.wal"}, {"skincolor", "0"},
			{"character", "0"}, {"ctf_team", "0"}, {"dt_team", "0"}, {"dm_taunts", "1"}, {"cl_autowweaponswitch", "0"},
		};
		int i;
		commands = true;
		for (i = 0; i < countof(userinfo); i++)
			Cvar_Get(userinfo[i].name, userinfo[i].value, CVAR_USERINFO|CVAR_ARCHIVE, "Daikatana");
		if (!Cmd_Exists("menu_main_force"))
			Cmd_AddCommand("menu_main_force", CLDK_MenuForce_f);
		if (!Cmd_Exists("togglequickconsole"))
			Cmd_AddCommand("togglequickconsole", CLDK_ToggleQuickConsole_f);
	}
	return true;
}

/*
Its default.cfg, which it hasn't: its engine's daikatana.cfg (exec default_keys.cfg's binds, then its settings), less
what was of its own engine's video, sound, input and network drivers, which FTE's own defaults are better at.
*/
static qboolean CLDK_DefaultSkipped(const char *name)
{
	static const char *skip[] = {"gl_", "vid_", "s_", "scr_", "in_", "win_", "r_", "cl_stereo", "AudioEngDLL", "rate",
		"dedicated", "developer", "hostname", "log_server", "LogFilePath", "LogOutput", "viewsize", NULL};
	int i;
	for (i = 0; skip[i]; i++)
		if (!Q_strncasecmp(name, skip[i], strlen(skip[i])))
			return true;
	return false;
}
char *CLDK_DefaultConfig(void)
{
	size_t len, o;
	char *file, *out, *line, *next, name[64];
	const char *s;
	if (!CLDK_IsDaikatana())
		return NULL;
	file = FS_LoadMallocFile("daikatana.cfg", &len);
	if (!file)
		return NULL;
	out = Z_Malloc(len + 64);
	o = strlen(strcpy(out, "unbindall\n"));
	for (line = file; line && *line; line = next)
	{
		next = strchr(line, '\n');
		if (next)
			*next++ = 0;
		s = line;
		while (*s == ' ' || *s == '\t')
			s++;
		if (!Q_strncasecmp(s, "set ", 4) || !Q_strncasecmp(s, "seta ", 5))
		{
			s = COM_ParseOut(strchr(s, ' '), name, sizeof(name));
			if (!s || CLDK_DefaultSkipped(name))
				continue;
		}
		o += strlen(strcpy(out + o, line));
		out[o++] = '\n';
		out[o] = 0;
	}
	BZ_Free(file);
	return out;
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
int CLDKF_RealMilliseconds(void)
{
	return (int)(realtime*1000);
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
const char *CLDKF_ImageName(int index)
{
	if (index > 0 && index < countof(cl.image_name) && cl.image_name[index] && *cl.image_name[index])
		return cl.image_name[index];
	return NULL;
}
//its fog (its renderer's GL_LINEAR, from start to end), as FTE's under r_fog_linear (the gamemode's set it):
//density's the end, depthbias the start
void CLDKF_SetFog(int active, float r, float g, float b, float start, float end)
{
	fogstate_t *f = &cl.fog[FOGTYPE_AIR];
	if (!active || end <= 0)
	{
		if (f->density)
			CL_ResetFog(FOGTYPE_AIR);
		return;
	}
	if (f->density == end && f->depthbias == start && f->alpha == 1 &&
		f->colour[0] == SRGBf(r) && f->colour[1] == SRGBf(g) && f->colour[2] == SRGBf(b))
		return;
	CL_ResetFog(FOGTYPE_AIR);
	f->density = end;
	f->depthbias = start;
	f->colour[0] = SRGBf(r);
	f->colour[1] = SRGBf(g);
	f->colour[2] = SRGBf(b);
	f->alpha = 1;
}
//its view in water (cl_ents.cpp's CL_CalcViewValues): fogged in the water's colour (CMDK_SurfaceColour) from 32 to
//1024 (FTE's water fog, which it has in water). Its tint of the colour lasted only the frame the view went in (the
//server's blend replaced it each frame after), so there's none: nor FTE's own
qboolean CLDK_ContentsColour(int contents, cshift_t *shift)
{
	vec3_t rgb, c;
	fogstate_t *f = &cl.fog[FOGTYPE_WATER];
	int i;
	if (!(contents & (FTECONTENTS_WATER|FTECONTENTS_SLIME|FTECONTENTS_LAVA)) || !CLDK_IsDaikatana())
		return false;
	if (!CMDK_SurfaceColour(cl.worldmodel, r_refdef.vieworg, rgb))
		return false;
	for (i = 0; i < 3; i++)
	{
		shift->destcolor[i] = rgb[i]*255;
		c[i] = SRGBf(rgb[i]);
	}
	shift->percent = 0;
	if (f->density != 1024 || f->depthbias != 32 || f->alpha != 1 || !VectorCompare(f->colour, c))
	{
		f->density = 1024;
		f->depthbias = 32;
		VectorCopy(c, f->colour);
		f->alpha = 1;
		f->time = realtime;
		VectorCopy(c, cl.oldfog[FOGTYPE_WATER].colour);	//(no fading from what it was)
		cl.oldfog[FOGTYPE_WATER].density = f->density;
		cl.oldfog[FOGTYPE_WATER].depthbias = f->depthbias;
		cl.oldfog[FOGTYPE_WATER].alpha = 1;
	}
	return true;
}
const char *CLDKF_PlayerSkinString(int player)
{	//(cl_parse.c keeps them at the start of the general ones)
	if (player >= 0 && player < Q2MAX_CLIENTS && player < countof(cl.configstring_general) && cl.configstring_general[player])
		return cl.configstring_general[player];
	return NULL;
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
//its MP3 channels past the music's (voices): sounds of their own channels, everywhere
#define CLDK_STREAMCHANNEL	(1024)
void CLDKF_PlayStream(const char *name, int chan, float volume, int loop)
{
	sfx_t *sfx = S_PrecacheSound(name);
	Con_DPrintf("Daikatana: mp3 channel %i: %s%s\n", chan, name, sfx?"":" (no sound)");
	if (!sfx)
		return;
	S_StopSound(0, CLDK_STREAMCHANNEL + chan);
	S_StartSound(0, CLDK_STREAMCHANNEL + chan, sfx, NULL, NULL, (volume > 0)?volume:1, ATTN_NONE, 0, 0, loop?CF_FORCELOOP:0);
}
void CLDKF_StopStream(int chan)
{
	S_StopSound(0, CLDK_STREAMCHANNEL + chan);
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
const char *CLDKF_ModelPath(void *model)
{
	return model?((model_t *)model)->name:"";
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
	float backlerp, int skinnum, void *skin, float alpha, float scale, const float *rgb, unsigned int dkrenderfx)
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
	if (skin)
		ent->customskin = CLDK_ModelSkin(model, skin);
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
void *CLDKF_RegisterDecalShader(const char *texture)
{	//its surface sprites' (gl_surfsprite.cpp): bullet holes, scorch marks, blood on the walls
	return R_RegisterShader(va("dkdecal:%s", texture), SUF_NONE,
		va("{\n"
			"cull disable\n"
			"polygonoffset\n"
			"{\n"
				"clampmap %s\n"
				"blendfunc blend\n"
				"rgbgen vertex\n"
				"alphagen vertex\n"
			"}\n"
		"}\n", texture));
}
void *CLDKF_RegisterGlowShader(void)
{	//its spotlights' volumes (R_DrawSpotlight): vertex coloured, GL_SRC_ALPHA GL_ONE
	return R_RegisterShader("dkglow", SUF_NONE,
		"{\n"
			"cull disable\n"
			"{\n"
				"map $whiteimage\n"
				"blendfunc GL_SRC_ALPHA GL_ONE\n"
				"rgbgen vertex\n"
				"alphagen vertex\n"
			"}\n"
		"}\n");
}
int CLDKF_PlaneNormal(int planeindex, float *normal)
{
	if (!cl.worldmodel || planeindex < 0 || planeindex >= cl.worldmodel->numplanes || !cl.worldmodel->planes)
		return false;
	VectorCopy(cl.worldmodel->planes[planeindex].normal, normal);
	return true;
}
void CLDKF_LightPoint(const float *point, float *rgb)
{
	vec3_t diffuse, ambient, dir;
	if (!cl.worldmodel || !cl.worldmodel->funcs.LightPointValues)
	{
		VectorSet(rgb, 1, 1, 1);
		return;
	}
	cl.worldmodel->funcs.LightPointValues(cl.worldmodel, point, diffuse, ambient, dir);
	VectorAdd(diffuse, ambient, rgb);
	VectorScale(rgb, 1/255.0, rgb);
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

//models on its HUD: a view of no world, orthographic, over the area, as clq2_menu.c's player preview
void CLDKF_DrawIcons(float x, float y, float w, float h, int num, const cldk_icon_t *icons)
{
	static refdef_t saved;
	entity_t ent;
	int i, any = 0;
	if (num <= 0 || w <= 0 || h <= 0)
		return;
	for (i = 0; i < num; i++)
		if (icons[i].model && icons[i].alpha > 0)
			any++;
	if (!any)
		return;
	if (R2D_Flush)
		R2D_Flush();
	memcpy(&saved, &r_refdef, sizeof(saved));
	CL_ClearEntityLists();
	for (i = 0; i < num; i++)
	{
		const cldk_icon_t *ic = &icons[i];
		vec3_t f, r, u;
		if (!ic->model || ic->alpha <= 0)
			continue;
		V_ClearEntity(&ent);
		ent.model = ic->model;
		//the view's a unit to the HUD's
		ent.origin[0] = 512;
		ent.origin[1] = -(ic->x - w/2);
		ent.origin[2] = -(ic->y - h/2);
		VectorCopy(ent.origin, ent.oldorigin);
		//its axes in its view (x right, y down, z out) to FTE's (x in, y left, z up), mirrored sideways (that
		//keeps it a rotation, its own view being a mirror)
		AngleVectors(ic->angles, f, r, u);
		VectorSet(ent.axis[0], -f[2], f[0], -f[1]);
		VectorSet(ent.axis[1], r[2], -r[0], r[1]);	//(left, of right)
		VectorSet(ent.axis[2], -u[2], u[0], -u[1]);
		ent.scale = ic->scale;
		ent.framestate.g[FS_REG].frame[0] = ic->frame;
		ent.framestate.g[FS_REG].frame[1] = ic->oldframe;
		ent.framestate.g[FS_REG].lerpweight[0] = 1 - ic->backlerp;
		ent.framestate.g[FS_REG].lerpweight[1] = ic->backlerp;
		ent.flags = RF_FULLBRIGHT|RF_NOSHADOW;
		Vector4Set(ent.shaderRGBAf, 1, 1, 1, bound(0, ic->alpha, 1));
		if (ic->alpha < 1)
			ent.flags |= RF_TRANSLUCENT;
		VectorSet(ent.glowmod, 1, 1, 1);
		V_AddEntity(&ent);
	}

	VectorClear(r_refdef.vieworg);
	VectorClear(r_refdef.viewangles);
	VectorSet(r_refdef.viewaxis[0], 1, 0, 0);
	VectorSet(r_refdef.viewaxis[1], 0, 1, 0);
	VectorSet(r_refdef.viewaxis[2], 0, 0, 1);
	r_refdef.flags = RDF_NOWORLDMODEL;
	r_refdef.fovv_x = r_refdef.fov_x = w;
	r_refdef.fovv_y = r_refdef.fov_y = h;
	r_refdef.vrect.x = cldkarea.x + x*cldkarea.scale;
	r_refdef.vrect.y = cldkarea.y + y*cldkarea.scale;
	r_refdef.vrect.width = w*cldkarea.scale;
	r_refdef.vrect.height = h*cldkarea.scale;
	r_refdef.time = cl.time;
	r_refdef.useperspective = false;
	r_refdef.mindist = 1;
	r_refdef.maxdist = 2048;
	r_refdef.playerview = &cl.playerview[0];
	memset(&r_refdef.globalfog, 0, sizeof(r_refdef.globalfog));
	r_refdef.areabitsknown = false;
	R_RenderView();
	memcpy(&r_refdef, &saved, sizeof(saved));
}

/*
=====================================================================
its menus (daikatana/client/menu): FTE's menu while it's the game
=====================================================================
*/
void *CLDKF_GLGetProcAddress(const char *name)
{	//(FTE keeps its GL to itself: its video menu has no driver info)
	return NULL;
}
//its cvar names that are FTE's by others
static const char *CLDK_CvarName(const char *name)
{
	static const struct {const char *dk, *fte;} names[] =
	{
		{"s_volume",	"volume"},
		{"s_mp3_volume",	"bgmvolume"},
		{"vid_gamma",	"gamma"},
		{"in_mouse",	"in_mouse"},
	};
	size_t i;
	for (i = 0; i < countof(names); i++)
		if (!strcmp(names[i].dk, name))
			return names[i].fte;
	return name;
}
const char *CLDKF_CvarString(const char *name)
{
	cvar_t *v = Cvar_FindVar(CLDK_CvarName(name));
	return v?v->string:"";
}
const char *CLDKF_CvarCreate(const char *name, const char *value, int archive)
{
	cvar_t *v = Cvar_FindVar(CLDK_CvarName(name));
	if (!v)
		v = Cvar_Get(CLDK_CvarName(name), value?value:"", archive?CVAR_ARCHIVE:0, "Daikatana");
	return v?v->string:"";
}

//its key numbers (Quake II's) and FTE's
static int CLDK_FTEKey(int k)
{
	static const struct {int dk, fte;} keys[] =
	{
		{128, K_UPARROW}, {129, K_DOWNARROW}, {130, K_LEFTARROW}, {131, K_RIGHTARROW},
		{132, K_LALT}, {133, K_LCTRL}, {134, K_LSHIFT},
		{135, K_F1}, {136, K_F2}, {137, K_F3}, {138, K_F4}, {139, K_F5}, {140, K_F6},
		{141, K_F7}, {142, K_F8}, {143, K_F9}, {144, K_F10}, {145, K_F11}, {146, K_F12},
		{147, K_INS}, {148, K_DEL}, {149, K_PGDN}, {150, K_PGUP}, {151, K_HOME}, {152, K_END},
		{160, K_KP_HOME}, {161, K_KP_UPARROW}, {162, K_KP_PGUP}, {163, K_KP_LEFTARROW}, {164, K_KP_5},
		{165, K_KP_RIGHTARROW}, {166, K_KP_END}, {167, K_KP_DOWNARROW}, {168, K_KP_PGDN}, {169, K_KP_ENTER},
		{170, K_KP_INS}, {171, K_KP_DEL}, {172, K_KP_SLASH}, {173, K_KP_MINUS}, {174, K_KP_PLUS},
		{200, K_MOUSE1}, {201, K_MOUSE2}, {202, K_MOUSE3},
		{203, K_JOY1}, {204, K_JOY2}, {205, K_JOY3}, {206, K_JOY4},
		{239, K_MWHEELDOWN}, {240, K_MWHEELUP}, {255, K_PAUSE},
	};
	size_t i;
	if (k > 0 && k < 128)
		return k;
	for (i = 0; i < countof(keys); i++)
		if (keys[i].dk == k)
			return keys[i].fte;
	if (k >= 207 && k <= 238)
		return K_AUX1 + (k - 207);
	return 0;
}
static int CLDK_DKKey(int k)
{
	int d;
	if (k > 0 && k < 128)
		return k;
	if (k == K_RALT)	return 132;
	if (k == K_RCTRL)	return 133;
	if (k == K_RSHIFT)	return 134;
	for (d = 128; d < 256; d++)
		if (CLDK_FTEKey(d) == k)
			return d;
	return 0;
}
const char *CLDKF_KeyBinding(int dkkey)
{
	int k = CLDK_FTEKey(dkkey);
	const char *b;
	if (!k)
		return NULL;
	b = keybindings[k][0];
	return (b && *b)?b:NULL;
}
void CLDKF_SetKeyBinding(int dkkey, const char *binding)
{
	int k = CLDK_FTEKey(dkkey);
	if (k)
		Key_SetBinding(k, ~0, binding, RESTRICT_LOCAL);
}
const char *CLDKF_KeyName(int dkkey)
{
	int k = CLDK_FTEKey(dkkey);
	return k?Key_KeynumToString(k, 0):"<UNKNOWN KEYNUM>";
}
int CLDKF_KeyDown(int dkkey)
{
	int k = CLDK_FTEKey(dkkey);
	return k && keydown[k];
}
const char *CLDKF_WriteDir(void)
{
	static char path[MAX_OSPATH];
	if (!FS_SystemPath("", FS_GAMEONLY, path, sizeof(path)))
		*path = 0;
	else if (*path && path[strlen(path)-1] == '/')
		path[strlen(path)-1] = 0;	//(its are without)
	return path;
}
void *CLDKF_TagMalloc(size_t size, int tag)
{	//as FTE's Quake II game API's (the game's IonCommon's allocations are freed through either)
	return Z_TagMalloc(size, tag);
}
void CLDKF_TagFree(void *mem)
{
	Z_TagFree(mem);
}
int CLDKF_InGame(void)
{
	return cls.state == ca_active;
}
void CLDKF_StopAllSounds(void)
{
	S_StopAllSounds(true);
}
void CLDKF_MousePos(int *x, int *y)
{	//the menu's drawn over the whole screen, its virtual one (cldkarea's set to that as it is)
	*x = (mousecursor_x - cldkarea.x) / cldkarea.scale;
	*y = (mousecursor_y - cldkarea.y) / cldkarea.scale;
}

static menu_t cldk_menu;
static void CLDK_MenuArea(void)
{	//the whole screen, the HUD's way (its menus fit 640x480 in it)
	cldkarea.x = 0;
	cldkarea.y = 0;
	cldkarea.scale = (vid.height < 480)?vid.height/480.0:1;
	cldkarea.w = vid.width / cldkarea.scale;
	cldkarea.h = vid.height / cldkarea.scale;
}
static qboolean CLDK_MenuKeyEvent(menu_t *m, qboolean isdown, unsigned int devid, int key, int unicode)
{
	int dk;
	CLDK_MenuArea();
	if (key == K_TOUCHTAP)
		key = K_MOUSE1;
	if (isdown && unicode >= 32 && unicode < 127 && !keydown[K_LCTRL] && !keydown[K_RCTRL])
		dk = unicode;	//typed text by character (shift, layouts)
	else
		dk = CLDK_DKKey(key);
	if (key == K_MOUSE1 || key == K_TOUCHTAP)
	{
		int x, y;
		CLDKF_MousePos(&x, &y);
		CLDK_MenuMouse(x, y);
	}
	if (dk)
		CLDK_MenuKey(dk, isdown);
	if (!CLDK_MenuActive())
		CLDKF_CloseMenu();
	return true;
}
static void CLDK_MenuDrawMenu(menu_t *m)
{
	int x, y;
	CLDK_MenuArea();
	CLDKF_MousePos(&x, &y);
	CLDK_MenuMouse(x, y);
	CLDK_MenuDraw();
	if (!CLDK_MenuActive())
		CLDKF_CloseMenu();
}
static void CLDK_MenuRelease(menu_t *m, qboolean forced)
{
	if (CLDK_MenuActive())
		CLDK_MenuKey(-1, true);	//(FTE closed it: a map start, closemenu...: it leaves)
}
void CLDKF_CloseMenu(void)
{
	if (Menu_IsLinked(&cldk_menu))
		Menu_Unlink(&cldk_menu, false);
}
//its menu_main_force <button>: its menus at one of the main buttons (0 new game, 2 load, 3 save...): the game
//stuffs it, and it binds
static void CLDK_MenuForce_f(void)
{
	CLDK_OpenMenuAt(atoi(Cmd_Argv(1)));
}
qboolean CLDK_OpenMenu(void)
{
	return CLDK_OpenMenuAt(-1);
}
//FTE's menu (togglemenu, the escape key) while it's the game
qboolean CLDK_OpenMenuAt(int button)
{
	if (!CLDK_IsDaikatana())
		return false;
	cldk_menu.keyevent = CLDK_MenuKeyEvent;
	cldk_menu.drawmenu = CLDK_MenuDrawMenu;
	cldk_menu.release = CLDK_MenuRelease;
	cldk_menu.cursor = &key_customcursor[kc_console];	//absolute pointer
	cldk_menu.isopaque = true;
	CLDK_MenuArea();
	if (!Menu_IsLinked(&cldk_menu))
	{
		CLDK_MenuOpen(button);
		if (CLDK_MenuActive())
			Menu_Push(&cldk_menu, false);
	}
	else if (button >= 0)
		CLDK_MenuOpen(button);	//(up already: to that button's)
	return true;
}

//its cinematics (its server's, RDF_LETTERBOX with the player's state): 2:1, the rest black, as its client had them
#define DKRDF_LETTERBOX	0x0020
qboolean CLDK_InCinematic(void)
{
	return CLDK_IsDaikatana() && cls.state == ca_active && (CLDKF_RDFlags() & DKRDF_LETTERBOX);
}
static void CLDK_DrawLetterbox(const vrect_t *area)
{
	float h = area->width / 2, bar;
	if (h >= area->height)
		return;
	bar = (area->height - h) / 2;
	R2D_ImageColours(0, 0, 0, 1);
	R2D_FillBlock(area->x, area->y, area->width, bar);
	R2D_FillBlock(area->x, area->y + area->height - bar, area->width, bar);
	R2D_ImageColours(1, 1, 1, 1);
}

/*
An image as a model's skin (its entities' with EF2_IMAGESKIN, its players', its menus' buttons'): its renderer put it
on every surface of the model but those with SRF_STATICSKIN. The handle's the image's; the skin file's the model's
with it (CLDK_ModelSkin), made from the dkm's surfaces as the model's drawn.
*/
#define DKSRF_STATICSKIN	0x00000080
static char cldkskinimages[512][MAX_QPATH];
static int cldknumskinimages;
void *CLDKF_RegisterSkin(const char *image)
{
	int i;
	if (!image || !*image)
		return NULL;
	for (i = 0; i < cldknumskinimages; i++)
		if (!strcmp(cldkskinimages[i], image))
			return (void *)(size_t)(i+1);
	if (cldknumskinimages == countof(cldkskinimages))
		return NULL;
	Q_strncpyz(cldkskinimages[cldknumskinimages], image, sizeof(cldkskinimages[0]));
	return (void *)(size_t)++cldknumskinimages;
}
static const char *CLDK_SkinText(model_t *mod, const char *image)
{
	static char text[8192];
	size_t len, o = 0;
	qbyte *f = FS_LoadMallocFile(mod->name, &len);
	int i, num, ofs;
	Q_snprintfz(text, sizeof(text), "replace \"\" \"%s\"\n", image);	//(what's not a dkm: all of it)
	if (f && len >= 4*22 && !memcmp(f, "DKMD", 4))
	{	//dkmheader_t's num_surfaces, ofs_surfaces; dkmsurface_t's name[32], flags
		num = LittleLong(((int *)f)[12]);
		ofs = LittleLong(((int *)f)[18]);
		*text = 0;
		for (i = 0; i < num && ofs >= 0 && ofs + (i+1)*52 <= (int)len; i++)
		{
			const qbyte *s = f + ofs + i*52;
			char name[33];
			memcpy(name, s, 32);
			name[32] = 0;
			if (LittleLong(*(const int *)(s+32)) & DKSRF_STATICSKIN)
				continue;
			Q_snprintfz(text+o, sizeof(text)-o, "replace \"%s\" \"%s\"\n", name, image);
			o += strlen(text+o);
		}
	}
	if (f)
		BZ_Free(f);
	return text;
}
static skinid_t CLDK_ModelSkin(model_t *mod, void *skin)
{
	static struct {model_t *mod; int image; skinid_t id;} skins[512];
	static int numskins;
	int image = (int)(size_t)skin - 1, i;
	if (!mod || image < 0 || image >= cldknumskinimages)
		return 0;
	for (i = 0; i < numskins; i++)
		if (skins[i].mod == mod && skins[i].image == image)
			return skins[i].id;
	if (numskins == countof(skins))
		return 0;
	skins[numskins].mod = mod;
	skins[numskins].image = image;
	skins[numskins].id = Mod_ReadSkinFile(va("%s_dk%i.skin", mod->name, image), CLDK_SkinText(mod, cldkskinimages[image]));
	return skins[numskins++].id;
}
//its re.RenderFrame with no world (its menus'), as clq2_menu.c's player preview
void CLDKF_RenderView(float x, float y, float w, float h, float fov_x, float fov_y, const float *vieworg,
	const float *viewangles, int num, const cldk_rentity_t *ents)
{
	static refdef_t saved;
	entity_t ent;
	int i;
	if (num <= 0 || w <= 0 || h <= 0)
		return;
	if (R2D_Flush)
		R2D_Flush();
	memcpy(&saved, &r_refdef, sizeof(saved));
	CL_ClearEntityLists();
	for (i = 0; i < num; i++)
	{
		const cldk_rentity_t *e = &ents[i];
		if (!e->model)
			continue;
		V_ClearEntity(&ent);
		ent.model = e->model;
		VectorCopy(e->origin, ent.origin);
		VectorCopy(e->origin, ent.oldorigin);
		VectorCopy(e->angles, ent.angles);
		ent.angles[0] *= r_meshpitch.value;
		ent.angles[2] *= r_meshroll.value;
		AngleVectors(ent.angles, ent.axis[0], ent.axis[1], ent.axis[2]);
		VectorInverse(ent.axis[1]);
		ent.scale = (e->scale > 0)?e->scale:1;
		ent.framestate.g[FS_REG].frame[0] = e->frame;
		ent.framestate.g[FS_REG].frame[1] = e->oldframe;
		ent.framestate.g[FS_REG].lerpweight[0] = 1 - e->backlerp;
		ent.framestate.g[FS_REG].lerpweight[1] = e->backlerp;
		ent.customskin = CLDK_ModelSkin(e->model, e->skin);
		ent.flags = RF_NOSHADOW | (e->fullbright?RF_FULLBRIGHT:0);
		Vector4Set(ent.shaderRGBAf, e->rgb[0], e->rgb[1], e->rgb[2], bound(0, e->alpha, 1));
		if (e->alpha < 1)
			ent.flags |= RF_TRANSLUCENT;
		VectorSet(ent.glowmod, 1, 1, 1);
		V_AddEntity(&ent);
	}
	VectorCopy(vieworg, r_refdef.vieworg);
	VectorCopy(viewangles, r_refdef.viewangles);
	AngleVectors(r_refdef.viewangles, r_refdef.viewaxis[0], r_refdef.viewaxis[1], r_refdef.viewaxis[2]);
	VectorInverse(r_refdef.viewaxis[1]);
	r_refdef.flags = RDF_NOWORLDMODEL;
	r_refdef.fovv_x = r_refdef.fov_x = fov_x;
	r_refdef.fovv_y = r_refdef.fov_y = fov_y;
	r_refdef.vrect.x = cldkarea.x + x*cldkarea.scale;
	r_refdef.vrect.y = cldkarea.y + y*cldkarea.scale;
	r_refdef.vrect.width = w*cldkarea.scale;
	r_refdef.vrect.height = h*cldkarea.scale;
	r_refdef.time = realtime;
	r_refdef.useperspective = true;
	r_refdef.mindist = 1;
	r_refdef.maxdist = 4096;
	r_refdef.playerview = &cl.playerview[0];
	memset(&r_refdef.globalfog, 0, sizeof(r_refdef.globalfog));
	r_refdef.areabitsknown = false;
	R_RenderView();
	memcpy(&r_refdef, &saved, sizeof(saved));
}

//the status bar and the rest of its HUD, for the first seat (it had no splitscreen)
qboolean CLDK_DrawHUDArea(int seat, const vrect_t *area)
{
	if (!CLDK_IsDaikatana())
		return false;
	if (seat)
		return true;
	if (CLDK_InCinematic())
	{	//no HUD in them
		CLDK_DrawLetterbox(area);
		return true;
	}
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
