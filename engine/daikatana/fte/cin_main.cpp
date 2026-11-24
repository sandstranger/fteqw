/*
FTE: its engine's cinematics (dk_gce_main.cpp's playback, without its editor, gce.dll): the scripts
(cin/scripts/<name>.script) the game starts (it stuffs cin_load_and_play), played on the server beside the game:
their entities' tasks through the world's interface (gstate->CIN_WorldPlayback), their camera for the world's
cinematic camera (gstate->CIN_CamLocation), which the player's view follows (svdk_game.cpp sends the view
entity's place as the player's); their fov, fades and letterbox go with the player's state.

GPL v2 or later.
*/
#include "dk_shared.h"
#include "memmgrcommon.h"
#include "dk_misc.h"
#include "dk_array.h"
#include "dk_point.h"
#include "dk_buffer.h"

#include "dk_cin_types.h"
#include "dk_gce_spline.h"
#include "dk_gce_script.h"
#include "dk_gce_entities.h"
#include "dk_cin_world_interface.h"
#include "dk_cin_playback.h"

const CCinematicScript *LoadScript(const char *name);	//cin_load.cpp
void DeleteScript(const CCinematicScript *script);
void ScriptColorInit(float r, float g, float b, float a);	//cin_playback.cpp

//the game's command's args (svdk_game.cpp's)
int			DKCIN_Argc(void);
const char	*DKCIN_Argv(int i);
int			DKCIN_Milliseconds(void);

//its interface to world.dll
cin_world_import_t	world_import;
bool				got_world_import = false;

//its script
static CCinematicScript script;

//true if we are playing a script
static bool playback_running = false;
//true if we have swapped out the player entity with the hiro actor bot.
static bool playback_using_hiro_actor = false;
//true if a script that is currently playing controls the camera.
static bool playback_running_with_camera = false;

static int	cin_last_tick;
static bool	cin_changed_timescale;

void CIN_WorldImport(void *funcs)
{
	if (!funcs)
	{
		got_world_import = false;
		return;
	}
	//the pointer passed in is type cin_world_import_t
	world_import = *(cin_world_import_t *)funcs;
	got_world_import = true;
}

int CIN_Running(void)
{
	return playback_running?1:0;
}
bool CIN_RunningWithCamera(void)
{
	return playback_running && playback_running_with_camera;
}

static void CIN_RemoveCinematicEntities(void)
{
	if (got_world_import && world_import.RemoveAllEntities != NULL)
		world_import.RemoveAllEntities();
}

static void CIN_Start(const char *name, int start_shot, int end_shot)
{
	if (playback_running == true)
		return;

	if (DKCIN_CvarValue("sv_cinematics", "1") == 0)
	{
		CIN_RemoveCinematicEntities();
		CIN_Stop(name);
		return;
	}

	// SCG[11/17/99]: set the server state
	if (got_world_import && world_import.SetServerState != NULL)
		world_import.SetServerState(TRUE, name);

	cin_last_tick = DKCIN_Milliseconds();
	cin_changed_timescale = false;
	playback_running = true;

	script.PlaybackReset(start_shot, end_shot);

	//assume by default that we are not going to use the player actor.
	playback_using_hiro_actor = false;

	DKCIN_CvarSet("ai_scriptrunning", 1.0f);

	//check if the script has camera info.
	if (script.HaveCameraInfo() == true)
	{
		playback_running_with_camera = true;

		//turn on our special camera mode.
		DKCIN_Command("cam_toggle cin_on\n");

		CINE_SpawnHiroActor();
		playback_using_hiro_actor = true;
	}

	script.RegsterCinematicSounds();
}

void CIN_Stop(const char *name)
{
	if (playback_running == false)
		return;

	// SCG[11/17/99]: set the server state
	if (got_world_import && world_import.SetServerState != NULL)
		world_import.SetServerState(FALSE, NULL);

	playback_running = false;

	DKCIN_CvarSet("ai_scriptrunning", 0.0f);

	CINE_RemoveHiroActor();

	//check if the script had camera info.
	if (playback_running_with_camera == true)
	{
		//turn off our special camera mode.
		DKCIN_Command("cam_toggle cin_off\n");
		playback_running_with_camera = false;
	}

	// reset the timescale if we changed it
	if (cin_changed_timescale)
		DKCIN_CvarSet("timescale", 1.0f);

	CinStopTrackedMP3();
}

//advances the positions of all active cinematic seqences
void CIN_Play(void)
{
	int cur, time;
	if (CIN_Running() == 0)
		return;

	//get the elapsed time.
	cur = DKCIN_Milliseconds();
	time = (int)((cur - cin_last_tick)*ScriptTimescaleCurrent());
	if (time > 100)
		time = 100;
	if (time < 0)
		time = 0;

	if (1 != ScriptTimescaleCurrent())
	{
		cin_changed_timescale = true;
		DKCIN_CvarSet("timescale", ScriptTimescaleCurrent());
	}

	script.PlaybackTimeElapsed(time);

	//remember the current tick for next time.
	cin_last_tick = cur;

	//check if the cinematic is over.
	if (script.PlaybackDone() == true)
		CIN_Stop(NULL);
}

void CIN_CamLocation(CVector &fpos, CVector &angles)
{
	//true if we have ever set the camera.  If we have set the camera at least once,
	//we will always return the most recent camera position.
	static bool set_camera_once = false;
	static CVector pos, dir;

	if (set_camera_once == false && playback_running_with_camera == false)
		return;
	if (playback_running_with_camera == true)
	{
		script.PlaybackCamera(pos, dir);
		set_camera_once = true;	}
	fpos = pos;
	angles = dir;
}

static void CIN_LoadAndPlay(const char *script_name)
{
	const CCinematicScript *loaded;
	if (script_name == NULL || script_name[0] == '\0')
		return;
	if (playback_running)
		return;	//FTE: (its CIN_Start wouldn't have it; not the script it's playing gone from under it either)
	loaded = LoadScript(script_name);
	if (loaded == NULL)
	{
		DKCIN_Printf("cinematic %s: no such script (cin/scripts/%s.script)\n", script_name, script_name);
		return;
	}
	DKCIN_Printf("cinematic %s: %i shots%s\n", script_name, loaded->NumShots(), loaded->HaveCameraInfo()?", a camera":"");
	//make this script the current script.
	script = *loaded;
	DeleteScript(loaded);
	//start the playback.
	CIN_Start(script_name, 0, script.NumShots() - 1);
}

//its commands (as its console had them; the game stuffs them)
bool CIN_Command(void)
{
	const char *cmd = DKCIN_Argv(0);
	if (!stricmp(cmd, "cin_load_and_play"))
	{
		if (DKCIN_Argc() == 2)
			CIN_LoadAndPlay(DKCIN_Argv(1));
		return true;
	}
	if (!stricmp(cmd, "cin_play"))
	{
		if (CIN_Running() == 1)
			CIN_Stop(NULL);
		else
			CIN_Start(NULL, 0, script.NumShots() - 1);
		return true;
	}
	if (!stricmp(cmd, "cin_cancel_shots"))
	{
		CIN_Stop(NULL);
		return true;
	}
	if (!stricmp(cmd, "cin_play_all_shots"))
	{
		CIN_Start(NULL, 0, atoi(DKCIN_Argv(1)));
		return true;
	}
	if (!stricmp(cmd, "cin_play_from_current_shot"))
	{
		if (DKCIN_Argc() == 3 && atoi(DKCIN_Argv(1)) >= 0 && atoi(DKCIN_Argv(2)) >= 1)
			CIN_Start(NULL, atoi(DKCIN_Argv(1)), atoi(DKCIN_Argv(2)));
		return true;
	}
	if (!stricmp(cmd, "cin_skip"))
	{	//FTE: its escape key's, from the player
		if (CIN_Running() == 1)
		{
			CIN_Stop(NULL);
			CIN_RemoveCinematicEntities();
		}
		return true;
	}
	return false;
}
