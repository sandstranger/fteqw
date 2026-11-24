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
int			CLDKF_RealMilliseconds(void);				//real time (the menus')
void		*CLDKF_GLGetProcAddress(const char *name);	//FTE's GL's, for the video menu's driver info (null if none)

//its menus' (menu/, cldk_menu_sys.cpp)
const char	*CLDKF_CvarString(const char *name);		//"" if there's no such cvar
const char	*CLDKF_CvarCreate(const char *name, const char *value, int archive);	//its value, made if there wasn't one
const char	*CLDKF_KeyBinding(int dkkey);				//its key numbers (Quake II's): null if unbound
void		CLDKF_SetKeyBinding(int dkkey, const char *binding);
const char	*CLDKF_KeyName(int dkkey);
int			CLDKF_KeyDown(int dkkey);
const char	*CLDKF_WriteDir(void);						//where the game's files are written (its gamedir's)
void		CLDKF_CloseMenu(void);
void		CLDKF_StopAllSounds(void);
void		CLDKF_MousePos(int *x, int *y);				//in the HUD's units
int			CLDKF_InGame(void);							//connected, a level up
void		*CLDKF_TagMalloc(size_t size, int tag);		//FTE's zone's, as the game's are (svdk_game.cpp)
void		CLDKF_TagFree(void *mem);
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
void		CLDKF_PlayStream(const char *name, int chan, float volume, int loop);	//a sound of its own channel, everywhere
void		CLDKF_StopStream(int chan);

//its scene
void		*CLDKF_RegisterModel(const char *name);
void		*CLDKF_ModelForIndex(int modelindex);
const char	*CLDKF_ModelPath(void *model);				//its file's
void		CLDKF_AddEntity(void *model, const float *origin, const float *oldorigin, const float *angles, int frame, int oldframe,
				float backlerp, int skinnum, void *skin, float alpha, float scale, const float *rgb, unsigned int dkrenderfx);	//skin: CLDKF_RegisterSkin's
void		CLDKF_AddLight(const float *origin, float radius, float r, float g, float b);
void		CLDKF_AddTriangles(void *shader, int numverts, const float *xyz, const float *st, const float *rgba);
void		*CLDKF_RegisterParticleShader(void);
void		*CLDKF_RegisterBeamShader(const char *texture);	//blended, vertex colours, both sides; null: untextured
void		*CLDKF_RegisterDecalShader(const char *texture);	//as a beam's, offset onto the surface under it
void		*CLDKF_RegisterGlowShader(void);			//untextured, added to the scene (its spotlights' volumes)
int			CLDKF_PlaneNormal(int planeindex, float *normal);	//the map's; false if there's no such plane
void		CLDKF_LightPoint(const float *point, float *rgb);	//the map's light there (0 to 1ish)
void		*CLDKF_RegisterSound(const char *name);
void		CLDKF_StartSound(void *sfx, const float *origin, int entnum, int channel, float volume, float attenuation, float timeofs);
float		CLDKF_Trace(const float *start, const float *end, int passent, int mask, float *endpos, float *normal, int *entnum, int *contents);
int			CLDKF_PointContents(const float *point);
//the world and the frame's solid entities but passent's (its crosshair's aim); entnum: -1 nothing, 0 the world
float		CLDKF_TraceShot(const float *start, const float *mins, const float *maxs, const float *end, int passent, float *endpos, int *entnum, int *startsolid);
void		CLDKF_ViewFov(float *fov_x, float *fov_y);	//the view's
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

//models on the HUD (its inventory's, its boosts'), as its renderer drew them: in an orthographic view of the area
//(x, y, w, h, the HUD's units), each where it is in it, a unit of it scale of the HUD's
typedef struct
{
	void	*model;
	float	x, y;			//in the area
	float	angles[3];		//its renderer's (x right, y down, z out of the screen)
	int		frame, oldframe;
	float	backlerp, alpha, scale;
} cldk_icon_t;
void		CLDKF_DrawIcons(float x, float y, float w, float h, int num, const cldk_icon_t *icons);
//a view of no world (its menus' buttons: its re.RenderFrame's there), over the area, in perspective
typedef struct
{
	void	*model, *skin;		//skin: CLDKF_RegisterSkin's (null: the model's own)
	float	origin[3], angles[3];
	int		frame, oldframe;
	float	backlerp, alpha, scale;
	float	rgb[3];
	int		fullbright;
} cldk_rentity_t;
void		CLDKF_RenderView(float x, float y, float w, float h, float fov_x, float fov_y, const float *vieworg,
				const float *viewangles, int num, const cldk_rentity_t *ents);
void		*CLDKF_RegisterSkin(const char *image);		//an image as a model's skin (but its SRF_STATICSKIN surfaces')
const char	*CLDKF_ImageName(int index);				//the server's image (configstring), or null
void		CLDKF_SetFog(int active, float r, float g, float b, float start, float end);	//linear, as its renderer's
const char	*CLDKF_PlayerSkinString(int player);		//its CS_PLAYERSKINS one ("name\model\skin\character"), or null

//its player movement (the game's own pmove, dk_pmove.cpp), for FTE's client's prediction (cl_pred.c)
typedef struct
{
	float			fraction, endpos[3], normal[3], dist;
	int				allsolid, startsolid, contents, surfaceflags;
	int				hit;		//an entity or the world
} cldk_trace_t;
typedef struct
{
	//its pmove_state_t (in and out)
	int				pm_type;	//Quake II's (FTE's player state's)
	short			origin[3], velocity[3];
	unsigned short	pm_flags;
	unsigned char	pm_time;
	short			gravity, delta_angles[3];
	//the command
	unsigned char	msec, buttons, impulse, lightlevel;
	short			angles[3], forwardmove, sidemove, upmove;
	//out
	float			viewangles[3], viewheight;
	int				onground;
	//the world
	void			(*tracebox)(const float *start, const float *mins, const float *maxs, const float *end, cldk_trace_t *out);
	int				(*pointcontents)(const float *point);	//Quake II's contents (its are those)
} cldk_pmove_t;

//the game's client code, for FTE's
void		CLDK_ParseMessage(const unsigned char *data, int length);	//an svcq2dk_message's
void		CLDK_DrawHUD(void);
void		CLDK_AddScene(void);						//its effects, each frame, as entities are added
void		CLDK_Reset(void);							//new connection, new level
void		CLDK_Pmove(cldk_pmove_t *pm);				//its pmove, a command's
void		CLDK_MenuOpen(int button);					//its menus (menu/), at a main button (its dk_main_button; -1: as it was)
int			CLDK_MenuActive(void);
void		CLDK_MenuDraw(void);						//(over CLDKF_DrawPic's area: the whole screen, its 640x480)
void		CLDK_MenuKey(int dkkey, int down);
void		CLDK_MenuMouse(int x, int y);				//its HUD units, where the pointer is

#ifdef __cplusplus
}
#endif
#endif
