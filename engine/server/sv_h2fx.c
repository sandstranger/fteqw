/*
Hexen II's effects on the server, as uHexen2 (https://github.com/sezero/uhexen2) keeps them (sv_effect.c): the
starteffect builtin puts an effect in a list of MAX_H2EFFECTS and sends it reliably, endeffect takes it out again,
a client that (re)joins gets every one still running (rain, fountains, earthquakes and the like last for the
level), and the list goes into saved games with the entities.

Only clients with PEXT2_H2EFFECTS get these messages (svcfte_h2starteffect/svcfte_h2endeffect, uHexen2's wire
format); they draw them with uHexen2's own code (client/clh2_fx.c). Everyone else keeps the particle-script versions
pr_cmds.c sends as custom temp entities, which a PEXT2_H2EFFECTS client ignores.

Also uHexen2's updatesoundpos (a sound playing on an entity follows it: the mission pack's projectiles and cube of
force), and its particleexplosion and rain_go messages.

GPL v2 or later, as uHexen2: Copyright (C) 1996-1997 Id Software, Inc., (C) 1997-1998 Raven Software Corp.
*/
#include "quakedef.h"

#if defined(HEXEN2) && defined(HAVE_SERVER)
#include "h2fx.h"
#include "pr_common.h"

typedef struct
{
	int type;			//CE_*, 0 when the slot is free
	float expire_time;	//0 when it lasts until endeffect (or the level's end)
	vec3_t parm[7];		//the builtin's arguments after the type, as given (vectors, or a float in [0])
} svh2effect_t;
static svh2effect_t svh2effects[MAX_H2EFFECTS];

void SVH2FX_Clear(void)
{
	memset(svh2effects, 0, sizeof(svh2effects));
}

//uHexen2's SV_SendEffect: the effect in its wire format
static void SVH2FX_WriteEffect(sizebuf_t *sb, int idx)
{
	svh2effect_t *e = &svh2effects[idx];
	float *p0 = e->parm[0], *p1 = e->parm[1], *p2 = e->parm[2], *p3 = e->parm[3];

	MSG_WriteByte (sb, svcfte_h2starteffect);
	MSG_WriteByte (sb, idx);
	MSG_WriteByte (sb, e->type);

	switch (e->type)
	{
	case CE_RAIN:	//min_org max_org e_size dir color count wait
		MSG_WriteCoord(sb, p0[0]);
		MSG_WriteCoord(sb, p0[1]);
		MSG_WriteCoord(sb, p0[2]);
		MSG_WriteCoord(sb, p1[0]);
		MSG_WriteCoord(sb, p1[1]);
		MSG_WriteCoord(sb, p1[2]);
		MSG_WriteCoord(sb, p2[0]);
		MSG_WriteCoord(sb, p2[1]);
		MSG_WriteCoord(sb, p2[2]);
		MSG_WriteCoord(sb, p3[0]);
		MSG_WriteCoord(sb, p3[1]);
		MSG_WriteCoord(sb, p3[2]);
		MSG_WriteShort(sb, (int)e->parm[4][0]);
		MSG_WriteShort(sb, (int)e->parm[5][0]);
		MSG_WriteFloat(sb, e->parm[6][0]);
		break;

	case CE_SNOW:	//min_org max_org flags dir count
		MSG_WriteCoord(sb, p0[0]);
		MSG_WriteCoord(sb, p0[1]);
		MSG_WriteCoord(sb, p0[2]);
		MSG_WriteCoord(sb, p1[0]);
		MSG_WriteCoord(sb, p1[1]);
		MSG_WriteCoord(sb, p1[2]);
		MSG_WriteByte(sb, (int)p2[0]);
		MSG_WriteCoord(sb, p3[0]);
		MSG_WriteCoord(sb, p3[1]);
		MSG_WriteCoord(sb, p3[2]);
		MSG_WriteByte(sb, (int)e->parm[4][0]);
		break;

	case CE_FOUNTAIN:	//pos angle movedir color cnt
		MSG_WriteCoord(sb, p0[0]);
		MSG_WriteCoord(sb, p0[1]);
		MSG_WriteCoord(sb, p0[2]);
		MSG_WriteAngle8(sb, p1[0]);
		MSG_WriteAngle8(sb, p1[1]);
		MSG_WriteAngle8(sb, p1[2]);
		MSG_WriteCoord(sb, p2[0]);
		MSG_WriteCoord(sb, p2[1]);
		MSG_WriteCoord(sb, p2[2]);
		MSG_WriteShort(sb, (int)p3[0]);
		MSG_WriteByte(sb, (int)e->parm[4][0]);
		break;

	case CE_QUAKE:	//origin radius
		MSG_WriteCoord(sb, p0[0]);
		MSG_WriteCoord(sb, p0[1]);
		MSG_WriteCoord(sb, p0[2]);
		MSG_WriteFloat(sb, p1[0]);
		break;

	case CE_WHITE_SMOKE:
	case CE_GREEN_SMOKE:
	case CE_GREY_SMOKE:
	case CE_RED_SMOKE:
	case CE_SLOW_WHITE_SMOKE:
	case CE_TELESMK1:
	case CE_TELESMK2:
	case CE_GHOST:
	case CE_REDCLOUD:	//origin velocity framelength (its frame is 0)
		MSG_WriteCoord(sb, p0[0]);
		MSG_WriteCoord(sb, p0[1]);
		MSG_WriteCoord(sb, p0[2]);
		MSG_WriteFloat(sb, p1[0]);
		MSG_WriteFloat(sb, p1[1]);
		MSG_WriteFloat(sb, p1[2]);
		MSG_WriteFloat(sb, p2[0]);
		MSG_WriteFloat(sb, 0);
		break;
	case CE_FLAMESTREAM:
	case CE_ACID_MUZZFL:
	case CE_FLAMEWALL:
	case CE_FLAMEWALL2:
	case CE_ONFIRE:	//mission pack: origin velocity frame (its framelength is 0.05)
		MSG_WriteCoord(sb, p0[0]);
		MSG_WriteCoord(sb, p0[1]);
		MSG_WriteCoord(sb, p0[2]);
		MSG_WriteFloat(sb, p1[0]);
		MSG_WriteFloat(sb, p1[1]);
		MSG_WriteFloat(sb, p1[2]);
		MSG_WriteFloat(sb, 0.05);
		MSG_WriteFloat(sb, p2[0]);
		break;

	case CE_SM_WHITE_FLASH:
	case CE_YELLOWRED_FLASH:
	case CE_BLUESPARK:
	case CE_YELLOWSPARK:
	case CE_SM_CIRCLE_EXP:
	case CE_BG_CIRCLE_EXP:
	case CE_SM_EXPLOSION:
	case CE_LG_EXPLOSION:
	case CE_FLOOR_EXPLOSION:
	case CE_FLOOR_EXPLOSION3:
	case CE_BLUE_EXPLOSION:
	case CE_REDSPARK:
	case CE_GREENSPARK:
	case CE_ICEHIT:
	case CE_MEDUSA_HIT:
	case CE_MEZZO_REFLECT:
	case CE_FLOOR_EXPLOSION2:
	case CE_XBOW_EXPLOSION:
	case CE_NEW_EXPLOSION:
	case CE_MAGIC_MISSILE_EXPLOSION:
	case CE_BONE_EXPLOSION:
	case CE_BLDRN_EXPL:
	case CE_ACID_HIT:
	case CE_ACID_SPLAT:
	case CE_ACID_EXPL:
	case CE_LBALL_EXPL:
	case CE_FIREWALL_SMALL:
	case CE_FIREWALL_MEDIUM:
	case CE_FIREWALL_LARGE:
	case CE_FBOOM:
	case CE_BOMB:
	case CE_BRN_BOUNCE:
	case CE_LSHOCK:
	case CE_WHITE_FLASH:
	case CE_BLUE_FLASH:
	case CE_SM_BLUE_FLASH:
	case CE_RED_FLASH:
	case CE_RIDER_DEATH:
	case CE_TELEPORTERPUFFS:	//origin
		MSG_WriteCoord(sb, p0[0]);
		MSG_WriteCoord(sb, p0[1]);
		MSG_WriteCoord(sb, p0[2]);
		break;

	case CE_TELEPORTERBODY:	//origin velocity skinnum
		MSG_WriteCoord(sb, p0[0]);
		MSG_WriteCoord(sb, p0[1]);
		MSG_WriteCoord(sb, p0[2]);
		MSG_WriteFloat(sb, p1[0]);
		MSG_WriteFloat(sb, p1[1]);
		MSG_WriteFloat(sb, p1[2]);
		MSG_WriteFloat(sb, p2[0]);
		break;

	case CE_BONESHARD:
	case CE_BONESHRAPNEL:	//origin velocity angle, and the velocity again for the avelocity (as uHexen2)
		MSG_WriteCoord(sb, p0[0]);
		MSG_WriteCoord(sb, p0[1]);
		MSG_WriteCoord(sb, p0[2]);
		MSG_WriteFloat(sb, p1[0]);
		MSG_WriteFloat(sb, p1[1]);
		MSG_WriteFloat(sb, p1[2]);
		MSG_WriteFloat(sb, p2[0]);
		MSG_WriteFloat(sb, p2[1]);
		MSG_WriteFloat(sb, p2[2]);
		MSG_WriteFloat(sb, p1[0]);
		MSG_WriteFloat(sb, p1[1]);
		MSG_WriteFloat(sb, p1[2]);
		break;

	case CE_GRAVITYWELL:	//origin color lifetime
		MSG_WriteCoord(sb, p0[0]);
		MSG_WriteCoord(sb, p0[1]);
		MSG_WriteCoord(sb, p0[2]);
		MSG_WriteShort(sb, (int)p1[0]);
		MSG_WriteFloat(sb, p2[0]);
		break;

	case CE_CHUNK:	//origin type srcvel numchunks
		MSG_WriteCoord(sb, p0[0]);
		MSG_WriteCoord(sb, p0[1]);
		MSG_WriteCoord(sb, p0[2]);
		MSG_WriteByte (sb, (int)p1[0]);
		MSG_WriteCoord(sb, p2[0]);
		MSG_WriteCoord(sb, p2[1]);
		MSG_WriteCoord(sb, p2[2]);
		MSG_WriteByte (sb, (int)p3[0]);
		break;
	}
}

static qboolean SVH2FX_ValidType(int type)
{
	return type > CE_NONE && type <= CE_ONFIRE;
}

struct svh2fxctx_s
{
	int idx;
};
static void SVH2FX_StartMulticast(client_t *cl, sizebuf_t *msg, void *vctx)
{
	struct svh2fxctx_s *ctx = vctx;
	if (cl->fteprotocolextensions2 & PEXT2_H2EFFECTS)
		SVH2FX_WriteEffect(msg, ctx->idx);
}
static void SVH2FX_EndMulticast(client_t *cl, sizebuf_t *msg, void *vctx)
{
	struct svh2fxctx_s *ctx = vctx;
	if (cl->fteprotocolextensions2 & PEXT2_H2EFFECTS)
	{
		MSG_WriteByte(msg, svcfte_h2endeffect);
		MSG_WriteByte(msg, ctx->idx);
	}
}

/*
uHexen2's SV_ParseEffect: starteffect(type, ...). Returns the effect's index, for endeffect, or -1 for an effect it
doesn't know (pr_cmds.c reports that).
*/
int SVH2FX_StartEffect(pubprogfuncs_t *prinst, struct globalvars_s *pr_globals)
{
	int idx, i;
	int type = G_FLOAT(OFS_PARM0);
	svh2effect_t *e;
	struct svh2fxctx_s ctx;

	if (!SVH2FX_ValidType(type))
		return -1;

	for (idx = 0; idx < MAX_H2EFFECTS; idx++)
	{
		if (!svh2effects[idx].type || (svh2effects[idx].expire_time && svh2effects[idx].expire_time <= sv.time))
			break;
	}
	if (idx >= MAX_H2EFFECTS)
	{	//uHexen2 stops the game here
		Con_Printf(CON_WARNING"starteffect: MAX_EFFECTS reached\n");
		return MAX_H2EFFECTS;
	}

	e = &svh2effects[idx];
	memset(e, 0, sizeof(*e));
	e->type = type;
	for (i = 0; i < 7; i++)
		VectorCopy(G_VECTOR(OFS_PARM1 + i*3), e->parm[i]);

	switch (type)
	{
	case CE_WHITE_SMOKE:
	case CE_GREEN_SMOKE:
	case CE_GREY_SMOKE:
	case CE_RED_SMOKE:
	case CE_SLOW_WHITE_SMOKE:
	case CE_TELESMK1:
	case CE_TELESMK2:
	case CE_GHOST:
	case CE_REDCLOUD:
	case CE_ACID_MUZZFL:
	case CE_FLAMESTREAM:
	case CE_FLAMEWALL:
	case CE_FLAMEWALL2:
	case CE_ONFIRE:
	case CE_SM_WHITE_FLASH:
	case CE_YELLOWRED_FLASH:
	case CE_BLUESPARK:
	case CE_YELLOWSPARK:
	case CE_SM_CIRCLE_EXP:
	case CE_BG_CIRCLE_EXP:
	case CE_SM_EXPLOSION:
	case CE_LG_EXPLOSION:
	case CE_FLOOR_EXPLOSION:
	case CE_FLOOR_EXPLOSION3:
	case CE_BLUE_EXPLOSION:
	case CE_REDSPARK:
	case CE_GREENSPARK:
	case CE_ICEHIT:
	case CE_MEDUSA_HIT:
	case CE_MEZZO_REFLECT:
	case CE_FLOOR_EXPLOSION2:
	case CE_XBOW_EXPLOSION:
	case CE_NEW_EXPLOSION:
	case CE_MAGIC_MISSILE_EXPLOSION:
	case CE_BONE_EXPLOSION:
	case CE_BLDRN_EXPL:
	case CE_ACID_HIT:
	case CE_ACID_SPLAT:
	case CE_ACID_EXPL:
	case CE_LBALL_EXPL:
	case CE_FIREWALL_SMALL:
	case CE_FIREWALL_MEDIUM:
	case CE_FIREWALL_LARGE:
	case CE_FBOOM:
	case CE_BOMB:
	case CE_BRN_BOUNCE:
	case CE_LSHOCK:
	case CE_WHITE_FLASH:
	case CE_BLUE_FLASH:
	case CE_SM_BLUE_FLASH:
	case CE_RED_FLASH:
	case CE_TELEPORTERPUFFS:
	case CE_TELEPORTERBODY:
		e->expire_time = sv.time + 1;
		break;
	case CE_BONESHARD:
	case CE_BONESHRAPNEL:
		e->expire_time = sv.time + 10;
		break;
	case CE_CHUNK:
		e->expire_time = sv.time + 3;
		break;
	default:	//rain, snow, fountains, earthquakes, the rider's death and gravity wells last
		break;
	}

	ctx.idx = idx;
	SV_MulticastCB(NULL, MULTICAST_ALL_R, NULL, FULLDIMENSIONMASK, SVH2FX_StartMulticast, &ctx);
	return idx;
}

//uHexen2's PF_endeffect
void SVH2FX_EndEffect(int idx)
{
	struct svh2fxctx_s ctx;
	if (idx < 0 || idx >= MAX_H2EFFECTS || !svh2effects[idx].type)
		return;
	svh2effects[idx].type = 0;

	ctx.idx = idx;
	SV_MulticastCB(NULL, MULTICAST_ALL_R, NULL, FULLDIMENSIONMASK, SVH2FX_EndMulticast, &ctx);
}

//uHexen2's SV_UpdateEffects: the effects still running, for a client coming in
void SVH2FX_SendEffects(client_t *cl)
{
	int idx;
	char buf[1024];
	sizebuf_t msg;

	if (!(cl->fteprotocolextensions2 & PEXT2_H2EFFECTS))
		return;
	for (idx = 0; idx < MAX_H2EFFECTS; idx++)
	{
		if (!svh2effects[idx].type)
			continue;
		if (svh2effects[idx].expire_time && svh2effects[idx].expire_time <= sv.time)
			continue;	//done with (uHexen2 sends those too, to play once more)

		memset(&msg, 0, sizeof(msg));
		msg.data = buf;
		msg.maxsize = sizeof(buf);
		msg.prim = cl->datagram.prim;
		SVH2FX_WriteEffect(&msg, idx);
		ClientReliableCheckBlock(cl, msg.cursize);
		ClientReliableWrite_SZ(cl, msg.data, msg.cursize);
	}
}

/*
uHexen2's SV_SaveEffects/SV_LoadEffects: a line for each, after the entities.
h2effect <index> <type> <expire_time> <21 parameters>
*/
void SVH2FX_SaveEffects(vfsfile_t *f)
{
	int idx, i;
	for (idx = 0; idx < MAX_H2EFFECTS; idx++)
	{
		svh2effect_t *e = &svh2effects[idx];
		if (!e->type)
			continue;
		VFS_PRINTF(f, "h2effect %i %i %.9g", idx, e->type, e->expire_time);
		for (i = 0; i < 7; i++)
			VFS_PRINTF(f, " %.9g %.9g %.9g", e->parm[i][0], e->parm[i][1], e->parm[i][2]);
		VFS_PRINTF(f, "\n");
	}
}
qboolean SVH2FX_LoadEffect(const char **ptr)
{
	const char *l = *ptr;
	char token[256];
	com_tokentype_t tt;
	int idx, i, j;
	svh2effect_t e;

	l = COM_ParseTokenOut(l, NULL, token, sizeof(token), &tt);
	idx = atoi(token);
	if (tt != TTP_RAWTOKEN || idx < 0 || idx >= MAX_H2EFFECTS)
		return false;
	memset(&e, 0, sizeof(e));
	l = COM_ParseTokenOut(l, NULL, token, sizeof(token), &tt);
	e.type = atoi(token);
	if (tt != TTP_RAWTOKEN || !SVH2FX_ValidType(e.type))
		return false;
	l = COM_ParseTokenOut(l, NULL, token, sizeof(token), &tt);
	e.expire_time = atof(token);
	for (i = 0; i < 7; i++)
		for (j = 0; j < 3; j++)
		{
			l = COM_ParseTokenOut(l, NULL, token, sizeof(token), &tt);
			if (tt != TTP_RAWTOKEN)
				return false;
			e.parm[i][j] = atof(token);
		}
	svh2effects[idx] = e;
	*ptr = l;
	return true;
}

/*
uHexen2's SV_UpdateSoundPos: where a sound playing on the entity's channel is now, the middle of its box.
*/
struct svh2soundposctx_s
{
	int ent, channel;
	vec3_t org;
};
static void SVH2FX_SoundPosMulticast(client_t *cl, sizebuf_t *msg, void *vctx)
{
	struct svh2soundposctx_s *ctx = vctx;
	if (!(cl->fteprotocolextensions2 & PEXT2_H2EFFECTS))
		return;
	if (msg->cursize + 4 + 3*4 > msg->maxsize)
		return;	//as uHexen2, it's only an update
	MSG_WriteByte(msg, svcfte_h2soundpos);
	MSG_WriteShort(msg, ctx->ent);
	MSG_WriteByte(msg, ctx->channel);
	MSG_WriteCoord(msg, ctx->org[0]);
	MSG_WriteCoord(msg, ctx->org[1]);
	MSG_WriteCoord(msg, ctx->org[2]);
}
void SVH2FX_UpdateSoundPos(pubprogfuncs_t *prinst, edict_t *ent, int channel)
{
	struct svh2soundposctx_s ctx;
	int i;
	ctx.ent = NUM_FOR_EDICT(prinst, ent);
	ctx.channel = channel;
	for (i = 0; i < 3; i++)
		ctx.org[i] = ent->v->origin[i] + 0.5*(ent->v->mins[i] + ent->v->maxs[i]);
	SV_MulticastCB(ctx.org, MULTICAST_ALL, NULL, FULLDIMENSIONMASK, SVH2FX_SoundPosMulticast, &ctx);
}

/*
particleexplosion(org, color, radius, counter) and rain_go(min, max, size, dir, color, count), in uHexen2's messages
for a client that draws them as uHexen2; the others get what legacy() writes (FTE's own versions).
*/
struct svh2partctx_s
{
	vec3_t org, size;
	int color, radius, count;
	float xdir, ydir;
	void (*legacy)(sizebuf_t *msg, void *ctx);
	void *legacyctx;
};
static void SVH2FX_ParticleExplosionMulticast(client_t *cl, sizebuf_t *msg, void *vctx)
{
	struct svh2partctx_s *ctx = vctx;
	if (!(cl->fteprotocolextensions2 & PEXT2_H2EFFECTS))
	{
		if (ctx->legacy && (cl->fteprotocolextensions & PEXT_HEXEN2))
			ctx->legacy(msg, ctx->legacyctx);
		return;
	}
	if (msg->cursize + 7 + 3*4 > msg->maxsize)
		return;
	MSG_WriteByte(msg, svcfte_h2particleexplosion);
	MSG_WriteCoord(msg, ctx->org[0]);
	MSG_WriteCoord(msg, ctx->org[1]);
	MSG_WriteCoord(msg, ctx->org[2]);
	MSG_WriteShort(msg, ctx->color);
	MSG_WriteShort(msg, ctx->radius);
	MSG_WriteShort(msg, ctx->count);
}
void SVH2FX_ParticleExplosion(float *org, int color, int radius, int count, void (*legacy)(sizebuf_t *msg, void *ctx), void *legacyctx)
{
	struct svh2partctx_s ctx;
	VectorCopy(org, ctx.org);
	ctx.color = color;
	ctx.radius = radius;
	ctx.count = count;
	ctx.legacy = legacy;
	ctx.legacyctx = legacyctx;
	SV_MulticastCB(ctx.org, MULTICAST_PVS, NULL, FULLDIMENSIONMASK, SVH2FX_ParticleExplosionMulticast, &ctx);
}

static void SVH2FX_RainMulticast(client_t *cl, sizebuf_t *msg, void *vctx)
{
	struct svh2partctx_s *ctx = vctx;
	if (!(cl->fteprotocolextensions2 & PEXT2_H2EFFECTS))
	{
		if (ctx->legacy)
			ctx->legacy(msg, ctx->legacyctx);
		return;
	}
	if (msg->cursize + 7 + 6*4 > msg->maxsize)
		return;
	MSG_WriteByte(msg, svcfte_h2raineffect);
	MSG_WriteCoord(msg, ctx->org[0]);
	MSG_WriteCoord(msg, ctx->org[1]);
	MSG_WriteCoord(msg, ctx->org[2]);
	MSG_WriteCoord(msg, ctx->size[0]);
	MSG_WriteCoord(msg, ctx->size[1]);
	MSG_WriteCoord(msg, ctx->size[2]);
	MSG_WriteAngle8(msg, ctx->xdir);
	MSG_WriteAngle8(msg, ctx->ydir);
	MSG_WriteShort(msg, ctx->color);
	MSG_WriteShort(msg, ctx->count);
}
void SVH2FX_RainGo(float *min, float *max, float *size, float *dir, int color, int count, void (*legacy)(sizebuf_t *msg, void *ctx), void *legacyctx)
{
	struct svh2partctx_s ctx;
	//uHexen2's PF_rain_go: from the top of the box
	ctx.org[0] = min[0];
	ctx.org[1] = min[1];
	ctx.org[2] = max[2];
	VectorCopy(size, ctx.size);
	ctx.xdir = (int)dir[0];
	ctx.ydir = (int)dir[1];
	ctx.color = color;
	ctx.count = count;
	ctx.legacy = legacy;
	ctx.legacyctx = legacyctx;
	SV_MulticastCB(ctx.org, MULTICAST_ALL, NULL, FULLDIMENSIONMASK, SVH2FX_RainMulticast, &ctx);
}
#endif
