/*
Hexen II's effects and particles as uHexen2 (https://github.com/sezero/uhexen2) runs and draws them: its cl_effect.c
(the server's starteffect/endeffect effects: sprites, chunks, rain, snow, fountains, earthquakes, the rider's
death, gravity wells, teleporter puffs...) and its r_part.c (Hexen II's particles: the model trails, the particle2/3/4
builtins, particleexplosion, rain_go, temp entity splashes, the sun staff's trail, the dark field), kept close to
uHexen2's code. FTE's own scripted versions (h2part) remain for cl_h2effects 0.

The effects come from the server's list of them (server/sv_h2fx.c, PEXT2_H2EFFECTS), or a Hexen II demo.
Particles are one batch of triangles a frame, textured and coloured as uHexen2's GL renderer draws them; the
effects' sprites and models are entities.

GPL v2 or later, as uHexen2:
Copyright (C) 1996-1997  Id Software, Inc.
Copyright (C) 1997-1998  Raven Software Corp.
*/
#include "quakedef.h"

#if defined(HEXEN2) && defined(HAVE_CLIENT)
#include "h2fx.h"
#include "shader.h"

static cvar_t cl_h2effects = CVARFD("cl_h2effects", "1", CVAR_ARCHIVE, "Hexen II's effects, particles and trails as Hexen II (uHexen2) draws them. 0 uses FTE's particle scripts (h2part) instead.");
static cvar_t snow_flurry = CVARFD("snow_flurry", "1", CVAR_ARCHIVE, "Hexen II's snow drifts about as it falls.");
static cvar_t snow_active = CVARFD("snow_active", "1", CVAR_ARCHIVE, "Hexen II's snow falls.");

#define HX_FRAME_TIME	0.05
#define MLS_TORCH		3	//uHexen2's, lightstyle 27 (24+3)

/*
=====================================================================
uHexen2's particle.h
=====================================================================
*/
typedef enum
{
	pt_static,
	pt_grav,
	pt_fastgrav,
	pt_slowgrav,
	pt_fire,
	pt_explode,
	pt_explode2,
	pt_blob,
	pt_blob2,
	pt_rain,
	pt_c_explode,
	pt_c_explode2,
	pt_spit,
	pt_fireball,
	pt_ice,
	pt_spell,
	pt_test,
	pt_quake,
	pt_rd,			/* rider's death */
	pt_vorpal,
	pt_setstaff,
	pt_magicmissile,
	pt_boneshard,
	pt_scarab,
	pt_acidball,
	pt_darken,
	pt_snow,
	pt_gravwell,
	pt_redfire
} ptype_t;

typedef enum
{
	h2rt_rocket_trail = 0,
	h2rt_smoke,
	h2rt_blood,
	h2rt_tracer,
	h2rt_slight_blood,
	h2rt_tracer2,
	h2rt_voor_trail,
	h2rt_fireball,
	h2rt_ice,
	h2rt_spit,
	h2rt_spell,
	h2rt_vorpal,
	h2rt_setstaff,
	h2rt_magicmissile,
	h2rt_boneshard,
	h2rt_scarab,
	h2rt_acidball,
	h2rt_bloodshot
} rt_type_t;

typedef struct particle_s
{
	vec3_t		org;
	float		color;
	struct particle_s	*next;
	vec3_t		vel;
	vec3_t		min_org;
	vec3_t		max_org;
	float		ramp;
	float		die;
	qbyte		type;
	qbyte		flags;
	qbyte		count;
} particle_t;

/*an effect's sprite or model, as uHexen2's entity_t has it*/
typedef struct
{
	vec3_t		origin;
	vec3_t		angles;
	model_t		*model;
	int			frame;
	int			skinnum;
	int			scale;		//100 is its size, 0 too
	int			drawflags;
	int			abslight;
} h2entity_t;

/*
=====================================================================
uHexen2's effects.h, the client's half
=====================================================================
*/
struct EffectT
{
	int			type;
	float		expire_time;

	union
	{
		struct
		{
			vec3_t e_size, dir, min_org, max_org;
			float next_time, wait;
			int color, count, veer, flags;
		} Rain;
		struct
		{
			vec3_t pos, angle, movedir;
			vec3_t vforward, h2_vup, h2_vright;
			int color, cnt;
		} Fountain;
		struct
		{
			vec3_t origin;
			float radius;
		} Quake;
		struct
		{
			vec3_t origin;
			vec3_t velocity;
			int entity_index;
			float time_amount, framelength, frame;
		} Smoke;
		struct
		{
			vec3_t origin;
			int entity_index;
			float time_amount;
			int reverse;	/* Forward animation has been
					   run, now go backwards */
		} Flash;
		struct
		{
			vec3_t origin;
			int entity_index[13];
			float time_amount;
			int stage;
			int color;
			float lifetime;
		} RD; /* Rider Death */
		struct
		{
			int entity_index[16];
			vec3_t origin;
			vec3_t velocity[16];
			float time_amount, framelength;
			float skinnum;
		} Teleporter;
		struct
		{
			vec3_t angle;
			vec3_t origin;
			vec3_t avelocity;
			vec3_t velocity;
			int entity_index;
			float time_amount;
		} Missile;
		struct
		{
			int	entity_index[16];
			vec3_t	velocity[16];
			vec3_t	avel[3];
			vec3_t	origin;
			unsigned char type;
			vec3_t	srcVel;
			unsigned char numChunks;
			float	time_amount;
			float	aveScale;
		} Chunk;

	} ef;
};

static struct EffectT	cl_Effects[MAX_H2EFFECTS];
static float			h2_frametime;	//cl.time - cl.oldtime, as uHexen2's
static double			h2_lasttime;
/*
uHexen2 runs at most 72 frames a second (host_maxfps), and some of its effects make particles every frame (fountains,
earthquakes, the rider's death, gravity wells, the dark field). Those make them at that rate here, not every frame.
*/
#define H2FX_EMITTIME	(1.0/72)
static float			h2_emitaccum;
static qboolean		h2_emitframe;
static vec3_t			h2_vieworg, h2_vpn, h2_vright, h2_vup;	//the view, for the particles and the snow

static float H2FX_Gravity(void)
{

	return movevars.gravity ? movevars.gravity : 800;
}

static qboolean H2FX_PointEmpty(const vec3_t org)
{	//uHexen2: Mod_PointInLeaf (org, cl.worldmodel)->contents == CONTENTS_EMPTY
	if (!cl.worldmodel || cl.worldmodel->loadstate != MLS_LOADED)
		return true;
	return cl.worldmodel->funcs.PointContents(cl.worldmodel, NULL, (vec_t*)org) == FTECONTENTS_EMPTY;
}

static void q_sincosdeg(float deg, float *s, float *c)
{
	*s = sin(deg * (M_PI/180));
	*c = cos(deg * (M_PI/180));
}
static void q_sincosrad(float rad, float *s, float *c)
{
	*s = sin(rad);
	*c = cos(rad);
}

/*
=====================================================================
uHexen2's r_part.c
=====================================================================
*/
#define	SFL_FLUFFY		1	// All largish flakes
#define	SFL_MIXED		2	// Mixed flakes
#define	SFL_HALF_BRIGHT		4	// All flakes start darker
#define	SFL_NO_MELT		8	// Flakes don't melt when his surface, just go away
#define	SFL_IN_BOUNDS		16	// Flakes cannot leave the bounds of their box
#define	SFL_NO_TRANS		32	// All flakes start non-translucent
#define	SFL_64			64
#define	SFL_128			128

#define	MAX_PARTICLES		7000	// default max # of particles at one time
#define	ABSOLUTE_MIN_PARTICLES	512	// no fewer than this no matter what's
					// on the command line

static int ramp1[8]  = { 416,   416+2, 416+4, 416+6,  416+8,  416+10, 416+12, 416+14 };
static int ramp2[8]  = { 384+4, 384+6, 384+8, 384+10, 384+12, 384+13, 384+14, 384+15 };
static int ramp3[8]  = { 0x6d, 0x6b, 6, 5, 4, 3 };
static int ramp4[16] = { 416, 416+1, 416+2, 416+3, 416+4, 416+5, 416+6, 416+7, 416+8, 416+9, 416+10, 416+11, 416+12, 416+13, 416+14, 416+15 };
static int ramp5[16] = { 400, 400+1, 400+2, 400+3, 400+4, 400+5, 400+6, 400+7, 400+8, 400+9, 400+10, 400+11, 400+12, 400+13, 400+14, 400+15 };
static int ramp6[16] = { 256, 256+1, 256+2, 256+3, 256+4, 256+5, 256+6, 256+7, 256+8, 256+9, 256+10, 256+11, 256+12, 256+13, 256+14, 256+15 };
static int ramp7[16] = { 384, 384+1, 384+2, 384+3, 384+4, 384+5, 384+6, 384+7, 384+8, 384+9, 384+10, 384+11, 384+12, 384+13, 384+14, 384+15 };
static int ramp8[16] = { 175, 174, 173, 172, 171, 170, 169, 168, 167, 166, 13, 14, 15, 16, 17, 18 };
static int ramp9[16] = { 416, 416+1, 416+2, 416+3, 416+4, 416+5, 416+6, 416+7, 416+8, 416+9, 416+10, 416+11, 416+12, 416+13, 416+14, 416+15 };
/* MISSION PACK */
static int ramp10[16] ={ 432, 432+1, 432+2, 432+3, 432+4, 432+5, 432+6, 432+7, 432+8, 432+9, 432+10, 432+11, 432+12, 432+13, 432+14, 432+15 };
static int ramp11[8] = { 424, 424+1, 424+2, 424+3, 424+4, 424+5, 424+6, 424+7 };
static int ramp12[8] = { 136, 137, 138, 139, 140, 141, 142, 143 };

static particle_t	*active_particles, *free_particles;
static particle_t	*particles;
static int	r_numparticles;

static	vec3_t	rider_origin;

static void R_RunParticleEffect2 (vec3_t org, vec3_t dmin, vec3_t dmax, int color, ptype_t effect, int count);
static void R_RunParticleEffect3 (vec3_t org, vec3_t box, int color, ptype_t effect, int count);
static void R_RunParticleEffect4 (vec3_t org, float radius, int color, ptype_t effect, int count);

static void R_InitParticles (void)
{
	int		i;

	i = COM_CheckParm ("-particles");

	if (i && i < com_argc-1)
	{
		r_numparticles = atoi(com_argv[i+1]);
		if (r_numparticles < ABSOLUTE_MIN_PARTICLES)
			r_numparticles = ABSOLUTE_MIN_PARTICLES;
	}
	else
	{
		r_numparticles = MAX_PARTICLES;
	}

	particles = (particle_t *) BZ_Malloc (r_numparticles * sizeof(particle_t));
}

static particle_t *AllocParticle (void)
{
	particle_t	*p;

	if (!free_particles)
		return NULL;

	p = free_particles;
	free_particles = p->next;
	p->next = active_particles;
	active_particles = p;

	return p;
}

static void R_DarkFieldParticles (vec3_t org)
{
	int		i, j, k;
	particle_t	*p;
	float		vel;
	vec3_t		dir;

	for (i = -16; i < 16; i += 8)
	{
		for (j = -16; j < 16; j += 8)
		{
			for (k = 0; k < 32; k += 8)
			{
				p = AllocParticle();
				if (!p)
					return;

				p->die = cl.time + 0.2 + (rand() & 7) * 0.02;
				p->color = 150 + (rand() % 6);
				p->type = pt_slowgrav;

				dir[0] = j * 8;
				dir[1] = i * 8;
				dir[2] = k * 8;

				p->org[0] = org[0] + i + (rand() & 3);
				p->org[1] = org[1] + j + (rand() & 3);
				p->org[2] = org[2] + k + (rand() & 3);

				VectorNormalize (dir);
				vel = 50 + (rand() & 63);
				VectorScale (dir, vel, p->vel);
			}
		}
	}
}

static void R_ClearParticles (void)
{
	int		i;

	if (!r_numparticles)
		return;
	free_particles = &particles[0];
	active_particles = NULL;

	for (i = 0; i < r_numparticles-1; i++)
		particles[i].next = &particles[i+1];
	particles[r_numparticles-1].next = NULL;
}

static void R_ParticleExplosion (vec3_t org)
{
	int		i, j;
	particle_t	*p;

	for (i = 0; i < 1024; i++)
	{
		p = AllocParticle();
		if (!p)
			return;

		p->die = cl.time + 5;
		p->color = ramp1[0];
		p->ramp = rand() & 3;
		if (i & 1)
		{
			p->type = pt_explode;
			for (j = 0; j < 3; j++)
			{
				p->org[j] = org[j] + ((rand() & 31) - 16);
				p->vel[j] = (rand() & 511) - 256;
			}
		}
		else
		{
			p->type = pt_explode2;
			for (j = 0; j < 3; j++)
			{
				p->org[j] = org[j] + ((rand() & 31) - 16);
				p->vel[j] = (rand() & 511) - 256;
			}
		}
	}
}

static void R_RunParticleEffect (vec3_t org, vec3_t dir, int color, int count)
{
	int		i, j;
	particle_t	*p;

	for (i = 0; i < count; i++)
	{
		p = AllocParticle();
		if (!p)
			return;

		if (count == 1024)
		{	// rocket explosion
			p->die = cl.time + 5;
			p->color = ramp1[0];
			p->ramp = rand() & 3;
			if (i & 1)
			{
				p->type = pt_explode;
				for (j = 0; j < 3; j++)
				{
					p->org[j] = org[j] + ((rand() & 31) - 16);
					p->vel[j] = (rand() & 511) - 256;
				}
			}
			else
			{
				p->type = pt_explode2;
				for (j = 0; j < 3; j++)
				{
					p->org[j] = org[j] + ((rand() & 31) - 16);
					p->vel[j] = (rand() & 511) - 256;
				}
			}
		}
		else
		{
			p->die = cl.time + 0.1 * (rand() % 5);
			p->color = 256 + 16 + 12 + (rand() & 3);
			p->type = pt_slowgrav;
			for (j = 0; j < 3; j++)
			{
				p->org[j] = org[j] + ((rand() & 15) - 8);
				p->vel[j] = dir[j] * 15;
			}
		}
	}
}

static void R_RunParticleEffect2 (vec3_t org, vec3_t dmin, vec3_t dmax, int color, ptype_t effect, int count)
{
	int		i, j;
	particle_t	*p;
	float		num;

	for (i = 0; i < count; i++)
	{
		p = AllocParticle();
		if (!p)
			return;

		p->die = cl.time + 2;
		p->color = color;
		p->type = effect;
		p->ramp = 0;
		for (j = 0; j < 3; j++)
		{
			num = rand() * (1.0 / RAND_MAX);
			p->org[j] = org[j] + ((rand() & 8) - 4); //added randomness to org
			p->vel[j] = dmin[j] + ((dmax[j] - dmin[j]) * num);
		}
	}
}

static void R_RunParticleEffect3 (vec3_t org, vec3_t box, int color, ptype_t effect, int count)
{
	int		i, j;
	particle_t	*p;
	float		num;

	for (i = 0; i < count; i++)
	{
		p = AllocParticle();
		if (!p)
			return;

		p->die = cl.time + 2;
		p->color = color;
		p->type = effect;
		p->ramp = 0;
		for (j = 0; j < 3; j++)
		{
			num = rand() * (1.0 / RAND_MAX);
			p->org[j] = org[j] + ((rand() & 15) - 8);
			p->vel[j] = (box[j] * num * 2) - box[j];
		}
	}
}

static void R_RunParticleEffect4 (vec3_t org, float radius, int color, ptype_t effect, int count)
{
	int		i, j;
	particle_t	*p;
	float		num;

	for (i = 0; i < count; i++)
	{
		p = AllocParticle();
		if (!p)
			return;

		p->die = cl.time + 2;
		p->color = color;
		p->type = effect;
		p->ramp = 0;
		for (j = 0; j < 3; j++)
		{
			num = rand() * (1.0 / RAND_MAX);
			p->org[j] = org[j] + ((rand() & 15) - 8);
			p->vel[j] = (radius * num * 2) - radius;
		}
	}
}

static void R_LavaSplash (vec3_t org)
{
	int		i, j, k;
	particle_t	*p;
	float		vel;
	vec3_t		dir;

	for (i = -16; i < 16; i++)
	{
		for (j = -16; j < 16; j++)
		{
			for (k = 0; k < 1; k++)
			{
				p = AllocParticle();
				if (!p)
					return;

				p->die = cl.time + 2 + (rand() & 31) * 0.02;
				p->color = 224 + (rand() & 7);
				p->type = pt_slowgrav;

				dir[0] = j*8 + (rand() & 7);
				dir[1] = i*8 + (rand() & 7);
				dir[2] = 256;

				p->org[0] = org[0] + dir[0];
				p->org[1] = org[1] + dir[1];
				p->org[2] = org[2] + (rand() & 63);

				VectorNormalize (dir);
				vel = 50 + (rand() & 63);
				VectorScale (dir, vel, p->vel);
			}
		}
	}
}

static void R_TeleportSplash (vec3_t org)
{
	int		i, j, k;
	particle_t	*p;
	float		vel;
	vec3_t		dir;

	for (i = -16; i < 16; i += 4)
	{
		for (j = -16; j < 16; j += 4)
		{
			for (k = -24; k < 32; k += 4)
			{
				p = AllocParticle();
				if (!p)
					return;

				p->die = cl.time + 0.2 + (rand() & 7) * 0.02;
				p->color = 7 + (rand() & 7);
				p->type = pt_slowgrav;

				dir[0] = j * 8;
				dir[1] = i * 8;
				dir[2] = k * 8;

				p->org[0] = org[0] + i + (rand() & 3);
				p->org[1] = org[1] + j + (rand() & 3);
				p->org[2] = org[2] + k + (rand() & 3);

				VectorNormalize (dir);
				vel = 50 + (rand() & 63);
				VectorScale (dir, vel, p->vel);
			}
		}
	}
}

static void R_RunQuakeEffect (vec3_t org, float distance)
{
	int		i;
	particle_t	*p;
	float		num, num2;

	for (i = 0; i < 100; i++)
	{
		p = AllocParticle();
		if (!p)
			return;

		p->die = cl.time + 0.3 * (rand() % 5);
		p->color = (rand() & 3) + ((rand() % 3) * 16) + (13 * 16) + 256 + 11;
		p->type = pt_quake;
		p->ramp = 0;

		num = rand() * (1.0 / RAND_MAX);
		num2 = distance * num;
		num = rand() * (1.0 / RAND_MAX);
		q_sincosrad(num * 2 * M_PI, &p->org[1], &p->org[0]);
		p->org[0] *= num2;
		p->org[1] *= num2;
		p->org[0] += org[0];
		p->org[1] += org[1];
		num = rand() * (1.0 / RAND_MAX);
		p->org[2] = org[2] + 15*num;
		p->org[2] = org[2];

		num = rand() * (1.0 / RAND_MAX);
		p->vel[0] = (num * 40) - 20;
		num = rand() * (1.0 / RAND_MAX);
		p->vel[1] = (num * 40) - 20;
		num = rand() * (1.0 / RAND_MAX);
		p->vel[2] = 65*num + 80;
	}
}

static void R_SunStaffTrail(vec3_t source, vec3_t dest)
{
	int		i;
	particle_t	*p;
	vec3_t		vec, dist;
	float		length, size;
	vec3_t		src;

	VectorCopy(source, src);	//uHexen2 moves the stream's source along, which it then sets again
	VectorSubtract(dest, src, vec);
	length = VectorNormalize(vec);
	dist[0] = vec[0];
	dist[1] = vec[1];
	dist[2] = vec[2];

	size = 10;

	while (length > 0)
	{
		length -= size;

		if ((p = AllocParticle()) == NULL)
			return;

		p->die = cl.time + 2;

		p->ramp = rand() & 3;
		p->color = ramp6[(int)(p->ramp)];

		p->type = pt_spit;

		for (i = 0; i < 3; i++)
		{
			p->org[i] = src[i] + ((rand() & 3) - 2);
		}

		p->vel[0] = (rand() % 10) - 5;
		p->vel[1] = (rand() % 10) - 5;
		p->vel[2] = (rand() % 10);

		VectorAdd(src, dist, src);
	}
}

static void RiderParticle (int count, vec3_t origin)
{
	int		i;
	particle_t	*p;
	float	radius, angle;

	VectorCopy(origin, rider_origin);

	for (i = 0; i < count; i++)
	{
		p = AllocParticle();
		if (!p)
			return;

		p->die = cl.time + 4;
		p->color = 256 + 16 + 15;
		p->type = pt_rd;
		p->ramp = 0;

		angle = (rand() % 360) / (2 * M_PI);
		radius = 300 + (rand() & 255);
		q_sincosrad(angle, &p->org[0], &p->org[1]);
		p->org[0] *= radius;
		p->org[1] *= radius;
		p->org[0] += origin[0];
		p->org[1] += origin[1];
		p->org[2]  = origin[2];
		p->org[2] += (rand() & 255) - 30;

		p->vel[0] = (rand() & 255) - 127;
		p->vel[1] = (rand() & 255) - 127;
		p->vel[2] = (rand() & 255) - 127;
	}
}

static void GravityWellParticle (int count, vec3_t origin, int color)
{
	int		i;
	particle_t	*p;
	float	radius,angle;

	VectorCopy(origin, rider_origin);

	for (i = 0; i < count; i++)
	{
		p = AllocParticle();
		if (!p)
			return;

		p->die = cl.time + 4;
		p->color = color + (rand() & 15);
		p->type = pt_gravwell;
		p->ramp = 0;

		angle = (rand() % 360) / (2 * M_PI);
		radius = 300 + (rand() & 255);
		q_sincosrad(angle, &p->org[0], &p->org[1]);
		p->org[0] *= radius;
		p->org[1] *= radius;
		p->org[0] += origin[0];
		p->org[1] += origin[1];
		p->org[2]  = origin[2];
		p->org[2] += (rand() & 255) - 30;

		p->vel[0] = (rand() & 255) - 127;
		p->vel[1] = (rand() & 255) - 127;
		p->vel[2] = (rand() & 255) - 127;
	}
}

static void R_RocketTrail (vec3_t startorg, vec3_t end, int type)
{
	vec3_t	vec, dist;
	float	len, size, lifetime;
	int			j;
	particle_t	*p;
	static int tracercount;
	vec3_t	start;

	VectorCopy(startorg, start);
	VectorSubtract (end, start, vec);
	len = VectorNormalize (vec);
	dist[0] = vec[0];
	dist[1] = vec[1];
	dist[2] = vec[2];
	size = 1;
	lifetime = 2;
	switch (type)
	{
		case 9:
		// Spit
			break;

		case 8:
		// Ice
			size *= 5 * 3;
			dist[0] *= 5 * 3;
			dist[1] *= 5 * 3;
			dist[2] *= 5 * 3;
			break;

		case h2rt_acidball:
		// Ice
			size = 5;
			lifetime = .8;
			break;

		default:
			size = 3;
			dist[0] *= 3;
			dist[1] *= 3;
			dist[2] *= 3;
			break;
	}

	while (len > 0)
	{
		len -= size;

		p = AllocParticle();
		if (!p)
			return;

		VectorClear (p->vel);
		p->die = cl.time + lifetime;

		switch (type)
		{
			case h2rt_rocket_trail:
			// rocket trail
				p->ramp = rand() & 3;
				p->color = ramp3[(int)p->ramp];
				p->type = pt_fire;
				for (j = 0; j < 3; j++)
					p->org[j] = start[j] + ((rand() % 6) - 3);
				break;

			case h2rt_smoke:
			// smoke smoke
				p->ramp = (rand() & 3) + 2;
				p->color = ramp3[(int)p->ramp];
				p->type = pt_fire;
				for (j = 0; j < 3; j++)
					p->org[j] = start[j] + ((rand() % 6) - 3);
				break;

			case h2rt_blood:
			// blood
				p->type = pt_slowgrav;
				p->color = 134 + (rand() & 7);
				for (j = 0; j < 3; j++)
					p->org[j] = start[j] + ((rand() % 6) - 3);
				break;

			case h2rt_tracer:
			case h2rt_tracer2:
			// tracer
				p->die = cl.time + 0.5;
				p->type = pt_static;
				if (type == 3)
					p->color = 130 + (rand() & 6);	// 243 + (rand() & 3);
				else
					p->color = 230 + ((tracercount & 4) << 1);

				tracercount++;

				VectorCopy (start, p->org);
				if (tracercount & 1)
				{
					p->vel[0] = 30 * vec[1];
					p->vel[1] = 30 * -vec[0];
				}
				else
				{
					p->vel[0] = 30 * -vec[1];
					p->vel[1] = 30 * vec[0];
				}
				break;

			case h2rt_slight_blood:
			// slight blood
				p->type = pt_slowgrav;
				p->color = 134 + (rand() & 7);
				for (j = 0; j < 3; j++)
					p->org[j] = start[j] + ((rand() % 6) - 3);
				len -= size;
				break;

			case h2rt_bloodshot:
			// bloodshot trail
				p->type = pt_darken;
				p->color = 136 + (rand() & 5);
				for (j = 0; j < 3; j++)
					p->org[j] = start[j] + ((rand() & 3) - 2);
				len -= size;
				break;

			case h2rt_voor_trail:
			// voor trail
				p->color = 9*16 + 8 + (rand() & 3);
				p->type = pt_static;
				p->die = cl.time + 0.3;
				for (j = 0; j < 3; j++)
					p->org[j] = start[j] + ((rand() & 15) - 8);
				break;

			case h2rt_fireball:
			// Fireball
				p->ramp = rand() & 3;
				p->color = ramp4[(int)(p->ramp)];
				p->type = pt_fireball;
				for (j = 0; j < 3; j++)
					p->org[j] = start[j] + ((rand() & 3) - 2);
				p->org[2] += 2;	// compensate for model
				p->vel[0] = (rand() % 200) - 100;
				p->vel[1] = (rand() % 200) - 100;
				p->vel[2] = (rand() % 200) - 100;
				break;

			case h2rt_acidball:
			// Acid ball
				p->ramp = rand() & 3;
				p->color = ramp10[(int)(p->ramp)];
				p->type = pt_acidball;
				p->die = cl.time + 0.5;
				for (j = 0; j < 3; j++)
					p->org[j] = start[j] + ((rand() & 3) - 2);
				p->org[2] += 2;	// compensate for model
				p->vel[0] = (rand() % 40) - 20;
				p->vel[1] = (rand() % 40) - 20;
				p->vel[2] = (rand() % 40) - 20;
				break;

			case h2rt_ice:
			// Ice
				p->ramp = rand() & 3;
				p->color = ramp5[(int)(p->ramp)];
				p->type = pt_ice;
				for (j = 0; j < 3; j++)
					p->org[j] = start[j] + ((rand() & 3) - 2);
				p->org[2] += 2;	// compensate for model
				p->vel[0] = (rand() % 16) - 8;
				p->vel[1] = (rand() % 16) - 8;
				p->vel[2] = (rand() % 20) - 40;
				break;

			case h2rt_spit:
			// Spit
				p->ramp = rand() & 3;
				p->color = ramp6[(int)(p->ramp)];
				p->type = pt_spit;
				for (j = 0; j < 3; j++)
					p->org[j] = start[j] + ((rand() & 3) - 2);
				p->org[2] += 2;	// compensate for model
				p->vel[0] = (rand() % 10) - 5;
				p->vel[1] = (rand() % 10) - 5;
				p->vel[2] = (rand() % 10);
				break;

			case h2rt_spell:
			// Spell
				p->ramp = rand() & 3;
				p->color = ramp6[(int)(p->ramp)];
				p->type = pt_spell;
				for (j = 0; j < 3; j++)
					p->org[j] = start[j] + ((rand() & 3) - 2);
				p->vel[0] = (rand() % 10) - 5;
				p->vel[1] = (rand() % 10) - 5;
				p->vel[2] = (rand() % 10);
				p->vel[0] = vec[0] * -10;
				p->vel[1] = vec[1] * -10;
				p->vel[2] = vec[2] * -10;
				break;

			case h2rt_vorpal:
			// vorpal missile
				p->type = pt_vorpal;
				p->color = 44 + (rand() & 3) + 256;
				for (j = 0; j < 2; j++)
					p->org[j] = start[j] + ((rand() % 48) - 24);

				p->org[2] = start[2] + ((rand() & 15) - 8);

				break;

			case h2rt_setstaff:
			// set staff
				p->type = pt_setstaff;
				p->color = ramp9[0];
				p->ramp = rand() & 3;

				for (j = 0; j < 2; j++)
					p->org[j] = start[j] + ((rand() % 6) - 3);

				p->org[2] = start[2] + ((rand() % 10) - 5);

				p->vel[0] = (rand() & 7) - 4;
				p->vel[1] = (rand() & 7) - 4;
				break;

			case h2rt_magicmissile:
			// magic missile
				p->type = pt_magicmissile;
				p->color = 148 + (rand() & 11);
				p->ramp = rand() & 3;
				for (j = 0; j < 2; j++)
					p->org[j] = start[j] + ((rand() % 48) - 24);

				p->org[2] = start[2] + ((rand() % 48) - 24);

				p->vel[2] = -((rand() & 15) + 8);
				break;

			case h2rt_boneshard:
			// bone shard
				p->type = pt_boneshard;
				p->color = 368 + (rand() & 16);
				for (j = 0; j < 2; j++)
					p->org[j] = start[j] + ((rand() % 48) - 24);

				p->org[2] = start[2] + ((rand() % 48) - 24);

				p->vel[2] = -((rand() & 15) + 8);
				break;

			case h2rt_scarab:
			// scarab staff
				p->type = pt_scarab;
				p->color = 250 + (rand() & 3);
				for (j = 0; j < 3; j++)
					p->org[j] = start[j] + (rand() & 7);

				p->vel[2] = -(rand() & 7);
				break;
		}

		VectorAdd (start, dist, start);
	}
}

static void R_RainEffect (vec3_t org, vec3_t e_size, int x_dir, int y_dir, int color, int count)
{
	int		i, holdint;
	particle_t	*p;
	float		z_time;

	for (i = 0; i < count; i++)
	{
		p = AllocParticle();
		if (!p)
			return;

		p->vel[0] = x_dir;	// X and Y motion
		p->vel[1] = y_dir;
		p->vel[2] = -(rand() % 956);
		if (p->vel[2] > -256)
		{
			p->vel[2] += -256;
		}

		z_time = -(e_size[2]/p->vel[2]);
		p->die = cl.time + z_time;
		p->color = color;
		p->ramp = rand() & 3;

		p->type = pt_rain;

		holdint = e_size[0];
		p->org[0] = org[0] + (holdint>0?(rand() % holdint):0);
		holdint = e_size[1];
		p->org[1] = org[1] + (holdint>0?(rand() % holdint):0);
		p->org[2] = org[2];
	}
}

static void R_SnowEffect (vec3_t org1, vec3_t org2, int flags, vec3_t alldir, int count)
{
	int		i, j, holdint;
	particle_t	*p;
	qboolean	empty;

	count *= snow_active.ival;
	for (i = 0; i < count; i++)
	{
		p = AllocParticle();
		if (!p)
			return;

		p->vel[0] = alldir[0];	// X and Y motion
		p->vel[1] = alldir[1];
		p->vel[2] = alldir[2] * ((rand() & 15) + 7)/10;

		p->flags = flags;

		// have a console variable 'happy_snow' that makes all snowflakes happy snow!
		if ((rand() & 0x7f) <= 1)
			p->count = 69;	// happy snow!
		else if (flags & SFL_FLUFFY || (flags & SFL_MIXED && (rand() & 3)))
			p->count = (rand() & 31) + 10;	// From 10 to 41 scale, will be divided
		else
			p->count = 10;

		if (flags & SFL_HALF_BRIGHT)	// Start darker
			p->color = 26 + (rand() % 5);
		else
			p->color = 18 + (rand() % 12);

		if (!(flags & SFL_NO_TRANS))	// Start translucent
			p->color += 256;

		p->die = cl.time + 7;
		p->ramp = rand() & 3;
		p->type = pt_snow;

		holdint = org2[0] - org1[0];
		p->org[0] = org1[0] + (holdint>0?(rand() % holdint):0);
		holdint = org2[1] - org1[1];
		p->org[1] = org1[1] + (holdint>0?(rand() % holdint):0);
		p->org[2] = org2[2];

		j = 50;
		empty = H2FX_PointEmpty(p->org);
		while (!empty && j)
		{	// Make sure it doesn't start in a solid
			holdint = org2[0] - org1[0];
			p->org[0] = org1[0] + (holdint>0?(rand() % holdint):0);
			holdint = org2[1] - org1[1];
			p->org[1] = org1[1] + (holdint>0?(rand() % holdint):0);
			j--;	// No infinite loops
			empty = H2FX_PointEmpty(p->org);
		}
		if (!empty)
			p->die = -1;	//uHexen2 stops the game: "Snow entity top plane is not in an empty area (sorry!)"

		VectorCopy(org1, p->min_org);
		VectorCopy(org2, p->max_org);
	}
}

static void R_ColoredParticleExplosion (vec3_t org, int color, int radius, int counter)
{
	int		i, j;
	particle_t	*p;

	if (radius <= 0)
		radius = 1;	//uHexen2 would divide by 0
	for (i = 0; i < counter; i++)
	{
		p = AllocParticle();
		if (!p)
			return;

		p->die = cl.time + 3;
		p->color = color;
		p->ramp = rand() & 3;

		if (i & 1)
		{
			p->type = pt_c_explode;
			for (j = 0; j < 3; j++)
			{
				p->org[j] = org[j] + ((rand() % (radius * 2)) - radius);
				p->vel[j] = (rand() & 511) - 256;
			}
		}
		else
		{
			p->type = pt_c_explode2;
			for (j = 0; j < 3; j++)
			{
				p->org[j] = org[j] + ((rand() % (radius * 2)) - radius);
				p->vel[j] = (rand() & 511) - 256;
			}
		}
	}
}

static void R_UpdateParticles (void)
{
	particle_t	*p, *kill;
	float		grav, grav2, percent;
	int		i;
	float		time2, time3, time4;
	float		time1;
	float		dvel;
	float		frametime;
	float		vel0, vel1, vel2;
	float		colindex;
	vec3_t		diff;

	frametime = h2_frametime;
	time4 = frametime * 20;
	time3 = frametime * 15;
	time2 = frametime * 10;
	time1 = frametime * 5;
	grav = frametime * H2FX_Gravity() * 0.05;
	grav2 = frametime * H2FX_Gravity() * 0.025;
	dvel = 4 * frametime;
	percent = (frametime / HX_FRAME_TIME);

	for ( ;; )
	{
		kill = active_particles;
		if (kill && kill->die < cl.time)
		{
			active_particles = kill->next;
			kill->next = free_particles;
			free_particles = kill;
			continue;
		}
		break;
	}

	for (p = active_particles ; p ; p = p->next)
	{
		for ( ;; )
		{
			kill = p->next;
			if (kill && kill->die < cl.time)
			{
				p->next = kill->next;
				kill->next = free_particles;
				free_particles = kill;
				continue;
			}
			break;
		}

		if (p->type == pt_rain)
		{
			vel0 = p->vel[0]*.001;
			vel1 = p->vel[1]*.001;
			vel2 = p->vel[2]*.001;
			for (i = 0; i < 4; i++)
			{
				p->org[0] += vel0;
				p->org[1] += vel1;
				p->org[2] += vel2;
			}
			p->org[0] += p->vel[0] * (frametime - .004);
			p->org[1] += p->vel[1] * (frametime - .004);
			p->org[2] += p->vel[2] * (frametime - .004);
		}
		else if (p->type == pt_snow)
		{
			if (p->vel[0] == 0 && p->vel[1] == 0 && p->vel[2] == 0)
			{
			// Stopped moving
				if (p->color == 256 + 31)	// Most translucent white
				{
				// Go away
					p->die = -1;
				}
				else
				{
				// Count fifty and fade in translucency
				// once each time
					p->ramp += 1;
					if (p->ramp >= 7)
					{
						p->color += 1;	//Get more translucent
						p->ramp = 0;
					}
				}
			}
			else
			{
				if (snow_flurry.ival == 1)
				{
				    if (rand() & 31)
				    {
				// Add flurry movement
					float			snow_speed;
					vec3_t			save_vel;
					snow_speed = p->vel[0] * p->vel[0] +
							p->vel[1] * p->vel[1] +
							p->vel[2] * p->vel[2];
					snow_speed = sqrt(snow_speed);

					VectorCopy(p->vel, save_vel);

					save_vel[0] += ( (rand() * (2.0 / RAND_MAX)) - 1 ) * 30;
					save_vel[1] += ( (rand() * (2.0 / RAND_MAX)) - 1 ) * 30;
					if ((rand() & 7) || p->vel[2] > 10)
						save_vel[2] += ( (rand() * (2.0 / RAND_MAX)) - 1 ) * 30;

					VectorNormalize(save_vel);
					VectorScale(save_vel, snow_speed, p->vel);	// retain speed but use new dir
				    }
				}

				VectorScale(p->vel, frametime, diff);
				VectorAdd(p->org, diff, p->org);

				if (p->flags & SFL_IN_BOUNDS)
				{
				// Always stay inside the boundry!
					if ( p->org[0] < p->min_org[0]	||
						p->org[0] > p->max_org[0] ||
						p->org[1] < p->min_org[1] ||
						p->org[1] > p->max_org[1] ||
						p->org[2] < p->min_org[2] ||
						p->org[2] > p->max_org[2] )
					{
						p->die = -1;
					}
				}
				else
				{
					// if hit solid, go to last position,
					// no velocity, fade out.
					if (!H2FX_PointEmpty(p->org))
					{
						if (p->flags & SFL_NO_MELT)
						{
						// Don't melt, just die
							p->die = -1;
						}
						else
						{
						// still have small prob of snow melting on emitter
							VectorScale(diff, 0.2, p->vel);
							i = 6;
							while (!H2FX_PointEmpty(p->org))
							{
								p->org[0] -= p->vel[0];
								p->org[1] -= p->vel[1];
								p->org[2] -= p->vel[2];
								i--; //no infinite loops
								if (!i)
								{
									p->die = -1;	//should never happen now!
									break;
								}
							}
							p->vel[0] = p->vel[1] = p->vel[2] = 0;
							p->ramp = 0;
						}
					}
				}
			}
		}
		else
		{
			p->org[0] += p->vel[0] * frametime;
			p->org[1] += p->vel[1] * frametime;
			p->org[2] += p->vel[2] * frametime;
		}

		switch (p->type)
		{
		case pt_static:
			break;

		case pt_fire:
			p->ramp += time1;
			if ((int)p->ramp >= 6)
				p->die = -1;
			else
				p->color = ramp3[(int)p->ramp];
			p->vel[2] += grav;
			break;

		case pt_explode:
			p->ramp += time2;
			if ((int)p->ramp >= 8)
				p->die = -1;
			else
				p->color = ramp1[(int)p->ramp];
			for (i = 0; i < 3; i++)
				p->vel[i] += p->vel[i]*dvel;
			p->vel[2] -= grav;
			break;

		case pt_explode2:
			p->ramp += time3;
			if ((int)p->ramp >= 8)
				p->die = -1;
			else
				p->color = ramp2[(int)p->ramp];
			for (i = 0; i < 3; i++)
				p->vel[i] -= p->vel[i] * frametime;
			p->vel[2] -= grav;
			break;

		case pt_c_explode:
			p->ramp += time2;
			if ((int)p->ramp >= 8)
				p->die = -1;
			else if (time2)
				p->color--;
			for (i = 0; i < 3; i++)
				p->vel[i] += p->vel[i]*dvel;
			p->vel[2] -= grav;
			break;

		case pt_c_explode2:
			p->ramp += time3;
			if ((int)p->ramp >= 8)
				p->die = -1;
			else if (time3)
				p->color -= 2;
			for (i = 0; i < 3; i++)
				p->vel[i] -= p->vel[i] * frametime;
			p->vel[2] -= grav;
			break;

		case pt_grav:
		case pt_slowgrav:
			p->vel[2] -= grav;
			break;

		case pt_fastgrav:
			p->vel[2] -= grav * 4;
			break;

		case pt_rain:
			break;

		case pt_snow:
			break;

		case pt_fireball:
			p->ramp += time3;
			if ((int)p->ramp >= 16)
				p->die = -1;
			else
				p->color = ramp4[(int)p->ramp];
			break;

		case pt_acidball:
			p->ramp += time4 * 1.4;
			if ((int)p->ramp >= 23)
				p->die = -1;
			else if ((int)p->ramp >= 15)
				p->color = ramp11[(int)p->ramp - 15];
			else
				p->color = ramp10[(int)p->ramp];
			p->vel[2] -= grav;
			break;

		case pt_spit:
			p->ramp += time3;
			if ((int)p->ramp >= 16)
				p->die = -1;
			else
				p->color = ramp6[(int)p->ramp];
			break;

		case pt_ice:
			p->ramp += time4;
			if ((int)p->ramp >= 16)
				p->die = -1;
			else
				p->color = ramp5[(int)p->ramp];
			p->vel[2] -= grav;
			break;

		case pt_spell:
			p->ramp += time2;
			if ((int)p->ramp >= 16)
				p->die = -1;
			else
				p->color = ramp7[(int)p->ramp];
			break;

		case pt_test:
			p->vel[2] += 1.3;
			p->ramp += time3;
			if ((int)p->ramp >= 13 || ((int)p->ramp > 10 && (int)p->vel[2] < 20) )
				p->die = -1;
			else
				p->color = ramp8[(int)p->ramp];
			break;

		case pt_quake:
			p->vel[0] *= 1.05;
			p->vel[1] *= 1.05;
			p->vel[2] -= grav * 4;
			break;

		case pt_rd:
			if (!frametime)
				break;

			p->ramp += percent;
			if ((int)p->ramp > 50)
			{
				p->ramp = 50;
				p->die = -1;
			}
			p->color = 256 + 16 + 16 - (p->ramp / (50/16));

			VectorSubtract(rider_origin, p->org, diff);

			vel0 = 1 / (51 - p->ramp);
			p->org[0] += diff[0] * vel0;
			p->org[1] += diff[1] * vel0;
			p->org[2] += diff[2] * vel0;

			break;

		case pt_gravwell:
			if (!frametime)
				break;

			p->ramp += percent;
			if ((int)p->ramp > 35)
			{
				p->ramp = 35;
				p->die = -1;
			}

			VectorSubtract(rider_origin, p->org, diff);

			vel0 = 1 / (36 - p->ramp);
			p->org[0] += diff[0] * vel0;
			p->org[1] += diff[1] * vel0;
			p->org[2] += diff[2] * vel0;

			break;

		case pt_vorpal:
			--p->color;
			if ((int)p->color <= 37 + 256)
				p->die = -1;
			break;

		case pt_setstaff:
			p->ramp += time1;
			if ((int)p->ramp >= 16)
				p->die = -1;
			else
				p->color = ramp9[(int)p->ramp];

			p->vel[0] *= 1.08 * percent;
			p->vel[1] *= 1.08 * percent;
			p->vel[2] -= grav2;
			break;

		case pt_redfire:
			p->ramp += frametime * 3;
			if ((int)p->ramp >= 8)
				p->die = -1;
			else
				p->color = ramp12[(int)p->ramp] + 256;

			p->vel[0] *= .9;
			p->vel[1] *= .9;
			p->vel[2] += grav/2;
			break;

		case pt_magicmissile:
			--p->color;
			if ((int)p->color < 149)
				p->color = 149;
			p->ramp += time1;
			if ((int)p->ramp > 16)
				p->die = -1;
			break;

		case pt_boneshard:
			--p->color;
			if ((int)p->color < 368)
				p->die = -1;
			break;

		case pt_scarab:
			--p->color;
			if ((int)p->color < 250)
				p->die = -1;
			break;

		case pt_darken:
			p->vel[2] -= grav;	//Also gravity
			--p->color;
			colindex = 0;
			while (colindex < 224)
			{
				if (colindex == 192 || colindex == 200)
					colindex += 8;
				else
					colindex += 16;
				if (p->color == colindex)
					p->die = -1;
			}
		    break;
		}
	}
}

/*
uHexen2's GL R_DrawParticles: a triangle each, 1.5 units of the view's up and right, bigger further away, its corner
of the 16x16 dot texture (snow has bigger flakes and "happy snow"). Colours below 256 are the palette's, opaque; from
256 the translucency table's: 16 colours, 16 levels each.
*/
static const float ptex_coord[4][3][2] =
{
	{ {1.000, 0.000}, {1.000, 0.500}, {0.500, 0.000} },	// any, or snow count < 30
	{ {0.000, 1.000}, {0.500, 1.000}, {0.000, 0.500} },	// snow count >= 30
	{ {0.000, 0.000}, {0.815, 0.000}, {0.000, 0.815} },	// snow count >= 40
	{ {1.000, 1.000}, {1.000, 0.180}, {0.180, 1.000} }	// snow count >= 69 : happy snow!
};
static const qbyte dottexture[16][16] =
{
	{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},//1
	{0,0,0,1,0,1,0,0,0,0,0,0,1,1,0,0},
	{0,0,0,0,1,0,0,0,0,0,0,1,1,1,1,0},
	{0,1,0,1,1,1,0,1,0,0,0,1,1,1,1,0},//4
	{0,0,1,1,1,1,1,0,0,0,0,0,1,1,0,0},
	{0,1,0,1,1,1,0,1,0,0,0,0,0,0,0,0},
	{0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0},
	{0,0,0,1,0,1,0,0,0,0,0,0,0,0,0,0},//8
	{0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,0},
	{0,0,0,0,0,0,0,0,0,1,1,1,0,1,0,0},
	{0,0,0,0,0,0,0,0,1,1,0,1,1,0,1,0},
	{0,0,0,1,0,0,0,0,1,1,1,1,1,0,1,0},//12
	{0,1,1,1,0,0,0,0,1,1,0,1,1,0,1,0},
	{0,0,1,1,1,0,0,0,0,1,1,1,0,1,0,0},
	{0,0,1,0,0,0,0,0,0,0,1,1,1,0,0,0},
	{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},//16
};
static const int ColorIndex[16] = {
	0, 31, 47, 63, 79, 95, 111, 127, 143, 159, 175, 191, 199, 207, 223, 231
};
static const unsigned int ColorPercent[16] = {
	25, 51, 76, 102, 114, 127, 140, 153, 165, 178, 191, 204, 216, 229, 237, 247
};
static shader_t *h2fx_particleshader;
static int h2fx_particletris = -1;	//index: cl_stris may be reallocated by others later in the frame
static int h2fx_particleframe = -1;

static void R_DrawParticles (void)
{
	int		i, color;
	particle_t	*p;
	float		scale;
	vec3_t		r_pup, r_pright;
	scenetris_t *t;
	vec4_t		rgba;
	unsigned int c;
#define	SCALE_BASE	((p->type == pt_snow) ? p->count/10 : 1)

	if (!active_particles)
		return;

	if (!h2fx_particleshader)
	{
		qbyte data[16][16][4];
		int x, y;
		for (x = 0; x < 16; x++)
		{
			for (y = 0; y < 16; y++)
			{
				data[y][x][0] = 255;
				data[y][x][1] = 255;
				data[y][x][2] = 255;
				data[y][x][3] = dottexture[x][y]*255;
			}
		}
		Image_GetTexture("h2particle", "particles", IF_LINEAR|IF_NOMIPMAP|IF_NOPICMIP|IF_CLAMP|IF_NOPURGE|IF_INEXACT, data, NULL, 16, 16, TF_RGBA32);
		h2fx_particleshader = R_RegisterShader("particles_h2", SUF_NONE,
			"{\n"
				"program defaultsprite\n"
				"nomipmaps\n"
				"surfaceparm nodlight\n"
				"{\n"
					"clampmap h2particle\n"
					"rgbgen vertex\n"
					"alphagen vertex\n"
					"blendfunc blend\n"
				"}\n"
			"}\n");
	}
	if (h2fx_particleframe != cl_framecount || h2fx_particletris < 0)
	{	//one batch a frame, after the scene's other triangles were cleared
		if (cl_numstris == cl_maxstris)
		{
			cl_maxstris += 8;
			cl_stris = BZ_Realloc(cl_stris, sizeof(*cl_stris)*cl_maxstris);
		}
		h2fx_particletris = cl_numstris++;
		t = &cl_stris[h2fx_particletris];
		memset(t, 0, sizeof(*t));
		t->shader = h2fx_particleshader;
		t->flags = BEF_NODLIGHT|BEF_NOSHADOWS;
		t->firstidx = cl_numstrisidx;
		t->firstvert = cl_numstrisvert;
		h2fx_particleframe = cl_framecount;
	}
	t = &cl_stris[h2fx_particletris];

	VectorScale (h2_vup, 1.5, r_pup);
	VectorScale (h2_vright, 1.5, r_pright);

	for (p = active_particles ; p ; p = p->next)
	{
		// hack a scale up to keep particles from disapearing
		scale = (p->org[0] - h2_vieworg[0])*h2_vpn[0] +
			(p->org[1] - h2_vieworg[1])*h2_vpn[1] +
			(p->org[2] - h2_vieworg[2])*h2_vpn[2];
		if (scale < 20)
			scale = SCALE_BASE;
		else
			scale = SCALE_BASE + scale * 0.004;

	/* clamp color to 0-511: particle->type 10 and 11 (pt_c_explode
	 * and pt_c_explode2, e.g. Crusader's ice particles hitting a
	 * wall) lead to negative values, because R_UpdateParticles ()
	 * decrements their color against time. */
		color = ((int)p->color) & 0x01ff;
		if (color < 256)
		{
			if (color == 255)
				continue;	// 255 is transparent
			c = d_8to24srgbtable[color];
			Vector4Set(rgba, ((c>>0)&0xff)/255.0, ((c>>8)&0xff)/255.0, ((c>>16)&0xff)/255.0, 1);
		}
		else
		{
			color -= 256;
			c = d_8to24srgbtable[ColorIndex[color>>4]];
			Vector4Set(rgba, ((c>>0)&0xff)/255.0, ((c>>8)&0xff)/255.0, ((c>>16)&0xff)/255.0, ColorPercent[15 - (color&15)]/255.0);
		}

		// setup texture coordinates
		i = 0;
		if (p->type == pt_snow)
		{
			if (p->count >= 69)
				i = 3;	// happy snow!
			else if (p->count >= 40)
				i = 2;
			else if (p->count >= 30)
				i = 1;
		}

		if (cl_numstrisvert+3 > cl_maxstrisvert)
			cl_stris_ExpandVerts(cl_numstrisvert+1024*3);
		if (cl_numstrisidx+3 > cl_maxstrisidx)
		{
			cl_maxstrisidx = cl_numstrisidx+1024*3;
			cl_strisidx = BZ_Realloc(cl_strisidx, sizeof(*cl_strisidx)*cl_maxstrisidx);
		}

		Vector4Copy(rgba, cl_strisvertc[cl_numstrisvert+0]);
		Vector4Copy(rgba, cl_strisvertc[cl_numstrisvert+1]);
		Vector4Copy(rgba, cl_strisvertc[cl_numstrisvert+2]);

		Vector2Copy(ptex_coord[i][0], cl_strisvertt[cl_numstrisvert+0]);
		Vector2Copy(ptex_coord[i][1], cl_strisvertt[cl_numstrisvert+1]);
		Vector2Copy(ptex_coord[i][2], cl_strisvertt[cl_numstrisvert+2]);

		VectorCopy(p->org, cl_strisvertv[cl_numstrisvert+0]);
		VectorMA(p->org, scale, r_pup, cl_strisvertv[cl_numstrisvert+1]);
		VectorMA(p->org, scale, r_pright, cl_strisvertv[cl_numstrisvert+2]);

		cl_strisidx[cl_numstrisidx++] = (cl_numstrisvert - t->firstvert) + 0;
		cl_strisidx[cl_numstrisidx++] = (cl_numstrisvert - t->firstvert) + 1;
		cl_strisidx[cl_numstrisidx++] = (cl_numstrisvert - t->firstvert) + 2;
		cl_numstrisvert += 3;
		t->numvert += 3;
		t->numidx += 3;
	}
#undef SCALE_BASE
}

/*
=====================================================================
uHexen2's cl_effect.c
=====================================================================
*/
#define MAX_EFFECT_ENTITIES		256

static h2entity_t	EffectEntities[MAX_EFFECT_ENTITIES];
static qboolean	EntityUsed[MAX_EFFECT_ENTITIES];
static int	EffectEntityCount;

static int NewEffectEntity (void);
static void FreeEffectEntity (int idx);

static model_t *H2FX_ModForName(const char *name)
{	//uHexen2 stops the game when one is missing. its frames are needed straight away, so wait for it.
	model_t *mod = Mod_ForName(name, MLV_WARNSYNC);
	if (!mod || mod->loadstate != MLS_LOADED)
		return NULL;
	return mod;
}
static int H2FX_NumFrames(h2entity_t *ent)
{
	return ent->model ? ent->model->numframes : 0;
}

static void CL_ClearEffects (void)
{
	memset(cl_Effects, 0, sizeof(cl_Effects));
	memset(EntityUsed, 0, sizeof(EntityUsed));
	EffectEntityCount = 0;
}

static void CL_FreeEffect (int idx)
{
	int		i;

	switch (cl_Effects[idx].type)
	{
	case CE_RAIN:
	case CE_SNOW:
	case CE_FOUNTAIN:
	case CE_QUAKE:
		break;

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
		FreeEffectEntity(cl_Effects[idx].ef.Smoke.entity_index);
		break;

	/* Just go through animation and then remove */
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
	case CE_BRN_BOUNCE:
	case CE_LSHOCK:
	case CE_ACID_HIT:
	case CE_ACID_SPLAT:
	case CE_ACID_EXPL:
	case CE_LBALL_EXPL:
	case CE_FBOOM:
	case CE_BOMB:
	case CE_FIREWALL_SMALL:
	case CE_FIREWALL_MEDIUM:
	case CE_FIREWALL_LARGE:
		FreeEffectEntity(cl_Effects[idx].ef.Smoke.entity_index);
		break;

	/* Go forward then backward through animation then remove */
	case CE_WHITE_FLASH:
	case CE_BLUE_FLASH:
	case CE_SM_BLUE_FLASH:
	case CE_RED_FLASH:
		FreeEffectEntity(cl_Effects[idx].ef.Flash.entity_index);
		break;

	case CE_RIDER_DEATH:
	case CE_GRAVITYWELL:
		break;

	case CE_TELEPORTERPUFFS:
		for (i = 0 ; i < 8 ; ++i)
			FreeEffectEntity(cl_Effects[idx].ef.Teleporter.entity_index[i]);
		break;

	case CE_TELEPORTERBODY:
		FreeEffectEntity(cl_Effects[idx].ef.Teleporter.entity_index[0]);
		break;

	case CE_BONESHARD:
	case CE_BONESHRAPNEL:
		FreeEffectEntity(cl_Effects[idx].ef.Missile.entity_index);
		break;

	case CE_CHUNK:
		for (i = 0 ; i < cl_Effects[idx].ef.Chunk.numChunks ; i++)
			FreeEffectEntity(cl_Effects[idx].ef.Chunk.entity_index[i]);
		break;
	}

	memset(&cl_Effects[idx], 0, sizeof(struct EffectT));
}

static float H2FX_ReadAngle(void)
{	//uHexen2's MSG_ReadAngle
	return MSG_ReadChar() * (360.0f/256.0f);
}

/*
svc_start_effect. hasframe: the mission pack's smoke frame is in the message (uHexen2: protocol > 18, which FTE's
and Hexen II demos are).
*/
static void CLH2_ParseEffect (qboolean hasframe)
{
	int		i, idx;
	qboolean	ImmediateFree;
	h2entity_t	*ent;
	int		dir;
	float		sinval, cosval;
	float		skinnum;
	float		final;

	ImmediateFree = false;

	idx = MSG_ReadByte();
	if (cl_Effects[idx].type)
		CL_FreeEffect(idx);

	memset(&cl_Effects[idx], 0, sizeof(struct EffectT));

	cl_Effects[idx].type = MSG_ReadByte();

	switch (cl_Effects[idx].type)
	{
	case CE_RAIN:
		cl_Effects[idx].ef.Rain.min_org[0] = MSG_ReadCoord();
		cl_Effects[idx].ef.Rain.min_org[1] = MSG_ReadCoord();
		cl_Effects[idx].ef.Rain.min_org[2] = MSG_ReadCoord();
		cl_Effects[idx].ef.Rain.max_org[0] = MSG_ReadCoord();
		cl_Effects[idx].ef.Rain.max_org[1] = MSG_ReadCoord();
		cl_Effects[idx].ef.Rain.max_org[2] = MSG_ReadCoord();
		cl_Effects[idx].ef.Rain.e_size[0] = MSG_ReadCoord();
		cl_Effects[idx].ef.Rain.e_size[1] = MSG_ReadCoord();
		cl_Effects[idx].ef.Rain.e_size[2] = MSG_ReadCoord();
		cl_Effects[idx].ef.Rain.dir[0] = MSG_ReadCoord();
		cl_Effects[idx].ef.Rain.dir[1] = MSG_ReadCoord();
		cl_Effects[idx].ef.Rain.dir[2] = MSG_ReadCoord();
		cl_Effects[idx].ef.Rain.color = MSG_ReadShort();
		cl_Effects[idx].ef.Rain.count = MSG_ReadShort();
		cl_Effects[idx].ef.Rain.wait = MSG_ReadFloat();
		break;

	case CE_SNOW:
		cl_Effects[idx].ef.Rain.min_org[0] = MSG_ReadCoord();
		cl_Effects[idx].ef.Rain.min_org[1] = MSG_ReadCoord();
		cl_Effects[idx].ef.Rain.min_org[2] = MSG_ReadCoord();
		cl_Effects[idx].ef.Rain.max_org[0] = MSG_ReadCoord();
		cl_Effects[idx].ef.Rain.max_org[1] = MSG_ReadCoord();
		cl_Effects[idx].ef.Rain.max_org[2] = MSG_ReadCoord();
		cl_Effects[idx].ef.Rain.flags = MSG_ReadByte();
		cl_Effects[idx].ef.Rain.dir[0] = MSG_ReadCoord();
		cl_Effects[idx].ef.Rain.dir[1] = MSG_ReadCoord();
		cl_Effects[idx].ef.Rain.dir[2] = MSG_ReadCoord();
		cl_Effects[idx].ef.Rain.count = MSG_ReadByte();
		break;

	case CE_FOUNTAIN:
		cl_Effects[idx].ef.Fountain.pos[0] = MSG_ReadCoord ();
		cl_Effects[idx].ef.Fountain.pos[1] = MSG_ReadCoord ();
		cl_Effects[idx].ef.Fountain.pos[2] = MSG_ReadCoord ();
		cl_Effects[idx].ef.Fountain.angle[0] = H2FX_ReadAngle ();
		cl_Effects[idx].ef.Fountain.angle[1] = H2FX_ReadAngle ();
		cl_Effects[idx].ef.Fountain.angle[2] = H2FX_ReadAngle ();
		cl_Effects[idx].ef.Fountain.movedir[0] = MSG_ReadCoord ();
		cl_Effects[idx].ef.Fountain.movedir[1] = MSG_ReadCoord ();
		cl_Effects[idx].ef.Fountain.movedir[2] = MSG_ReadCoord ();
		cl_Effects[idx].ef.Fountain.color = MSG_ReadShort ();
		cl_Effects[idx].ef.Fountain.cnt = MSG_ReadByte ();
		AngleVectors (cl_Effects[idx].ef.Fountain.angle,
				cl_Effects[idx].ef.Fountain.vforward,
				cl_Effects[idx].ef.Fountain.h2_vright,
				cl_Effects[idx].ef.Fountain.h2_vup);
		break;

	case CE_QUAKE:
		cl_Effects[idx].ef.Quake.origin[0] = MSG_ReadCoord ();
		cl_Effects[idx].ef.Quake.origin[1] = MSG_ReadCoord ();
		cl_Effects[idx].ef.Quake.origin[2] = MSG_ReadCoord ();
		cl_Effects[idx].ef.Quake.radius = MSG_ReadFloat ();
		break;

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
		cl_Effects[idx].ef.Smoke.origin[0] = MSG_ReadCoord ();
		cl_Effects[idx].ef.Smoke.origin[1] = MSG_ReadCoord ();
		cl_Effects[idx].ef.Smoke.origin[2] = MSG_ReadCoord ();

		cl_Effects[idx].ef.Smoke.velocity[0] = MSG_ReadFloat ();
		cl_Effects[idx].ef.Smoke.velocity[1] = MSG_ReadFloat ();
		cl_Effects[idx].ef.Smoke.velocity[2] = MSG_ReadFloat ();

		cl_Effects[idx].ef.Smoke.framelength = MSG_ReadFloat ();
		/* smoke frame is a mission pack thing only. */
		if (hasframe)
			cl_Effects[idx].ef.Smoke.frame = MSG_ReadFloat ();

		if ((cl_Effects[idx].ef.Smoke.entity_index = NewEffectEntity()) != -1)
		{
			ent = &EffectEntities[cl_Effects[idx].ef.Smoke.entity_index];
			VectorCopy(cl_Effects[idx].ef.Smoke.origin, ent->origin);

			if ((cl_Effects[idx].type == CE_WHITE_SMOKE) ||
					(cl_Effects[idx].type == CE_SLOW_WHITE_SMOKE))
				ent->model = H2FX_ModForName("models/whtsmk1.spr");
			else if (cl_Effects[idx].type == CE_GREEN_SMOKE)
				ent->model = H2FX_ModForName("models/grnsmk1.spr");
			else if (cl_Effects[idx].type == CE_GREY_SMOKE)
				ent->model = H2FX_ModForName("models/grysmk1.spr");
			else if (cl_Effects[idx].type == CE_RED_SMOKE)
				ent->model = H2FX_ModForName("models/redsmk1.spr");
			else if (cl_Effects[idx].type == CE_TELESMK1)
				ent->model = H2FX_ModForName("models/telesmk1.spr");
			else if (cl_Effects[idx].type == CE_TELESMK2)
				ent->model = H2FX_ModForName("models/telesmk2.spr");
			else if (cl_Effects[idx].type == CE_REDCLOUD)
				ent->model = H2FX_ModForName("models/rcloud.spr");
			else if (cl_Effects[idx].type == CE_FLAMESTREAM)
				ent->model = H2FX_ModForName("models/flamestr.spr");
			else if (cl_Effects[idx].type == CE_ACID_MUZZFL)
			{
				ent->model = H2FX_ModForName("models/muzzle1.spr");
				ent->drawflags = DRF_TRANSLUCENT | MLS_ABSLIGHT;
				ent->abslight = 51;
			}
			else if (cl_Effects[idx].type == CE_FLAMEWALL)
				ent->model = H2FX_ModForName("models/firewal1.spr");
			else if (cl_Effects[idx].type == CE_FLAMEWALL2)
				ent->model = H2FX_ModForName("models/firewal2.spr");
			else if (cl_Effects[idx].type == CE_ONFIRE)
			{
				float	rdm = rand() & 3;

				if (rdm < 1)
					ent->model = H2FX_ModForName("models/firewal1.spr");
				else if (rdm < 2)
					ent->model = H2FX_ModForName("models/firewal2.spr");
				else
					ent->model = H2FX_ModForName("models/firewal3.spr");

				ent->drawflags = DRF_TRANSLUCENT;
				ent->abslight = 255;
				ent->frame = cl_Effects[idx].ef.Smoke.frame;
			}

			if (cl_Effects[idx].type != CE_REDCLOUD &&
					cl_Effects[idx].type != CE_ACID_MUZZFL &&
					cl_Effects[idx].type != CE_FLAMEWALL)
				ent->drawflags = DRF_TRANSLUCENT;

			if (cl_Effects[idx].type == CE_FLAMESTREAM)
			{
				ent->drawflags = DRF_TRANSLUCENT | MLS_ABSLIGHT;
				ent->abslight = 255;
				ent->frame = cl_Effects[idx].ef.Smoke.frame;
			}

			if (cl_Effects[idx].type == CE_GHOST)
			{
				ent->model = H2FX_ModForName("models/ghost.spr");
				ent->drawflags = DRF_TRANSLUCENT | MLS_ABSLIGHT;
				ent->abslight = 127;
			}
			if (!ent->model)
			{
				FreeEffectEntity(cl_Effects[idx].ef.Smoke.entity_index);
				ImmediateFree = true;
			}
		}
		else
			ImmediateFree = true;
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
	case CE_BRN_BOUNCE:
	case CE_LSHOCK:
	case CE_ACID_HIT:
	case CE_ACID_SPLAT:
	case CE_ACID_EXPL:
	case CE_LBALL_EXPL:
	case CE_FBOOM:
	case CE_BOMB:
	case CE_FIREWALL_SMALL:
	case CE_FIREWALL_MEDIUM:
	case CE_FIREWALL_LARGE:
		cl_Effects[idx].ef.Smoke.origin[0] = MSG_ReadCoord ();
		cl_Effects[idx].ef.Smoke.origin[1] = MSG_ReadCoord ();
		cl_Effects[idx].ef.Smoke.origin[2] = MSG_ReadCoord ();
		if ((cl_Effects[idx].ef.Smoke.entity_index = NewEffectEntity()) != -1)
		{
			ent = &EffectEntities[cl_Effects[idx].ef.Smoke.entity_index];
			VectorCopy(cl_Effects[idx].ef.Smoke.origin, ent->origin);

			if (cl_Effects[idx].type == CE_BLUESPARK)
				ent->model = H2FX_ModForName("models/bspark.spr");
			else if (cl_Effects[idx].type == CE_YELLOWSPARK)
				ent->model = H2FX_ModForName("models/spark.spr");
			else if (cl_Effects[idx].type == CE_SM_CIRCLE_EXP)
				ent->model = H2FX_ModForName("models/fcircle.spr");
			else if (cl_Effects[idx].type == CE_BG_CIRCLE_EXP)
				ent->model = H2FX_ModForName("models/xplod29.spr");
			else if (cl_Effects[idx].type == CE_SM_WHITE_FLASH)
				ent->model = H2FX_ModForName("models/sm_white.spr");
			else if (cl_Effects[idx].type == CE_YELLOWRED_FLASH)
			{
				ent->model = H2FX_ModForName("models/yr_flsh.spr");
				ent->drawflags = DRF_TRANSLUCENT;
			}
			else if (cl_Effects[idx].type == CE_SM_EXPLOSION)
				ent->model = H2FX_ModForName("models/sm_expld.spr");
			else if (cl_Effects[idx].type == CE_LG_EXPLOSION)
				ent->model = H2FX_ModForName("models/bg_expld.spr");
			else if (cl_Effects[idx].type == CE_FLOOR_EXPLOSION)
				ent->model = H2FX_ModForName("models/fl_expld.spr");
			else if (cl_Effects[idx].type == CE_FLOOR_EXPLOSION3)
				ent->model = H2FX_ModForName("models/biggy.spr");
			else if (cl_Effects[idx].type == CE_BLUE_EXPLOSION)
				ent->model = H2FX_ModForName("models/xpspblue.spr");
			else if (cl_Effects[idx].type == CE_REDSPARK)
				ent->model = H2FX_ModForName("models/rspark.spr");
			else if (cl_Effects[idx].type == CE_GREENSPARK)
				ent->model = H2FX_ModForName("models/gspark.spr");
			else if (cl_Effects[idx].type == CE_ICEHIT)
				ent->model = H2FX_ModForName("models/icehit.spr");
			else if (cl_Effects[idx].type == CE_MEDUSA_HIT)
				ent->model = H2FX_ModForName("models/medhit.spr");
			else if (cl_Effects[idx].type == CE_MEZZO_REFLECT)
				ent->model = H2FX_ModForName("models/mezzoref.spr");
			else if (cl_Effects[idx].type == CE_FLOOR_EXPLOSION2)
				ent->model = H2FX_ModForName("models/flrexpl2.spr");
			else if (cl_Effects[idx].type == CE_XBOW_EXPLOSION)
				ent->model = H2FX_ModForName("models/xbowexpl.spr");
			else if (cl_Effects[idx].type == CE_NEW_EXPLOSION)
				ent->model = H2FX_ModForName("models/gen_expl.spr");
			else if (cl_Effects[idx].type == CE_MAGIC_MISSILE_EXPLOSION)
				ent->model = H2FX_ModForName("models/mm_expld.spr");
			else if (cl_Effects[idx].type == CE_BONE_EXPLOSION)
				ent->model = H2FX_ModForName("models/bonexpld.spr");
			else if (cl_Effects[idx].type == CE_BLDRN_EXPL)
				ent->model = H2FX_ModForName("models/xplsn_1.spr");
			else if (cl_Effects[idx].type == CE_ACID_HIT)
				ent->model = H2FX_ModForName("models/axplsn_2.spr");
			else if (cl_Effects[idx].type == CE_ACID_SPLAT)
				ent->model = H2FX_ModForName("models/axplsn_1.spr");
			else if (cl_Effects[idx].type == CE_ACID_EXPL)
			{
				ent->model = H2FX_ModForName("models/axplsn_5.spr");
				ent->drawflags = MLS_ABSLIGHT;
				ent->abslight = 255;
			}
			else if (cl_Effects[idx].type == CE_FBOOM)
				ent->model = H2FX_ModForName("models/fboom.spr");
			else if (cl_Effects[idx].type == CE_BOMB)
				ent->model = H2FX_ModForName("models/pow.spr");
			else if (cl_Effects[idx].type == CE_LBALL_EXPL)
				ent->model = H2FX_ModForName("models/Bluexp3.spr");
			else if (cl_Effects[idx].type == CE_FIREWALL_SMALL)
				ent->model = H2FX_ModForName("models/firewal1.spr");
			else if (cl_Effects[idx].type == CE_FIREWALL_MEDIUM)
				ent->model = H2FX_ModForName("models/firewal5.spr");
			else if (cl_Effects[idx].type == CE_FIREWALL_LARGE)
				ent->model = H2FX_ModForName("models/firewal4.spr");
			else if (cl_Effects[idx].type == CE_BRN_BOUNCE)
				ent->model = H2FX_ModForName("models/spark.spr");
			else if (cl_Effects[idx].type == CE_LSHOCK)
			{
				ent->model = H2FX_ModForName("models/vorpshok.mdl");
				ent->drawflags = MLS_TORCH;
				ent->angles[2] = 90;
				ent->scale = 255;
			}
			if (!ent->model)
			{
				FreeEffectEntity(cl_Effects[idx].ef.Smoke.entity_index);
				ImmediateFree = true;
			}
		}
		else
		{
			ImmediateFree = true;
		}
		break;

	case CE_WHITE_FLASH:
	case CE_BLUE_FLASH:
	case CE_SM_BLUE_FLASH:
	case CE_RED_FLASH:
		cl_Effects[idx].ef.Flash.origin[0] = MSG_ReadCoord ();
		cl_Effects[idx].ef.Flash.origin[1] = MSG_ReadCoord ();
		cl_Effects[idx].ef.Flash.origin[2] = MSG_ReadCoord ();
		cl_Effects[idx].ef.Flash.reverse = 0;
		if ((cl_Effects[idx].ef.Flash.entity_index = NewEffectEntity()) != -1)
		{
			ent = &EffectEntities[cl_Effects[idx].ef.Flash.entity_index];
			VectorCopy(cl_Effects[idx].ef.Flash.origin, ent->origin);

			if (cl_Effects[idx].type == CE_WHITE_FLASH)
				ent->model = H2FX_ModForName("models/gryspt.spr");
			else if (cl_Effects[idx].type == CE_BLUE_FLASH)
				ent->model = H2FX_ModForName("models/bluflash.spr");
			else if (cl_Effects[idx].type == CE_SM_BLUE_FLASH)
				ent->model = H2FX_ModForName("models/sm_blue.spr");
			else if (cl_Effects[idx].type == CE_RED_FLASH)
				ent->model = H2FX_ModForName("models/redspt.spr");

			ent->drawflags = DRF_TRANSLUCENT;
			if (!ent->model)
			{
				FreeEffectEntity(cl_Effects[idx].ef.Flash.entity_index);
				ImmediateFree = true;
			}
		}
		else
		{
			ImmediateFree = true;
		}
		break;

	case CE_RIDER_DEATH:
		cl_Effects[idx].ef.RD.origin[0] = MSG_ReadCoord ();
		cl_Effects[idx].ef.RD.origin[1] = MSG_ReadCoord ();
		cl_Effects[idx].ef.RD.origin[2] = MSG_ReadCoord ();
		break;

	case CE_GRAVITYWELL:
		cl_Effects[idx].ef.RD.origin[0] = MSG_ReadCoord ();
		cl_Effects[idx].ef.RD.origin[1] = MSG_ReadCoord ();
		cl_Effects[idx].ef.RD.origin[2] = MSG_ReadCoord ();
		cl_Effects[idx].ef.RD.color = MSG_ReadShort ();
		cl_Effects[idx].ef.RD.lifetime = MSG_ReadFloat ();
		break;

	case CE_TELEPORTERPUFFS:
		cl_Effects[idx].ef.Teleporter.origin[0] = MSG_ReadCoord ();
		cl_Effects[idx].ef.Teleporter.origin[1] = MSG_ReadCoord ();
		cl_Effects[idx].ef.Teleporter.origin[2] = MSG_ReadCoord ();

		cl_Effects[idx].ef.Teleporter.framelength = .05;
		dir = 0;
		for (i = 0 ; i < 8 ; ++i)
			cl_Effects[idx].ef.Teleporter.entity_index[i] = -1;
		for (i = 0 ; i < 8 ; ++i)
		{
			if ((cl_Effects[idx].ef.Teleporter.entity_index[i] = NewEffectEntity()) != -1)
			{
				ent = &EffectEntities[cl_Effects[idx].ef.Teleporter.entity_index[i]];
				VectorCopy(cl_Effects[idx].ef.Teleporter.origin, ent->origin);

				q_sincosdeg(dir, &sinval, &cosval);

				cl_Effects[idx].ef.Teleporter.velocity[i][0] = 10*cosval;
				cl_Effects[idx].ef.Teleporter.velocity[i][1] = 10*sinval;
				cl_Effects[idx].ef.Teleporter.velocity[i][2] = 0;
				dir += 45;

				ent->model = H2FX_ModForName("models/telesmk2.spr");
				ent->drawflags = DRF_TRANSLUCENT;
				if (!ent->model)
				{
					ImmediateFree = true;
					break;
				}
			}
			else
			{
				ImmediateFree = true;
				break;
			}
		}
		if (ImmediateFree) {
			for (i = 0 ; i < 8 ; ++i) {
				if (cl_Effects[idx].ef.Teleporter.entity_index[i] == -1)
					break;
				FreeEffectEntity(cl_Effects[idx].ef.Teleporter.entity_index[i]);
			}
			break;
		}
		break;

	case CE_TELEPORTERBODY:
		cl_Effects[idx].ef.Teleporter.origin[0] = MSG_ReadCoord ();
		cl_Effects[idx].ef.Teleporter.origin[1] = MSG_ReadCoord ();
		cl_Effects[idx].ef.Teleporter.origin[2] = MSG_ReadCoord ();

		cl_Effects[idx].ef.Teleporter.velocity[0][0] = MSG_ReadFloat ();
		cl_Effects[idx].ef.Teleporter.velocity[0][1] = MSG_ReadFloat ();
		cl_Effects[idx].ef.Teleporter.velocity[0][2] = MSG_ReadFloat ();

		skinnum = MSG_ReadFloat ();

		cl_Effects[idx].ef.Teleporter.framelength = .05;
		dir = 0;
		if ((cl_Effects[idx].ef.Teleporter.entity_index[0] = NewEffectEntity()) != -1)
		{
			ent = &EffectEntities[cl_Effects[idx].ef.Teleporter.entity_index[0]];
			VectorCopy(cl_Effects[idx].ef.Teleporter.origin, ent->origin);

			ent->model = H2FX_ModForName("models/teleport.mdl");
			ent->drawflags = SCALE_TYPE_XYONLY | DRF_TRANSLUCENT;
			ent->scale = 100;
			ent->skinnum = skinnum;
			if (!ent->model)
			{
				FreeEffectEntity(cl_Effects[idx].ef.Teleporter.entity_index[0]);
				ImmediateFree = true;
			}
		}
		else
		{
			ImmediateFree = true;
		}
		break;

	case CE_BONESHARD:
	case CE_BONESHRAPNEL:
		cl_Effects[idx].ef.Missile.origin[0] = MSG_ReadCoord ();
		cl_Effects[idx].ef.Missile.origin[1] = MSG_ReadCoord ();
		cl_Effects[idx].ef.Missile.origin[2] = MSG_ReadCoord ();

		cl_Effects[idx].ef.Missile.velocity[0] = MSG_ReadFloat ();
		cl_Effects[idx].ef.Missile.velocity[1] = MSG_ReadFloat ();
		cl_Effects[idx].ef.Missile.velocity[2] = MSG_ReadFloat ();

		cl_Effects[idx].ef.Missile.angle[0] = MSG_ReadFloat ();
		cl_Effects[idx].ef.Missile.angle[1] = MSG_ReadFloat ();
		cl_Effects[idx].ef.Missile.angle[2] = MSG_ReadFloat ();

		cl_Effects[idx].ef.Missile.avelocity[0] = MSG_ReadFloat ();
		cl_Effects[idx].ef.Missile.avelocity[1] = MSG_ReadFloat ();
		cl_Effects[idx].ef.Missile.avelocity[2] = MSG_ReadFloat ();

		if ((cl_Effects[idx].ef.Missile.entity_index = NewEffectEntity()) != -1)
		{
			ent = &EffectEntities[cl_Effects[idx].ef.Missile.entity_index];
			VectorCopy(cl_Effects[idx].ef.Missile.origin, ent->origin);
			if (cl_Effects[idx].type == CE_BONESHARD)
				ent->model = H2FX_ModForName("models/boneshot.mdl");
			else if (cl_Effects[idx].type == CE_BONESHRAPNEL)
				ent->model = H2FX_ModForName("models/boneshrd.mdl");
			if (!ent->model)
			{
				FreeEffectEntity(cl_Effects[idx].ef.Missile.entity_index);
				ImmediateFree = true;
			}
		}
		else
			ImmediateFree = true;
		break;

	case CE_CHUNK:
		cl_Effects[idx].ef.Chunk.origin[0] = MSG_ReadCoord ();
		cl_Effects[idx].ef.Chunk.origin[1] = MSG_ReadCoord ();
		cl_Effects[idx].ef.Chunk.origin[2] = MSG_ReadCoord ();

		cl_Effects[idx].ef.Chunk.type = MSG_ReadByte ();

		cl_Effects[idx].ef.Chunk.srcVel[0] = MSG_ReadCoord ();
		cl_Effects[idx].ef.Chunk.srcVel[1] = MSG_ReadCoord ();
		cl_Effects[idx].ef.Chunk.srcVel[2] = MSG_ReadCoord ();

		cl_Effects[idx].ef.Chunk.numChunks = MSG_ReadByte ();

		cl_Effects[idx].ef.Chunk.time_amount = 4.0;

		cl_Effects[idx].ef.Chunk.aveScale =
					30 + 100 * (cl_Effects[idx].ef.Chunk.numChunks / 40.0);

		if (cl_Effects[idx].ef.Chunk.numChunks > 16)
			cl_Effects[idx].ef.Chunk.numChunks = 16;

		for (i = 0 ; i < cl_Effects[idx].ef.Chunk.numChunks ; i++)
			cl_Effects[idx].ef.Chunk.entity_index[i] = -1;
		for (i = 0 ; i < cl_Effects[idx].ef.Chunk.numChunks ; i++)
		{
			if ((cl_Effects[idx].ef.Chunk.entity_index[i] = NewEffectEntity()) != -1)
			{
				ent = &EffectEntities[cl_Effects[idx].ef.Chunk.entity_index[i]];
				VectorCopy(cl_Effects[idx].ef.Chunk.origin, ent->origin);

				VectorCopy(cl_Effects[idx].ef.Chunk.srcVel,
							cl_Effects[idx].ef.Chunk.velocity[i]);
				VectorScale(cl_Effects[idx].ef.Chunk.velocity[i],
							.80 + ((rand() % 4) / 10.0),
							cl_Effects[idx].ef.Chunk.velocity[i]);
				/* temp modify them... */
				cl_Effects[idx].ef.Chunk.velocity[i][0] += (rand() % 140) - 70;
				cl_Effects[idx].ef.Chunk.velocity[i][1] += (rand() % 140) - 70;
				cl_Effects[idx].ef.Chunk.velocity[i][2] += (rand() % 140) - 70;

				/* are these in degrees or radians? */
				ent->angles[0] = rand() % 360;
				ent->angles[1] = rand() % 360;
				ent->angles[2] = rand() % 360;

				ent->scale = cl_Effects[idx].ef.Chunk.aveScale + (rand() % 40);

				/* make this overcomplicated */
				final = (rand() % 100) * .01;
				if ((cl_Effects[idx].ef.Chunk.type == THINGTYPE_GLASS) ||
					(cl_Effects[idx].ef.Chunk.type == THINGTYPE_REDGLASS) ||
					(cl_Effects[idx].ef.Chunk.type == THINGTYPE_CLEARGLASS) ||
					(cl_Effects[idx].ef.Chunk.type == THINGTYPE_WEBS))
				{
					if (final < 0.20)
						ent->model = H2FX_ModForName ("models/shard1.mdl");
					else if (final < 0.40)
						ent->model = H2FX_ModForName ("models/shard2.mdl");
					else if (final < 0.60)
						ent->model = H2FX_ModForName ("models/shard3.mdl");
					else if (final < 0.80)
						ent->model = H2FX_ModForName ("models/shard4.mdl");
					else
						ent->model = H2FX_ModForName ("models/shard5.mdl");

					if (cl_Effects[idx].ef.Chunk.type == THINGTYPE_CLEARGLASS)
					{
						ent->skinnum = 1;
						ent->drawflags |= DRF_TRANSLUCENT;
					}
					else if (cl_Effects[idx].ef.Chunk.type == THINGTYPE_REDGLASS)
					{
						ent->skinnum = 2;
					}
					else if (cl_Effects[idx].ef.Chunk.type == THINGTYPE_WEBS)
					{
						ent->skinnum = 3;
						ent->drawflags |= DRF_TRANSLUCENT;
					}
				}
				else if (cl_Effects[idx].ef.Chunk.type == THINGTYPE_WOOD)
				{
					if (final < 0.25)
						ent->model = H2FX_ModForName ("models/splnter1.mdl");
					else if (final < 0.50)
						ent->model = H2FX_ModForName ("models/splnter2.mdl");
					else if (final < 0.75)
						ent->model = H2FX_ModForName ("models/splnter3.mdl");
					else
						ent->model = H2FX_ModForName ("models/splnter4.mdl");
				}
				else if (cl_Effects[idx].ef.Chunk.type == THINGTYPE_METAL)
				{
					if (final < 0.25)
						ent->model = H2FX_ModForName ("models/metlchk1.mdl");
					else if (final < 0.50)
						ent->model = H2FX_ModForName ("models/metlchk2.mdl");
					else if (final < 0.75)
						ent->model = H2FX_ModForName ("models/metlchk3.mdl");
					else
						ent->model = H2FX_ModForName ("models/metlchk4.mdl");
				}
				else if (cl_Effects[idx].ef.Chunk.type == THINGTYPE_FLESH)
				{
					if (final < 0.33)
						ent->model = H2FX_ModForName ("models/flesh1.mdl");
					else if (final < 0.66)
						ent->model = H2FX_ModForName ("models/flesh2.mdl");
					else
						ent->model = H2FX_ModForName ("models/flesh3.mdl");
				}
				else if (cl_Effects[idx].ef.Chunk.type == THINGTYPE_BROWNSTONE)
				{
					if (final < 0.25)
						ent->model = H2FX_ModForName ("models/schunk1.mdl");
					else if (final < 0.50)
						ent->model = H2FX_ModForName ("models/schunk2.mdl");
					else if (final < 0.75)
						ent->model = H2FX_ModForName ("models/schunk3.mdl");
					else
						ent->model = H2FX_ModForName ("models/schunk4.mdl");
					ent->skinnum = 1;
				}
				else if ((cl_Effects[idx].ef.Chunk.type == THINGTYPE_CLAY) ||
						(cl_Effects[idx].ef.Chunk.type == THINGTYPE_BONE))
				{
					if (final < 0.25)
						ent->model = H2FX_ModForName ("models/clshard1.mdl");
					else if (final < 0.50)
						ent->model = H2FX_ModForName ("models/clshard2.mdl");
					else if (final < 0.75)
						ent->model = H2FX_ModForName ("models/clshard3.mdl");
					else
						ent->model = H2FX_ModForName ("models/clshard4.mdl");
					if (cl_Effects[idx].ef.Chunk.type == THINGTYPE_BONE)
					{
						ent->skinnum = 1;	/* bone skin is second */
					}
				}
				else if (cl_Effects[idx].ef.Chunk.type == THINGTYPE_LEAVES)
				{
					if (final < 0.33)
						ent->model = H2FX_ModForName ("models/leafchk1.mdl");
					else if (final < 0.66)
						ent->model = H2FX_ModForName ("models/leafchk2.mdl");
					else
						ent->model = H2FX_ModForName ("models/leafchk3.mdl");
				}
				else if (cl_Effects[idx].ef.Chunk.type == THINGTYPE_HAY)
				{
					if (final < 0.33)
						ent->model = H2FX_ModForName ("models/hay1.mdl");
					else if (final < 0.66)
						ent->model = H2FX_ModForName ("models/hay2.mdl");
					else
						ent->model = H2FX_ModForName ("models/hay3.mdl");
				}
				else if (cl_Effects[idx].ef.Chunk.type == THINGTYPE_CLOTH)
				{
					if (final < 0.33)
						ent->model = H2FX_ModForName ("models/clthchk1.mdl");
					else if (final < 0.66)
						ent->model = H2FX_ModForName ("models/clthchk2.mdl");
					else
						ent->model = H2FX_ModForName ("models/clthchk3.mdl");
				}
				else if (cl_Effects[idx].ef.Chunk.type == THINGTYPE_WOOD_LEAF)
				{
					if (final < 0.14)
						ent->model = H2FX_ModForName ("models/splnter1.mdl");
					else if (final < 0.28)
						ent->model = H2FX_ModForName ("models/leafchk1.mdl");
					else if (final < 0.42)
						ent->model = H2FX_ModForName ("models/splnter2.mdl");
					else if (final < 0.56)
						ent->model = H2FX_ModForName ("models/leafchk2.mdl");
					else if (final < 0.70)
						ent->model = H2FX_ModForName ("models/splnter3.mdl");
					else if (final < 0.84)
						ent->model = H2FX_ModForName ("models/leafchk3.mdl");
					else
						ent->model = H2FX_ModForName ("models/splnter4.mdl");
				}
				else if (cl_Effects[idx].ef.Chunk.type == THINGTYPE_WOOD_METAL)
				{
					if (final < 0.125)
						ent->model = H2FX_ModForName ("models/splnter1.mdl");
					else if (final < 0.25)
						ent->model = H2FX_ModForName ("models/metlchk1.mdl");
					else if (final < 0.375)
						ent->model = H2FX_ModForName ("models/splnter2.mdl");
					else if (final < 0.50)
						ent->model = H2FX_ModForName ("models/metlchk2.mdl");
					else if (final < 0.625)
						ent->model = H2FX_ModForName ("models/splnter3.mdl");
					else if (final < 0.75)
						ent->model = H2FX_ModForName ("models/metlchk3.mdl");
					else if (final < 0.875)
						ent->model = H2FX_ModForName ("models/splnter4.mdl");
					else
						ent->model = H2FX_ModForName ("models/metlchk4.mdl");
				}
				else if (cl_Effects[idx].ef.Chunk.type == THINGTYPE_WOOD_STONE)
				{
					if (final < 0.125)
						ent->model = H2FX_ModForName ("models/splnter1.mdl");
					else if (final < 0.25)
						ent->model = H2FX_ModForName ("models/schunk1.mdl");
					else if (final < 0.375)
						ent->model = H2FX_ModForName ("models/splnter2.mdl");
					else if (final < 0.50)
						ent->model = H2FX_ModForName ("models/schunk2.mdl");
					else if (final < 0.625)
						ent->model = H2FX_ModForName ("models/splnter3.mdl");
					else if (final < 0.75)
						ent->model = H2FX_ModForName ("models/schunk3.mdl");
					else if (final < 0.875)
						ent->model = H2FX_ModForName ("models/splnter4.mdl");
					else
						ent->model = H2FX_ModForName ("models/schunk4.mdl");
				}
				else if (cl_Effects[idx].ef.Chunk.type == THINGTYPE_METAL_STONE)
				{
					if (final < 0.125)
						ent->model = H2FX_ModForName ("models/metlchk1.mdl");
					else if (final < 0.25)
						ent->model = H2FX_ModForName ("models/schunk1.mdl");
					else if (final < 0.375)
						ent->model = H2FX_ModForName ("models/metlchk2.mdl");
					else if (final < 0.50)
						ent->model = H2FX_ModForName ("models/schunk2.mdl");
					else if (final < 0.625)
						ent->model = H2FX_ModForName ("models/metlchk3.mdl");
					else if (final < 0.75)
						ent->model = H2FX_ModForName ("models/schunk3.mdl");
					else if (final < 0.875)
						ent->model = H2FX_ModForName ("models/metlchk4.mdl");
					else
						ent->model = H2FX_ModForName ("models/schunk4.mdl");
				}
				else if (cl_Effects[idx].ef.Chunk.type == THINGTYPE_METAL_CLOTH)
				{
					if (final < 0.14)
						ent->model = H2FX_ModForName ("models/metlchk1.mdl");
					else if (final < 0.28)
						ent->model = H2FX_ModForName ("models/clthchk1.mdl");
					else if (final < 0.42)
						ent->model = H2FX_ModForName ("models/metlchk2.mdl");
					else if (final < 0.56)
						ent->model = H2FX_ModForName ("models/clthchk2.mdl");
					else if (final < 0.70)
						ent->model = H2FX_ModForName ("models/metlchk3.mdl");
					else if (final < 0.84)
						ent->model = H2FX_ModForName ("models/clthchk3.mdl");
					else
						ent->model = H2FX_ModForName ("models/metlchk4.mdl");
				}
				else if (cl_Effects[idx].ef.Chunk.type == THINGTYPE_ICE)
				{
					ent->model = H2FX_ModForName("models/shard.mdl");
					ent->skinnum = 0;
					ent->frame = rand() % 2;
					ent->drawflags |= DRF_TRANSLUCENT|MLS_ABSLIGHT;
					ent->abslight = 127;
				}
				else if (cl_Effects[idx].ef.Chunk.type == THINGTYPE_METEOR)
				{
					ent->model = H2FX_ModForName("models/tempmetr.mdl");
					ent->skinnum = 0;
				}
				else if (cl_Effects[idx].ef.Chunk.type == THINGTYPE_ACID)
				{	/* no spinning if possible... */
					ent->model = H2FX_ModForName("models/sucwp2p.mdl");
					ent->skinnum = 0;
				}
				else if (cl_Effects[idx].ef.Chunk.type == THINGTYPE_GREENFLESH)
				{	/* spider guts */
					if (final < 0.33)
						ent->model = H2FX_ModForName ("models/sflesh1.mdl");
					else if (final < 0.66)
						ent->model = H2FX_ModForName ("models/sflesh2.mdl");
					else
						ent->model = H2FX_ModForName ("models/sflesh3.mdl");

					ent->skinnum = 0;
				}
				else// if (cl_Effects[idx].ef.Chunk.type == THINGTYPE_GREYSTONE)
				{
					if (final < 0.25)
						ent->model = H2FX_ModForName ("models/schunk1.mdl");
					else if (final < 0.50)
						ent->model = H2FX_ModForName ("models/schunk2.mdl");
					else if (final < 0.75)
						ent->model = H2FX_ModForName ("models/schunk3.mdl");
					else
						ent->model = H2FX_ModForName ("models/schunk4.mdl");
					ent->skinnum = 0;
				}
			}
			else
			{
				ImmediateFree = true;
				break;
			}
		}
		if (ImmediateFree) {
			for (i = 0 ; i < cl_Effects[idx].ef.Chunk.numChunks ; i++) {
				if (cl_Effects[idx].ef.Chunk.entity_index[i] == -1)
					break;
				FreeEffectEntity(cl_Effects[idx].ef.Chunk.entity_index[i]);
			}
			break;
		}

		for (i = 0 ; i < 3 ; i++)
		{
			cl_Effects[idx].ef.Chunk.avel[i][0] = (rand() % 850) - 425;
			cl_Effects[idx].ef.Chunk.avel[i][1] = (rand() % 850) - 425;
			cl_Effects[idx].ef.Chunk.avel[i][2] = (rand() % 850) - 425;
		}

		break;

	default:
		Host_EndGame ("CLH2_ParseEffect: bad type %i", cl_Effects[idx].type);
		cl_Effects[idx].type = CE_NONE;
		return;
	}

	if (ImmediateFree)
	{
		cl_Effects[idx].type = CE_NONE;
	}
}

static void CL_EndEffect (void)
{
	int		idx;

	idx = MSG_ReadByte();

	CL_FreeEffect(idx);
}

/*an effect's sprite or model goes into the scene*/
static void CL_LinkEntity (h2entity_t *e)
{
	entity_t ent;
	if (!e->model)
		return;

	V_ClearEntity(&ent);
	ent.model = e->model;
	VectorCopy(e->origin, ent.origin);
	VectorCopy(e->origin, ent.oldorigin);
	VectorCopy(e->angles, ent.angles);
	ent.framestate.g[FS_REG].frame[0] = e->frame;
	ent.framestate.g[FS_REG].frame[1] = e->frame;
	ent.framestate.g[FS_REG].lerpweight[0] = 1;
	ent.framestate.g[FS_REG].frametime[0] = ent.framestate.g[FS_REG].frametime[1] = cl.time;
	ent.skinnum = e->skinnum;
	ent.scale = (e->scale && e->scale != 100) ? e->scale / 100.0 : 1;
	ent.drawflags = e->drawflags;
	ent.abslight = e->abslight;
	ent.playerindex = -1;
	ent.topcolour = TOP_DEFAULT;
	ent.bottomcolour = BOTTOM_DEFAULT;
	ent.shaderRGBAf[0] = ent.shaderRGBAf[1] = ent.shaderRGBAf[2] = ent.shaderRGBAf[3] = 1;
	ent.glowmod[0] = ent.glowmod[1] = ent.glowmod[2] = 1;
	ent.flags = RF_NOSHADOW;
	V_AddEntity(&ent);
}

static void CL_UpdateEffects (void)
{
	int		i;
	int		idx, cur_frame;
	vec3_t		mymin, mymax;
	float		frametime;
	vec3_t		org, org2, alldir, snow_org;
	int		x_dir, y_dir;
	h2entity_t	*ent;
	float		distance, smoketime;

	frametime = h2_frametime;
	if (!frametime)
		return;

	for (idx = 0 ; idx < MAX_H2EFFECTS ; idx++)
	{
		if (!cl_Effects[idx].type)
			continue;

		switch (cl_Effects[idx].type)
		{
		case CE_RAIN:
			org[0] = cl_Effects[idx].ef.Rain.min_org[0];
			org[1] = cl_Effects[idx].ef.Rain.min_org[1];
			org[2] = cl_Effects[idx].ef.Rain.max_org[2];

			org2[0] = cl_Effects[idx].ef.Rain.e_size[0];
			org2[1] = cl_Effects[idx].ef.Rain.e_size[1];
			org2[2] = cl_Effects[idx].ef.Rain.e_size[2];

			x_dir = cl_Effects[idx].ef.Rain.dir[0];
			y_dir = cl_Effects[idx].ef.Rain.dir[1];

			cl_Effects[idx].ef.Rain.next_time += frametime;
			if (cl_Effects[idx].ef.Rain.next_time >= cl_Effects[idx].ef.Rain.wait)
			{
				R_RainEffect(org, org2, x_dir, y_dir, cl_Effects[idx].ef.Rain.color,
								cl_Effects[idx].ef.Rain.count);
				cl_Effects[idx].ef.Rain.next_time = 0;
			}
			break;

		case CE_SNOW:
			VectorCopy(cl_Effects[idx].ef.Rain.min_org, org);
			VectorCopy(cl_Effects[idx].ef.Rain.max_org, org2);
			VectorCopy(cl_Effects[idx].ef.Rain.dir, alldir);

			VectorAdd(org, org2, snow_org);

			snow_org[0] *= 0.5;
			snow_org[1] *= 0.5;
			snow_org[2] *= 0.5;

			snow_org[2] = h2_vieworg[2];

			VectorSubtract(snow_org, h2_vieworg, snow_org);

			distance = VectorNormalize(snow_org);

			cl_Effects[idx].ef.Rain.next_time += frametime;
			/* jfm:  fixme, check distance to player first */
			if (cl_Effects[idx].ef.Rain.next_time >= 0.10 && distance < 1024)
			{
				R_SnowEffect(org, org2, cl_Effects[idx].ef.Rain.flags, alldir,
								cl_Effects[idx].ef.Rain.count);

				cl_Effects[idx].ef.Rain.next_time = 0;
			}
			break;

		case CE_FOUNTAIN:
			mymin[0] = (-3 * cl_Effects[idx].ef.Fountain.h2_vright[0] * cl_Effects[idx].ef.Fountain.movedir[0]) +
				   (-3 * cl_Effects[idx].ef.Fountain.vforward[0] * cl_Effects[idx].ef.Fountain.movedir[1]) +
				   (2 * cl_Effects[idx].ef.Fountain.h2_vup[0] * cl_Effects[idx].ef.Fountain.movedir[2]);
			mymin[1] = (-3 * cl_Effects[idx].ef.Fountain.h2_vright[1] * cl_Effects[idx].ef.Fountain.movedir[0]) +
				   (-3 * cl_Effects[idx].ef.Fountain.vforward[1] * cl_Effects[idx].ef.Fountain.movedir[1]) +
				   (2 * cl_Effects[idx].ef.Fountain.h2_vup[1] * cl_Effects[idx].ef.Fountain.movedir[2]);
			mymin[2] = (-3 * cl_Effects[idx].ef.Fountain.h2_vright[2] * cl_Effects[idx].ef.Fountain.movedir[0]) +
				   (-3 * cl_Effects[idx].ef.Fountain.vforward[2] * cl_Effects[idx].ef.Fountain.movedir[1]) +
				   (2 * cl_Effects[idx].ef.Fountain.h2_vup[2] * cl_Effects[idx].ef.Fountain.movedir[2]);
			mymin[0] *= 15;
			mymin[1] *= 15;
			mymin[2] *= 15;

			mymax[0] = (3 * cl_Effects[idx].ef.Fountain.h2_vright[0] * cl_Effects[idx].ef.Fountain.movedir[0]) +
				   (3 * cl_Effects[idx].ef.Fountain.vforward[0] * cl_Effects[idx].ef.Fountain.movedir[1]) +
				   (10 * cl_Effects[idx].ef.Fountain.h2_vup[0] * cl_Effects[idx].ef.Fountain.movedir[2]);
			mymax[1] = (3 * cl_Effects[idx].ef.Fountain.h2_vright[1] * cl_Effects[idx].ef.Fountain.movedir[0]) +
				   (3 * cl_Effects[idx].ef.Fountain.vforward[1] * cl_Effects[idx].ef.Fountain.movedir[1]) +
				   (10 * cl_Effects[idx].ef.Fountain.h2_vup[1] * cl_Effects[idx].ef.Fountain.movedir[2]);
			mymax[2] = (3 * cl_Effects[idx].ef.Fountain.h2_vright[2] * cl_Effects[idx].ef.Fountain.movedir[0]) +
				   (3 * cl_Effects[idx].ef.Fountain.vforward[2] * cl_Effects[idx].ef.Fountain.movedir[1]) +
				   (10 * cl_Effects[idx].ef.Fountain.h2_vup[2] * cl_Effects[idx].ef.Fountain.movedir[2]);
			mymax[0] *= 15;
			mymax[1] *= 15;
			mymax[2] *= 15;

			if (h2_emitframe)
			R_RunParticleEffect2 (cl_Effects[idx].ef.Fountain.pos, mymin, mymax,
						cl_Effects[idx].ef.Fountain.color, pt_fastgrav,
						cl_Effects[idx].ef.Fountain.cnt);
			break;

		case CE_QUAKE:
			if (h2_emitframe)
			R_RunQuakeEffect (cl_Effects[idx].ef.Quake.origin,
						cl_Effects[idx].ef.Quake.radius);
			break;

		case CE_WHITE_SMOKE:
		case CE_GREEN_SMOKE:
		case CE_GREY_SMOKE:
		case CE_RED_SMOKE:
		case CE_SLOW_WHITE_SMOKE:
		case CE_TELESMK1:
		case CE_TELESMK2:
		case CE_GHOST:
		case CE_REDCLOUD:
		case CE_FLAMESTREAM:
		case CE_ACID_MUZZFL:
		case CE_FLAMEWALL:
		case CE_FLAMEWALL2:
		case CE_ONFIRE:
			cl_Effects[idx].ef.Smoke.time_amount += frametime;
			ent = &EffectEntities[cl_Effects[idx].ef.Smoke.entity_index];

			smoketime = cl_Effects[idx].ef.Smoke.framelength;
			if (!smoketime)
				smoketime = HX_FRAME_TIME;

			ent->origin[0] += (frametime/smoketime) * cl_Effects[idx].ef.Smoke.velocity[0];
			ent->origin[1] += (frametime/smoketime) * cl_Effects[idx].ef.Smoke.velocity[1];
			ent->origin[2] += (frametime/smoketime) * cl_Effects[idx].ef.Smoke.velocity[2];

			while (cl_Effects[idx].ef.Smoke.time_amount >= smoketime)
			{
				ent->frame++;
				cl_Effects[idx].ef.Smoke.time_amount -= smoketime;
			}

			if (ent->frame >= H2FX_NumFrames(ent))
			{
				CL_FreeEffect(idx);
			}
			else
			{
				CL_LinkEntity(ent);
			}

			break;

		/* Just go through animation and then remove */
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
		case CE_BRN_BOUNCE:
		case CE_ACID_HIT:
		case CE_ACID_SPLAT:
		case CE_ACID_EXPL:
		case CE_LBALL_EXPL:
		case CE_FBOOM:
		case CE_BOMB:
		case CE_FIREWALL_SMALL:
		case CE_FIREWALL_MEDIUM:
		case CE_FIREWALL_LARGE:
			cl_Effects[idx].ef.Smoke.time_amount += frametime;
			ent = &EffectEntities[cl_Effects[idx].ef.Smoke.entity_index];

			if (cl_Effects[idx].type != CE_BG_CIRCLE_EXP)
			{
				while (cl_Effects[idx].ef.Smoke.time_amount >= HX_FRAME_TIME)
				{
					ent->frame++;
					cl_Effects[idx].ef.Smoke.time_amount -= HX_FRAME_TIME;
				}
			}
			else
			{
				while (cl_Effects[idx].ef.Smoke.time_amount >= HX_FRAME_TIME * 2)
				{
					ent->frame++;
					cl_Effects[idx].ef.Smoke.time_amount -= HX_FRAME_TIME * 2;
				}
			}
			if (ent->frame >= H2FX_NumFrames(ent))
			{
				CL_FreeEffect(idx);
			}
			else
			{
				CL_LinkEntity(ent);
			}
			break;

		case CE_LSHOCK:
			ent = &EffectEntities[cl_Effects[idx].ef.Smoke.entity_index];
			if (ent->skinnum == 0)
				ent->skinnum = 1;
			else if (ent->skinnum == 1)
				ent->skinnum = 0;
			ent->scale -= 10;
			if (ent->scale <= 10)
			{
				CL_FreeEffect(idx);
			}
			else
			{
				CL_LinkEntity(ent);
			}
			break;

		/* Go forward then backward through animation then remove */
		case CE_WHITE_FLASH:
		case CE_BLUE_FLASH:
		case CE_SM_BLUE_FLASH:
		case CE_RED_FLASH:
			cl_Effects[idx].ef.Flash.time_amount += frametime;
			ent = &EffectEntities[cl_Effects[idx].ef.Flash.entity_index];

			while (cl_Effects[idx].ef.Flash.time_amount >= HX_FRAME_TIME)
			{
				if (!cl_Effects[idx].ef.Flash.reverse)
				{
					if (ent->frame >= H2FX_NumFrames(ent)-1)
					{
					/* Ran through forward animation */
						cl_Effects[idx].ef.Flash.reverse = 1;
						ent->frame--;
					}
					else
						ent->frame++;
				}
				else
				{
					ent->frame--;
				}

				cl_Effects[idx].ef.Flash.time_amount -= HX_FRAME_TIME;
			}

			if ((ent->frame <= 0) && (cl_Effects[idx].ef.Flash.reverse))
			{
				CL_FreeEffect(idx);
			}
			else
			{
				CL_LinkEntity(ent);
			}
			break;

		case CE_RIDER_DEATH: {
			float sinval, cosval;
			cl_Effects[idx].ef.RD.time_amount += frametime;
			if (cl_Effects[idx].ef.RD.time_amount >= 1)
			{
				cl_Effects[idx].ef.RD.stage++;
				cl_Effects[idx].ef.RD.time_amount -= 1;
			}

			VectorCopy(cl_Effects[idx].ef.RD.origin, org);
			q_sincosrad(cl_Effects[idx].ef.RD.time_amount * 2 * M_PI, &sinval, &cosval);
			org[0] += sinval * 30;
			org[1] += cosval * 30;

			if (cl_Effects[idx].ef.RD.stage <= 6)
			{
				RiderParticle(h2_emitframe?cl_Effects[idx].ef.RD.stage + 1:0, org);
			}
			else
			{
				/* To set the rider's origin point for the particles */
				RiderParticle(0, org);
				if (cl_Effects[idx].ef.RD.stage == 7)
				{
					int s;
					for (s = 0; s < MAX_SPLITS; s++)
					{
						cl.playerview[s].cshifts[CSHIFT_BONUS].destcolor[0] = 255;
						cl.playerview[s].cshifts[CSHIFT_BONUS].destcolor[1] = 255;
						cl.playerview[s].cshifts[CSHIFT_BONUS].destcolor[2] = 255;
						cl.playerview[s].cshifts[CSHIFT_BONUS].percent = 256;
					}
				}
				else if (cl_Effects[idx].ef.RD.stage > 13)
				{
					CL_FreeEffect(idx);
				}
			} }
			break;

		case CE_GRAVITYWELL: {
			float sinval, cosval;
			cl_Effects[idx].ef.RD.time_amount += frametime*2;
			if (cl_Effects[idx].ef.RD.time_amount >= 1)
				cl_Effects[idx].ef.RD.time_amount -= 1;

			VectorCopy(cl_Effects[idx].ef.RD.origin, org);
			q_sincosrad(cl_Effects[idx].ef.RD.time_amount * 2 * M_PI, &sinval, &cosval);
			org[0] += sinval * 30;
			org[1] += cosval * 30;

			if (cl_Effects[idx].ef.RD.lifetime < cl.time)
			{
				CL_FreeEffect(idx);
			}
			else
			{
				GravityWellParticle(h2_emitframe?rand() % 8:0, org, cl_Effects[idx].ef.RD.color);
			} }
			break;

		case CE_TELEPORTERPUFFS:
			cl_Effects[idx].ef.Teleporter.time_amount += frametime;
			smoketime = cl_Effects[idx].ef.Teleporter.framelength;

			ent = &EffectEntities[cl_Effects[idx].ef.Teleporter.entity_index[0]];
			while (cl_Effects[idx].ef.Teleporter.time_amount >= HX_FRAME_TIME)
			{
				ent->frame++;
				cl_Effects[idx].ef.Teleporter.time_amount -= HX_FRAME_TIME;
			}
			cur_frame = ent->frame;

			if (cur_frame >= H2FX_NumFrames(ent))
			{
				CL_FreeEffect(idx);
				break;
			}

			for (i = 0 ; i < 8 ; ++i)
			{
				ent = &EffectEntities[cl_Effects[idx].ef.Teleporter.entity_index[i]];

				ent->origin[0] += (frametime/smoketime) * cl_Effects[idx].ef.Teleporter.velocity[i][0];
				ent->origin[1] += (frametime/smoketime) * cl_Effects[idx].ef.Teleporter.velocity[i][1];
				ent->origin[2] += (frametime/smoketime) * cl_Effects[idx].ef.Teleporter.velocity[i][2];
				ent->frame = cur_frame;

				CL_LinkEntity(ent);
			}
			break;

		case CE_TELEPORTERBODY:
			cl_Effects[idx].ef.Teleporter.time_amount += frametime;
			smoketime = cl_Effects[idx].ef.Teleporter.framelength;

			ent = &EffectEntities[cl_Effects[idx].ef.Teleporter.entity_index[0]];
			while (cl_Effects[idx].ef.Teleporter.time_amount >= HX_FRAME_TIME)
			{
				ent->scale -= 15;
				cl_Effects[idx].ef.Teleporter.time_amount -= HX_FRAME_TIME;
			}

			ent = &EffectEntities[cl_Effects[idx].ef.Teleporter.entity_index[0]];
			ent->angles[1] += 45;

			if (ent->scale <= 10)
			{
				CL_FreeEffect(idx);
			}
			else
			{
				CL_LinkEntity(ent);
			}
			break;

		case CE_BONESHARD:
		case CE_BONESHRAPNEL:
			cl_Effects[idx].ef.Missile.time_amount += frametime;
			ent = &EffectEntities[cl_Effects[idx].ef.Missile.entity_index];

			ent->angles[0] += frametime * cl_Effects[idx].ef.Missile.avelocity[0];
			ent->angles[1] += frametime * cl_Effects[idx].ef.Missile.avelocity[1];
			ent->angles[2] += frametime * cl_Effects[idx].ef.Missile.avelocity[2];

			ent->origin[0] += frametime * cl_Effects[idx].ef.Missile.velocity[0];
			ent->origin[1] += frametime * cl_Effects[idx].ef.Missile.velocity[1];
			ent->origin[2] += frametime * cl_Effects[idx].ef.Missile.velocity[2];

			CL_LinkEntity(ent);
			break;

		case CE_CHUNK:
			cl_Effects[idx].ef.Chunk.time_amount -= frametime;
			if (cl_Effects[idx].ef.Chunk.time_amount < 0)
			{
				CL_FreeEffect(idx);
			}
			else
			{
				for (i = 0 ; i < cl_Effects[idx].ef.Chunk.numChunks ; i++)
				{
					vec3_t		oldorg;
					int		moving = 1;

					ent = &EffectEntities[cl_Effects[idx].ef.Chunk.entity_index[i]];

					VectorCopy(ent->origin, oldorg);

					ent->origin[0] += frametime * cl_Effects[idx].ef.Chunk.velocity[i][0];
					ent->origin[1] += frametime * cl_Effects[idx].ef.Chunk.velocity[i][1];
					ent->origin[2] += frametime * cl_Effects[idx].ef.Chunk.velocity[i][2];

					if (!H2FX_PointEmpty(ent->origin))
					{	/* bouncing prolly won't work... */
						VectorCopy(oldorg, ent->origin);

						cl_Effects[idx].ef.Chunk.velocity[i][0] = 0;
						cl_Effects[idx].ef.Chunk.velocity[i][1] = 0;
						cl_Effects[idx].ef.Chunk.velocity[i][2] = 0;

						moving = 0;
					}
					else
					{
						ent->angles[0] += frametime * cl_Effects[idx].ef.Chunk.avel[i%3][0];
						ent->angles[1] += frametime * cl_Effects[idx].ef.Chunk.avel[i%3][1];
						ent->angles[2] += frametime * cl_Effects[idx].ef.Chunk.avel[i%3][2];
					}

					if (cl_Effects[idx].ef.Chunk.time_amount < frametime * 3)
					{	/* chunk leaves in 3 frames */
						ent->scale *= .7;
					}

					CL_LinkEntity(ent);

					cl_Effects[idx].ef.Chunk.velocity[i][2] -= frametime * 500; /* apply gravity */

					switch (cl_Effects[idx].ef.Chunk.type)
					{
					case THINGTYPE_FLESH:
						if (moving)
							R_RocketTrail (oldorg, ent->origin, 17);
						break;
					case THINGTYPE_ICE:
						if (moving)
							R_RocketTrail (oldorg, ent->origin, h2rt_ice);
						break;
					case THINGTYPE_ACID:
						if (moving)
							R_RocketTrail (oldorg, ent->origin, h2rt_acidball);
						break;
					case THINGTYPE_METEOR:
						R_RocketTrail (oldorg, ent->origin, 1);
						break;
					case THINGTYPE_GREENFLESH:
						if (moving)
							R_RocketTrail (oldorg, ent->origin, h2rt_acidball);
						break;
					}
				}
			}
			break;
		}
	}
}

static int NewEffectEntity (void)
{
	h2entity_t	*ent;
	int		counter;

	if (EffectEntityCount == MAX_EFFECT_ENTITIES)
		return -1;

	for (counter = 0 ; counter < MAX_EFFECT_ENTITIES ; counter++)
	{
		if (!EntityUsed[counter])
			break;
	}

	EntityUsed[counter] = true;
	EffectEntityCount++;
	ent = &EffectEntities[counter];
	memset(ent, 0, sizeof(*ent));

	return counter;
}

static void FreeEffectEntity (int idx)
{
	if (idx < 0 || idx >= MAX_EFFECT_ENTITIES || !EntityUsed[idx])
		return;
	EntityUsed[idx] = false;
	EffectEntityCount--;
}

/*
=====================================================================
What the rest of the client calls
=====================================================================
*/

static void CLH2FX_Status_f(void)
{
	int idx, n = 0, counts[CE_ONFIRE+1] = {0};
	particle_t *p;
	for (idx = 0; idx < MAX_H2EFFECTS; idx++)
		if (cl_Effects[idx].type > 0 && cl_Effects[idx].type <= CE_ONFIRE)
			counts[cl_Effects[idx].type]++;
	for (p = active_particles; p; p = p->next)
		n++;
	Con_Printf("hexen2 effects: %s, %s\n", CLH2FX_Enabled()?"uHexen2's":"FTE's particle scripts", (cls.fteprotocolextensions2 & PEXT2_H2EFFECTS)?"from the server's list":"the server doesn't send them");
	for (idx = 1; idx <= CE_ONFIRE; idx++)
		if (counts[idx])
			Con_Printf("  effect type %i: %i\n", idx, counts[idx]);
	Con_Printf("  %i effect entities, %i/%i particles\n", EffectEntityCount, n, r_numparticles);
}

void CLH2FX_Init(void)
{
	Cvar_Register(&cl_h2effects, "Hexen II");
	Cvar_Register(&snow_flurry, "Hexen II");
	Cvar_Register(&snow_active, "Hexen II");
	Cmd_AddCommandD("h2fx_status", CLH2FX_Status_f, "Lists the Hexen II effects running, and the particles.");
	R_InitParticles();
	R_ClearParticles();
	CL_ClearEffects();
}

//Hexen II's effects, particles and trails are uHexen2's (else FTE's particle scripts do them)
qboolean CLH2FX_Enabled(void)
{
	extern qboolean sbar_hexen2;
	return cl_h2effects.ival && sbar_hexen2 && cls.protocol != CP_QUAKE2 && particles;
}

//the server sends uHexen2's effects (and FTE's particle-script versions too, which are then left be)
qboolean CLH2FX_Native(void)
{
	return (cls.fteprotocolextensions2 & PEXT2_H2EFFECTS) && CLH2FX_Enabled();
}

void CLH2FX_ClearState(void)
{
	if (particles)
		R_ClearParticles();
	CL_ClearEffects();
	h2_lasttime = 0;
	h2fx_particletris = -1;
}

//svc_start_effect, svc_end_effect: kept even while drawn by FTE's scripts instead, as a level's rain lasts
void CLH2FX_ParseStartEffect(qboolean hasframe)
{
	CLH2_ParseEffect(hasframe);
}
void CLH2FX_ParseEndEffect(void)
{
	CL_EndEffect();
}

//uHexen2's svc_particle_explosion: particleexplosion()
void CLH2FX_ParseParticleExplosion(void)
{
	vec3_t	org;
	short	color, radius, counter;

	org[0] = MSG_ReadCoord();
	org[1] = MSG_ReadCoord();
	org[2] = MSG_ReadCoord();
	color = MSG_ReadShort();
	radius = MSG_ReadShort();
	counter = MSG_ReadShort();

	if (CLH2FX_Enabled())
		R_ColoredParticleExplosion(org,color,radius,counter);
	else
		P_RunParticleEffect4(org, color, radius, 255, counter);	//as FTE's own message has them
}

//uHexen2's svc_raineffect: rain_go()
void CLH2FX_ParseRainEffect(void)
{
	vec3_t	org, e_size;
	short	color,count;
	int	x_dir, y_dir;

	org[0] = MSG_ReadCoord();
	org[1] = MSG_ReadCoord();
	org[2] = MSG_ReadCoord();
	e_size[0] = MSG_ReadCoord();
	e_size[1] = MSG_ReadCoord();
	e_size[2] = MSG_ReadCoord();
	x_dir = H2FX_ReadAngle();
	y_dir = H2FX_ReadAngle();
	color = MSG_ReadShort();
	count = MSG_ReadShort();

	if (CLH2FX_Enabled())
		R_RainEffect(org,e_size,x_dir,y_dir,color,count);
}

//uHexen2's svc_sound_update_pos: updatesoundpos()
void CLH2FX_ParseSoundPos(void)
{
	vec3_t	pos;
	int	ent, channel;

	ent = MSG_ReadShort() & 0xffff;
	channel = MSG_ReadByte();
	pos[0] = MSG_ReadCoord();
	pos[1] = MSG_ReadCoord();
	pos[2] = MSG_ReadCoord();

	S_UpdateSoundPos(ent, channel, pos);
}

//the particle builtins: svc_particle (blood), particle2, particle3 and particle4
void CLH2FX_RunParticleEffect(vec3_t org, vec3_t dir, int color, int count)
{
	R_RunParticleEffect(org, dir, color, count);
}
void CLH2FX_RunParticleEffect2(vec3_t org, vec3_t dmin, vec3_t dmax, int color, int effect, int count)
{
	R_RunParticleEffect2(org, dmin, dmax, color, effect, count);
}
void CLH2FX_RunParticleEffect3(vec3_t org, vec3_t box, int color, int effect, int count)
{
	R_RunParticleEffect3(org, box, color, effect, count);
}
void CLH2FX_RunParticleEffect4(vec3_t org, float radius, int color, int effect, int count)
{
	R_RunParticleEffect4(org, radius, color, effect, count);
}
void CLH2FX_ColoredParticleExplosion(vec3_t org, int color, int radius, int count)
{
	R_ColoredParticleExplosion(org, color, radius, count);
}

//temp entities
void CLH2FX_ParticleExplosion(vec3_t org)
{
	R_ParticleExplosion(org);
}
void CLH2FX_LavaSplash(vec3_t org)
{
	R_LavaSplash(org);
}
void CLH2FX_TeleportSplash(vec3_t org)
{
	R_TeleportSplash(org);
}
void CLH2FX_SunStaffTrail(vec3_t source, vec3_t dest)
{
	R_SunStaffTrail(source, dest);
}

//the dark field (an entity's EF_DARKFIELD)
void CLH2FX_DarkField(vec3_t org)
{
	if (h2_emitframe)
		R_DarkFieldParticles(org);
}

/*
uHexen2's CL_RelinkEntities: the trail (and light) a model's flags give it. False when it has none of those, for
FTE's own trails to do.
*/
qboolean CLH2FX_ModelTrail(int entnum, unsigned int modelflags, vec3_t oldorg, vec3_t origin)
{
	dlight_t *dl;

	if (modelflags & MF_GIB)
		R_RocketTrail (oldorg, origin, h2rt_blood);
	else if (modelflags & MF_ZOMGIB)
		R_RocketTrail (oldorg, origin, h2rt_slight_blood);
	else if (modelflags & MFH2_BLOODSHOT)
		R_RocketTrail (oldorg, origin, h2rt_bloodshot);
	else if (modelflags & MF_TRACER)
		R_RocketTrail (oldorg, origin, h2rt_tracer);
	else if (modelflags & MF_TRACER2)
		R_RocketTrail (oldorg, origin, h2rt_tracer2);
	else if (modelflags & (MF_ROCKET|MFH2_SPIDERBLOOD))	//FTE gives the spider's guts a flag of their own
		R_RocketTrail (oldorg, origin, h2rt_rocket_trail);
	else if (modelflags & MFH2_FIREBALL)
	{
		R_RocketTrail (oldorg, origin, h2rt_fireball);

		dl = CL_NewDlight(entnum, origin, 120 - (rand() % 20), 0.01, 1.6, 0.4, 0.4);
		dl->corona = 0;
	}
	else if (modelflags & MFH2_ACIDBALL)
	{
		R_RocketTrail (oldorg, origin, h2rt_acidball);

		dl = CL_NewDlight(entnum, origin, 120 - (rand() % 20), 0.01, 0.4, 1.6, 0.4);
		dl->corona = 0;
	}
	else if (modelflags & MFH2_ICE)
		R_RocketTrail (oldorg, origin, h2rt_ice);
	else if (modelflags & MFH2_SPIT)
	{
		R_RocketTrail (oldorg, origin, h2rt_spit);

		//uHexen2: a negative radius, a light that darkens
		dl = CL_NewDlight(entnum, origin, 120 + (rand() % 20), 0.05, -1, -1, -1);
		dl->flags &= ~(LFLAG_NORMALMODE|LFLAG_REALTIMEMODE|LFLAG_FLASHBLEND);
		dl->flags |= LFLAG_LIGHTMAP;
		dl->corona = 0;
	}
	else if (modelflags & MFH2_SPELL)
		R_RocketTrail (oldorg, origin, h2rt_spell);
	else if (modelflags & MF_GRENADE)
		R_RocketTrail (oldorg, origin, h2rt_smoke);
	else if (modelflags & MF_TRACER3)
		R_RocketTrail (oldorg, origin, h2rt_voor_trail);
	else if (modelflags & MFH2_VORP_MISSILE)
		R_RocketTrail (oldorg, origin, h2rt_vorpal);
	else if (modelflags & MFH2_SET_STAFF)
		R_RocketTrail (oldorg, origin, h2rt_setstaff);
	else if (modelflags & MFH2_MAGICMISSILE)
	{
		if ((rand() & 3) < 1)
			R_RocketTrail (oldorg, origin, h2rt_magicmissile);
	}
	else if (modelflags & MFH2_BONESHARD)
		R_RocketTrail (oldorg, origin, h2rt_boneshard);
	else if (modelflags & MFH2_SCARAB)
		R_RocketTrail (oldorg, origin, h2rt_scarab);
	else
		return false;
	return true;
}

/*
Once a frame, after the entities: the effects and particles move on (as uHexen2's R_UpdateParticles and
CL_UpdateEffects), the effects' sprites and models go into the scene, and the particles are drawn.
*/
void CLH2FX_AddEntities(void)
{
	playerview_t *pv = &cl.playerview[0];

	if (!CLH2FX_Enabled())
		return;

	//the view the particles face, and the snow is near
	AngleVectors(pv->simangles, h2_vpn, h2_vright, h2_vup);
	VectorCopy(pv->simorg, h2_vieworg);

	if (cl.time < h2_lasttime || !h2_lasttime)
		h2_lasttime = cl.time;	//a new map
	h2_frametime = cl.time - h2_lasttime;
	if (h2_frametime > 1)
		h2_frametime = 1;	//from a pause in time, rather than a frame
	h2_lasttime = cl.time;

	h2_emitaccum += h2_frametime;
	h2_emitframe = h2_emitaccum >= H2FX_EMITTIME;
	if (h2_emitframe)
	{
		h2_emitaccum -= H2FX_EMITTIME;
		if (h2_emitaccum > H2FX_EMITTIME)
			h2_emitaccum = 0;	//slower than that: every frame
	}

	if (h2_frametime > 0)
	{
		R_UpdateParticles();
		CL_UpdateEffects();
	}
	else
	{	//no time passed (paused): they still need to be seen
		int idx, i;
		for (idx = 0; idx < MAX_H2EFFECTS; idx++)
		{
			switch(cl_Effects[idx].type)
			{
			case CE_NONE:
			case CE_RAIN:
			case CE_SNOW:
			case CE_FOUNTAIN:
			case CE_QUAKE:
			case CE_RIDER_DEATH:
			case CE_GRAVITYWELL:
				break;
			case CE_WHITE_FLASH:
			case CE_BLUE_FLASH:
			case CE_SM_BLUE_FLASH:
			case CE_RED_FLASH:
				CL_LinkEntity(&EffectEntities[cl_Effects[idx].ef.Flash.entity_index]);
				break;
			case CE_TELEPORTERPUFFS:
				for (i = 0; i < 8; i++)
					CL_LinkEntity(&EffectEntities[cl_Effects[idx].ef.Teleporter.entity_index[i]]);
				break;
			case CE_TELEPORTERBODY:
				CL_LinkEntity(&EffectEntities[cl_Effects[idx].ef.Teleporter.entity_index[0]]);
				break;
			case CE_BONESHARD:
			case CE_BONESHRAPNEL:
				CL_LinkEntity(&EffectEntities[cl_Effects[idx].ef.Missile.entity_index]);
				break;
			case CE_CHUNK:
				for (i = 0; i < cl_Effects[idx].ef.Chunk.numChunks; i++)
					CL_LinkEntity(&EffectEntities[cl_Effects[idx].ef.Chunk.entity_index[i]]);
				break;
			default:	//smoke, sprites
				CL_LinkEntity(&EffectEntities[cl_Effects[idx].ef.Smoke.entity_index]);
				break;
			}
		}
	}

	R_DrawParticles();
}
#endif
