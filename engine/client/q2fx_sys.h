/*
The boundary between yquake2's client effects (engine/client/q2fx: particles, trails, temp entities,
explosions, beams, lasers, muzzle flashes, dlights -- id's code as yquake2 keeps it) and FTE (clq2_fx.c).

The effects code is compiled with yquake2's own types (entity_t, entity_state_t, qboolean...) and FTE's
code with FTE's, and the two sets cannot meet in one file. Everything that crosses goes through here,
in plain C types only.
*/
#ifndef Q2FX_SYS_H
#define Q2FX_SYS_H

/* --- FTE calls the effects (q2fx/q2fx_bridge.c) --- */

void Q2FX_ClearState(void);		/* map change / disconnect: particles, dlights, temp entities, sustains */
void Q2FX_RegisterMedia(void);	/* the models and sounds the effects use (after the map loads) */

/* the client state the effects read, set before they run: times in milliseconds as yquake2 counts them */
void Q2FX_SetFrame(int time_ms, int servertime_ms, int serverframe, int playernum, float lerpfrac,
		const float *vieworg, const float *forward, const float *right, const float *up,
		const float *gunoffset, const float *oldgunoffset, int hand,
		float laseralpha, int limitsparksounds, int footsteps);

void Q2FX_ParseTEnt(void);			/* svc_temp_entity */
void Q2FX_ParseMuzzleFlash(void);	/* svc_muzzleflash */
void Q2FX_ParseMuzzleFlash2(void);	/* svc_muzzleflash2 */
void Q2FX_EntityEvent(int number, int event, const float *origin);	/* footsteps, teleports, item respawns */
void Q2FX_TeleporterParticles(const float *origin);				/* EF_TELEPORTER */
void Q2FX_RereleaseParticles(int effect, const float *origin);
void Q2FXSYS_MuzzleFlash2Ex(int entnum, int flash_number);		/* FTE's: the rerelease's MZ2_ past the original's 210 */	/* the rerelease's: 0 EF_TELEPORTER2, 1 EF_HOLOGRAM, 2 EF_BARREL_EXPLODING */

/* an entity (re)appeared, or teleported: its trails start from origin */
void Q2FX_EntityReset(int entnum, const float *origin);
/* yquake2's automatic particle trails and their lights for one entity, from where it was last frame
   to origin (lerped). frame is the entity's frame (the bfg's light ramp). */
void Q2FX_EntityEffects(int entnum, int effects, int frame, const float *origin);

/* every frame, once the entities are in: temp entities, explosions, beams, lasers, sustains,
   particles and dlights */
void Q2FX_AddEntities(void);

/* --- the effects call FTE (clq2_fx.c) --- */

void *Q2FXSYS_RegisterModel(const char *name);
void *Q2FXSYS_RegisterSound(const char *name);
void Q2FXSYS_PrecachePic(const char *name);
void Q2FXSYS_StartSound(const float *origin, int entnum, int channel, void *sfx, float volume, float attenuation, float timeofs);

/* a refresh entity, as yquake2's entity_t describes it. angles in degrees, backlerp 1 = oldframe.
   For Q2 RF_BEAM: origin/oldorigin are the ends, frame the diameter, skinnum the palette colours. */
void Q2FXSYS_AddEntity(void *model, const float *origin, const float *angles, const float *oldorigin,
		int frame, int oldframe, float backlerp, int skinnum, float alpha, int flags);
void Q2FXSYS_AddParticle(const float *org, unsigned int color, float alpha);	/* color: palette index */
void Q2FXSYS_AddLight(const float *org, float intensity, float r, float g, float b);	/* yquake2's scale: 1 = full */

int Q2FXSYS_ReadByte(void);
int Q2FXSYS_ReadShort(void);
int Q2FXSYS_ReadLong(void);
void Q2FXSYS_ReadPos(float *pos);
void Q2FXSYS_ReadDir(float *dir);

/* where a client entity is now (0 when it is not known) */
int Q2FXSYS_GetEntity(int entnum, float *origin, float *angles);

void Q2FXSYS_Print(int developer, const char *text);
void Q2FXSYS_Error(const char *text);

#endif
