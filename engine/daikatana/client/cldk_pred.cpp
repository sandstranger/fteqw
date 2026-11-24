/*
Its client's prediction (cl_pred.cpp): its own player movement (the game's, fte/dk_pmove.cpp, as its engine had it
for both), a command at a time, for FTE's client's prediction loop (client/cl_pred.c), in FTE's world.

GPL v2 or later.
*/
#include "cldk_local.h"

void DKPM_Pmove(pmove_t *pmove);	//fte/dk_pmove.cpp's

float			cldk_maxspeed = 320;	//the player's (svcdk_fte_stats)

static cldk_pmove_t	*cpm;
static csurface_t	pmsurface;

static trace_t CLDK_PMTraceBox(const CVector &start, const CVector &mins, const CVector &maxs, const CVector &end)
{
	float s[3] = {start.x, start.y, start.z}, mn[3] = {mins.x, mins.y, mins.z};
	float mx[3] = {maxs.x, maxs.y, maxs.z}, e[3] = {end.x, end.y, end.z};
	cldk_trace_t ct;
	trace_t tr;
	memset(&ct, 0, sizeof(ct));
	memset(&tr, 0, sizeof(tr));
	cpm->tracebox(s, mn, mx, e, &ct);
	tr.allsolid = ct.allsolid;
	tr.startsolid = ct.startsolid;
	tr.fraction = ct.fraction;
	tr.endpos.Set(ct.endpos[0], ct.endpos[1], ct.endpos[2]);
	tr.plane.normal.Set(ct.normal[0], ct.normal[1], ct.normal[2]);
	tr.plane.dist = ct.dist;
	tr.contents = ct.contents;
	pmsurface.flags = ct.surfaceflags;
	tr.surface = &pmsurface;
	tr.ent = ct.hit?(struct edict_s *)1:NULL;	//(the server's are edicts: it only looks at whether there's one)
	return tr;
}
static trace_t CLDK_PMTraceLine(const CVector &start, const CVector &end)
{
	CVector zero(0, 0, 0);
	return CLDK_PMTraceBox(start, zero, zero, end);
}
static int CLDK_PMPointContents(const CVector &p)
{
	float o[3] = {p.x, p.y, p.z};
	return cpm->pointcontents(o);
}

extern "C" void CLDK_Pmove(cldk_pmove_t *c)
{
	pmove_t pm;
	int i;
	memset(&pm, 0, sizeof(pm));
	switch(c->pm_type)
	{	//Quake II's, as svdk_game.cpp made them of its
	case 0:		pm.s.pm_type = PM_NORMAL;		break;
	case 1:		pm.s.pm_type = PM_SPECTATOR;	break;
	case 2:		pm.s.pm_type = PM_DEAD;			break;
	case 3:		pm.s.pm_type = PM_GIB;			break;
	default:	pm.s.pm_type = PM_FREEZE;		break;
	}
	for (i = 0; i < 3; i++)
	{
		pm.s.origin[i] = c->origin[i];
		pm.s.velocity[i] = c->velocity[i];
		pm.s.delta_angles[i] = c->delta_angles[i];
		pm.cmd.angles[i] = c->angles[i];
	}
	pm.s.pm_flags = c->pm_flags;
	pm.s.pm_time = c->pm_time;
	pm.s.gravity = c->gravity;
	pm.cmd.msec = c->msec;
	pm.cmd.buttons = c->buttons;
	pm.cmd.impulse = c->impulse;
	pm.cmd.lightlevel = c->lightlevel;
	pm.cmd.forwardmove = c->forwardmove;
	pm.cmd.sidemove = c->sidemove;
	pm.cmd.upmove = c->upmove;
	pm.max_speed = cldk_maxspeed;
	pm.TraceBox = CLDK_PMTraceBox;
	pm.TraceLine = CLDK_PMTraceLine;
	pm.pointcontents = CLDK_PMPointContents;

	cpm = c;
	DKPM_Pmove(&pm);
	cpm = NULL;

	for (i = 0; i < 3; i++)
	{
		c->origin[i] = pm.s.origin[i];
		c->velocity[i] = pm.s.velocity[i];
		c->viewangles[i] = pm.viewangles[i];
	}
	c->pm_flags = pm.s.pm_flags;
	c->pm_time = pm.s.pm_time;
	c->viewheight = pm.viewheight;
	c->onground = pm.groundEntity != NULL;
}
