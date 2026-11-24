/*
Its renderer's surface sprites (ref_gl/gl_surfsprite.cpp): bullet holes, scorch marks, blood, on the plane they
hit, faded out after a minute (two seconds for the quick ones). Its sprites' frames (.sp2) come from their files;
they're drawn as quads on the plane, in FTE's scene (not clipped to the face, as its renderer did).

GPL v2 or later.
*/
#include "cldk_local.h"

#define	MAX_SURF_SPRITES		512
#define MAX_SPRITES_PER_PLANE	32
#define SURF_SPRITE_DURATION		60.0f
#define SURF_SPRITE_QUICK_DURATION	2.0f
#define SURF_SPRITE_FADE_TIME		5.0f

typedef struct
{
	void	*shader;
	CVector	points[4];
	CVector	light;
	int		plane;
	float	expire;		//seconds, its client's time
	float	alpha;
} surfsprite_t;
static surfsprite_t	ssprites[MAX_SURF_SPRITES];
static int			numssprites;

//its sprites' frames: their images and sizes, from their files
#define MAX_SPRCACHE 64
#define MAX_SPRFRAMES 16
static struct
{
	void	*model;
	int		numframes;
	int		width[MAX_SPRFRAMES], height[MAX_SPRFRAMES];
	void	*shader[MAX_SPRFRAMES];
} sprcache[MAX_SPRCACHE];
static int numsprcache;

void CLDK_SurfSpritesReset(void)
{
	numssprites = 0;
	memset(sprcache, 0, sizeof(sprcache));
	numsprcache = 0;
}

static int CLDK_SpriteCache(void *model)
{
	int i, len, f;
	const char *name;
	const dsprite_t *spr;
	for (i = 0; i < numsprcache; i++)
		if (sprcache[i].model == model)
			return i;
	if (numsprcache == MAX_SPRCACHE)
		numsprcache = 0;	//(more than there are)
	i = numsprcache++;
	memset(&sprcache[i], 0, sizeof(sprcache[i]));
	sprcache[i].model = model;
	name = CLDKF_ModelPath(model);
	spr = (name && *name)?(const dsprite_t *)CLDKF_LoadFile(name, &len):NULL;
	if (!spr)
		return i;
	if (len >= (int)sizeof(dsprite_t) - (int)sizeof(dsprframe_t) && spr->ident == IDSPRITEHEADER)
	{
		for (f = 0; f < spr->numframes && f < MAX_SPRFRAMES; f++)
		{
			char img[MAX_SKINNAME+1];
			if ((const unsigned char *)&spr->frames[f+1] > (const unsigned char *)spr + len)
				break;
			memcpy(img, spr->frames[f].name, MAX_SKINNAME);
			img[MAX_SKINNAME] = 0;
			sprcache[i].width[f] = spr->frames[f].width;
			sprcache[i].height[f] = spr->frames[f].height;
			sprcache[i].shader[f] = CLDKF_RegisterDecalShader(img);		}
		sprcache[i].numframes = f;
	}
	CLDKF_FreeFile((void *)spr);
	return i;
}

//R_SurfaceSpriteAdd
void CLDK_AddSurfaceSprite(CVector &vOrigin, CVector &vEntOrigin, CVector &vEntAngles, void *pHitModel, int nIndex,
	void *pSpriteModel, int nFrame, float fRoll, float fScale, byte nFlags)
{
	float n[3], o[3], l[3], t = cl.time * 0.001f;
	CVector normal, origin, angles, forward, right, up;
	surfsprite_t *s;
	int c, i, w, h, onplane = 0;

	if (!pSpriteModel || !pHitModel || !CLDKF_CvarValue("gl_surfacesprites", "1"))
		return;
	if (cl.refdef.rdflags & RDF_NOWORLDMODEL)
		return;
	if (!CLDKF_PlaneNormal(nIndex, n))
		return;
	c = CLDK_SpriteCache(pSpriteModel);
	if (nFrame < 0 || nFrame >= sprcache[c].numframes)
		nFrame = 0;
	if (!sprcache[c].numframes || !sprcache[c].shader[nFrame])
		return;
	w = sprcache[c].width[nFrame];
	h = sprcache[c].height[nFrame];

	//its normal's into the surface (from the side it's seen from), its origin an eighth out of it
	normal.Set(n[0], n[1], n[2]);
	if (DotProduct(normal, cl.refdef.vieworg - vOrigin) > 0)
		normal = -normal;
	normal.Normalize();
	origin = vOrigin - normal * 0.125f;
	normal.VectorToAngles(angles);
	angles.z = fRoll;
	AngleToVectors(angles, forward, right, up);
	up = up * fabs(fScale);
	right = right * fabs(fScale);

	//too many: the oldest goes (on its plane, if there are too many there)
	for (i = 0; i < numssprites; i++)
		if (ssprites[i].plane == nIndex && ssprites[i].expire > t)
			onplane++;
	if (onplane >= MAX_SPRITES_PER_PLANE)
	{
		for (i = 0; i < numssprites; i++)
			if (ssprites[i].plane == nIndex && ssprites[i].expire > t)
			{
				ssprites[i].expire = t;
				break;
			}
	}
	if (numssprites == MAX_SURF_SPRITES)
	{
		memmove(ssprites, ssprites+1, sizeof(*ssprites)*(MAX_SURF_SPRITES-1));
		numssprites--;
	}
	s = &ssprites[numssprites++];
	s->shader = sprcache[c].shader[nFrame];
	s->plane = nIndex;
	s->alpha = 1;
	s->expire = t + ((nFlags & SSF_QUICKFADE)?SURF_SPRITE_QUICK_DURATION:SURF_SPRITE_DURATION);
	if (nFlags & SSF_FULLBRIGHT)
		s->light.Set(1, 1, 1);
	else
	{	//the light under it, as its renderer had it (with its gl_modulate, 2)
		o[0] = origin.x; o[1] = origin.y; o[2] = origin.z;
		CLDKF_LightPoint(o, l);
		s->light.Set(l[0]*2, l[1]*2, l[2]*2);
	}
	//its corners: 0 (0,1), 1 (0,0), 2 (1,0), 3 (1,1)
	s->points[0] = origin + up * (float)(-h/2) + right * (float)(-w/2);
	s->points[1] = origin + up * (float)(h - h/2) + right * (float)(-w/2);
	s->points[2] = origin + up * (float)(h - h/2) + right * (float)(w - w/2);
	s->points[3] = origin + up * (float)(-h/2) + right * (float)(w - w/2);
}

//R_DrawSurfaceSprites, UpdateSurfaceSprites
void CLDK_DrawSurfaceSprites(void)
{
	static float xyz[MAX_SURF_SPRITES*6*3], st[MAX_SURF_SPRITES*6*2], rgba[MAX_SURF_SPRITES*6*4];
	static const float sts[6][2] = {{0,1}, {0,0}, {1,0}, {0,1}, {1,0}, {1,1}};
	static const int corner[6] = {0, 1, 2, 0, 2, 3};
	float t = cl.time * 0.001f;
	int i, j, k, n;
	void *shader;
	bool done[MAX_SURF_SPRITES];

	for (i = 0; i < numssprites; )
	{	//fading out once they've expired, then gone
		surfsprite_t *s = &ssprites[i];
		if (s->expire < t)
		{
			s->alpha = (s->expire + SURF_SPRITE_FADE_TIME - t) / SURF_SPRITE_FADE_TIME;
			if (s->alpha < 0)
			{
				memmove(s, s+1, sizeof(*s)*(numssprites-i-1));
				numssprites--;
				continue;
			}
		}
		i++;
	}

	//by their images
	memset(done, 0, sizeof(done));
	for (i = 0; i < numssprites; i++)
	{
		if (done[i])
			continue;
		shader = ssprites[i].shader;
		n = 0;
		for (j = i; j < numssprites; j++)
		{
			surfsprite_t *s = &ssprites[j];
			if (done[j] || s->shader != shader)
				continue;
			done[j] = true;
			for (k = 0; k < 6; k++, n++)
			{
				const CVector &p = s->points[corner[k]];
				xyz[n*3+0] = p.x; xyz[n*3+1] = p.y; xyz[n*3+2] = p.z;
				st[n*2+0] = sts[k][0]; st[n*2+1] = sts[k][1];
				rgba[n*4+0] = s->light.x; rgba[n*4+1] = s->light.y; rgba[n*4+2] = s->light.z; rgba[n*4+3] = s->alpha;
			}
		}
		CLDKF_AddTriangles(shader, n, xyz, st, rgba);
	}
}
