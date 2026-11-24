/*
What Daikatana's client code (daikatana/client) has of FTE's client (client/cldk_fte.c), and what FTE's client calls
of it. Plain C: the game's headers and FTE's don't mix.
*/
#ifndef CLDK_FTE_H
#define CLDK_FTE_H

#ifdef __cplusplus
extern "C" {
#endif

//FTE's, for the game's client code
void		*CLDKF_RegisterPic(const char *name, int *width, int *height);	//null if there's no such image
void		CLDKF_DrawPic(void *pic, float x, float y, float w, float h, float s1, float t1, float s2, float t2, const float *rgba);
void		CLDKF_DrawFill(float x, float y, float w, float h, const float *rgba);
void		CLDKF_VidSize(int *width, int *height);		//the virtual screen the HUD's drawn on
int			CLDKF_Milliseconds(void);					//the client's time
float		CLDKF_CvarValue(const char *name, const char *defaultvalue);
void		CLDKF_CvarSet(const char *name, const char *value);
void		CLDKF_LocalSound(const char *name);
void		CLDKF_AddCommand(const char *text);			//to the console (forwarded to the server, as the game's are)
void		CLDKF_Print(const char *text);				//to the console
int			CLDKF_Stat(int i);							//Quake II's (short) stats
int			CLDKF_RDFlags(void);
const char	*CLDKF_MapName(void);
int			CLDKF_ModelIndex(const char *name);			//0 if the server hasn't precached it
const char	*CLDKF_ModelName(int modelindex);			//"" if there's none
void		*CLDKF_LoadFile(const char *name, int *length);
void		CLDKF_FreeFile(void *buf);
void		CLDKF_Music(const char *name, int loop);		//null: stop

//its scene
void		*CLDKF_RegisterModel(const char *name);
void		*CLDKF_ModelForIndex(int modelindex);
void		CLDKF_AddEntity(void *model, const float *origin, const float *oldorigin, const float *angles, int frame, int oldframe,
				float backlerp, int skinnum, float alpha, float scale, const float *rgb, unsigned int dkrenderfx);
void		CLDKF_AddLight(const float *origin, float radius, float r, float g, float b);
void		CLDKF_AddTriangles(void *shader, int numverts, const float *xyz, const float *st, const float *rgba);
void		*CLDKF_RegisterParticleShader(void);
void		*CLDKF_RegisterSound(const char *name);
void		CLDKF_StartSound(void *sfx, const float *origin, int entnum, int channel, float volume, float attenuation, float timeofs);
float		CLDKF_Trace(const float *start, const float *end, int passent, int mask, float *endpos, float *normal, int *entnum, int *contents);
int			CLDKF_PointContents(const float *point);
int			CLDKF_EntityState(int entnum, float *origin, float *angles, float *lerporigin, int *modelindex, int *frame);
//the last frame's entities: what Quake II's entity state has of Daikatana's (its server's as they are)
typedef struct
{
	float			origin[3], angles[3], old_origin[3];
	int				modelindex, frame, skinnum, sound, event;
	unsigned int	effects, renderfx;
} cldk_estate_t;
int			CLDKF_NumFrameEntities(void);
int			CLDKF_FrameEntity(int i, cldk_estate_t *current, cldk_estate_t *prev, int *isnew);	//its number, 0 if none
void		CLDKF_PlayerVelocity(float *velocity);
void		CLDKF_View(float *origin, float *angles);
int			CLDKF_ServerFrame(void);
float		CLDKF_LerpFrac(void);
float		CLDKF_FrameTime(void);
int			CLDKF_PlayerNum(void);

//the game's client code, for FTE's
void		CLDK_ParseMessage(const unsigned char *data, int length);	//an svcq2dk_message's
void		CLDK_DrawHUD(void);
void		CLDK_AddScene(void);						//its effects, each frame, as entities are added
void		CLDK_Reset(void);							//new connection, new level

#ifdef __cplusplus
}
#endif
#endif
