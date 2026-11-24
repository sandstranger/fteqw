/*
What its client had of its renderer's models (ref_gl/hierarchy.cpp): the hardpoints of its .dkm models, the
surfaces muzzle flashes, weapons, flags... go on (hr_muzzle, hp_gun, ctf_flag), from the model's own file.

GPL v2 or later.
*/
#include <stddef.h>
#include "cldk_local.h"

#define MAX_DKMCACHE 256
static struct
{
	void			*model;		//FTE's
	unsigned char	*file;		//null: not a .dkm (or not there)
	int				length;
} dkmcache[MAX_DKMCACHE];
static int numdkmcache;

void CLDK_ModelsReset(void)
{
	int i;
	for (i = 0; i < numdkmcache; i++)
		if (dkmcache[i].file)
			CLDKF_FreeFile(dkmcache[i].file);
	memset(dkmcache, 0, sizeof(dkmcache));
	numdkmcache = 0;
}

//its file, as it is (it's little-endian, as everything it runs on), if it looks whole
static const dmdl_t *CLDK_ModelFile(void *model, int *length)
{
	int i;
	const char *name;
	const dmdl_t *h;
	for (i = 0; i < numdkmcache; i++)
		if (dkmcache[i].model == model)
		{
			*length = dkmcache[i].length;
			return (const dmdl_t *)dkmcache[i].file;
		}
	if (numdkmcache == MAX_DKMCACHE)
		CLDK_ModelsReset();
	i = numdkmcache++;
	dkmcache[i].model = model;
	name = CLDKF_ModelPath(model);
	if (!name || !*name || !strstr(name, ".dkm"))
		return NULL;
	dkmcache[i].file = (unsigned char *)CLDKF_LoadFile(name, &dkmcache[i].length);
	h = (const dmdl_t *)dkmcache[i].file;
	if (h && (dkmcache[i].length < (int)sizeof(dmdl_t) || (h->version != ALIAS_VERSION && h->version != ALIAS_VERSION2)
		|| h->ofs_end > dkmcache[i].length || h->ofs_tris < 0 || h->ofs_frames < 0 || h->ofs_surfaces < 0
		|| h->ofs_frames + (size_t)h->num_frames*h->framesize > (size_t)dkmcache[i].length
		|| h->ofs_surfaces + (size_t)h->num_surfaces*sizeof(dsurface_t) > (size_t)dkmcache[i].length))
	{
		CLDKF_FreeFile(dkmcache[i].file);
		dkmcache[i].file = NULL;
	}
	*length = dkmcache[i].file?dkmcache[i].length:0;
	return (const dmdl_t *)dkmcache[i].file;
}

//R_GetModelHardpoint: the middle of the first triangle of the surface, in the frames' lerp, where the entity is;
//hardPt's left as it is if there's no such surface
void CLDK_GetModelHardpoint(char *pSurfName, int curFrame, int lastFrame, entity_t &ent, CVector &hardPt)
{
	const dmdl_t *h;
	const unsigned char *base, *tri;
	dtriangle_t t;	//(the file's sections needn't be aligned: everything bigger than a byte is copied out of it)
	int len, i, surf = -1, k;
	bool found = false;
	CVector move, frontv, backv, forward, right, up, temp(0, 0, 0);
	float frontlerp = 1.0f - ent.backlerp;

	if (!ent.model || !pSurfName)
		return;
	h = CLDK_ModelFile(ent.model, &len);
	if (!h)
		return;
	base = (const unsigned char *)h;
	for (i = 0; i < h->num_surfaces; i++)
	{
		char sname[sizeof(((dsurface_t*)0)->name)+1];
		memcpy(sname, base + h->ofs_surfaces + i*sizeof(dsurface_t) + offsetof(dsurface_t, name), sizeof(sname)-1);
		sname[sizeof(sname)-1] = 0;
		if (!stricmp(sname, pSurfName))
		{
			surf = i;
			break;
		}
	}
	if (surf < 0)
		return;
	//its triangles are as long as their uv frames (its renderer took them a fixed size: the same for one)
	if (h->ofs_tris < 0 || h->ofs_tris + sizeof(dtriangle_t) > (size_t)len)
		return;
	tri = base + h->ofs_tris;
	for (i = 0; i < h->num_tris; i++)
	{
		dtriangle_t ti;
		if (tri + sizeof(dtriangle_t) > base + len)
			return;
		memcpy(&ti, tri, sizeof(ti));
		if (ti.index_surface == surf)
		{
			t = ti;
			found = true;
			break;
		}
		tri += sizeof(dtriangle_t) + ((ti.num_uvframes > 1)?(ti.num_uvframes - 1)*sizeof(dstframe_t):0);
	}
	if (!found)
		memcpy(&t, base + h->ofs_tris, sizeof(t));	//(its renderer's: the first)

	if (curFrame < 0 || curFrame >= h->num_frames)
		curFrame = 0;
	if (lastFrame < 0 || lastFrame >= h->num_frames)
		lastFrame = 0;
	{
		//both frame kinds start with scale, translate, name
		const unsigned char *cf = base + h->ofs_frames + curFrame * h->framesize;
		const unsigned char *lf = base + h->ofs_frames + lastFrame * h->framesize;
		float cfst[6], lfst[6];	//scale, translate
		memcpy(cfst, cf, sizeof(cfst));
		memcpy(lfst, lf, sizeof(lfst));
		move = ent.backlerp * CVector(lfst[3], lfst[4], lfst[5]) + frontlerp * CVector(cfst[3], cfst[4], cfst[5]);
		frontv = frontlerp * CVector(cfst[0], cfst[1], cfst[2]);
		backv = ent.backlerp * CVector(lfst[0], lfst[1], lfst[2]);
		cf += offsetof(daliasframe_t, verts);
		lf += offsetof(daliasframe_t, verts);
		for (k = 0; k < 3; k++)
		{
			int v = t.index_xyz[k];
			CVector cv, lv;
			if (v < 0 || v >= h->num_xyz)
				return;
			if (h->version == ALIAS_VERSION)
			{
				const unsigned char *c = cf + v*sizeof(dtrivertx_t), *l = lf + v*sizeof(dtrivertx_t);
				cv.Set(c[0], c[1], c[2]);
				lv.Set(l[0], l[1], l[2]);
			}
			else
			{	//11, 10, 11 bits (alias_quant.h)
				const unsigned char *cb = cf + v*sizeof(dtrivertx2_t), *lb = lf + v*sizeof(dtrivertx2_t);
				unsigned int c = cb[0] | (cb[1]<<8) | (cb[2]<<16) | ((unsigned int)cb[3]<<24);
				unsigned int l = lb[0] | (lb[1]<<8) | (lb[2]<<16) | ((unsigned int)lb[3]<<24);
				cv.Set((float)(c >> 21), (float)((c >> 11) & 0x3ff), (float)(c & 0x7ff));
				lv.Set((float)(l >> 21), (float)((l >> 11) & 0x3ff), (float)(l & 0x7ff));
			}
			temp.x += move.x + lv.x * backv.x + cv.x * frontv.x;
			temp.y += move.y + lv.y * backv.y + cv.y * frontv.y;
			temp.z += move.z + lv.z * backv.z + cv.z * frontv.z;
		}
	}
	temp = temp * (1.0f/3);

	AngleToVectors(ent.angles, forward, right, up);
	right = -right;
	hardPt.x = temp.x * forward.x + temp.y * right.x + temp.z * up.x + ent.origin.x;
	hardPt.y = temp.x * forward.y + temp.y * right.y + temp.z * up.y + ent.origin.y;
	hardPt.z = temp.x * forward.z + temp.y * right.z + temp.z * up.z + ent.origin.z;
}
