/*
Daikatana's beams as its renderer drew them (ref_gl/gl_beams.cpp): dk_beams.cpp's beam list (lasers, lightning,
trails) and its RF_BEAM_MOVING entities, as quads facing the view, in FTE's scene as triangles (cldk_fte.h).

GPL v2 or later.
*/
#include "cldk_local.h"
#include "dk_beams.h"

//its renderer's r_beamTextures (gl_rmisc.cpp)
static const char *beamtexnames[BEAM_TEX_NUMTEXTURES] =
{
	"pics/misc/w_zap001.tga",		//BEAM_TEX_LIGHTNING
	"pics/misc/laser.tga",			//BEAM_TEX_LASER
	"pics/misc/beamspark.tga",		//BEAM_TEX_LASERSPARK
	"pics/misc/novalaser.tga",		//BEAM_TEX_NOVALASER
	"pics/misc/tracer.tga",			//BEAM_TEX_TRACER
	"skins/we_mflash2.tga",			//BEAM_TEX_MF_GENERIC
	"skins/we_sclawslash.bmp",		//BEAM_TEX_GRAPPLE_CHAIN
	"pics/misc/we_disctr.tga",		//BEAM_TEX_DISCUSTRAIL
};
static void		*beamshaders[BEAM_TEX_NUMTEXTURES];
static void		*beamwhite;		//BEAM_TEX_NONE: its colour alone
static void		*glowshader;	//its spotlights' volumes

void CLDK_RegisterBeams(void)
{
	int i;
	for (i = 0; i < BEAM_TEX_NUMTEXTURES; i++)
		beamshaders[i] = CLDKF_RegisterBeamShader(beamtexnames[i]);
	beamwhite = CLDKF_RegisterBeamShader(NULL);
	glowshader = CLDKF_RegisterGlowShader();
}

#define MAX_BEAMVERTS (BEAM_MAX_SEGMENTS*6)
static float	bxyz[MAX_BEAMVERTS*3], bst[MAX_BEAMVERTS*2], brgba[MAX_BEAMVERTS*4];
static int		bnum;

static void BeamVert(const CVector &p, float s, float t, const CVector &rgb, float a)
{
	if (bnum >= MAX_BEAMVERTS)
		return;
	bxyz[bnum*3+0] = p.x;	bxyz[bnum*3+1] = p.y;	bxyz[bnum*3+2] = p.z;
	bst[bnum*2+0] = s;		bst[bnum*2+1] = t;
	brgba[bnum*4+0] = rgb.x;	brgba[bnum*4+1] = rgb.y;	brgba[bnum*4+2] = rgb.z;
	brgba[bnum*4+3] = (a < 0)?0:((a > 1)?1:a);
	bnum++;
}
//a quad as its GL_QUADS had them: 0 (0,0), 1 (0,1), 2 (1,1), 3 (1,0)
static void BeamQuad(const CVector &p0, const CVector &p1, const CVector &p2, const CVector &p3, const CVector &rgb, float a01, float a23)
{
	BeamVert(p0, 0, 0, rgb, a01);
	BeamVert(p1, 0, 1, rgb, a01);
	BeamVert(p2, 1, 1, rgb, a23);
	BeamVert(p0, 0, 0, rgb, a01);
	BeamVert(p2, 1, 1, rgb, a23);
	BeamVert(p3, 1, 0, rgb, a23);
}
static void BeamFlush(int texIndex)
{
	void *shader = (texIndex >= 0 && texIndex < BEAM_TEX_NUMTEXTURES)?beamshaders[texIndex]:beamwhite;
	if (bnum)
		CLDKF_AddTriangles(shader, bnum, bxyz, bst, brgba);
	bnum = 0;
}

//the beam's sideways half-width at this point, facing the view; false if it's end on
static bool BeamRight(const CVector &start, const CVector &dir, float radius, CVector &right)
{
	CVector toview = start - cl.refdef.vieworg;
	float dist;
	CrossProduct(toview, dir, right);
	dist = right.Length();
	if (dist < 0.001f)
		return false;
	right = right * (-radius / dist);
	return true;
}

static void DrawSegmented(beam_t *beam, const CVector &rgb)
{
	beamSeg_t *seg;
	CVector right, p0, p1, p2, p3;
	float a = beam->alpha, a2;
	bool first = true;
	bool fade = !!(beam->flags & BEAMFLAG_ALPHAFADE);
	for (seg = beam->firstSeg; seg; seg = seg->nextSeg)
	{
		if (!BeamRight(seg->segStart, seg->segNormal, beam->startRadius, right))
			continue;
		if (first)
		{
			p0 = seg->segStart + right;
			p1 = seg->segStart - right;
			first = false;
		}
		else
		{	//each starts where the last ended
			p0 = p3;
			p1 = p2;
		}
		p3 = seg->segEnd + right;
		p2 = seg->segEnd - right;
		if (fade)
		{	//to nothing along its length
			CVector v = seg->segEnd - seg->segStart;
			a2 = a - (beam->beamLength > 0?(v.Length() / beam->beamLength) * beam->alpha:0);
			BeamQuad(p0, p1, p2, p3, rgb, a, a2);
			a = a2;
		}
		else
			BeamQuad(p0, p1, p2, p3, rgb, seg->alpha, seg->alpha);
	}
}

static void DrawSingle(beam_t *beam, const CVector &rgb)
{
	beamSeg_t *seg = beam->firstSeg;
	CVector right, rs, re, p0, p1, p2, p3;
	if (!seg || !BeamRight(seg->segStart, seg->segNormal, 1, right))
		return;
	rs = right * beam->startRadius;
	re = right * beam->endRadius;
	p3 = seg->segEnd + re;
	p2 = seg->segEnd - re;
	p0 = seg->segStart + rs;
	p1 = seg->segStart - rs;
	BeamQuad(p0, p1, p2, p3, rgb, beam->alpha, (beam->flags & BEAMFLAG_ALPHAFADE)?0:beam->alpha);
}

//beam_DrawBeamList
void CLDK_DrawBeams(void)
{
	int i;
	if (cl.refdef.rdflags & RDF_NOWORLDMODEL)
		return;
	for (i = 0; i < beamList.numBeams; i++)
	{
		beam_t *beam = &beamList.beams[i];
		CVector rgb;
		if ((beam->flags & BEAMFLAG_RGBCOLOR) || beam->texIndex <= 0)
			rgb = beam->rgbColor;
		else
			rgb.Set(1, 1, 1);
		if (beam->numSegs > 1)
			DrawSegmented(beam, rgb);
		else if (beam->numSegs == 1)
			DrawSingle(beam, rgb);
		BeamFlush(beam->texIndex);
	}
}

//beam_DrawBeam: an entity's (RF_BEAM_MOVING: render_scale's its length, mins its direction), the laser's
void CLDK_DrawEntityBeam(entity_t *e)
{
	CVector normal, right, p0, p1, p2, p3, rgb(1, 1, 1);
	float alpha = (e->flags & RF_TRANSLUCENT)?e->alpha:1;
	float radius = (e->frame < 0)?-(float)e->frame / 1000.0f:e->frame;	//(-frame: in 1000ths)
	CVector oldorigin = e->oldorigin;
	if (e->flags & RF_BEAM_MOVING)
	{
		oldorigin.x = e->origin.x - e->mins.x * e->render_scale.x;
		oldorigin.y = e->origin.y - e->mins.y * e->render_scale.y;
		oldorigin.z = e->origin.z - e->mins.z * e->render_scale.z;
	}
	normal = oldorigin - e->origin;
	if (!BeamRight(e->origin, normal, radius, right))
		return;
	p3 = oldorigin + right;
	p2 = oldorigin - right;
	p0 = e->origin + right;
	p1 = e->origin - right;
	BeamQuad(p0, p1, p2, p3, rgb, alpha, alpha);
	BeamFlush(BEAM_TEX_LASER);
}

//R_DrawSpotlight: a spotlight's volume, two tubes from it to where it shines (render_scale), widening, fading, added
#define SPOT_START_ALPHA	0.25f
#define SPOT_END_ALPHA		0.05f
void CLDK_DrawSpotlight(entity_t *e)
{
	CVector forward, right, up, length = e->render_scale - e->origin;
	CVector s[16], en[16], black(0, 0, 0);
	float radius1 = (float)e->frame, radius2;
	int light, i;
	if (!glowshader)
		return;
	AngleToVectors(e->angles, forward, right, up);
	for (light = 0; light < 2; light++)
	{
		radius2 = radius1 + length.Length() * 0.2f;
		for (i = 0; i < 16; i++)
		{
			float a = i * (float)(M_PI * 2 / 16), c = cos(a), sn = sin(a);
			s[i] = e->origin + forward * radius1 + right * (c * radius1) + up * (sn * radius1);
			en[i] = e->render_scale + forward * radius2 + right * (c * radius2) + up * (sn * radius2);
		}
		for (i = 0; i < 16; i++)
		{
			int j = (i + 1) & 15;
			BeamVert(s[i], 0, 0, e->color, SPOT_START_ALPHA);
			BeamVert(s[j], 0, 0, e->color, SPOT_START_ALPHA);
			BeamVert(en[j], 0, 0, black, SPOT_END_ALPHA);
			BeamVert(s[i], 0, 0, e->color, SPOT_START_ALPHA);
			BeamVert(en[j], 0, 0, black, SPOT_END_ALPHA);
			BeamVert(en[i], 0, 0, black, SPOT_END_ALPHA);
		}
		length = length * (2.0f/3);
		radius1 *= 2.0f/3;
	}
	if (bnum)
		CLDKF_AddTriangles(glowshader, bnum, bxyz, bst, brgba);
	bnum = 0;
}
