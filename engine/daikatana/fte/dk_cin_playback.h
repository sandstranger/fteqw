// #pragma once

#ifndef _DK_CIN_PLAYBACK_H_
#define _DK_CIN_PLAYBACK_H_

//FTE: its engine's functions world.dll had functions of the same names as (world/cin_playback.cpp, the world's
//interface to them) are CINE_ here, where they're one

//
//functions used by playback and gce.dll to get information they need from
//the server.
//

bool CINE_EntityPosition(const char *entity_name, const char *id, CVector &pos, CVector &angles);

void CINE_TeleportEntity(const char *entity_name, const char *id, CVector &pos, CVector &angles, bool use_angles);

void CINE_SpawnHiroActor();
void CINE_RemoveHiroActor();
void CinStopTrackedMP3();

//returns the current horizontal fov that should be used in the current point in the script.
float ScriptFOVCurrent();

float ScriptTimescaleCurrent();

bool ScriptColorFading();
float ScriptRCurrent();
float ScriptGCurrent();
float ScriptBCurrent();
float ScriptACurrent();

//FTE: its engine's playback, on the game's server (cin_main.cpp): what it had of its engine, here
FILE	*DKCIN_OpenFile(const char *path);	//a game file (from paks), read with fread; null if there's none
void	DKCIN_CloseFile(FILE *f);
void	DKCIN_Command(const char *text);	//as its Cbuf_AddText: its own (cin_*) here, the game's for the player
void	DKCIN_PlaySound(const char *name, int channel, float volume, bool loop);	//to the player
void	DKCIN_PlayMP3(const char *name, int channel, float volume, bool loop);
void	DKCIN_StopMP3(int channel);
int		DKCIN_PlayerNum(void);				//its cl.playernum: the first player's
float	DKCIN_CvarValue(const char *name, const char *defaultvalue);
void	DKCIN_Printf(const char *fmt, ...);	//to the console, for developers
void	DKCIN_CvarSet(const char *name, float value);

//its engine's (cin_main.cpp)
int		CIN_Running(void);
void	CIN_Play(void);
void	CIN_Stop(const char *name);
void	CIN_CamLocation(CVector &pos, CVector &angles);
void	CIN_WorldImport(void *import);		//the world's (gstate->CIN_WorldPlayback)
bool	CIN_Command(void);					//one of its commands (cin_*), the args the game's: true if it was
bool	CIN_RunningWithCamera(void);

#endif // _DK_CIN_PLAYBACK_H_
