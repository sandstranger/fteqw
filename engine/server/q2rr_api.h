/*
The Quake II Rerelease (KEX) game library's interface, GAME_API_VERSION 2023 and CGAME_API_VERSION 2022, as C.

id Software's game.h (https://github.com/id-Software/quake2-rerelease-dll) declares it in C++. These are the same
structures and function tables, field for field, so FTE can load the rerelease's game library (the shipped
game_x64.dll, or one built from that source for another platform) without C++. The layouts are checked against
game.h by tools/q2rr_layout_check.py. Everything here is prefixed rr_/RR_ to keep it apart from FTE's own (game API 3)
Quake II declarations.

GPL v2 or later, as the rerelease game source: Copyright (c) ZeniMax Media Inc.
*/
#ifndef Q2RR_API_H
#define Q2RR_API_H

#include <stdint.h>
#include <stddef.h>

#define RR_GAME_API_VERSION		2023
#define RR_CGAME_API_VERSION	2022

typedef unsigned char rr_bool;		//C++ bool
typedef float rr_vec3_t[3];			//vec3_t: x, y, z

#define RR_MAX_STATS			64
#define RR_MAX_ITEMS			256
#define RR_MAX_CLIENTS			256
#define RR_MAX_EDICTS			8192
#define RR_MAX_MODELS			8192
#define RR_MAX_SOUNDS			2048
#define RR_MAX_IMAGES			512
#define RR_MAX_LIGHTSTYLES		256
#define RR_MAX_SHADOW_LIGHTS	256
#define RR_MAX_GENERAL			(RR_MAX_CLIENTS*2)
#define RR_MAX_WHEEL_ITEMS		32
#define RR_MAX_NETNAME			32
#define RR_MAX_LOCALIZATION_ARGS	8

//configstrings
#define RR_CS_NAME				0
#define RR_CS_CDTRACK			1
#define RR_CS_SKY				2
#define RR_CS_SKYAXIS			3
#define RR_CS_SKYROTATE			4
#define RR_CS_STATUSBAR			5
#define RR_CS_AIRACCEL			59
#define RR_CS_MAXCLIENTS		60
#define RR_CS_MAPCHECKSUM		61
#define RR_CS_MODELS			62
#define RR_CS_SOUNDS			(RR_CS_MODELS + RR_MAX_MODELS)
#define RR_CS_IMAGES			(RR_CS_SOUNDS + RR_MAX_SOUNDS)
#define RR_CS_LIGHTS			(RR_CS_IMAGES + RR_MAX_IMAGES)
#define RR_CS_SHADOWLIGHTS		(RR_CS_LIGHTS + RR_MAX_LIGHTSTYLES)
#define RR_CS_ITEMS				(RR_CS_SHADOWLIGHTS + RR_MAX_SHADOW_LIGHTS)
#define RR_CS_PLAYERSKINS		(RR_CS_ITEMS + RR_MAX_ITEMS)
#define RR_CS_GENERAL			(RR_CS_PLAYERSKINS + RR_MAX_CLIENTS)
#define RR_CS_WHEEL_WEAPONS		(RR_CS_GENERAL + RR_MAX_GENERAL)
#define RR_CS_WHEEL_AMMO		(RR_CS_WHEEL_WEAPONS + RR_MAX_WHEEL_ITEMS)
#define RR_CS_WHEEL_POWERUPS	(RR_CS_WHEEL_AMMO + RR_MAX_WHEEL_ITEMS)
#define RR_CS_CD_LOOP_COUNT		(RR_CS_WHEEL_POWERUPS + RR_MAX_WHEEL_ITEMS)
#define RR_CS_GAME_STYLE		(RR_CS_CD_LOOP_COUNT + 1)
#define RR_MAX_CONFIGSTRINGS	(RR_CS_GAME_STYLE + 1)
#define RR_CS_MAX_STRING_LENGTH	96

//print_type_t
#define RR_PRINT_LOW			0
#define RR_PRINT_MEDIUM			1
#define RR_PRINT_HIGH			2
#define RR_PRINT_CHAT			3
#define RR_PRINT_TYPEWRITER		4
#define RR_PRINT_CENTER			5
#define RR_PRINT_TTS			6
#define RR_PRINT_BROADCAST		(1<<3)
#define RR_PRINT_NO_NOTIFY		(1<<4)

//svflags_t
#define RR_SVF_NOCLIENT			(1u<<0)
#define RR_SVF_DEADMONSTER		(1u<<1)
#define RR_SVF_MONSTER			(1u<<2)
#define RR_SVF_PLAYER			(1u<<3)
#define RR_SVF_BOT				(1u<<4)
#define RR_SVF_NOBOTS			(1u<<5)
#define RR_SVF_RESPAWNING		(1u<<6)
#define RR_SVF_PROJECTILE		(1u<<7)
#define RR_SVF_INSTANCED		(1u<<8)
#define RR_SVF_DOOR				(1u<<9)
#define RR_SVF_NOCULL			(1u<<10)
#define RR_SVF_HULL				(1u<<11)

//contents_t (the bsp ones are Quake II's)
#define RR_CONTENTS_PLAYER		(1u<<30)
#define RR_CONTENTS_PROJECTILE	(1u<<31)

//pmtype_t
enum {RR_PM_NORMAL, RR_PM_GRAPPLE, RR_PM_NOCLIP, RR_PM_SPECTATOR, RR_PM_DEAD, RR_PM_GIB, RR_PM_FREEZE};

//button_t
#define RR_BUTTON_ATTACK		(1<<0)
#define RR_BUTTON_USE			(1<<1)
#define RR_BUTTON_HOLSTER		(1<<2)
#define RR_BUTTON_JUMP			(1<<3)
#define RR_BUTTON_CROUCH		(1<<4)
#define RR_BUTTON_ANY			(1<<7)

//BoxEdictsResult_t
#define RR_BOXEDICTS_KEEP		0
#define RR_BOXEDICTS_SKIP		1
#define RR_BOXEDICTS_END		64

//PathReturnCode
#define RR_PATH_NONAVAILABLE	8

typedef struct {uint8_t r, g, b, a;} rr_rgba_t;
typedef struct {float x, y;} rr_vec2_t;

typedef struct rr_cvar_s
{
	char		*name;
	char		*string;
	char		*latched_string;	// for CVAR_LATCH vars
	uint32_t	flags;
	int32_t		modified_count;		// changed each time the cvar is changed, but never zero
	float		value;
	struct rr_cvar_s *next;
	int32_t		integer;			// integral value
} rr_cvar_t;

typedef struct
{
	rr_vec3_t	normal;
	float		dist;
	uint8_t		type;
	uint8_t		signbits;
	uint8_t		pad[2];
} rr_cplane_t;

typedef struct
{
	char		name[32];
	uint32_t	flags;
	int32_t		value;
	uint32_t	id;				// unique texinfo ID, offset by 1 (0 is 'null')
	char		material[16];
} rr_csurface_t;

struct rr_edict_s;
typedef struct
{
	rr_bool		allsolid;
	rr_bool		startsolid;
	float		fraction;
	rr_vec3_t	endpos;
	rr_cplane_t	plane;
	rr_csurface_t *surface;
	uint32_t	contents;
	struct rr_edict_s *ent;
	rr_cplane_t	plane2;			// second surface normal at impact
	rr_csurface_t *surface2;
} rr_trace_t;

typedef struct
{
	int32_t		pm_type;
	rr_vec3_t	origin;
	rr_vec3_t	velocity;
	uint16_t	pm_flags;
	uint16_t	pm_time;		//ms
	int16_t		gravity;
	rr_vec3_t	delta_angles;
	int8_t		viewheight;
} rr_pmove_state_t;

typedef struct
{
	uint8_t		msec;
	uint8_t		buttons;
	rr_vec3_t	angles;
	float		forwardmove, sidemove;
	uint32_t	server_frame;
} rr_usercmd_t;

typedef struct
{
	uint32_t	number;
	rr_vec3_t	origin;
	rr_vec3_t	angles;
	rr_vec3_t	old_origin;
	int32_t		modelindex;
	int32_t		modelindex2, modelindex3, modelindex4;
	int32_t		frame;
	int32_t		skinnum;
	uint64_t	effects;
	uint32_t	renderfx;
	uint32_t	solid;
	int32_t		sound;
	uint8_t		event;
	float		alpha;
	float		scale;
	uint8_t		instance_bits;
	float		loop_volume;
	float		loop_attenuation;
	int32_t		owner;
	int32_t		old_frame;
} rr_entity_state_t;

typedef struct
{
	rr_pmove_state_t pmove;
	rr_vec3_t	viewangles;
	rr_vec3_t	viewoffset;
	rr_vec3_t	kick_angles;
	rr_vec3_t	gunangles;
	rr_vec3_t	gunoffset;
	int32_t		gunindex;
	int32_t		gunskin;
	int32_t		gunframe;
	int32_t		gunrate;
	float		screen_blend[4];
	float		damage_blend[4];
	float		fov;
	uint8_t		rdflags;
	int16_t		stats[RR_MAX_STATS];
	uint8_t		team_id;
} rr_player_state_t;

typedef struct
{
	rr_player_state_t ps;
	int32_t		ping;
	//the game's own data follows
} rr_gclient_t;

typedef struct
{
	int32_t		item_id;
	int32_t		max_count;
} rr_armorInfo_t;

typedef struct
{
	rr_bool		init;
	uint64_t	ent_flags;
	uint8_t		buttons;
	uint32_t	spawnflags;
	int32_t		item_id;
	int32_t		armor_type;
	int32_t		armor_value;
	int32_t		health;
	int32_t		max_health;
	int32_t		starting_health;
	int32_t		weapon;
	int32_t		team;
	int32_t		lobby_usernum;
	int32_t		respawntime;
	int32_t		viewheight;
	int32_t		last_attackertime;
	uint8_t		waterlevel;
	rr_vec3_t	viewangles;
	rr_vec3_t	viewforward;
	rr_vec3_t	velocity;
	rr_vec3_t	start_origin;
	rr_vec3_t	end_origin;
	struct rr_edict_s *enemy;
	struct rr_edict_s *ground_entity;
	const char	*classname;
	const char	*targetname;
	char		netname[RR_MAX_NETNAME];
	int32_t		inventory[RR_MAX_ITEMS];
	rr_armorInfo_t armor_info[3];
} rr_sv_entity_t;

typedef struct rr_edict_s
{
	rr_entity_state_t s;
	rr_gclient_t *client;
	rr_sv_entity_t sv;		// read only info about this entity for the server
	rr_bool		inuse;
	rr_bool		linked;
	int32_t		linkcount;
	int32_t		areanum, areanum2;
	uint32_t	svflags;
	rr_vec3_t	mins, maxs;
	rr_vec3_t	absmin, absmax, size;
	uint8_t		solid;
	uint32_t	clipmask;
	struct rr_edict_s *owner;
	//the game's own data follows
} rr_edict_t;

typedef struct
{
	int32_t		numPathPoints;
	float		pathDistSqr;
	rr_vec3_t	firstMovePoint;
	rr_vec3_t	secondMovePoint;
	int32_t		pathLinkType;
	int32_t		returnCode;
} rr_PathInfo;

typedef int32_t (*rr_BoxEdictsFilter_t)(rr_edict_t *, void *);

typedef struct
{
	uint32_t	tick_rate;
	float		frame_time_s;
	uint32_t	frame_time_ms;

	void (*Broadcast_Print)(int32_t printlevel, const char *message);
	void (*Com_Print)(const char *msg);
	void (*Client_Print)(rr_edict_t *ent, int32_t printlevel, const char *message);
	void (*Center_Print)(rr_edict_t *ent, const char *message);
	void (*sound)(rr_edict_t *ent, uint8_t channel, int soundindex, float volume, float attenuation, float timeofs);
	void (*positioned_sound)(const float *origin, rr_edict_t *ent, uint8_t channel, int soundindex, float volume, float attenuation, float timeofs);
	void (*local_sound)(rr_edict_t *target, const float *origin, rr_edict_t *ent, uint8_t channel, int soundindex, float volume, float attenuation, float timeofs, uint32_t dupe_key);
	void (*configstring)(int num, const char *string);
	const char *(*get_configstring)(int num);
	void (*Com_Error)(const char *message);
	int (*modelindex)(const char *name);
	int (*soundindex)(const char *name);
	int (*imageindex)(const char *name);
	void (*setmodel)(rr_edict_t *ent, const char *name);
	rr_trace_t (*trace)(const float *start, const float *mins, const float *maxs, const float *end, const rr_edict_t *passent, uint32_t contentmask);
	rr_trace_t (*clip)(rr_edict_t *entity, const float *start, const float *mins, const float *maxs, const float *end, uint32_t contentmask);
	uint32_t (*pointcontents)(const float *point);
	rr_bool (*inPVS)(const float *p1, const float *p2, rr_bool portals);
	rr_bool (*inPHS)(const float *p1, const float *p2, rr_bool portals);
	void (*SetAreaPortalState)(int portalnum, rr_bool open);
	rr_bool (*AreasConnected)(int area1, int area2);
	void (*linkentity)(rr_edict_t *ent);
	void (*unlinkentity)(rr_edict_t *ent);
	size_t (*BoxEdicts)(const float *mins, const float *maxs, rr_edict_t **list, size_t maxcount, int32_t areatype, rr_BoxEdictsFilter_t filter, void *filter_data);
	void (*multicast)(const float *origin, int32_t to, rr_bool reliable);
	void (*unicast)(rr_edict_t *ent, rr_bool reliable, uint32_t dupe_key);
	void (*WriteChar)(int c);
	void (*WriteByte)(int c);
	void (*WriteShort)(int c);
	void (*WriteLong)(int c);
	void (*WriteFloat)(float f);
	void (*WriteString)(const char *s);
	void (*WritePosition)(const float *pos);
	void (*WriteDir)(const float *pos);
	void (*WriteAngle)(float f);
	void (*WriteEntity)(const rr_edict_t *e);
	void *(*TagMalloc)(size_t size, int tag);
	void (*TagFree)(void *block);
	void (*FreeTags)(int tag);
	rr_cvar_t *(*cvar)(const char *var_name, const char *value, uint32_t flags);
	rr_cvar_t *(*cvar_set)(const char *var_name, const char *value);
	rr_cvar_t *(*cvar_forceset)(const char *var_name, const char *value);
	int (*argc)(void);
	const char *(*argv)(int n);
	const char *(*args)(void);
	void (*AddCommandString)(const char *text);
	void (*DebugGraph)(float value, int color);
	void *(*GetExtension)(const char *name);

	void (*Bot_RegisterEdict)(const rr_edict_t *edict);
	void (*Bot_UnRegisterEdict)(const rr_edict_t *edict);
	int32_t (*Bot_MoveToPoint)(const rr_edict_t *bot, const float *point, const float moveTolerance);
	int32_t (*Bot_FollowActor)(const rr_edict_t *bot, const rr_edict_t *actor);
	rr_bool (*GetPathToGoal)(const void *request, rr_PathInfo *info);
	void (*Loc_Print)(rr_edict_t *ent, int32_t level, const char *base, const char **args, size_t num_args);
	void (*Draw_Line)(const float *start, const float *end, const rr_rgba_t *color, const float lifeTime, const rr_bool depthTest);
	void (*Draw_Point)(const float *point, const float size, const rr_rgba_t *color, const float lifeTime, const rr_bool depthTest);
	void (*Draw_Circle)(const float *origin, const float radius, const rr_rgba_t *color, const float lifeTime, const rr_bool depthTest);
	void (*Draw_Bounds)(const float *mins, const float *maxs, const rr_rgba_t *color, const float lifeTime, const rr_bool depthTest);
	void (*Draw_Sphere)(const float *origin, const float radius, const rr_rgba_t *color, const float lifeTime, const rr_bool depthTest);
	void (*Draw_OrientedWorldText)(const float *origin, const char *text, const rr_rgba_t *color, const float size, const float lifeTime, const rr_bool depthTest);
	void (*Draw_StaticWorldText)(const float *origin, const float *angles, const char *text, const rr_rgba_t *color, const float size, const float lifeTime, const rr_bool depthTest);
	void (*Draw_Cylinder)(const float *origin, const float halfHeight, const float radius, const rr_rgba_t *color, const float lifeTime, const rr_bool depthTest);
	void (*Draw_Ray)(const float *origin, const float *direction, const float length, const float size, const rr_rgba_t *color, const float lifeTime, const rr_bool depthTest);
	void (*Draw_Arrow)(const float *start, const float *end, const float size, const rr_rgba_t *lineColor, const rr_rgba_t *arrowColor, const float lifeTime, const rr_bool depthTest);
	void (*ReportMatchDetails_Multicast)(rr_bool is_end);
	uint32_t (*ServerFrame)(void);
	void (*SendToClipBoard)(const char *text);
	size_t (*Info_ValueForKey)(const char *s, const char *key, char *buffer, size_t buffer_len);
	rr_bool (*Info_RemoveKey)(char *s, const char *key);
	rr_bool (*Info_SetValueForKey)(char *s, const char *key, const char *value);
} rr_game_import_t;

typedef struct
{
	int			apiversion;

	void (*PreInit)(void);
	void (*Init)(void);
	void (*Shutdown)(void);
	void (*SpawnEntities)(const char *mapname, const char *entstring, const char *spawnpoint);
	char *(*WriteGameJson)(rr_bool autosave, size_t *out_size);
	void (*ReadGameJson)(const char *json);
	char *(*WriteLevelJson)(rr_bool transition, size_t *out_size);
	void (*ReadLevelJson)(const char *json);
	rr_bool (*CanSave)(void);
	rr_edict_t *(*ClientChooseSlot)(const char *userinfo, const char *social_id, rr_bool isBot, rr_edict_t **ignore, size_t num_ignore, rr_bool cinematic);
	rr_bool (*ClientConnect)(rr_edict_t *ent, char *userinfo, const char *social_id, rr_bool isBot);
	void (*ClientBegin)(rr_edict_t *ent);
	void (*ClientUserinfoChanged)(rr_edict_t *ent, const char *userinfo);
	void (*ClientDisconnect)(rr_edict_t *ent);
	void (*ClientCommand)(rr_edict_t *ent);
	void (*ClientThink)(rr_edict_t *ent, rr_usercmd_t *cmd);
	void (*RunFrame)(rr_bool main_loop);
	void (*PrepFrame)(void);
	void (*ServerCommand)(void);

	rr_edict_t	*edicts;
	size_t		edict_size;
	uint32_t	num_edicts;
	uint32_t	max_edicts;
	int32_t		server_flags;

	void (*Pmove)(void *pmove);
	void *(*GetExtension)(const char *name);
	void (*Bot_SetWeapon)(rr_edict_t *botEdict, const int weaponIndex, const rr_bool instantSwitch);
	void (*Bot_TriggerEdict)(rr_edict_t *botEdict, rr_edict_t *edict);
	void (*Bot_UseItem)(rr_edict_t *botEdict, const int32_t itemID);
	int32_t (*Bot_GetItemID)(const char *classname);
	void (*Edict_ForceLookAtPoint)(rr_edict_t *edict, const float *point);
	rr_bool (*Bot_PickedUpItem)(rr_edict_t *botEdict, rr_edict_t *itemEdict);
	rr_bool (*Entity_IsVisibleToPlayer)(rr_edict_t *ent, rr_edict_t *player);
	const void *(*GetShadowLightData)(int32_t entity_number);
} rr_game_export_t;

//server_flags_t
#define RR_SERVER_FLAG_SLOW_TIME	(1<<0)
#define RR_SERVER_FLAG_INTERMISSION	(1<<1)
#define RR_SERVER_FLAG_LOADING		(1<<2)

/*
=====================================================================
the client game (cgame)
=====================================================================
*/
typedef struct {int32_t x, y, width, height;} rr_vrect_t;
enum {RR_TEXT_ALIGN_LEFT, RR_TEXT_ALIGN_CENTER, RR_TEXT_ALIGN_RIGHT};

typedef struct
{
	char		layout[1024];
	int16_t		inventory[RR_MAX_ITEMS];
} rr_cg_server_data_t;

typedef struct
{
	uint32_t	tick_rate;
	float		frame_time_s;
	uint32_t	frame_time_ms;

	void (*Com_Print)(const char *msg);
	const char *(*get_configstring)(int num);
	void (*Com_Error)(const char *message);
	void *(*TagMalloc)(size_t size, int tag);
	void (*TagFree)(void *block);
	void (*FreeTags)(int tag);
	rr_cvar_t *(*cvar)(const char *var_name, const char *value, uint32_t flags);
	rr_cvar_t *(*cvar_set)(const char *var_name, const char *value);
	rr_cvar_t *(*cvar_forceset)(const char *var_name, const char *value);
	void (*AddCommandString)(const char *text);
	void *(*GetExtension)(const char *name);
	rr_bool (*CL_FrameValid)(void);
	float (*CL_FrameTime)(void);
	uint64_t (*CL_ClientTime)(void);
	uint64_t (*CL_ClientRealTime)(void);
	int32_t (*CL_ServerFrame)(void);
	int32_t (*CL_ServerProtocol)(void);
	const char *(*CL_GetClientName)(int32_t index);
	const char *(*CL_GetClientPic)(int32_t index);
	const char *(*CL_GetClientDogtag)(int32_t index);
	const char *(*CL_GetKeyBinding)(const char *binding);
	rr_bool (*Draw_RegisterPic)(const char *name);
	void (*Draw_GetPicSize)(int *w, int *h, const char *name);
	void (*SCR_DrawChar)(int x, int y, int scale, int num, rr_bool shadow);
	void (*SCR_DrawPic)(int x, int y, int w, int h, const char *name);
	void (*SCR_DrawColorPic)(int x, int y, int w, int h, const char *name, const rr_rgba_t *color);
	void (*SCR_SetAltTypeface)(rr_bool enabled);
	void (*SCR_DrawFontString)(const char *str, int x, int y, int scale, const rr_rgba_t *color, rr_bool shadow, int32_t align);
	rr_vec2_t (*SCR_MeasureFontString)(const char *str, int scale);
	float (*SCR_FontLineHeight)(int scale);
	rr_bool (*CL_GetTextInput)(const char **msg, rr_bool *is_team);
	int32_t (*CL_GetWarnAmmoCount)(int32_t weapon_id);
	const char *(*Localize)(const char *base, const char **args, size_t num_args);
	int32_t (*SCR_DrawBind)(int32_t isplit, const char *binding, const char *purpose, int x, int y, int scale);
	rr_bool (*CL_InAutoDemoLoop)(void);
} rr_cgame_import_t;

typedef struct
{
	int			apiversion;

	void (*Init)(void);
	void (*Shutdown)(void);
	void (*DrawHUD)(int32_t isplit, const rr_cg_server_data_t *data, rr_vrect_t hud_vrect, rr_vrect_t hud_safe, int32_t scale, int32_t playernum, const rr_player_state_t *ps);
	void (*TouchPics)(void);
	int16_t (*LayoutFlags)(const rr_player_state_t *ps);
	int32_t (*GetActiveWeaponWheelWeapon)(const rr_player_state_t *ps);
	uint32_t (*GetOwnedWeaponWheelWeapons)(const rr_player_state_t *ps);
	int16_t (*GetWeaponWheelAmmoCount)(const rr_player_state_t *ps, int32_t ammo_id);
	int16_t (*GetPowerupWheelCount)(const rr_player_state_t *ps, int32_t powerup_id);
	int16_t (*GetHitMarkerDamage)(const rr_player_state_t *ps);
	void (*Pmove)(void *pmove);
	void (*ParseConfigString)(int32_t i, const char *s);
	void (*ParseCenterPrint)(const char *str, int isplit, rr_bool instant);
	void (*ClearNotify)(int32_t isplit);
	void (*ClearCenterprint)(int32_t isplit);
	void (*NotifyMessage)(int32_t isplit, const char *msg, rr_bool is_chat);
	void (*GetMonsterFlashOffset)(uint16_t id, float *offset);
	void *(*GetExtension)(const char *name);
} rr_cgame_export_t;

//layout_flags_t
#define RR_LAYOUTS_LAYOUT			(1<<0)
#define RR_LAYOUTS_INVENTORY		(1<<1)
#define RR_LAYOUTS_HIDE_HUD			(1<<2)
#define RR_LAYOUTS_INTERMISSION		(1<<3)
#define RR_LAYOUTS_HELP				(1<<4)
#define RR_LAYOUTS_HIDE_CROSSHAIR	(1<<5)

#endif
