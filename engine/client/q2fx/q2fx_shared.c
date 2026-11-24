/*
The few functions of yquake2's common/shared/shared.c the effects use, copied verbatim (the whole file
brings in OS headers FTE's other builds would not like). q2fx_rename.h gives them q2fx_ names.

Copyright (C) 1997-2001 Id Software, Inc. -- GPL v2 or later, as yquake2.
*/
#include "q2fx.h"

vec3_t vec3_origin = {0, 0, 0};

void
AngleVectors(const vec3_t angles, vec3_t forward, vec3_t right, vec3_t up)
{
	float angle;
	static float sr, sp, sy, cr, cp, cy;

	angle = angles[YAW] * (M_PI * 2 / 360);
	sy = (float)sin(angle);
	cy = (float)cos(angle);
	angle = angles[PITCH] * (M_PI * 2 / 360);
	sp = (float)sin(angle);
	cp = (float)cos(angle);
	angle = angles[ROLL] * (M_PI * 2 / 360);
	sr = (float)sin(angle);
	cr = (float)cos(angle);

	if (forward)
	{
		forward[0] = cp * cy;
		forward[1] = cp * sy;
		forward[2] = -sp;
	}

	if (right)
	{
		right[0] = (-1 * sr * sp * cy + - 1 * cr * -sy);
		right[1] = (-1 * sr * sp * sy + - 1 * cr * cy);
		right[2] = -1 * sr * cp;
	}

	if (up)
	{
		up[0] = (cr * sp * cy + - sr * -sy);
		up[1] = (cr * sp * sy + - sr * cy);
		up[2] = cr * cp;
	}
}

void
AngleVectors2(const vec3_t value1, vec3_t angles)
{
	float yaw, pitch;

	if ((value1[1] == 0) && (value1[0] == 0))
	{
		yaw = 0;

		if (value1[2] > 0)
		{
			pitch = 90;
		}

		else
		{
			pitch = 270;
		}
	}
	else
	{
		float forward;

		if (value1[0])
		{
			yaw = ((float)atan2(value1[1], value1[0]) * 180 / M_PI);
		}

		else if (value1[1] > 0)
		{
			yaw = 90;
		}

		else
		{
			yaw = 270;
		}

		if (yaw < 0)
		{
			yaw += 360;
		}

		forward = (float)sqrt(value1[0] * value1[0] + value1[1] * value1[1]);
		pitch = ((float)atan2(value1[2], forward) * 180 / M_PI);

		if (pitch < 0)
		{
			pitch += 360;
		}
	}

	angles[PITCH] = -pitch;
	angles[YAW] = yaw;
	angles[ROLL] = 0;
}

void
VectorMA(const vec3_t veca, float scale, const vec3_t vecb, vec3_t vecc)
{
	vecc[0] = veca[0] + scale * vecb[0];
	vecc[1] = veca[1] + scale * vecb[1];
	vecc[2] = veca[2] + scale * vecb[2];
}

vec_t
VectorLength(const vec3_t v)
{
	return sqrtf((v[0] * v[0]) +
               (v[1] * v[1]) +
	       (v[2] * v[2]));
}

void
CrossProduct(const vec3_t v1, const vec3_t v2, vec3_t cross)
{
	cross[0] = v1[1] * v2[2] - v1[2] * v2[1];
	cross[1] = v1[2] * v2[0] - v1[0] * v2[2];
	cross[2] = v1[0] * v2[1] - v1[1] * v2[0];
}

vec_t
VectorNormalize(vec3_t v)
{
	float length;

	length = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
	length = (float)sqrt(length);

	if (length)
	{
		float ilength;

		ilength = 1 / length;
		v[0] *= ilength;
		v[1] *= ilength;
		v[2] *= ilength;
	}

	return length;
}

void
VectorScale(const vec3_t in, vec_t scale, vec3_t out)
{
	out[0] = in[0] * scale;
	out[1] = in[1] * scale;
	out[2] = in[2] * scale;
}

void
Com_sprintf(char *dest, int size, const char *fmt, ...)
{
	int len;
	va_list argptr;

	va_start(argptr, fmt);
	len = vsnprintf(dest, size, fmt, argptr);
	va_end(argptr);

	if (len >= size)
	{
		Com_Printf("%s: overflow\n", __func__);
	}
}
