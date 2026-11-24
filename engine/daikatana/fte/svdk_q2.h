/*
FTE's side of svdk_game.cpp: Quake II's game API 3, as FTE's server has it (server/q2game.h, common/world.h,
client/client.h, common/protocol.h), in a namespace of its own: Daikatana's headers have the same names for
other things (edict_t, trace_t, cvar_t...). These are fixed layouts, the original game DLLs' ABI.
*/
#ifndef SVDK_Q2_H
#define SVDK_Q2_H

#if defined(_WIN32) && !defined(_WIN64)
#define FQ2_VARGS __cdecl
#else
#define FQ2_VARGS
#endif

namespace fq2
{
	typedef float vec3_t[3];
	typedef int qboolean;
	typedef unsigned char qbyte;

	enum {PM_NORMAL, PM_SPECTATOR, PM_DEAD, PM_GIB, PM_FREEZE};
	enum {SOLID_NOT, SOLID_TRIGGER, SOLID_BBOX, SOLID_BSP};
	enum {MULTICAST_ALL, MULTICAST_PHS, MULTICAST_PVS, MULTICAST_ALL_R, MULTICAST_PHS_R, MULTICAST_PVS_R};

	enum
	{
		Q2MAX_STATS = 32,
		Q2MAX_ENT_CLUSTERS = 16,
		Q2MAX_MODELS = 256,
		Q2MAX_SOUNDS = 256,
		Q2MAX_IMAGES = 256,
		Q2MAX_LIGHTSTYLES = 256,
		Q2MAX_ITEMS = 256,
		Q2MAX_CLIENTS = 256,
		Q2MAX_GENERAL = Q2MAX_CLIENTS*2,

		Q2CS_NAME = 0,
		Q2CS_CDTRACK = 1,
		Q2CS_SKY = 2,
		Q2CS_SKYAXIS = 3,
		Q2CS_SKYROTATE = 4,
		Q2CS_STATUSBAR = 5,
		Q2CS_AIRACCEL = 29,
		Q2CS_MAXCLIENTS = 30,
		Q2CS_MAPCHECKSUM = 31,
		Q2CS_MODELS = 32,
		Q2CS_SOUNDS = Q2CS_MODELS+Q2MAX_MODELS,
		Q2CS_IMAGES = Q2CS_SOUNDS+Q2MAX_SOUNDS,
		Q2CS_LIGHTS = Q2CS_IMAGES+Q2MAX_IMAGES,
		Q2CS_ITEMS = Q2CS_LIGHTS+Q2MAX_LIGHTSTYLES,
		Q2CS_PLAYERSKINS = Q2CS_ITEMS+Q2MAX_ITEMS,
		Q2CS_GENERAL = Q2CS_PLAYERSKINS+Q2MAX_CLIENTS,
		Q2MAX_CONFIGSTRINGS = Q2CS_GENERAL+Q2MAX_GENERAL,
	};

	//svc_ as FTE's Quake II clients have them (the original's)
	enum
	{
		svc_bad, svc_muzzleflash, svc_muzzleflash2, svc_temp_entity, svc_layout, svc_inventory,
		svc_nop, svc_disconnect, svc_reconnect, svc_sound, svc_print, svc_stufftext, svc_serverdata,
		svc_configstring, svc_spawnbaseline, svc_centerprint, svc_download, svc_playerinfo,
		svc_packetentities, svc_deltapacketentities, svc_frame
	};

	struct link_t
	{
		link_t *prev, *next;
	};

	struct csurface_t
	{
		char		name[16];
		int			flags;
		int			value;
	};
	struct cplane_t
	{
		vec3_t		normal;
		float		dist;
		qbyte		type;
		qbyte		signbits;
		qbyte		pad[2];
	};
	struct trace_t
	{
		qboolean	allsolid;
		qboolean	startsolid;
		float		fraction;
		vec3_t		endpos;
		cplane_t	plane;
		const csurface_t *surface;
		int			contents;
		void		*ent;
	};

	struct usercmd_t
	{
		qbyte		msec;
		qbyte		buttons;
		short		angles[3];
		short		forwardmove, sidemove, upmove;
		qbyte		impulse;
		qbyte		lightlevel;
	};

	struct pmove_state_t
	{
		int			pm_type;
		short		origin[3];		// 13.3
		short		velocity[3];	// 13.3
		qbyte		pm_flags;
		qbyte		pm_time;
		short		gravity;
		short		delta_angles[3];
	};

	struct player_state_t
	{
		pmove_state_t pmove;
		vec3_t		viewangles;
		vec3_t		viewoffset;
		vec3_t		kick_angles;
		vec3_t		gunangles;
		vec3_t		gunoffset;
		int			gunindex;
		int			gunframe;
		float		blend[4];
		float		fov;
		int			rdflags;
		short		stats[Q2MAX_STATS];
	};

	struct entity_state_t
	{
		int			number;
		vec3_t		origin;
		vec3_t		angles;
		vec3_t		old_origin;
		int			modelindex;
		int			modelindex2, modelindex3, modelindex4;
		int			frame;
		int			skinnum;
		unsigned int effects;
		int			renderfx;
		int			solid;
		int			sound;
		int			event;
	};

	struct gclient_t
	{
		player_state_t ps;
		int			ping;
	};

	struct edict_t
	{
		entity_state_t s;
		gclient_t	*client;
		qboolean	inuse;
		int			linkcount;
		link_t		area;
		int			num_clusters;
		int			clusternums[Q2MAX_ENT_CLUSTERS];
		int			headnode;
		int			areanum, areanum2;
		int			svflags;
		vec3_t		mins, maxs;
		vec3_t		absmin, absmax, size;
		int			solid;
		int			clipmask;
		edict_t		*owner;
	};

	struct pmove_t;		//never used: Daikatana's game has its own
	struct cvar_t;		//FTE's, only through svdk_fte.c

	struct game_import_t
	{
		void	(FQ2_VARGS *bprintf) (int printlevel, const char *fmt, ...);
		void	(FQ2_VARGS *dprintf) (const char *fmt, ...);
		void	(FQ2_VARGS *cprintf) (edict_t *ent, int printlevel, const char *fmt, ...);
		void	(FQ2_VARGS *centerprintf) (edict_t *ent, const char *fmt, ...);
		void	(FQ2_VARGS *sound) (edict_t *ent, int channel, int soundindex, float volume, float attenuation, float timeofs);
		void	(FQ2_VARGS *positioned_sound) (float *origin, edict_t *ent, int channel, int soundinedex, float volume, float attenuation, float timeofs);
		void	(FQ2_VARGS *configstring) (int num, const char *string);
		void	(FQ2_VARGS *error) (const char *fmt, ...);
		int		(FQ2_VARGS *modelindex) (const char *name);
		int		(FQ2_VARGS *soundindex) (const char *name);
		int		(FQ2_VARGS *imageindex) (const char *name);
		void	(FQ2_VARGS *setmodel) (edict_t *ent, const char *name);
		trace_t	(FQ2_VARGS *trace) (float *start, float *mins, float *maxs, float *end, edict_t *passent, int contentmask);
		int		(FQ2_VARGS *pointcontents) (float *point);
		qboolean (FQ2_VARGS *inPVS) (float *p1, float *p2);
		qboolean (FQ2_VARGS *inPHS) (float *p1, float *p2);
		void	(FQ2_VARGS *SetAreaPortalState) (unsigned int portalnum, qboolean open);
		qboolean (FQ2_VARGS *AreasConnected) (unsigned int area1, unsigned int area2);
		void	(FQ2_VARGS *linkentity) (edict_t *ent);
		void	(FQ2_VARGS *unlinkentity) (edict_t *ent);
		int		(FQ2_VARGS *BoxEdicts) (float *mins, float *maxs, edict_t **list, int maxcount, int areatype);
		void	(FQ2_VARGS *Pmove) (pmove_t *pmove);
		void	(FQ2_VARGS *multicast) (float *origin, int to);
		void	(FQ2_VARGS *unicast) (edict_t *ent, qboolean reliable);
		void	(FQ2_VARGS *WriteChar) (int c);
		void	(FQ2_VARGS *WriteByte) (int c);
		void	(FQ2_VARGS *WriteShort) (int c);
		void	(FQ2_VARGS *WriteLong) (int c);
		void	(FQ2_VARGS *WriteFloat) (float f);
		void	(FQ2_VARGS *WriteString) (const char *s);
		void	(FQ2_VARGS *WritePosition) (float *pos);
		void	(FQ2_VARGS *WriteDir) (float *pos);
		void	(FQ2_VARGS *WriteAngle) (float f);
		void	*(FQ2_VARGS *TagMalloc) (int size, int tag);
		void	(FQ2_VARGS *TagFree) (void *block);
		void	(FQ2_VARGS *FreeTags) (int tag);
		cvar_t	*(FQ2_VARGS *cvar) (const char *var_name, const char *value, int flags);
		cvar_t	*(FQ2_VARGS *cvar_set) (const char *var_name, const char *value);
		cvar_t	*(FQ2_VARGS *cvar_forceset) (const char *var_name, const char *value);
		int		(FQ2_VARGS *argc) (void);
		char	*(FQ2_VARGS *argv) (int n);
		char	*(FQ2_VARGS *args) (void);
		void	(FQ2_VARGS *AddCommandString) (const char *text);
		void	(FQ2_VARGS *DebugGraph) (float value, int color);
	};

	//Q2EX's entity state past game API 3's (FTE's q2entityex_t)
	struct entityex_t
	{
		unsigned int	effects_hi;
		qbyte			alpha;			//in 255ths; 0: the default
		qbyte			scale;			//in 16ths; 0: the default (1)
		qbyte			instance_bits;
	};

	struct game_export_t
	{
		int			apiversion;
		void		(FQ2_VARGS *Init) (void);
		void		(FQ2_VARGS *Shutdown) (void);
		void		(FQ2_VARGS *SpawnEntities) (const char *mapname, const char *entstring, const char *spawnpoint);
		void		(FQ2_VARGS *WriteGame) (const char *filename, qboolean autosave);
		void		(FQ2_VARGS *ReadGame) (const char *filename);
		void		(FQ2_VARGS *WriteLevel) (const char *filename);
		void		(FQ2_VARGS *ReadLevel) (const char *filename);
		qboolean	(FQ2_VARGS *ClientConnect) (edict_t *ent, char *userinfo);
		void		(FQ2_VARGS *ClientBegin) (edict_t *ent);
		void		(FQ2_VARGS *ClientUserinfoChanged) (edict_t *ent, char *userinfo);
		void		(FQ2_VARGS *ClientDisconnect) (edict_t *ent);
		void		(FQ2_VARGS *ClientCommand) (edict_t *ent);
		void		(FQ2_VARGS *ClientThink) (edict_t *ent, usercmd_t *cmd);
		void		(FQ2_VARGS *RunFrame) (void);
		void		(FQ2_VARGS *ServerCommand) (void);
		edict_t		*edicts;
		int			edict_size;
		int			num_edicts;
		int			max_edicts;
	};
}

//FTE's, what game API 3 hasn't (server/svdk_fte.c)
extern "C"
{
	void		SVDK_CvarRead(fq2::cvar_t *var, const char **string, const char **latched, float *value, int *flags, int *modified);
	const char	*SVDK_Gamedir(void);
	void		*SVDK_LoadFile(const char *path, int *length);	//null if it isn't there; free with SVDK_FreeFile
	void		SVDK_FreeFile(void *buf);
	int			SVDK_Milliseconds(void);
	int			SVDK_ExeArgc(void);
	const char	*SVDK_ExeArgv(int i);
	int			SVDK_IsLocalClient(int playernum);	//-1: no one there
	void		SVDK_ReliableWrite(int playernum, const void *data, int len);	//playernum -1: everyone connected
	int			SVDK_ReliableRoom(int playernum);	//back buffers they've room for
	int			SVDK_PlaneIndex(const float *normal, float dist);	//the map's plane that is; -1 if none
}

#endif
