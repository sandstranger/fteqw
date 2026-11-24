/*
The Quake II Rerelease's navigation meshes, for its game library's monsters (gi.GetPathToGoal).

KEX's engine loads bots/navigation/<map>.nav (in the rerelease's pak0) and finds paths through its nodes for the
game: monsters that can't see or reach the player go about by them. The file and the pathing are as Q2REPRO has
them (src/server/nav.c, Andrey Nazarov and Paril's reconstruction of KEX's), less the debug drawing:

- a NAV3 file, versions 1-6: nodes (flags, links, radius, position), links between them (walk, jumps, ledges,
  elevators...), traversals (where a jump or a drop starts and ends) and the map's entities some links go through;
- A* from the node closest to the start to the one closest to the goal, over the links the request allows;
- some nodes are conditional (in solid, in liquid, near hazards, over no floor, by locked doors): they're checked
  again every frame, and left out while their condition holds.

GPL v2 or later.
*/
#include "quakedef.h"

#ifdef Q2SERVER
#include "q2rr_api.h"
#include <float.h>

#define Distance(a,b) sqrt(((a)[0]-(b)[0])*((a)[0]-(b)[0]) + ((a)[1]-(b)[1])*((a)[1]-(b)[1]) + ((a)[2]-(b)[2])*((a)[2]-(b)[2]))

#define NAV_MAGIC			(('N')|('A'<<8)|('V'<<16)|('3'<<24))
#define NAV_VERSION_2		2	//entities have a model
#define NAV_VERSION_3		3	//links have team flags
#define NAV_VERSION_4		4	//traversals have a ladder plane
#define NAV_VERSION_6		6	//green and yellow teams gone (bits 2 and 3 of the link flags, before)
#define NAV_VERSION_LATEST	NAV_VERSION_6

//node flags
#define NF_TELEPORTER		(1<<0)
#define NF_PUSHER			(1<<1)
#define NF_ELEVATOR			(1<<2)
#define NF_LADDER			(1<<3)
#define NF_UNDERWATER		(1<<4)
#define NF_CHECKFORHAZARD	(1<<5)
#define NF_CHECKHASFLOOR	(1<<6)
#define NF_CHECKINSOLID		(1<<7)
#define NF_NOMONSTERS		(1<<8)
#define NF_CROUCH			(1<<9)
#define NF_NOPOI			(1<<10)
#define NF_CHECKINLIQUID	(1<<11)
#define NF_CHECKDOORLINKS	(1<<12)
#define NF_DISABLED			(1<<13)
#define NF_CONDITIONAL		(NF_CHECKDOORLINKS|NF_CHECKFORHAZARD|NF_CHECKHASFLOOR|NF_CHECKINLIQUID|NF_CHECKINSOLID)

//link types
#define NLT_WALK			0
#define NLT_LONGJUMP		1
#define NLT_TELEPORT		2
#define NLT_WALKOFFLEDGE	3
#define NLT_PUSHER			4
#define NLT_BARRIERJUMP		5
#define NLT_ELEVATOR		6
#define NLT_TRAIN			7
#define NLT_MANUAL_LONGJUMP	8
#define NLT_CROUCH			9
#define NLT_LADDER			10
#define NLT_MANUAL_BARRIERJUMP 11
#define NLT_PIVOTANDJUMP	12
#define NLT_ROCKETJUMP		13

#define NLF_ALLTEAMS		3	//red and blue

//the rerelease's contents and flags, as game.h has them
#define RRCONTENTS_SOLID		0x00000001
#define RRCONTENTS_WINDOW		0x00000002
#define RRCONTENTS_LAVA			0x00000008
#define RRCONTENTS_SLIME		0x00000010
#define RRCONTENTS_WATER		0x00000020
#define RRCONTENTS_PLAYERCLIP	0x00010000
#define RRCONTENTS_MONSTERCLIP	0x00020000
#define RRMASK_SOLID			(RRCONTENTS_SOLID|RRCONTENTS_WINDOW)
#define RRMASK_WATER			(RRCONTENTS_WATER|RRCONTENTS_LAVA|RRCONTENTS_SLIME)
#define RRSVFL_IS_LOCKED_DOOR	(1ull<<24)
#define RRSVFL_TRAP_DANGER		(1ull<<27)
#define RRRF_BEAM				(1u<<7)
#define RRSVF_NOCLIENT			(1u<<0)
#define RRSOLID_TRIGGER			1

#define PATH_POINT_TOO_CLOSE	64.f
#define NAV_FLOOR_DISTANCE		96.f

struct navnode_s;
struct navedict_s;
typedef struct
{
	vec3_t funnel;
	vec3_t start;
	vec3_t end;
	vec3_t ladder_plane;
} navtraversal_t;
typedef struct
{
	struct navnode_s *target;
	unsigned char type;
	unsigned char flags;
	navtraversal_t *traversal;
	struct navedict_s *edict;
} navlink_t;
typedef struct navnode_s
{
	unsigned short flags;
	short num_links;
	navlink_t *links;
	int id;
	short radius;
	vec3_t origin;
} navnode_t;
typedef struct navedict_s
{
	navlink_t *link;
	int model;
	vec3_t mins, maxs;
	const rr_edict_t *game_edict;
} navedict_t;

//the A* open set: a binary heap, ordered by f; a node's stale entries (it got a better g since) are skipped
typedef struct
{
	float f, g;
	int node;
} navopen_t;

static struct
{
	qboolean loaded;	//tried to, for this map
	char mapname[MAX_QPATH];

	int num_nodes, num_links, num_traversals, num_edicts;
	float heuristic;

	navnode_t *nodes;
	navlink_t *links;
	navtraversal_t *traversals;
	navedict_t *edicts;

	int num_conditional_nodes;
	navnode_t **conditional_nodes;

	//A*'s
	int *came_from;
	int *went_to;
	float *g_score;
	navopen_t *open;
	size_t num_open, max_open;

	//the game's entities that matter to nodes (traps, for hazards)
	const rr_edict_t **registered;
	size_t num_registered, max_registered;

	qboolean setup_entities;
	int nav_frame;

	rr_game_import_t *gi;
	rr_game_export_t *ge;
	cvar_t *debug;		//nav_debug: print the paths asked for
} nav;

//the mesh (not the game's entities that are registered: those are the game's, through map changes and loads)
static void Nav_Free(void)
{
	const rr_edict_t **registered = nav.registered;
	size_t num_registered = nav.num_registered, max_registered = nav.max_registered;
	rr_game_import_t *gi = nav.gi;
	rr_game_export_t *ge = nav.ge;
	cvar_t *debug = nav.debug;
	Z_Free(nav.nodes);
	Z_Free(nav.links);
	Z_Free(nav.traversals);
	Z_Free(nav.edicts);
	Z_Free(nav.conditional_nodes);
	Z_Free(nav.came_from);
	Z_Free(nav.went_to);
	Z_Free(nav.g_score);
	BZ_Free(nav.open);
	memset(&nav, 0, sizeof(nav));
	nav.registered = registered;
	nav.num_registered = num_registered;
	nav.max_registered = max_registered;
	nav.gi = gi;
	nav.ge = ge;
	nav.debug = debug;
}

//the game's starting a map (or loading one, or going): the mesh is loaded again on its first frame
void RRNav_Unload(void)
{
	Nav_Free();
	BZ_Free(nav.registered);
	nav.registered = NULL;
	nav.num_registered = nav.max_registered = 0;
}

typedef struct
{
	const qbyte *p, *end;
	qboolean bad;
} navreader_t;
static void Nav_Read(navreader_t *r, void *out, size_t size)
{
	if (r->bad || (size_t)(r->end - r->p) < size)
	{
		r->bad = true;
		memset(out, 0, size);
		return;
	}
	memcpy(out, r->p, size);
	r->p += size;
}
static int Nav_ReadInt(navreader_t *r)
{
	int v;
	Nav_Read(r, &v, sizeof(v));
	return LittleLong(v);
}
static short Nav_ReadShort(navreader_t *r)
{
	short v;
	Nav_Read(r, &v, sizeof(v));
	return LittleShort(v);
}
static qbyte Nav_ReadByte(navreader_t *r)
{
	qbyte v;
	Nav_Read(r, &v, sizeof(v));
	return v;
}
static float Nav_ReadFloat(navreader_t *r)
{
	float v;
	Nav_Read(r, &v, sizeof(v));
	return LittleFloat(v);
}
static void Nav_ReadVec(navreader_t *r, float *v, int n)
{
	int i;
	for (i = 0; i < n; i++)
		v[i] = Nav_ReadFloat(r);
}

static const char *Nav_Parse(navreader_t *r)
{
	int v, i, c;

	if (Nav_ReadInt(r) != NAV_MAGIC)
		return "bad magic";
	v = Nav_ReadInt(r);
	if (v < 1 || v > NAV_VERSION_LATEST)
		return va("unsupported version %i", v);

	nav.num_nodes = Nav_ReadInt(r);
	nav.num_links = Nav_ReadInt(r);
	nav.num_traversals = Nav_ReadInt(r);
	nav.heuristic = Nav_ReadFloat(r);
	if (r->bad)
		return "truncated";
	if (nav.num_nodes <= 0 || nav.num_nodes > 0x7fff || nav.num_links < 0 || nav.num_links > 0x7fff*64 || nav.num_traversals < 0 || nav.num_traversals > 0x7fff*64)
		return "bad counts";

	nav.nodes = Z_Malloc(sizeof(*nav.nodes) * nav.num_nodes);
	nav.links = Z_Malloc(sizeof(*nav.links) * max(1, nav.num_links));
	nav.traversals = Z_Malloc(sizeof(*nav.traversals) * max(1, nav.num_traversals));

	for (i = 0; i < nav.num_nodes; i++)
	{
		navnode_t *node = &nav.nodes[i];
		int first_link;
		node->id = i;
		node->flags = (unsigned short)Nav_ReadShort(r);
		node->num_links = Nav_ReadShort(r);
		first_link = Nav_ReadShort(r);
		if (node->num_links < 0 || first_link < 0 || first_link + node->num_links > nav.num_links)
			return "bad node link extents";
		node->links = &nav.links[first_link];
		node->radius = Nav_ReadShort(r);
		if (node->flags & NF_CONDITIONAL)
			nav.num_conditional_nodes++;
	}
	if (nav.num_conditional_nodes)
		nav.conditional_nodes = Z_Malloc(sizeof(*nav.conditional_nodes) * nav.num_conditional_nodes);
	for (i = 0, c = 0; i < nav.num_nodes; i++)
	{
		navnode_t *node = &nav.nodes[i];
		Nav_ReadVec(r, node->origin, 3);
		if (node->flags & NF_CONDITIONAL)
			nav.conditional_nodes[c++] = node;
	}

	for (i = 0; i < nav.num_links; i++)
	{
		navlink_t *link = &nav.links[i];
		int target = Nav_ReadShort(r), traversal;
		if (target < 0 || target >= nav.num_nodes)
			return "bad link target";
		link->target = &nav.nodes[target];
		link->type = Nav_ReadByte(r);
		link->flags = Nav_ReadByte(r);
		if (v < NAV_VERSION_3)
			link->flags = NLF_ALLTEAMS;
		else if (v < NAV_VERSION_6)
			link->flags &= ~((1<<2)|(1<<3));
		traversal = Nav_ReadShort(r);
		if (traversal != -1)
		{
			if (traversal < 0 || traversal >= nav.num_traversals)
				return "bad link traversal";
			link->traversal = &nav.traversals[traversal];
		}
	}

	for (i = 0; i < nav.num_traversals; i++)
	{
		navtraversal_t *t = &nav.traversals[i];
		Nav_ReadVec(r, t->funnel, 3);
		Nav_ReadVec(r, t->start, 3);
		Nav_ReadVec(r, t->end, 3);
		if (v >= NAV_VERSION_4)
			Nav_ReadVec(r, t->ladder_plane, 3);
	}

	nav.num_edicts = Nav_ReadInt(r);
	if (r->bad)
		return "truncated";
	if (nav.num_edicts < 0 || nav.num_edicts > nav.num_links)
		return "bad entity count";
	if (nav.num_edicts)
	{
		nav.edicts = Z_Malloc(sizeof(*nav.edicts) * nav.num_edicts);
		for (i = 0; i < nav.num_edicts; i++)
		{
			navedict_t *e = &nav.edicts[i];
			int link = Nav_ReadShort(r);
			if (link < 0 || link >= nav.num_links)
				return "bad entity link";
			e->link = &nav.links[link];
			e->link->edict = e;
			if (v >= NAV_VERSION_2)
				e->model = Nav_ReadInt(r);
			Nav_ReadVec(r, e->mins, 3);
			Nav_ReadVec(r, e->maxs, 3);
		}
	}
	if (r->bad)
		return "truncated";

	nav.came_from = Z_Malloc(sizeof(*nav.came_from) * nav.num_nodes);
	nav.went_to = Z_Malloc(sizeof(*nav.went_to) * nav.num_nodes);
	nav.g_score = Z_Malloc(sizeof(*nav.g_score) * nav.num_nodes);
	return NULL;
}

static void Nav_Load(const char *mapname)
{
	char filename[MAX_QPATH];
	navreader_t r;
	size_t size;
	qbyte *data;
	const char *err;

	Nav_Free();
	nav.loaded = true;
	Q_strncpyz(nav.mapname, mapname, sizeof(nav.mapname));
	if (!*mapname)
		return;

	Q_snprintfz(filename, sizeof(filename), "bots/navigation/%s.nav", mapname);
	data = FS_LoadMallocFile(filename, &size);
	if (!data)
	{
		Con_DPrintf("No navigation for %s\n", mapname);
		return;
	}
	r.p = data;
	r.end = data + size;
	r.bad = false;
	err = Nav_Parse(&r);
	BZ_Free(data);
	if (err)
	{
		Con_Printf(CON_WARNING"Couldn't load %s: %s\n", filename, err);
		Nav_Free();
		nav.loaded = true;
		Q_strncpyz(nav.mapname, mapname, sizeof(nav.mapname));
		return;
	}
	Con_DPrintf("%s: %i nodes, %i links, %i traversals, %i entities\n", filename, nav.num_nodes, nav.num_links, nav.num_traversals, nav.num_edicts);
}

/*
=====================================================================
Pathing
=====================================================================
*/
static rr_trace_t Nav_Trace(const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, uint32_t mask)
{
	return nav.gi->trace(start, mins, maxs, end, NULL, mask);
}

static float Nav_Heuristic(const navnode_t *goal, const navnode_t *node)
{
	return Distance(goal->origin, node->origin);
}
static float Nav_Weight(const navnode_t *node, const navlink_t *link)
{
	if (link->type == NLT_TELEPORT)
		return 1.0f;
	return Distance(node->origin, link->target->origin) * nav.heuristic;
}

static qboolean Nav_NodeAccessible(const rr_PathRequest *request, const navnode_t *node)
{
	uint32_t wf = request->pathFlags & (RR_PATHFLAG_WALK|RR_PATHFLAG_WATER);
	if (node->flags & NF_DISABLED)
		return false;
	if (request->nodeSearch.ignoreNodeFlags)
	{
		if (node->flags & NF_NOPOI)
			return false;
	}
	else
	{
		if (node->flags & (NF_NOMONSTERS | NF_CROUCH | NF_LADDER | NF_PUSHER | NF_TELEPORTER))
			return false;
		if ((node->flags & NF_UNDERWATER) && wf == RR_PATHFLAG_WALK)
			return false;
		if (!(node->flags & NF_UNDERWATER) && wf == RR_PATHFLAG_WATER)
			return false;
		if (!(request->pathFlags & RR_PATHFLAG_ELEVATOR) && (node->flags & NF_ELEVATOR))
			return false;
	}
	return true;
}

static qboolean Nav_LinkAccessible(const rr_PathRequest *request, const navnode_t *node, const navlink_t *link)
{
	if (!request->nodeSearch.ignoreNodeFlags)
	{
		qboolean entity_traversal = false;

		if (request->pathFlags == RR_PATHFLAG_WATER)
		{	//swimmers only go by walk links
			if (link->type != NLT_WALK)
				return false;
		}
		else if (link->type == NLT_ELEVATOR)
		{
			if (!(request->pathFlags & RR_PATHFLAG_ELEVATOR))
				return false;
			entity_traversal = true;
		}
		else if (link->type == NLT_WALKOFFLEDGE)
		{
			if (!(request->pathFlags & RR_PATHFLAG_WALKOFFLEDGE))
				return false;
			if (request->traversals.dropHeight > 0 && link->target->origin[2] < node->origin[2] - request->traversals.dropHeight)
				return false;
		}
		else if (link->type == NLT_LONGJUMP)
		{
			if (!(request->pathFlags & RR_PATHFLAG_LONGJUMP))
				return false;
		}
		else if (link->type == NLT_BARRIERJUMP)
		{
			if (!(request->pathFlags & RR_PATHFLAG_BARRIERJUMP))
				return false;
			if (request->traversals.jumpHeight > 0 && link->target->origin[2] > node->origin[2] + request->traversals.jumpHeight)
				return false;
		}

		if (link->edict && !entity_traversal)
			return false;
	}
	return Nav_NodeAccessible(request, link->target);
}

static navnode_t *Nav_ClosestNodeTo(const vec3_t p, const rr_PathRequest *request)
{
	static const vec3_t zero = {0,0,0};
	float best = FLT_MAX;
	navnode_t *c = NULL;
	float minheight = request->nodeSearch.minHeight > 0 ? request->nodeSearch.minHeight : 64.f;
	float maxheight = request->nodeSearch.maxHeight > 0 ? request->nodeSearch.maxHeight : 64.f;
	float radius = request->nodeSearch.radius > 0 ? request->nodeSearch.radius : 512.f;
	qboolean wateronly = request->pathFlags == RR_PATHFLAG_WATER;
	float bz = p[2] - minheight;
	float tz = p[2] + maxheight;
	int i;

	for (i = 0; i < nav.num_nodes; i++)
	{
		navnode_t *node = &nav.nodes[i];
		vec3_t end;
		rr_trace_t tr;
		float l;

		if (!request->nodeSearch.ignoreNodeFlags)
		{	//transitional, or not for monsters
			if (node->flags & (NF_DISABLED | NF_PUSHER | NF_TELEPORTER | NF_LADDER | NF_CROUCH | NF_NOMONSTERS))
				continue;
			if (wateronly && !(node->flags & NF_UNDERWATER))
				continue;
		}

		if (node->origin[2] < bz || node->origin[2] > tz)
			continue;
		l = sqrt((p[0]-node->origin[0])*(p[0]-node->origin[0]) + (p[1]-node->origin[1])*(p[1]-node->origin[1]));
		if (l > radius || l > best)
			continue;

		VectorCopy(node->origin, end);
		end[2] += 32;
		tr = Nav_Trace(p, zero, zero, end, RRMASK_SOLID | RRCONTENTS_PLAYERCLIP | RRCONTENTS_MONSTERCLIP);
		if (tr.fraction < 1)
			continue;

		best = l;
		c = node;
	}
	return c;
}

static const navlink_t *Nav_GetLink(const navnode_t *a, const navnode_t *b)
{
	const navlink_t *link;
	for (link = a->links; link != a->links + a->num_links; link++)
		if (link->target == b)
			return link;
	return NULL;
}

static qboolean Nav_NodeReached(const vec3_t pos, const navnode_t *node)
{
	vec3_t d;
	VectorSubtract(node->origin, pos, d);
	if (sqrt(d[0]*d[0] + d[1]*d[1]) > node->radius)
		return false;
	if (fabs(d[2]) > 64)
		return false;
	return true;
}

static void Nav_PushOpen(int node, float g, float f)
{
	size_t i, parent;
	navopen_t o;
	if (nav.num_open == nav.max_open)
	{
		nav.max_open = nav.max_open ? nav.max_open * 2 : 256;
		nav.open = BZ_Realloc(nav.open, sizeof(*nav.open) * nav.max_open);
	}
	o.f = f;
	o.g = g;
	o.node = node;
	for (i = nav.num_open++; i > 0; i = parent)
	{
		parent = (i - 1) / 2;
		if (nav.open[parent].f <= f)
			break;
		nav.open[i] = nav.open[parent];
	}
	nav.open[i] = o;
}
static navopen_t Nav_PopOpen(void)
{
	navopen_t top = nav.open[0], last = nav.open[--nav.num_open];
	size_t i = 0, child;
	for (;;)
	{
		child = i * 2 + 1;
		if (child >= nav.num_open)
			break;
		if (child + 1 < nav.num_open && nav.open[child + 1].f < nav.open[child].f)
			child++;
		if (last.f <= nav.open[child].f)
			break;
		nav.open[i] = nav.open[child];
		i = child;
	}
	if (nav.num_open)
		nav.open[i] = last;
	return top;
}

static void Nav_PushPathPoint(rr_PathInfo *info, const rr_PathRequest *request, const vec3_t p)
{
	if (info->numPathPoints < request->pathPoints.count)
		VectorCopy(p, request->pathPoints.array[info->numPathPoints]);
	info->numPathPoints++;
}

static void Nav_ReachedGoal(rr_PathInfo *info, const rr_PathRequest *request, int current)
{
	int num_points = 0, first_point = 0, n, p;
	const navlink_t *link = NULL;
	navnode_t *n0, *n1;

	//came_from runs goal->start: went_to is it the other way about, from the start node to the goal's (left out)
	for (n = current; nav.came_from[n] != -1; n = nav.came_from[n])
		num_points++;
	for (n = current, p = 0; nav.came_from[n] != -1; p++)
		n = nav.went_to[num_points - p - 1] = nav.came_from[n];

	if (num_points > 1)
	{
		n0 = &nav.nodes[nav.went_to[0]];
		n1 = &nav.nodes[nav.went_to[1]];
		link = Nav_GetLink(n0, n1);

		if (link && !request->nodeSearch.ignoreNodeFlags)
		{	//not a traversal: skip the first node if we're on it or past it
			if (link->type == NLT_WALK || link->type == NLT_CROUCH)
			{
				if (Nav_NodeReached(request->start, n0))
					first_point++;
				else
				{	//in line for the next one?
					vec3_t d = {0,0,0}, origin, path = {0,0,0};
					float l;
					d[0] = n1->origin[0] - n0->origin[0];
					d[1] = n1->origin[1] - n0->origin[1];
					l = sqrt(d[0]*d[0] + d[1]*d[1]);
					if (l)
					{
						d[0] /= l;
						d[1] /= l;
					}
					VectorMA(n0->origin, n0->radius, d, origin);
					path[0] = n1->origin[0] - origin[0];
					path[1] = n1->origin[1] - origin[1];
					if (DotProduct(d, path) > 0)
						first_point++;
				}
			}
		}
	}

	if (request->pathPoints.count)
	{	//the path, for the compass and the like
		if (Distance(request->start, nav.nodes[nav.went_to[first_point]].origin) > PATH_POINT_TOO_CLOSE)
			Nav_PushPathPoint(info, request, request->start);
		for (p = first_point; p < num_points; p++)
			Nav_PushPathPoint(info, request, nav.nodes[nav.went_to[p]].origin);
		if (Distance(request->goal, nav.nodes[nav.went_to[num_points - 1]].origin) > PATH_POINT_TOO_CLOSE)
			Nav_PushPathPoint(info, request, request->goal);
	}

	if (request->nodeSearch.ignoreNodeFlags)
	{	//no traversals wanted
		info->returnCode = RR_PATH_RAWPATHFOUND;
		return;
	}

	if (link && link->traversal)
	{
		VectorCopy(link->traversal->start, info->firstMovePoint);
		VectorCopy(link->traversal->end, info->secondMovePoint);
		info->returnCode = RR_PATH_TRAVERSALPENDING;
		return;
	}

	VectorCopy(nav.nodes[nav.went_to[first_point]].origin, info->firstMovePoint);
	if (first_point + 1 < num_points)
		VectorCopy(nav.nodes[nav.went_to[first_point + 1]].origin, info->secondMovePoint);
	else
		VectorCopy(request->goal, info->secondMovePoint);
	info->returnCode = RR_PATH_INPROGRESS;
}

static void Nav_Path(const rr_PathRequest *request, rr_PathInfo *info)
{
	navnode_t *start, *goal;
	int i;

	memset(info, 0, sizeof(*info));

	if (!nav.nodes || !nav.gi)
	{
		info->returnCode = RR_PATH_NONAVAILABLE;
		return;
	}
	if (!(request->pathFlags & (RR_PATHFLAG_WALK | RR_PATHFLAG_WATER)))
	{
		info->returnCode = RR_PATH_MISSINGWALKORSWIMFLAG;
		return;
	}

	start = Nav_ClosestNodeTo(request->start, request);
	if (!start)
	{
		info->returnCode = RR_PATH_NOSTARTNODE;
		return;
	}
	goal = Nav_ClosestNodeTo(request->goal, request);
	if (!goal)
	{
		info->returnCode = RR_PATH_NOGOALNODE;
		return;
	}
	if (start == goal || Distance(request->start, goal->origin) <= request->moveDist)
	{
		info->returnCode = RR_PATH_REACHEDGOAL;
		return;
	}

	if (!request->nodeSearch.ignoreNodeFlags)
	{
		if (nav.gi->pointcontents(request->start) & RRMASK_SOLID)
		{
			info->returnCode = RR_PATH_INVALIDSTART;
			return;
		}
		if (nav.gi->pointcontents(request->goal) & RRMASK_SOLID)
		{
			info->returnCode = RR_PATH_INVALIDGOAL;
			return;
		}
	}

	for (i = 0; i < nav.num_nodes; i++)
		nav.g_score[i] = FLT_MAX;
	nav.num_open = 0;

	nav.came_from[start->id] = -1;
	nav.g_score[start->id] = 0;
	Nav_PushOpen(start->id, 0, Nav_Heuristic(goal, start));

	info->returnCode = RR_PATH_NOPATHFOUND;

	while (nav.num_open)
	{
		navopen_t o = Nav_PopOpen();
		const navnode_t *node;
		const navlink_t *link;

		if (o.g > nav.g_score[o.node])
			continue;	//it got somewhere better since
		if (o.node == goal->id)
		{
			Nav_ReachedGoal(info, request, o.node);
			break;
		}

		node = &nav.nodes[o.node];
		for (link = node->links; link != node->links + node->num_links; link++)
		{
			int target = link->target->id;
			float g;
			if (!Nav_LinkAccessible(request, node, link))
				continue;
			g = nav.g_score[o.node] + Nav_Weight(node, link);
			if (g >= nav.g_score[target])
				continue;
			nav.came_from[target] = o.node;
			nav.g_score[target] = g;
			Nav_PushOpen(target, g, g + Nav_Heuristic(goal, link->target));
		}
	}
}

rr_bool RRNav_GetPathToGoal(const rr_PathRequest *request, rr_PathInfo *info)
{
	rr_PathInfo result;
	Nav_Path(request, &result);
	if (nav.debug && nav.debug->ival)
		Con_Printf("nav: (%g %g %g) to (%g %g %g): %i, %i points, move to (%g %g %g)\n",
			request->start[0], request->start[1], request->start[2], request->goal[0], request->goal[1], request->goal[2],
			result.returnCode, result.numPathPoints, result.firstMovePoint[0], result.firstMovePoint[1], result.firstMovePoint[2]);
	if (info)
		*info = result;
	return result.returnCode < RR_PATH_STARTPATHERRORS;
}

/*
=====================================================================
Conditional nodes, and the entities links go through
=====================================================================
*/
static qboolean Nav_BoundsIntersect(const vec3_t amins, const vec3_t amaxs, const vec3_t bmins, const vec3_t bmaxs)
{
	return	amins[0] <= bmaxs[0] && amaxs[0] >= bmins[0] &&
			amins[1] <= bmaxs[1] && amaxs[1] >= bmins[1] &&
			amins[2] <= bmaxs[2] && amaxs[2] >= bmins[2];
}
//does the line start->end go through the box
static qboolean Nav_BoundsIntersectLine(const vec3_t mins, const vec3_t maxs, const vec3_t start, const vec3_t end)
{
	vec3_t center, extents, linedir, linecenter, dir;
	float ld[3];
	int i;
	for (i = 0; i < 3; i++)
	{
		center[i] = (mins[i] + maxs[i]) * 0.5f;
		extents[i] = maxs[i] - center[i];
		linedir[i] = (end[i] - start[i]) * 0.5f;
		linecenter[i] = start[i] + linedir[i];
		dir[i] = linecenter[i] - center[i];
		ld[i] = fabs(linedir[i]);
		if (fabs(dir[i]) > extents[i] + ld[i])
			return false;
	}
	if (fabs(dir[1]*linedir[2] - dir[2]*linedir[1]) > extents[1]*ld[2] + extents[2]*ld[1])
		return false;
	if (fabs(dir[2]*linedir[0] - dir[0]*linedir[2]) > extents[0]*ld[2] + extents[2]*ld[0])
		return false;
	if (fabs(dir[0]*linedir[1] - dir[1]*linedir[0]) > extents[0]*ld[1] + extents[1]*ld[0])
		return false;
	return true;
}

static void Nav_UpdateConditionalNode(navnode_t *node)
{
	vec3_t mins = {-16, -16, -24}, maxs = {16, 16, 32}, origin;
	rr_trace_t tr;
	size_t i;

	node->flags &= ~NF_DISABLED;

	if (node->flags & NF_CROUCH)
		maxs[2] = 4;
	VectorCopy(node->origin, origin);
	origin[2] += 24;

	if (node->flags & NF_CHECKINSOLID)
	{
		tr = Nav_Trace(origin, mins, maxs, origin, RRMASK_SOLID);
		if (tr.startsolid || tr.allsolid)
		{
			node->flags |= NF_DISABLED;
			return;
		}
	}

	if (node->flags & NF_CHECKINLIQUID)
	{
		tr = Nav_Trace(origin, mins, maxs, origin, RRMASK_WATER);
		if (!(tr.startsolid || tr.allsolid))
		{
			node->flags |= NF_DISABLED;
			return;
		}
	}

	if (node->flags & NF_CHECKFORHAZARD)
	{
		tr = Nav_Trace(origin, mins, maxs, origin, RRCONTENTS_SLIME | RRCONTENTS_LAVA);
		if (tr.startsolid || tr.allsolid)
			node->flags |= NF_DISABLED;
		else
		{
			vec3_t absmin, absmax;
			VectorAdd(origin, mins, absmin);
			VectorAdd(origin, maxs, absmax);
			for (i = 0; i < nav.num_registered; i++)
			{
				const rr_edict_t *e = nav.registered[i];
				if (!e || !e->inuse)
					continue;
				if (!(e->sv.ent_flags & RRSVFL_TRAP_DANGER))
					continue;
				if (e->s.renderfx & RRRF_BEAM)
				{
					if (e->svflags & RRSVF_NOCLIENT)
						continue;
					if (Nav_BoundsIntersectLine(absmin, absmax, e->s.origin, e->s.old_origin))
					{
						node->flags |= NF_DISABLED;
						return;
					}
				}
				else if (e->solid == RRSOLID_TRIGGER)
				{
					if (Nav_BoundsIntersect(e->absmin, e->absmax, absmin, absmax))
					{
						node->flags |= NF_DISABLED;
						return;
					}
				}
			}
		}
	}

	if (node->flags & NF_CHECKHASFLOOR)
	{
		vec3_t fmins = {mins[0], mins[1], 0}, fmaxs = {maxs[0], maxs[1], 0}, end;
		VectorCopy(origin, end);
		end[2] -= NAV_FLOOR_DISTANCE;
		tr = Nav_Trace(origin, fmins, fmaxs, end, RRMASK_SOLID);
		if (tr.fraction == 1)
		{
			node->flags |= NF_DISABLED;
			return;
		}
	}

	if (node->flags & NF_CHECKDOORLINKS)
	{
		navlink_t *link;
		for (link = node->links; link != node->links + node->num_links; link++)
		{
			const rr_edict_t *e;
			if (!link->edict || !link->edict->game_edict)
				continue;
			e = link->edict->game_edict;
			if (!e->inuse)
				continue;
			if (e->sv.ent_flags & RRSVFL_IS_LOCKED_DOOR)
			{
				node->flags |= NF_DISABLED;
				return;
			}
		}
	}
}

//which of the map's entities each of the file's is: the one with its brush model
static void Nav_SetupEntities(void)
{
	int i, n;
	nav.setup_entities = true;
	for (i = 0; i < nav.num_edicts; i++)
	{
		navedict_t *e = &nav.edicts[i];
		for (n = 0; n < nav.ge->num_edicts; n++)
		{
			const rr_edict_t *ge = (const rr_edict_t *)((const char *)nav.ge->edicts + (size_t)n*nav.ge->edict_size);
			if (!ge->inuse || ge->s.modelindex != e->model)
				continue;
			if (!ge->sv.classname || !*ge->sv.classname)
				continue;	//only map-spawned ones
			if (Q_strncasecmp(ge->sv.classname, "func_", 5) && Q_strncasecmp(ge->sv.classname, "trigger_", 8))
				Con_DPrintf("Nav entity %i (a %s) might not be safe as an entity\n", i, ge->sv.classname);
			e->game_edict = ge;
			break;
		}
		if (!e->game_edict)
			Con_DPrintf("Nav entity %i is missing (needs an entity with model %i)\n", i, e->model);
	}
}

//the game's loaded
void RRNav_Init(void)
{
	nav.debug = Cvar_Get("nav_debug", "0", 0, "Quake2 game variables");
}

//before each of the game's frames
void RRNav_Frame(const char *mapname, rr_game_import_t *gi, rr_game_export_t *ge)
{
	int i;
	nav.gi = gi;
	nav.ge = ge;
	if (!nav.loaded || strcmp(nav.mapname, mapname))
		Nav_Load(mapname);
	if (!nav.nodes)
		return;

	if (++nav.nav_frame > 10)	//a second in (once the map's entities have settled)
		if (!nav.setup_entities)
			Nav_SetupEntities();

	for (i = 0; i < nav.num_conditional_nodes; i++)
		Nav_UpdateConditionalNode(nav.conditional_nodes[i]);
}

void RRNav_RegisterEdict(const rr_edict_t *e)
{
	size_t i, free_slot = nav.num_registered;
	for (i = 0; i < nav.num_registered; i++)
	{
		if (nav.registered[i] == e)
			return;
		if (!nav.registered[i])
			free_slot = i;
	}
	if (free_slot == nav.max_registered)
	{
		nav.max_registered = nav.max_registered ? nav.max_registered * 2 : 64;
		nav.registered = BZ_Realloc(nav.registered, sizeof(*nav.registered) * nav.max_registered);
	}
	nav.registered[free_slot] = e;
	if (free_slot == nav.num_registered)
		nav.num_registered++;
}

void RRNav_UnRegisterEdict(const rr_edict_t *e)
{
	size_t i;
	for (i = 0; i < (size_t)nav.num_edicts; i++)
	{
		if (nav.edicts[i].game_edict == e)
		{
			nav.edicts[i].game_edict = NULL;
			break;
		}
	}
	for (i = 0; i < nav.num_registered; i++)
	{
		if (nav.registered[i] == e)
		{
			nav.registered[i] = NULL;
			while (nav.num_registered && !nav.registered[nav.num_registered-1])
				nav.num_registered--;
			break;
		}
	}
}
#endif
