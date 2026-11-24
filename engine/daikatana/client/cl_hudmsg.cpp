/*
Daikatana's client's handlers for what its server sends its HUD: from its engine's client/ (cl_inventory.cpp,
cl_icons.cpp, cl_sidekick.cpp, cl_selector.cpp, and cl_main.cpp's and cl_parse.cpp's), over cldk_local.h.
Changes are marked FTE.
*/
#include "cldk_local.h"

int		xplevelup_mode;
int		xplevelup_selection;
float	xplevelup_timer;
qboolean	xplevelup_blink;
int		cl_death_gib;
float	cl_gameover_time, cl_gameover_alpha;
float	cl_theend_time, cl_theend_alpha;
int		cl_savegem_count;

//---- cl_inventory.cpp
//#include  "physics.h"

///////////////////////////////////////////////////////////////////////////////
//	defines
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
//	typedefs
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
//	globals
///////////////////////////////////////////////////////////////////////////////

invenIcon_t	inventoryIcons [CL_MAX_INVENTORY_ICONS];
int   numInventoryIcons;
int   inventory_mode;
int   inventory_selected;
float inventory_alpha;


void	inventory_init (void)
{
	//	reset inventory display times
	numInventoryIcons  = 0;
	inventory_mode     = INVMODE_OFF;  // by default
	inventory_selected = 0;
	inventory_alpha    = 0.0f;

	// reset hud defaults
	invmode_hud.alpha     = 0.0f;
	invmode_hud.direction = 0;
	invmode_hud.fade_time = 0.0f;
}


///////////////////////////////////////////////////////////////////////////////
//	inventory_ParseInventory
//
//	reads an svc_inventory message from the message buffer and sets up the
//	inventory list accordingly
///////////////////////////////////////////////////////////////////////////////

void	inventory_ParseInventory (void)
{
	int	i, modelIndex;
	qboolean bHideDisplay;

	bHideDisplay = MSG_ReadByte(&net_message); // hide display?
	qboolean bClear = MSG_ReadByte(&net_message);

	//Com_Printf("inventory_ParseInventory = %d\n",bHideDisplay);

	if (!bHideDisplay)  // if displaying the inventory contents
	{
		if (inventory_mode != INVMODE_ITEM_ICONS) // not open yet?
			S_StartLocalSound("global/inventory_open.wav"); //play open/close sound
		else
			S_StartLocalSound("global/inventory_update.wav"); //play up/down sound

		numInventoryIcons = 3;

		for (i = 0; i < numInventoryIcons; i++)
		{
			modelIndex = MSG_ReadShort(&net_message);
				
			inventoryIcons[i].modelindex = modelIndex;

			if (modelIndex != 0)
				inventoryIcons [i].model = cl.model_draw [modelIndex];
			else
				inventoryIcons [i].model = NULL;
		}

		inventory_mode = INVMODE_ITEM_ICONS; // set new hud mode

//		invmode_hud_setfade(1); // set up fade info
		if (scr_viewsize->value >= SCR_VIEWSIZE_NOSKILLS)
		{	
			invmode_hud.off_time = cl.time + 5000;
		}

		invmode_hud.direction = 1;       // alpha direction is towards fade out
		invmode_hud.fade_time = cl.time;  // start fade NOW / mode is reset when alpha reaches 0
	}
	else if (bClear && (inventory_mode == INVMODE_ITEM_ICONS))
	{
		for (i = 0; i < numInventoryIcons; i++)
			inventoryIcons [i].model = NULL;
	}
	// SCG[12/14/99]: control placed in world.dll/inventory.cpp
/*
	else
	{
		if (!check_weapon_hud()) // activate the weapon hud?
		{
			inventory_init(); // reset all vars
		}
	}
*/
}

void CL_InitWeaponIcons()
{
	int i;

	/* Preserve the icon data received from the server.  The weapon-icon
	   message can arrive before CL_PrepRefresh has registered all models.
	   The old memset here erased the model indices/names, so the icons
	   stayed empty until the next weapon-cycle message rebuilt them. */
	for (i = 0; i < numInventoryIcons; i++)
	{
		if (inventoryIcons[i].modelindex != 0)
			inventoryIcons[i].model = cl.model_draw[inventoryIcons[i].modelindex];
		else if (inventoryIcons[i].modelname[0])
		{
			inventoryIcons[i].modelindex = CL_ModelIndex(inventoryIcons[i].modelname);
			if (inventoryIcons[i].modelindex != 0)
				inventoryIcons[i].model = cl.model_draw[inventoryIcons[i].modelindex];
			else
				inventoryIcons[i].model = NULL;
		}
		else
			inventoryIcons[i].model = NULL;
	}
}

void CL_RefreshWeaponIconModels()
{
	int i;

	if (inventory_mode != INVMODE_WEAPON_ICONS)
		return;

	/* Rebind the renderer-facing model pointers every frame.  This covers
	   renderer/model-list refreshes without requiring another weapon-cycle
	   network message. */
	for (i = 0; i < numInventoryIcons; i++)
	{
		if (inventoryIcons[i].modelindex > 0 && inventoryIcons[i].modelindex < MAX_MODELS)
			inventoryIcons[i].model = cl.model_draw[inventoryIcons[i].modelindex];
	}
}

void CL_ParseWeaponIcons(void)
// read incomving svc_weapon_icon message from server
{
	int	i, modelIndex, count;
	byte hud_status;

	//Com_Printf("CL_ParseWeaponIcons()\n");

	hud_status = MSG_ReadByte(&net_message); // display on/off

	if (!hud_status) // force hud off?
	{
		invmode_hud.direction = -1;       // alpha direction is towards fade out
		invmode_hud.fade_time = cl.time;  // start fade NOW / mode is reset when alpha reaches 0

		return; // abort
	}

	numInventoryIcons = 6; // show 6 a total of 6 weapons

	for (i = 0; i < numInventoryIcons; i++)
	{
		modelIndex = MSG_ReadShort(&net_message);	// model index

		inventoryIcons[i].modelindex = modelIndex;

		if (modelIndex != 0)
		{
			inventoryIcons[i].model = cl.model_draw[modelIndex];
			strncpy( inventoryIcons[i].modelname, cl.configstrings[CS_MODELS+modelIndex], MAX_QPATH );
		}
		else
		{
			inventoryIcons[i].model = NULL;
		}

//		inventoryIcons[i].count = MSG_ReadShort(&net_message); // ammo count
		count = MSG_ReadShort(&net_message);
		if (count == -1)
		{
			if (inventoryIcons[i].count <= 0)
				inventoryIcons[i].count = count;
		}
		else 
			inventoryIcons[i].count = count;
	}

	inventory_selected = MSG_ReadShort(&net_message); // which # is active


	// set flag indicating to draw weapon hud/icons
	inventory_mode = INVMODE_WEAPON_ICONS;            // set new hud mode

	if (scr_viewsize->value >= SCR_VIEWSIZE_NOSKILLS)
	{	
		invmode_hud.off_time = cl.time + 5000;
	}

	invmode_hud.direction = 1;       // alpha direction is towards fade out
	invmode_hud.fade_time = cl.time;  // start fade NOW / mode is reset when alpha reaches 0
}
             
void CL_ParseAmmoUpdate()
{
	byte index = MSG_ReadByte(&net_message);
	short count = MSG_ReadShort(&net_message);
	if ((index < 0) || (index >= 6)) 
		return;

	inventoryIcons[index].count = count;
}
//---- cl_icons.cpp
///////////////////////////////////////////////////////////////////////////////
//	cl_icons.cpp
//
//	onscreen icon setup
//
///////////////////////////////////////////////////////////////////////////////

//#include	<windows.h>


///////////////////////////////////////////////////////////////////////////////
//	defines
///////////////////////////////////////////////////////////////////////////////

#define BOOST_NAME_LEFT      (0)
#define ICON_BLINK_DELAY     (500) // half second
#define ICON_HALF_CENTER     (36)	 // x offset

///////////////////////////////////////////////////////////////////////////////
//	globals
///////////////////////////////////////////////////////////////////////////////

extern void *statbar_font; // font used for display

onscreen_icons_info_t onscreen_icons_info;  


//
// functions
//

void CL_Boost_Icons_Init(void)
// initialize global structure
{
	onscreen_icons_info.num_icons = 0;

	for (int i = 0; i < MAX_ONSCREEN_ICONS; i++)
		memset(&onscreen_icons_info.icons[i],0x00,sizeof(onscreen_icons_t));
}




void CL_Boost_Icons_Think(void)
// calculate/check display time
{
	int i;
	onscreen_icons_t icon;
	DRAWSTRUCT drawStruct;
	int sx, sy/*, len*/;
	char buffer[MAX_QPATH];

	if (onscreen_icons_info.num_icons == 0)       // look before you leap
		return;

	if (statbar_font == NULL)
		statbar_font = re.LoadFont("statbar_font"); // load/check font

	sx = BOOST_NAME_LEFT;    // x at left
	sy = viddef.height - 260; // y at top of screen

	for (i = 0;i < MAX_ONSCREEN_ICONS;i++)
	{
		icon = onscreen_icons_info.icons[i]; 

		if (icon.models[0] != NULL)   // model exists?
		{
			if (icon.blink)             // model is in blink mode?
			{
				if (icon.blink_time == 0) // start blink not set yet
				{
					onscreen_icons_info.icons[i].blink_time = cl.time + ICON_BLINK_DELAY;
					onscreen_icons_info.icons[i].alpha = 0.0; // start off inviso
				}
				else
				{
					if (cl.time > icon.blink_time)  // time to flip blink?
					{
						if (icon.alpha == 0.0f)                       // is it inviso?
						{
							onscreen_icons_info.icons[i].alpha = 1.0f;  // make it visible

							//S_StartSoundQuake2(CVector(0,0,0), 0, CHAN_AUTO, S_RegisterSound("global/e_teleportstart.wav"), 1, ATTN_NORM, 0);
							S_StartLocalSound("global/e_teleportstart.wav");
						}
						else
							onscreen_icons_info.icons[i].alpha = 0.0f;  // make it inviso

						// set new blink time
						onscreen_icons_info.icons[i].blink_time = cl.time + ICON_BLINK_DELAY; 
					}
				}
			}  

			// no skills interface visible?
			if (scr_viewsize->value >= SCR_VIEWSIZE_NOSKILLS) 
			{
				// draw text so players know what skill icons are which
// SCG[1/16/00]: 				sprintf(buffer,"%s",tongue_statbar[T_SKILLS_POWER + i]);
				Com_sprintf(buffer,sizeof( buffer), "%s",tongue_statbar[T_SKILLS_POWER + i]);

				// determine pixel width to center the string
				sx = ICON_HALF_CENTER - (re.StringWidth(buffer, statbar_font, -1) >> 1);

//				len = strlen(buffer);

				drawStruct.nFlags = DSFLAG_BLEND | DSFLAG_NO_N64FILTER;
				drawStruct.szString = buffer;
				drawStruct.nXPos = sx;
				drawStruct.nYPos = sy;
				drawStruct.pImage = statbar_font;
				re.DrawString( drawStruct );
			}
		}

		sy += 36;  // increase y spacing
	}
}


void CL_Boost_Icons(void)
// retrieve net message from server to add to client icon list
{
	byte icon_action, icon_type;
	int num_icons, display_time;
	int model_index1, model_index2; 

	icon_action  = MSG_ReadByte(&net_message);  // retrieve action 
	icon_type    = MSG_ReadByte(&net_message);  // retrieve icon type
	display_time = MSG_ReadShort(&net_message); // retrieve display time

	num_icons = onscreen_icons_info.num_icons;

	// determine desired action 

	if (icon_action == BOOSTICON_ADD) // adding icon?
	{
		switch (icon_type)
		{
		case ICON_POWERBOOST  :
			model_index2 = CL_ModelIndex("models/global/a_pwrb.dkm"); // match artifacts.cpp model names
			model_index1 = CL_ModelIndex("models/global/a_pwrc.dkm");
			break;

		case ICON_ATTACKBOOST :
			model_index2 = CL_ModelIndex("models/global/a_atkb.dkm");
			model_index1 = CL_ModelIndex("models/global/a_atkc.dkm");
			break;

		case ICON_SPEEDBOOST  :
			model_index2 = CL_ModelIndex("models/global/a_spdb.dkm");
			model_index1 = CL_ModelIndex("models/global/a_spdc.dkm");
			break;

		case ICON_ACROBOOST   :
			model_index2 = CL_ModelIndex("models/global/a_acrb.dkm");
			model_index1 = CL_ModelIndex("models/global/a_acrc.dkm");
			break;

		case ICON_VITABOOST   :
			model_index2 = CL_ModelIndex("models/global/a_vtlb.dkm");
			model_index1 = CL_ModelIndex("models/global/a_vtlc.dkm");
			break;

		default :
			model_index2 = 0;  // no icon type found
			model_index1 = 0;
			break;
		}

		icon_type--; // to set 0 based
		_ASSERTE(icon_type >= 0 && icon_type < MAX_ONSCREEN_ICONS);     // check for bogus server value

		// set model 1 references, if one exists
		if (model_index1 > 0) 
		{
			onscreen_icons_info.icons[icon_type].models[0] = cl.model_draw[model_index1];
			onscreen_icons_info.icons[icon_type].models[1] = NULL;

			onscreen_icons_info.icons[icon_type].blink      = false;
			onscreen_icons_info.icons[icon_type].blink_time = 0;

			onscreen_icons_info.icons[icon_type].alpha      = 1.0f;

			// set model 2 references, if one exists
			if (model_index2 > 0) 
				onscreen_icons_info.icons[icon_type].models[1] = cl.model_draw[model_index2];
		}
		else
		{
			onscreen_icons_info.icons[num_icons].models[0] = NULL; // no go, dimaggio
			onscreen_icons_info.icons[num_icons].models[1] = NULL;
		}

		onscreen_icons_info.num_icons++;  // increment # of icons
	}
	else if (icon_action == BOOSTICON_DELETE) // delete/remove icon?
	{
		if (num_icons > 0)
			onscreen_icons_info.num_icons--;       // decrement # of current icons

		memset(&onscreen_icons_info.icons[icon_type - 1],0x00,sizeof(onscreen_icons_t));  // reset icon info
	}
	else if (icon_action == BOOSTICON_BLINK) // blink icon?
	{
		if (num_icons > 0)  // icons exists security
		{
			onscreen_icons_info.icons[icon_type - 1].blink = true;    // icon will be blinking
			onscreen_icons_info.icons[icon_type - 1].blink_time = 0;  // reset blink timer
		}
	}

}


//---- cl_sidekick.cpp



///////////////////////////////////////////////////////////////////////////////
//	defines
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
//	globals
///////////////////////////////////////////////////////////////////////////////

int sidekickcommand_selected;


///////////////////////////////////////////////////////////////////////////////
//	functions
///////////////////////////////////////////////////////////////////////////////

void sidekickcommand_Init(void)
{
	//	reset vars
	sidekickcommand_selected = 0;
}


void CL_Sidekick_Update(void)
{
	byte sidekick_whom   = MSG_ReadByte (&net_message);  // retrieve which sidekick
	int  sidekick_status = MSG_ReadShort(&net_message);  // activate or deactivate
	int  val             = MSG_ReadShort(&net_message);  // retrieve percentage value (100%)
	
	//Com_Printf("CL_Sidekick_Update, whom=%d, status=%d, perc=%d\n",sidekick_whom,sidekick_status,val);

	if( sidekick_status != SIDEKICK_STATUS_DEACTIVATE )
	{
		inventory_mode = INVMODE_SIDEKICK;
	}

	// exclusive messages only
	switch (sidekick_status)
	{
    case SIDEKICK_STATUS_ACTIVATE :
		cl.sidekicks_active |= sidekick_whom;
		break;
		
    case SIDEKICK_STATUS_DEACTIVATE :
		cl.sidekicks_active &= ~sidekick_whom;
		break;
		
    case SIDEKICK_STATUS_HEALTH :
		if (sidekick_whom & SIDEKICK_MIKIKO)
			cl.sidekick_mikiko_health = val;
		else
			cl.sidekick_superfly_health = val;
		break;
		
    case SIDEKICK_STATUS_ARMOR :
		if (sidekick_whom & SIDEKICK_MIKIKO)
			cl.sidekick_mikiko_armor = val;
		else
			cl.sidekick_superfly_armor = val;
		break;
		
	case SIDEKICK_STATUS_COMMANDING :
		cl.sidekick_commanding = sidekick_whom;
		break;
	}
	
}





void sidekicks_active_change(alphafx_t * flap, int old_status, int flag)
// adjust sidekick info for activity changes
{
	// check if sidekick has left PVS for first time
	if (old_status & flag)                // previously was in PVS
	{
		if (!(cl.sidekicks_active & flag))  // now gone...
		{
			// set up fader values
			flap->fade_time = 1.0f;           // display active
			flap->direction = -1;             // fading out
		}
	}
	// check if mikiko has entered PVS for first time
	else if (!(old_status & flag))             // previously not in PVS
	{
		if (cl.sidekicks_active & flag)     // now active...
		{
			// set up fader values
			flap->fade_time = 1.0f;           // display active
			flap->direction = 1;              // fading in
		}
	}
}




void sidekicks_active_update(int old_status)
// compare status of previous sidekicks actives versus current active
// sets special fx for interface graphics (alpha faders)
{
	if (old_status != cl.sidekicks_active)             // actives differ?
	{
		sidekicks_active_change(&flap_mikiko,   old_status, SIDEKICK_MIKIKO);
		sidekicks_active_change(&flap_superfly, old_status, SIDEKICK_SUPERFLY); 
	}
	
}
//---- cl_selector.cpp



///////////////////////////////////////////////////////////////////////////////
//	defines
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
//	globals
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
//	functions
///////////////////////////////////////////////////////////////////////////////
extern cvar_t *sv_episode;
qboolean CL_ValidateSelectorPos(int selection)
{
	if (cl.refdef.rdflags & RDF_DEATHMATCH)
		return TRUE;

	int episode = CL_Episode()/*FTE*/;//floor(sv_episode->value + 0.5);				// make sure its good!

	unsigned long val = 0;
	switch (selection)
	{
	case 0 : val = cl.frame.playerstate.stats[STAT_POWER];         break; // power
	case 1 : val = cl.frame.playerstate.stats[STAT_ATTACK];        break; // attack
	case 2 : val = cl.frame.playerstate.stats[STAT_SPEED];         break; // speed
	case 3 : val = cl.frame.playerstate.stats[STAT_JUMP];          break; // acro
	case 4 : val = cl.frame.playerstate.stats[STAT_MAX_HEALTH];    break; // vitality
	default: return FALSE;
	}

	return (val >= (episode + 1)) ? FALSE : TRUE;
}

void CL_ParseSelectorCommand(void)
// read incoming svc_selector message from server
{
	int  cmd_direction;
	byte cmd_activate;
	char buf_command[256];
	
	
	cmd_direction = MSG_ReadShort(&net_message); // direction
	cmd_activate  = MSG_ReadByte (&net_message);  // activating/applying command?
	
	//Com_Printf("CL_ParseSelectorCommand(%d,%d)\n",cmd_direction,cmd_activate);
	
	
	// branch, depending on mode
	
	if (xplevelup_mode)        // adjusting skill points on level up?
	{
		if (cmd_activate)        // applying skill point?
		{
			if (CL_ValidateSelectorPos(xplevelup_selection))
			{
				if (scr_viewsize->value >= SCR_VIEWSIZE_NOSKILLS &&    // is screen size disabling skill display
					!override_showhuds)                                // and not showing all huds?
				{
					skill_hud.direction = -1;      // start fade
					skill_hud.fade_time = cl.time; // fade NOW
				}
				
				// send command to server and pass selected skill point
				
				Com_sprintf(buf_command,sizeof(buf_command),"xplevelup %d\n",xplevelup_selection);
				Cbuf_AddText(buf_command);
				
				// reset vars
				xplevelup_mode      = false;
				xplevelup_selection = 0;
				xplevelup_timer     = 0;
				xplevelup_blink     = false;
				
				S_StartLocalSound("global/skill_increase.wav");  // play apply/de-activate sound
			}
			else
			{
				for (int i = 0; i < MAX_SKILLS; i++)
				{
					if (CL_ValidateSelectorPos(i))
					{
						xplevelup_selection = i;
						break;
					}
				}
			}
		}
		else                              // moving selector up/down
		{
			int i = 0;
			do
			{
				xplevelup_selection += cmd_direction;  // apply direction to current selection
				
				if (xplevelup_selection == MAX_SKILLS) // flip 
					xplevelup_selection = 0;
				else if (xplevelup_selection == -1) // boundary check
					xplevelup_selection = (MAX_SKILLS - 1);     // flip
			} while ((i++ < MAX_SKILLS) && (!CL_ValidateSelectorPos(xplevelup_selection)));
				
				S_StartLocalSound("global/skill_selector.wav");  // play up/down sound
		}
		
	}
	else
	{
		// assume sideick command handling
		if (cl.refdef.rdflags & RDF_COOP)
			return;
		
		if (cl.sidekicks_active) // any sidekicks available?
		{
			if (cmd_activate)  // activating sidekick command?
			{
				// activating sidekick command and interface already open?
				if (inventory_mode == INVMODE_SIDEKICK)  
				{
					// need to make sure these commands are equivalent to those found in world.dll
					switch (sidekickcommand_selected)
					{
					case 0 : Com_sprintf(buf_command,sizeof(buf_command),"sidekick_get\n");				break;
					case 1 : Com_sprintf(buf_command,sizeof(buf_command),"sidekick_come\n");			break;
					case 2 : Com_sprintf(buf_command,sizeof(buf_command),"sidekick_stay\n");            break;
					case 3 : Com_sprintf(buf_command,sizeof(buf_command),"sidekick_attack\n");          break;
					case 4 : Com_sprintf(buf_command,sizeof(buf_command),"sidekick_backoff\n");         break;
					default: memset( buf_command, 0, 256 ); break;
					}

					Cbuf_AddText(buf_command); // send command to server
					
					// play activate sound
					S_StartLocalSound("global/skill_increase.wav");
				}
				else // else just open the interface if 'apply' button is hit
				{
					S_StartLocalSound("global/skill_start.wav"); // play initial sound
				}
			}
			else
			{
				// not activating -- adjust sidekick selector
				sidekickcommand_selected += cmd_direction;
				
				if (sidekickcommand_selected == -1)                     // boundary check
					sidekickcommand_selected = NUM_SIDEKICKCOMMANDS - 1;  // flip
				else
					if (sidekickcommand_selected == NUM_SIDEKICKCOMMANDS) // boundary check
						sidekickcommand_selected = 0;                       // flip 
					
					S_StartLocalSound("global/skill_selector.wav"); // play up/down sound
			}
			
			
			// set flag indicating to draw sidekick command 
			inventory_mode = INVMODE_SIDEKICK;                    // set new hud mode
			
			if (scr_viewsize->value >= SCR_VIEWSIZE_NOSKILLS)
			{	
				invmode_hud.off_time = cl.time + 5000;
			}

			invmode_hud.direction = 1;       // alpha direction is towards fade out
			invmode_hud.fade_time = cl.time;  // start fade NOW / mode is reset when alpha reaches 0
			
			// any sidekick in 'command'?
			if (!cl.sidekick_commanding)
			{
				if (cl.sidekicks_active & SIDEKICK_MIKIKO)
					cl.sidekick_commanding = 2; // ttd: need global here!
				else
					if (cl.sidekicks_active & SIDEKICK_SUPERFLY)
						cl.sidekick_commanding = 3; // ttd: need global here!
			}
		}
		
	}
	
}



//---- cl_main.cpp
void CL_ParseXPLevel(void)
// retrieve svc_xplevel server message
{
	xplevelup_mode      = true;
	xplevelup_selection = 0;
	xplevelup_timer     = cl.time + XPLEVELUP_BLINKTIME;
	xplevelup_blink     = false;
	
	for (int i = 0; i < MAX_SKILLS; i++)
	{
		if (CL_ValidateSelectorPos(i))
		{
			xplevelup_selection = i;
			break;
		}
	}

	skill_hud.direction = 1; // start fade-in of skill bar, if not already showing
	
	S_StartLocalSound("global/skill_start.wav");  // play activate sound
}

void CL_InformClientDeath(void)
// server informs client of client/sidekick death
{
	cl_death_gib = MSG_ReadByte(&net_message); // retrieve total damage

	// don't do this all over again!
	if (cl_gameover_time)
		return;

	// reset last map name so when the map reloads soit won't be treated as
	// a level transition
	Cvar_ForceSet( "cl_lastmapname", "none" ); 

	CL_Boost_Icons_Init(); // reset power boosts if active

	cl_gameover_time  = cl.time + 2000; // time before fadein gameover graphic
	cl_gameover_alpha = 0.0f;
}

void CL_InformClientTheEnd(void)
{
	// don't do this all over again!
	if (cl_theend_time)
		return;

	cl_theend_time  = cl.time;
	cl_theend_alpha = 0.0f;
}
//---- cl_parse.cpp
void CL_ParseSaveGem(void)
// interpret svc_savegem server message
{
	char buf[32];
	byte gem_activate;
	
//	Com_Printf("CL_ParseSaveGem()\n");
	
	gem_activate      = MSG_ReadByte(&net_message);  // 'using' the savegem?  true/false
	cl_savegem_count  = MSG_ReadByte(&net_message);  // store # of savegems collected
	
	if (gem_activate) // using the savegem?
	{
//		sprintf(buf,"menu_main_force %d\n", (int)DKMB_SAVEGAME);  // summon the main menu with save menu as first
		Com_sprintf(buf,sizeof(buf), "menu_main_force %d\n", (int)DKMB_SAVEGAME);  // summon the main menu with save menu as first
		Cbuf_AddText (buf); 
	}
}

void CL_ClientRespawn(void)
// interpret svc_clientrespawn server message
// re-initialize client side variables regarding respawn
{
	cl_death_gib = false;
	
	cl_gameover_time  = 0.0f;
	cl_gameover_alpha = 0.0f;
}

void CL_ParseCrosshairMessage( void )
{
	byte	show_crosshair;
	
	show_crosshair = MSG_ReadByte( &net_message );
	
	if (show_crosshair)
		Cvar_Set("cv_crosshair_enabled", "1" );
	else
		Cvar_Set("cv_crosshair_enabled", "0" );
}

void CL_ParseStartMusic(void)
{
#ifdef _DEBUG
	SHOWNET("CL_ParseStartMusic");
#endif /* DEBUG */

	char *name = MSG_ReadString(&net_message);
	int chan = MSG_ReadShort(&net_message);

	S_StartMusic(name,chan);
}

void CL_ParseStartMP3(void)
{
#ifdef _DEBUG
	SHOWNET("CL_ParseStartMP3");
#endif /* DEBUG */

	char *name = MSG_ReadString(&net_message);
	int chan = MSG_ReadShort(&net_message);
	float vol = MSG_ReadFloat(&net_message);
	int play_count = MSG_ReadShort(&net_message);
	qboolean bRestart = MSG_ReadByte(&net_message) ? TRUE : FALSE;

	S_StartMP3(name, chan, vol, play_count, bRestart);
}

void CL_ParseStopMP3(void)
{
#ifdef _DEBUG
	SHOWNET("CL_ParseStopMP3");
#endif /* DEBUG */

	int chan = MSG_ReadShort(&net_message);
	S_StopMP3(chan);
}