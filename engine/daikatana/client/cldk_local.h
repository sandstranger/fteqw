/*
Daikatana's client code on FTE's client: its engine's client headers (dkinc/, client.h and what it has), and what
of its client the ported files (cl_hud, cl_hudmsg, cl_tent, cl_fx, dk_beams...) need that's elsewhere in it, over
FTE's client (cldk_fte.h, cldk_main.cpp).
*/
#ifndef CLDK_LOCAL_H
#define CLDK_LOCAL_H

#include "client.h"
#include "l__language.h"
#include "cldk_fte.h"

//cl_scrn.cpp's
extern int		scr_draw_progress;
extern qboolean	override_showhuds;
extern void		*con_font;
extern cvar_t	*statusbar_alpha;
extern cvar_t	*hud_scale;
extern cvar_t	*cl_dmhints;

//its alphafx_ts (cl_scrn.cpp's, cl_main.cpp's)
extern alphafx_t	skill_hud;
extern alphafx_t	status_hud;

//cl_main.cpp's (client.h has the rest)
extern float	cl_gameover_alpha;
int			CIN_RunningWithView(void);	//dk_gce_main.h's

int			CL_Episode(void);	//the map's episode (FTE: its worldspawn's, CM_KeyValue(map_epairs, "episode"))
const char	*CL_ConfigModelName(int modelindex);

//its entities (cldk_ents.cpp)
extern int	cldk_pmflags, cldk_pmtype;	//its pmove's, the server's (svcdk_fte_stats)
extern float cldk_maxspeed;				//the player's, for its pmove (cldk_pred.cpp)
void		CLDK_ParseEntState(void);	//svcdk_fte_entstate

//the HUD (cl_hud.cpp)
void	SCR_RegisterStatus(void);
void	SCR_DrawStats(void);
qboolean check_weapon_hud(void);
void	CL_ParseSidekickWeaponStatus(void);

//its handlers (cl_hudmsg.cpp: cl_inventory.cpp's, cl_icons.cpp's, cl_sidekick.cpp's, cl_selector.cpp's...)
#include "cl_inventory.h"
#include "cl_icons.h"
#include "cl_sidekick.h"
#include "cl_selector.h"
qboolean CL_ValidateSelectorPos(int selection);
void	CL_ParseXPLevel(void);
void	CL_InformClientDeath(void);
void	CL_InformClientTheEnd(void);
void	CL_ParseSaveGem(void);
void	CL_ClientRespawn(void);
void	CL_ParseCrosshairMessage(void);
void	CL_ParseStartMusic(void);
void	CL_ParseStartMP3(void);
void	CL_ParseStopMP3(void);

#endif
