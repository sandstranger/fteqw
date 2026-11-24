/*
Daikatana's client's HUD: from its engine's client/cl_scrn.cpp (SCR_DrawStats and what it draws), over cldk_local.h.
Changes are marked FTE.
*/
#include "cldk_local.h"

#define INFO_ALPHA(alpha)			((!scr_draw_progress) && (alpha > 0.0f) ? 0.7 : 0)
#define MODEL_ALPHA(alpha)			((!scr_draw_progress) && (alpha > 0.0f) ? 1.0 : 0)
void* sbar_font_g = NULL;     // green font
void* sbar_font_r = NULL;     // red font
void* statbar_font = NULL;    // status bar font
static bool scr_hud_scale_active = false;
static float scr_hud_scale_value = 1.0f;
alphafx_t	flap_mikiko;
alphafx_t	flap_superfly;
alphafx_t	invmode_hud;
alphafx_t	skill_hud;
alphafx_t	status_hud;


#define STATUSBAR_POWER2              (128)       // assume pix are 128x128
#define STATUSBAR_FONT_POWER2         (16)        // assume font pix are 16x16
#define NUM_STATUSBAR_FACES           (47)
#define NUM_STATUSBAR_ICONS           (5)
#define STATUSBAR_FACES_FRAMES        (8)

#define STATUSBAR_FACES_INVULNERABLE  (6)
#define STATUSBAR_FACES_INVISIBLE     (7)
#define STATUSBAR_FACES_FLAP_MIKIKO   (24)
#define STATUSBAR_FACES_FLAP_SUPERFLY (31)
#define STATUSBAR_FACES_FLAP_HIRO	  (38)
#define STATUSBAR_FACES_FLAP_INVIS	  (5)
#define STATUSBAR_FACES_FLAP_INVUL	  (6)
#define STATUSBAR_HIRO_NAME			  (45)
#define STATUSBAR_BLANK_FLAP		  (46)



typedef struct sb_info_s
{
	short x_add, y_add;      // image is centered on bottom of screen -- x,y is added to that position
} sb_info_t;


char* statusbar_faces[NUM_STATUSBAR_FACES] =
{
	"pics/statusbar/bottom_2_0a.tga",	// hiro, full health
	"pics/statusbar/bottom_2_0b.tga",
	"pics/statusbar/bottom_2_0c.tga",
	"pics/statusbar/bottom_2_0d.tga",
	"pics/statusbar/bottom_2_0e.tga",
	"pics/statusbar/bottom_2_0f.tga", // dead
	"pics/statusbar/bottom_2_0g.tga", // invulnerable
	"pics/statusbar/bottom_2_0h.tga", // invisible

	"pics/statusbar/bottom_2_1a.tga", // mikiko, full health
	"pics/statusbar/bottom_2_1b.tga",
	"pics/statusbar/bottom_2_1c.tga",
	"pics/statusbar/bottom_2_1d.tga",
	"pics/statusbar/bottom_2_1e.tga",
	"pics/statusbar/bottom_2_1f.tga", // dead
	"pics/statusbar/bottom_2_1g.tga", // invulnerable
	"pics/statusbar/bottom_2_1h.tga", // invisible

	"pics/statusbar/bottom_2_2a.tga", // superfly, full health
	"pics/statusbar/bottom_2_2b.tga",
	"pics/statusbar/bottom_2_2c.tga",
	"pics/statusbar/bottom_2_2d.tga",
	"pics/statusbar/bottom_2_2e.tga" ,
	"pics/statusbar/bottom_2_2f.tga", // dead
	"pics/statusbar/bottom_2_2g.tga", // invulnerable
	"pics/statusbar/bottom_2_2h.tga", // invisible

	"pics/statusbar/mikikoa.tga", 		//24 mikiko sidekick flap, full health
	"pics/statusbar/mikikob.tga",
	"pics/statusbar/mikikoc.tga",
	"pics/statusbar/mikikod.tga",
	"pics/statusbar/mikikoe.tga",
	"pics/statusbar/mikikof.tga",		// invisible
	"pics/statusbar/mikikog.tga",		// invulnerable

	"pics/statusbar/superflya.tga",   //31 superfly sidekick flap, full health
	"pics/statusbar/superflyb.tga",
	"pics/statusbar/superflyc.tga",
	"pics/statusbar/superflyd.tga",
	"pics/statusbar/superflye.tga",
	"pics/statusbar/superflyf.tga",		// invisible
	"pics/statusbar/superflyg.tga",		// invulnerable

	"pics/statusbar/hiroa.tga", 		//38 hiro sidekick flap, full health
	"pics/statusbar/hirob.tga",
	"pics/statusbar/hiroc.tga",
	"pics/statusbar/hirod.tga",
	"pics/statusbar/hiroe.tga",
	"pics/statusbar/hirof.tga",		// invisible
	"pics/statusbar/hirof.tga",		// invulnerable

	"pics/statusbar/hirotext.tga",
	"pics/statusbar/blank.tga"
};


char* statusbar_icons[NUM_STATUSBAR_ICONS] =
{
	"pics/statusbar/armoricon.bmp",
		"pics/statusbar/healthicon.bmp",
		"pics/statusbar/ammoicon.bmp",
		"pics/statusbar/fragicon.bmp",
		"pics/statusbar/explevicon.bmp",
};

enum e_sidekick_ammo_icons
{
	SIDEKICK_AMMO_BORDER = 0,
	SIDEKICK_AMMO_NADA,
	SIDEKICK_AMMO_LOW,
	SIDEKICK_AMMO_GOOD,
	SIDEKICK_AMMO_ROCKIN
};

static char* sidekick_ammo_icons[] =
{
	"pics/statusbar/ammo.tga",
	"pics/statusbar/ammo4.tga",
	"pics/statusbar/ammo3.tga",
	"pics/statusbar/ammo2.tga",
	"pics/statusbar/ammo1.tga"
};


int bottom_mainbar_offs[4] = { -158, 98, 226, -30 }; // piece 2 & 4 reversed

// used for number stat font placement
sb_info_t sb_info[] = {
	//    x    y
	{  -162, -35 }, // armor
	{   -71, -35 }, // health
	{    82, -35 }, // ammo
	{   172, -35 }, // frags/exp

};

// used for icon placement
sb_info_t sb_info_icon[] = {
	//    x    y
	{  -144, -64 },  // armor
	{   -54, -67 },  // health
	{    98, -66 },  // ammo
	{   196, -64 }   // frags/exps
};


// used for font placement
sb_info_t sb_info_text[] = {
	//    x    y
	{  -122, -22 },  // armor
	{   -40, -22 },  // health
	{   126, -22 },  // ammo
	{   212, -22 }   // frags/exps
};



// warning levels for primary stats (floor/ceiling values)
//
int stat_warning[4][2] = { 0,  25,   // armor 
-99,  25,   // health
0,   5,   // ammo
-99,  -1,   // frags
};


void SCR_RegisterStatus(void)
// register main status bar bitmaps
{
	int	i;

	for (i = 0;i < NUM_STATUSBAR_FACES;i++)
		re.RegisterPic(statusbar_faces[i], NULL, NULL, RESOURCE_EPISODE);

	for (i = 0;i < NUM_STATUSBAR_ICONS;i++)
		re.RegisterPic(statusbar_icons[i], NULL, NULL, RESOURCE_EPISODE);

	re.RegisterPic("pics/statusbar/bottom_1.tga", NULL, NULL, RESOURCE_EPISODE);
	re.RegisterPic("pics/statusbar/bottom_3.tga", NULL, NULL, RESOURCE_EPISODE);
	re.RegisterPic("pics/statusbar/bottom_4.tga", NULL, NULL, RESOURCE_EPISODE);
}



void global_hud_updates(alphafx_t* hud, float* model_alpha, float max_alpha)
// update invmode hud settings
{
	if (hud->off_time && cl.time > hud->off_time)
	{
		hud->off_time = 0;
		hud->direction = -1;
		hud->fade_time = cl.time;
		return;
	}

	if (hud->direction == 0)
	{
		if (hud->alpha > 0.0f)
			hud->alpha = max_alpha;
		else
			hud->alpha = 0.0f;
		return;
	}

	if (hud->direction == 1)                       // fading in? 
	{
		//		hud->alpha += FLAP_ALPHA_RATE_DOUBLE;        // increase alpha, double-time
		hud->alpha += 2 * FLAP_ALPHA_RATE;        // increase alpha, double-time

		if (hud->alpha >= max_alpha)                  // alpha level reached maximum?
		{
			hud->alpha = max_alpha;                // insure maximum
			hud->direction = 0;                        // stabilize alpha
			//hud->fade_time = 0.0f;                   // do not set this, it holds fadeout time
		}

		//		*model_alpha = hud->alpha;                   // to affect 3D model alphas
		*model_alpha = MODEL_ALPHA(hud->alpha);
	}
	else
	{
		if (hud->fade_time && cl.time > hud->fade_time)    // fading out?
		{
			//			hud->alpha -= FLAP_ALPHA_RATE;               // decrease alpha
			hud->alpha -= 2 * FLAP_ALPHA_RATE;               // decrease alpha

			if (hud->alpha <= 0)                          // alpha level reached minimum?
			{
				hud->alpha = 0.0f;                     // insure minimum
				hud->direction = 0;                        // stabilize alpha
				hud->fade_time = 0.0f;                     // reset time
				hud->off_time = 0;

				//				if (invmode_hud.alpha <= 0)
				//					inventory_init(); // reset all vars
			}

			//			*model_alpha = hud->alpha;                 // to affect 3D model alphas
			*model_alpha = MODEL_ALPHA(hud->alpha);
		}
	}

}




void invmode_hud_setfade(int dir)
// set hud mode info 
{
	invmode_hud.direction = dir;
	invmode_hud.fade_time = cl.time + INVMODE_FADETIME;  // set time before fade starts
}



#define SIDEKICK_HEALTHARMOR_BAR_Y      (43)
#define SIDEKICK_ARMOR_BAR_X_OFFSET     (15)
#define SIDEKICK_HEALTHARMOR_BAR_WIDTH   (3)

#define SIDEKICK_BAR_CRUNCHSCALE       (2.8) // compact bar (adjust y base with SIDEKICK_HEALTHARMOR_BAR_Y)

static void SCR_HUDDrawPic(DRAWSTRUCT& drawStruct);

void sidekick_draw_healtharmor_bar(int left, int health, int armor, float alpha)
// draw sidekick's health and armor bars
{
	DRAWSTRUCT drawStruct;
	int bar_height;
	int i, val;


	// pixel draw
	drawStruct.pImage = NULL;
	drawStruct.nFlags = DSFLAG_FLAT | DSFLAG_COLOR | DSFLAG_ALPHA;
	drawStruct.nLeft = left;
	drawStruct.nRight = SIDEKICK_HEALTHARMOR_BAR_WIDTH;
	drawStruct.fAlpha = alpha;

	// draw health and armor bars
	for (i = 0;i < 2;i++)
	{
		drawStruct.nTop = viddef.height - SIDEKICK_HEALTHARMOR_BAR_Y;

		if (i == 0) // health first
		{
			val = health;
			drawStruct.rgbColor.Set(1, 0, 0); // set rgb red color for heatlh
		}
		else        // armor second
		{
			val = armor;
			drawStruct.rgbColor.Set(1, 1, 0); // set rgb yellow color for armor
		}

		if (val < 0) // check lower boundary
			val = 0;
		if (val > 100)
			val = 100;	// check upper boundary too.

		bar_height = (100 / SIDEKICK_BAR_CRUNCHSCALE) - (val / SIDEKICK_BAR_CRUNCHSCALE);

		drawStruct.nTop += bar_height;                         // new start position of bar
		drawStruct.nBottom = (100 / SIDEKICK_BAR_CRUNCHSCALE) - bar_height;   // height of bar

		SCR_HUDDrawPic(drawStruct);

		// adjust x for armor bar
		drawStruct.nLeft += SIDEKICK_ARMOR_BAR_X_OFFSET;
	}

}





#define SKILLS_LABEL_TOP           (274)
#define SKILLS_WINDOW_TOP          (SKILLS_LABEL_TOP - 14)
#define SKILLS_LABEL_SPACING       (36)
#define SKILLS_CATEGORY_LABEL_TOP  (100)

#define SKILLS_NODULE_LEFT         (12)
#define SKILLS_NODULE_WIDTH        (6)
#define SKILLS_NODULE_HEIGHT       (8)
#define SKILLS_NODULE_SPACING      (8)
#define SKILLS_Y_OFFSET            (10)

//extern cvar_t *sv_episode;
static int SCR_HUDScaleCoord(float value)
{
	return (int)(value * scr_hud_scale_value + (value >= 0.0f ? 0.5f : -0.5f));
}

static int SCR_HUDTransformX(int value)
{
	float width = (float)viddef.width;
	float x = (float)value;

	/* Preserve the vanilla anchor zones: left, centre, and right. */
	if (x <= width * 0.25f)
		return SCR_HUDScaleCoord(x);
	if (x >= width * 0.75f)
		return (int)(width - (width - x) * scr_hud_scale_value + 0.5f);

	return (int)(width * 0.5f + (x - width * 0.5f) * scr_hud_scale_value + 0.5f);
}

static int SCR_HUDTransformY(int value)
{
	float height = (float)viddef.height;
	float y = (float)value;

	/* The game HUD is bottom-anchored.  In particular, the weapon selector
	   sits above the bottom edge and must grow upward when scaled. */
	if (y <= height * 0.25f)
		return SCR_HUDScaleCoord(y);

	return (int)(height - (height - y) * scr_hud_scale_value + 0.5f);
}

static int SCR_HUDScaleSize(int value)
{
	int scaled = (int)((float)value * scr_hud_scale_value + 0.5f);
	return scaled > 0 ? scaled : 1;
}

static void SCR_HUDPrepareDraw(DRAWSTRUCT& drawStruct)
{
	if (!scr_hud_scale_active)
		return;

	if (drawStruct.nFlags & DSFLAG_FLAT)
	{
		/* Flat primitives use nLeft/nTop as their actual screen position. */
		drawStruct.nLeft = SCR_HUDTransformX(drawStruct.nLeft);
		drawStruct.nTop = SCR_HUDTransformY(drawStruct.nTop);
		drawStruct.nRight = SCR_HUDScaleSize(drawStruct.nRight);
		drawStruct.nBottom = SCR_HUDScaleSize(drawStruct.nBottom);
		return;
	}

	drawStruct.nXPos = SCR_HUDTransformX(drawStruct.nXPos);
	drawStruct.nYPos = SCR_HUDTransformY(drawStruct.nYPos);

	if (drawStruct.nFlags & DSFLAG_SCALE)
	{
		drawStruct.fScaleX *= scr_hud_scale_value;
		drawStruct.fScaleY *= scr_hud_scale_value;
	}
	else
	{
		drawStruct.nFlags |= DSFLAG_SCALE;
		drawStruct.fScaleX = scr_hud_scale_value;
		drawStruct.fScaleY = scr_hud_scale_value;
	}
}

static void SCR_HUDDrawPic(DRAWSTRUCT& drawStruct)
{
	if (!scr_hud_scale_active)
	{
		DRAWSTRUCT hudDraw = drawStruct;
		hudDraw.nFlags |= DSFLAG_NO_N64FILTER;
		re.DrawPic(hudDraw);
		return;
	}

	DRAWSTRUCT scaled = drawStruct;
	SCR_HUDPrepareDraw(scaled);
	scaled.nFlags |= DSFLAG_NOMIP | DSFLAG_CLAMP | DSFLAG_NO_N64FILTER;
	re.DrawPic(scaled);
}

static void SCR_HUDDrawString(DRAWSTRUCT& drawStruct)
{
	if (!scr_hud_scale_active)
	{
		DRAWSTRUCT hudDraw = drawStruct;
		hudDraw.nFlags |= DSFLAG_NO_N64FILTER;
		re.DrawString(hudDraw);
		return;
	}

	DRAWSTRUCT scaled = drawStruct;
	SCR_HUDPrepareDraw(scaled);
	scaled.nFlags |= DSFLAG_NOMIP | DSFLAG_NO_N64FILTER;
	re.DrawString(scaled);
}

static void SCR_HelpLine1(const char *str)
{
	if (con_font == NULL)
		con_font = re.LoadFont("int_font");

	DRAWSTRUCT drawStruct;
	drawStruct.fAlpha = INFO_ALPHA(status_hud.alpha);
	drawStruct.nFlags = DSFLAG_PALETTE | DSFLAG_BLEND | DSFLAG_ALPHA | DSFLAG_COLOR;
	drawStruct.rgbColor.Set(1, 1, 0.25f);
	drawStruct.szString = (char *)str;
	drawStruct.nXPos = (viddef.width >> 1) - (((int)strlen(str) - 1) * STATUSBAR_FONT_POWER2 >> 2);
	drawStruct.nYPos = viddef.height - 95;
	drawStruct.pImage = con_font;
	SCR_HUDDrawString(drawStruct);
}

static void SCR_HelpLine2(const char *str)
{
	if (con_font == NULL)
		con_font = re.LoadFont("int_font");

	DRAWSTRUCT drawStruct;
	drawStruct.fAlpha = INFO_ALPHA(status_hud.alpha);
	drawStruct.nFlags = DSFLAG_PALETTE | DSFLAG_BLEND | DSFLAG_ALPHA | DSFLAG_COLOR;
	drawStruct.rgbColor.Set(1, 0, 0);
	drawStruct.szString = (char *)str;
	drawStruct.nXPos = (viddef.width >> 1) - (((int)strlen(str) - 1) * STATUSBAR_FONT_POWER2 >> 2);
	drawStruct.nYPos = viddef.height - 115;
	drawStruct.pImage = con_font;
	SCR_HUDDrawString(drawStruct);
}

static void SCR_HUDBegin(void)
{
	float user_scale = hud_scale ? hud_scale->value : 1.0f;

	if (user_scale < 1.00f) user_scale = 1.00f;
	if (user_scale > 3.00f) user_scale = 3.00f;

	/* viddef is already in physical pixels.  Do not apply the
	   640x480-to-display scale a second time. */
	scr_hud_scale_value = user_scale;
	scr_hud_scale_active = (user_scale < 0.999f || user_scale > 1.001f);
}

static void SCR_HUDEnd(void)
{
	scr_hud_scale_active = false;
}

void draw_skills_info(void)
// draw skill windows and levels
{
	DRAWSTRUCT drawStruct;
	int i, i2, y, val;
	char buf[32];

	//  if (scr_viewsize->value >= SCR_VIEWSIZE_NOSKILLS &&    // is screen size disabling skill display
	//      !xplevelup_mode &&                                 // and not in 'level up' mode?
	//      !override_showhuds)                                // and not showing all huds?
	//	  return;                                              // then bail out

	if (!skill_hud.alpha)
		return;

	int episode = CL_Episode()/*FTE*/;//floor(sv_episode->value + 0.5);				// make sure its good!
	bool deathmatch = (cl.refdef.rdflags & RDF_DEATHMATCH) > 0;
	// draw skills on left side
	for (i = 0; i < MAX_SKILLS; i++)
	{
		// draw skill titles
// SCG[1/16/00]: 		sprintf(buf,"%s",tongue_statbar[T_SKILLS_POWER + i]);
//		sprintf(buf,sizeof(buf),"%s",tongue_statbar[T_SKILLS_POWER + i]);
		Com_sprintf(buf, sizeof(buf), "%s", tongue_statbar[T_SKILLS_POWER + i]);

		drawStruct.nFlags = DSFLAG_BLEND | DSFLAG_PALETTE | DSFLAG_ALPHA;
		drawStruct.pImage = statbar_font;
		drawStruct.szString = buf;
		drawStruct.nXPos = 0;
		drawStruct.nYPos = (viddef.height - SKILLS_LABEL_TOP) + (i * SKILLS_LABEL_SPACING);
		drawStruct.fAlpha = INFO_ALPHA(skill_hud.alpha);//skill_hud.alpha;
		SCR_HUDDrawString(drawStruct);

		// draw skill window
		y = (viddef.height - SKILLS_WINDOW_TOP) + (i * SKILLS_LABEL_SPACING);  // store y for nodule placement

		drawStruct.pImage = re.RegisterPic("pics/statusbar/skill_window.tga", NULL, NULL, RESOURCE_INTERFACE);
		drawStruct.nFlags = DSFLAG_PALETTE | DSFLAG_BLEND | DSFLAG_ALPHA;
		drawStruct.nXPos = 0;
		drawStruct.nYPos = y;
		drawStruct.fAlpha = skill_hud.alpha;
		SCR_HUDDrawPic(drawStruct);

		// draw skill nodules for window
		switch (i)
		{
		case 0: val = cl.frame.playerstate.stats[STAT_POWER];         break; // power
		case 1: val = cl.frame.playerstate.stats[STAT_ATTACK];        break; // attack
		case 2: val = cl.frame.playerstate.stats[STAT_SPEED];         break; // speed
		case 3: val = cl.frame.playerstate.stats[STAT_JUMP];          break; // acro
		case 4: val = cl.frame.playerstate.stats[STAT_MAX_HEALTH];    break; // vitality
		default: val = 0; break;
		}

		// set up draw structure for flat pixel drawing
		drawStruct.pImage = NULL;
		drawStruct.nFlags = DSFLAG_FLAT | DSFLAG_COLOR | DSFLAG_ALPHA | DSFLAG_NO_N64FILTER;

		drawStruct.rgbColor.Set(0, 0, 0); // init rgb

		drawStruct.nTop = y + SKILLS_Y_OFFSET;   // y position
		drawStruct.nLeft = SKILLS_NODULE_LEFT;    // left margin
		drawStruct.nRight = SKILLS_NODULE_WIDTH;	  // set width of nodule
		drawStruct.nBottom = SKILLS_NODULE_HEIGHT;  // height of nodule
		drawStruct.fAlpha = INFO_ALPHA(skill_hud.alpha);//skill_hud.alpha;       

		for (i2 = 1; i2 < (MAX_SKILLS + 1); i2++) // draw individual nodules
		{
			if (deathmatch)
			{
				if (val >= i2)
					drawStruct.rgbColor.y = 1;   // g -- full
				else
					drawStruct.rgbColor.y = 0.2; // g -- dull
			}
			else
			{
				if (val >= i2)
					drawStruct.rgbColor.y = 1;   // g -- full
				//				else if (episode > 2)
				//					drawStruct.rgbColor.y = 0.2;   // g -- full
				else
				{
					if (i2 > episode + 1)
						drawStruct.rgbColor.y = 0.1;   // g -- full
					else
						drawStruct.rgbColor.y = 0.25;   // g -- full
				}
			}
			/*
						if (val >= i2)
							drawStruct.rgbColor.y = 1;   // g -- full
						else
							drawStruct.rgbColor.y = 0.2; // g -- dull
			*/

			SCR_HUDDrawPic(drawStruct); // do it

			// adjust x for armor bar
			drawStruct.nLeft += SKILLS_NODULE_SPACING; // shift right for next nodule
		}
	}

}




#define XPLEVEL_TOP            (SKILLS_LABEL_TOP + 50)
#define XPLEVEL_FONT_SPACING   (12)

#define XPLEVEL_NODULE_TOP     (10)
#define XPLEVEL_NODULE_LEFT    (194)
#define XPLEVEL_NODULE_WIDTH   (2)
#define XPLEVEL_NODULE_HEIGHT  (2)
#define XPLEVEL_NODULE_SPACING (3)

void draw_xplevel_info(void)
// experience points related draw routines
{
	//if (cl.frame.playerstate.stats[STAT_FRAGS] != NO_FRAGS_USED)  // frags active?  don't set up exps
	//	return;

	long exp_level[] =
#include "levels.h"

		// draw experience points nodules on window
		DRAWSTRUCT drawStruct;
	char buf[32];
	int y, i, nodules;
	long next_level_xps, current_xps, range_xps;


	//if (override_showhuds || scr_viewsize->value < SCR_VIEWSIZE_NOSTATUS)
	if (override_showhuds || status_hud.alpha)
	{
		// set up draw structure for flat pixel drawing
		drawStruct.pImage = NULL;
		drawStruct.nFlags = DSFLAG_FLAT | DSFLAG_COLOR | DSFLAG_ALPHA;

		drawStruct.rgbColor.Set(0, 0, 0); // init rgb

		drawStruct.nTop = viddef.height - XPLEVEL_NODULE_TOP; // y position
		drawStruct.nLeft = ((viddef.width - STATUSBAR_POWER2) >> 1);  // x position
		drawStruct.nLeft += XPLEVEL_NODULE_LEFT;
		drawStruct.nRight = XPLEVEL_NODULE_WIDTH;	             // set width of nodule
		drawStruct.nBottom = XPLEVEL_NODULE_HEIGHT;              // height of nodule
		drawStruct.fAlpha = status_hud.alpha;

		// calculate percentage from next experience point ceiling
		next_level_xps = exp_level[(cl.frame.playerstate.stats[STAT_LEVEL] + 1)];

		range_xps = next_level_xps - exp_level[cl.frame.playerstate.stats[STAT_LEVEL]];
		current_xps = next_level_xps - cl.frame.playerstate.stats[STAT_EXP];

		nodules = 10 - ((float)current_xps / (float)range_xps) * 10;

		for (i = 0;i < 10;i++)
		{
			if (i < nodules)
				drawStruct.rgbColor.y = 1;   // g - full
			else
				drawStruct.rgbColor.y = 0.2; // g - dull

			SCR_HUDDrawPic(drawStruct); // do it

			drawStruct.nTop -= XPLEVEL_NODULE_SPACING; // bump x right
		}
	}


	// check for 'level up'

	if (xplevelup_mode == false)  // not in xp level up mode?
		return;											// get outta here!

	// check blink time
	if (cl.time > xplevelup_timer)
	{
		xplevelup_timer = cl.time + XPLEVELUP_BLINKTIME;
		xplevelup_blink ^= 1;                       // toggle on/off
	}


	// draw header / instructions
	drawStruct.nFlags = DSFLAG_BLEND | DSFLAG_PALETTE;
	drawStruct.pImage = statbar_font;
	drawStruct.nXPos = 0;

	y = viddef.height - XPLEVEL_TOP;
	/*
		for (i = 0;i < 3;i++)
		{
			switch (i)
			{
			case 0 :
				if (!xplevelup_blink)
					Com_sprintf(buf,sizeof(buf), "%s",tongue_statbar[T_XPLEVEL_LEVELUP]);
				else
					Com_sprintf(buf,sizeof(buf), ""); // skip
				break;


			case 1 : // show which keys are used to select up/down
				Com_sprintf(buf,sizeof(buf),"");	// skip
				break;

			case 2 : // show which key is used to apply skill point
				Com_sprintf(buf,sizeof(buf), "%c  %s",Key_MatchCommand("hud_use"),
					tongue_statbar[T_XPLEVEL_ASSIGN]);
				break;
			}

			drawStruct.szString = buf;
			drawStruct.nYPos    = y;
			SCR_HUDDrawString( drawStruct );

			y += XPLEVEL_FONT_SPACING;
		}
	*/

	// draw skill selection arrow, blinking
	if (!xplevelup_blink)
	{
		drawStruct.nXPos = 64; // to the right of the skill window
		drawStruct.nYPos = (viddef.height - SKILLS_WINDOW_TOP) + (xplevelup_selection * SKILLS_LABEL_SPACING) + 6;
		// SCG[1/16/00]: 		sprintf(buf,ARROW_LEFT_STRING);
		Com_sprintf(buf, sizeof(buf), "%s %s", ARROW_LEFT_STRING, tongue_statbar[T_XPLEVEL_LEVELUP]);
		drawStruct.szString = buf;
		SCR_HUDDrawString(drawStruct);
	}


	// draw skill highlight
	drawStruct.pImage = re.RegisterPic("pics/statusbar/selec_skill.tga", NULL, NULL, RESOURCE_INTERFACE);
	drawStruct.nFlags = DSFLAG_PALETTE | DSFLAG_BLEND;
	drawStruct.nXPos = 0;
	drawStruct.nYPos = (viddef.height - SKILLS_WINDOW_TOP) + (xplevelup_selection * SKILLS_LABEL_SPACING) - 3;
	SCR_HUDDrawPic(drawStruct);
}




#define INV_WINDOW_TOP          (274)
#define INV_WINDOW_LEFT         (57)
#define INV_LABEL_SPACING       (34)
#define INV_CATEGORY_LABEL_TOP  (100)

#define INV_NODULE_LEFT         (57)
#define INV_NODULE_WIDTH        (6)
#define INV_NODULE_HEIGHT       (8)
#define INV_NODULE_SPACING      (8)
#define INV_Y_OFFSET            (9)

#define INVSELECT_WINDOW_LEFT		(INV_WINDOW_LEFT + 4)
#define INVSELECT_WINDOW_TOP    (INV_WINDOW_TOP  - 50)

void draw_inventory_info(void)
// draw inventory windows and information
{
	if (inventory_mode != INVMODE_ITEM_ICONS)
		return;

	DRAWSTRUCT drawStruct;

	// draw inventory window
	drawStruct.pImage = re.RegisterPic("pics/statusbar/inv_window2.tga", NULL, NULL, RESOURCE_INTERFACE);
	drawStruct.nFlags = DSFLAG_PALETTE | DSFLAG_BLEND | DSFLAG_ALPHA;
	drawStruct.nXPos = viddef.width - INV_WINDOW_LEFT;
	drawStruct.nYPos = viddef.height - INV_WINDOW_TOP;
	drawStruct.fAlpha = invmode_hud.alpha;
	SCR_HUDDrawPic(drawStruct);

	// [AJR] HACKAGE: Actually, this graphic shouldn't even be drawing in the FULL version, let alone the demo
#ifndef DAIKATANA_DEMO
	// draw highlighted selection
	drawStruct.pImage = re.RegisterPic("pics/statusbar/selec_inv.tga", NULL, NULL, RESOURCE_INTERFACE);
	drawStruct.nFlags = DSFLAG_PALETTE | DSFLAG_BLEND | DSFLAG_ALPHA;
	drawStruct.nXPos = viddef.width - INVSELECT_WINDOW_LEFT;
	drawStruct.nYPos = viddef.height - INVSELECT_WINDOW_TOP;
	drawStruct.fAlpha = invmode_hud.alpha;
	SCR_HUDDrawPic(drawStruct);
#endif

}


qboolean check_weapon_hud(void)
{
	// 10.16 -- if skill levels are shown (checking screen size), also show weapon hud
	if (override_showhuds || scr_viewsize->value < SCR_VIEWSIZE_NOSKILLS)
	{
		skill_hud.direction = 1;       // set skill_hud to fade in
		invmode_hud.direction = 1;

		return (true);
	}
	else
	{
		if (!xplevelup_mode)
		{
			skill_hud.direction = -1;       // fade in direction
			skill_hud.fade_time = cl.time;  // start fade NOW
		}

		invmode_hud.direction = -1;
		invmode_hud.fade_time = cl.time;  // start fade NOW

		return (false);
	}
}



#define WEAPON_WINDOW_A_TOP            (464)
#define WEAPON_WINDOW_B_TOP            (280)
#define WEAPON_WINDOW_LEFT              (84)
#define WEAPON_SELECTED_WINDOW_TOP     (464)
#define WEAPON_SELECTED_WINDOW_LEFT     (88)


void draw_weapon_info(void)
// draw weapon windows and information
{
	if (inventory_mode != INVMODE_WEAPON_ICONS)
		return;

	DRAWSTRUCT drawStruct;
	int i, val, center_x, y, height_spacing;
	char buf[32];

	height_spacing = 60;
	int number_offset[] = { -3, 58, 119, 180, 241, 302 };	// tweak with the ammo count positions a bit.

	/*
		// draw top weapon window
		drawStruct.pImage = re.RegisterPic( "pics/statusbar/weapn_win_b.tga", NULL, NULL, RESOURCE_INTERFACE );
		drawStruct.nFlags = DSFLAG_PALETTE | DSFLAG_BLEND | DSFLAG_ALPHA;
		drawStruct.nXPos  = viddef.width  - WEAPON_WINDOW_LEFT;
		drawStruct.nYPos  = viddef.height - WEAPON_WINDOW_A_TOP;
		drawStruct.fAlpha = invmode_hud.alpha;
	//	re.DrawPic( drawStruct );


		// draw bottom weapon window
		drawStruct.pImage = re.RegisterPic( "pics/statusbar/weapn_win_b.tga", NULL, NULL, RESOURCE_INTERFACE );
		drawStruct.nFlags = DSFLAG_PALETTE | DSFLAG_BLEND | DSFLAG_ALPHA;
		drawStruct.nXPos  = viddef.width  - WEAPON_WINDOW_LEFT;
		drawStruct.nYPos  = viddef.height - WEAPON_WINDOW_B_TOP;
		drawStruct.fAlpha = invmode_hud.alpha;
	//	re.DrawPic( drawStruct );
	*/
	// the selected item
	DRAWSTRUCT drawStructSel;
	drawStructSel.pImage = re.RegisterPic("pics/statusbar/selec_weapn.tga", NULL, NULL, RESOURCE_INTERFACE);
	drawStructSel.nFlags = DSFLAG_PALETTE | DSFLAG_BLEND | DSFLAG_ALPHA;
	drawStructSel.nXPos = viddef.width - WEAPON_SELECTED_WINDOW_LEFT;
	drawStructSel.nYPos = (viddef.height - WEAPON_SELECTED_WINDOW_TOP) + (inventory_selected * (height_spacing + 1));
	drawStructSel.fAlpha = invmode_hud.alpha;

	// draw ammo counts
	drawStruct.nFlags = DSFLAG_BLEND | DSFLAG_PALETTE | DSFLAG_ALPHA;
	drawStruct.pImage = statbar_font;

	y = viddef.height - 411;

	// the background
	DRAWSTRUCT drawStructPic;
	drawStructPic.pImage = re.RegisterPic("pics/statusbar/weapn_win.tga", NULL, NULL, RESOURCE_INTERFACE);
	drawStructPic.nFlags = DSFLAG_PALETTE | DSFLAG_BLEND | DSFLAG_ALPHA;
	drawStructPic.nXPos = viddef.width - WEAPON_SELECTED_WINDOW_LEFT;
	drawStructPic.fAlpha = invmode_hud.alpha;
	for (i = 0; i < 6;i++)
	{
		// draw the weapon bar window
		drawStructPic.nYPos = (viddef.height - WEAPON_SELECTED_WINDOW_TOP) + (i * (height_spacing + 1));
		SCR_HUDDrawPic(drawStructPic);

		// highlight the selected one
		if ((inventory_selected != -1) && (inventory_selected == i))
		{
			drawStructSel.nYPos = drawStructPic.nYPos;
			SCR_HUDDrawPic(drawStructSel);
		}

		if (inventoryIcons[i].count > -1)  // weapon in use
		{
			// if this is the current weapon selected, use the ammo
			// value from the playerstate record
//			if (i == inventory_selected) 
//				val = cl.frame.playerstate.stats[STAT_AMMO];
//			else
				// use the value that was passed down
			val = inventoryIcons[i].count;

			// if the ammo count is not -1, draw it (melee weapons use -1, so don't display)
			if (val != -1)
			{
				Com_sprintf(buf, sizeof(buf), "%d", val);

				// determine pixel width to center the string
				center_x = (viddef.width - 28) - (re.StringWidth(buf, statbar_font, -1) >> 1);

				drawStruct.szString = buf;
				drawStruct.nXPos = center_x;
				drawStruct.nYPos = y + number_offset[i];
				drawStruct.fAlpha = INFO_ALPHA(invmode_hud.alpha);

				SCR_HUDDrawString(drawStruct);
			}
		}

	}

}



#define COMMAND_WINDOW_B_TOP            (256)
#define COMMAND_WINDOW_B_LEFT           (128)
#define COMMAND_WINDOW_A_TOP            (COMMAND_WINDOW_B_TOP + 130)
#define COMMAND_WINDOW_A_LEFT           (128)

#define COMMAND_TEXT_CENTERX            (48)
#define COMMAND_TEXT_TOP                (COMMAND_WINDOW_B_TOP - 8)

#define COMMAND_SELECTED_WINDOW_TOP     (COMMAND_WINDOW_B_TOP - 4)
#define COMMAND_SELECTED_WINDOW_LEFT    (128)
#define COMMAND_SELECTED_SPACING         (60)

//  spacing based on table since windows are not spaced consistently
int sidekick_text_y[5] = { COMMAND_TEXT_TOP,
COMMAND_TEXT_TOP - 36,
COMMAND_TEXT_TOP - 61,
COMMAND_TEXT_TOP - 96,
COMMAND_TEXT_TOP - 121,
};

void draw_sidekickcommand_info(void)
// draw sidekick command interface
{
	if (inventory_mode != INVMODE_SIDEKICK)
		return;

	// show sidekick information (only in single-player)
	if (!cl.frame.playerstate.stats[STAT_SIDEKICKS_EXIST])
		return;

	if (!cl.sidekicks_active || (cl.refdef.rdflags & RDF_COOP))
		return;

	DRAWSTRUCT drawStruct;
	char buf[32];
	int i;


	// draw commanding sidekick
	if (!cl.sidekick_commanding)
	{
		if (cl.sidekicks_active & SIDEKICK_MIKIKO)
			cl.sidekick_commanding = 2; // ttd: need global here!
		else
			if (cl.sidekicks_active & SIDEKICK_SUPERFLY)
				cl.sidekick_commanding = 3; // ttd: need global here!
	}

	if (cl.sidekick_commanding == 2)  // TTD: create dkshared.h const here
		drawStruct.pImage = re.RegisterPic("pics/statusbar/command_mikiko.tga", NULL, NULL, RESOURCE_INTERFACE);
	else
		drawStruct.pImage = re.RegisterPic("pics/statusbar/command_superfly.tga", NULL, NULL, RESOURCE_INTERFACE);

	drawStruct.nFlags = DSFLAG_PALETTE | DSFLAG_BLEND | DSFLAG_ALPHA;
	drawStruct.nXPos = viddef.width - COMMAND_WINDOW_A_LEFT;
	drawStruct.nYPos = viddef.height - COMMAND_WINDOW_A_TOP;
	drawStruct.fAlpha = invmode_hud.alpha;
	SCR_HUDDrawPic(drawStruct);


	// draw command window
	drawStruct.pImage = re.RegisterPic("pics/statusbar/command_window.tga", NULL, NULL, RESOURCE_INTERFACE);
	drawStruct.nFlags = DSFLAG_PALETTE | DSFLAG_BLEND | DSFLAG_ALPHA;
	drawStruct.nXPos = viddef.width - COMMAND_WINDOW_B_LEFT;
	drawStruct.nYPos = viddef.height - COMMAND_WINDOW_B_TOP;
	drawStruct.fAlpha = invmode_hud.alpha;
	SCR_HUDDrawPic(drawStruct);


	// draw selected command highlight
	if (sidekickcommand_selected > -1)
	{
		drawStruct.pImage = re.RegisterPic("pics/statusbar/command_select.tga", NULL, NULL, RESOURCE_INTERFACE);
		drawStruct.nFlags = DSFLAG_PALETTE | DSFLAG_BLEND | DSFLAG_ALPHA;
		drawStruct.nXPos = viddef.width - COMMAND_SELECTED_WINDOW_LEFT;
		drawStruct.nYPos = viddef.height - (sidekick_text_y[sidekickcommand_selected]) - 3;
		drawStruct.fAlpha = invmode_hud.alpha;

		SCR_HUDDrawPic(drawStruct);
	}

	// draw command text
	drawStruct.nFlags = DSFLAG_BLEND | DSFLAG_PALETTE | DSFLAG_ALPHA;
	drawStruct.pImage = statbar_font;

	for (i = 0; i < 5;i++)
	{
		Com_sprintf(buf, sizeof(buf), "%s", tongue_sidekick[T_SIDEKICK_CMD_GET + i]); // get command text
		drawStruct.szString = buf;

		// determine pixel width to center the string
		drawStruct.nXPos = (viddef.width - COMMAND_TEXT_CENTERX) - (re.StringWidth(buf, statbar_font, -1) >> 1);
		drawStruct.nYPos = viddef.height - (sidekick_text_y[i]);
		drawStruct.fAlpha = INFO_ALPHA(invmode_hud.alpha);

		SCR_HUDDrawString(drawStruct);
	}
}



int calc_faceframe_index(int health)
// calculate index offset based on health value
{
	if (cl_death_gib)             // gibbed upon death?
		health = 0;                 // show gib face
	else
		if (health < 1)             // boundary check
			health = 1;								// min
		else
			if (health > 100)         // max
				health = 100;

	return ((100 - health) / 20);	// index to correct damage face
}


void check_flap_alphas(alphafx_t* flap)
// check/adjust alpha settings for sidekick flap graphics
{
	if (flap->fade_time)                 // active? 
	{
		flap->alpha += (FLAP_ALPHA_RATE * flap->direction);  // change alpha by constant rate
		if (flap->alpha < 0)                                 // check boundary
		{
			flap->alpha = 0.0f;
			flap->fade_time = 0.0f;                              // no longer show this graphic
		}
		else
			if (flap->alpha > statusbar_alpha->value)          // ceiling is global alpha setting
				flap->alpha = statusbar_alpha->value;
	}
}


void draw_gameover(void)
// draw 'game over' graphics after the player has died
{
	if (cl_gameover_time && cl.time > cl_gameover_time) // time to display gameover graphics?
	{
		// frags not active?  = single player
		if ((cl.frame.playerstate.stats[STAT_FRAGS] == NO_FRAGS_USED) && !(cl.frame.playerstate.stats[STAT_LAYOUTS] & LAYOUT_COOP))
		{
			cl_gameover_alpha += FLAP_ALPHA_RATE_DOUBLEDOUBLE;        // increase alpha

			if (cl_gameover_alpha > INVMODE_ALPHA_DEFAULT)            // cap alpha at default ceiling
				cl_gameover_alpha = INVMODE_ALPHA_DEFAULT;

			DRAWSTRUCT drawStruct;

			drawStruct.pImage = re.RegisterPic("pics/gameover.tga", NULL, NULL, RESOURCE_INTERFACE);
			drawStruct.nFlags = DSFLAG_PALETTE | DSFLAG_BLEND | DSFLAG_ALPHA;
			drawStruct.nXPos = (viddef.width >> 1) - 128;  // middle of horizontal screen
			drawStruct.nYPos = (viddef.height >> 1) - 128;  // middle of vertical screen
			drawStruct.fAlpha = cl_gameover_alpha;

			// TTD: could scale

			SCR_HUDDrawPic(drawStruct);
		}
	}
}

void draw_theend(void)
{
	if (cl_theend_time && cl.time > cl_theend_time) // time to display gameover graphics?
	{
		if (cl.frame.playerstate.stats[STAT_FRAGS] == NO_FRAGS_USED)  // frags not active?  = single player
		{
			cl_theend_alpha += FLAP_ALPHA_RATE_DOUBLE;        // increase alpha

			if (cl_theend_alpha > INVMODE_ALPHA_DEFAULT)            // cap alpha at default ceiling
				cl_theend_alpha = INVMODE_ALPHA_DEFAULT;

			DRAWSTRUCT drawStruct;

			drawStruct.pImage = re.RegisterPic("pics/final_end.tga", NULL, NULL, RESOURCE_INTERFACE);
			drawStruct.nFlags = DSFLAG_PALETTE | DSFLAG_BLEND | DSFLAG_ALPHA;
			drawStruct.nXPos = (viddef.width >> 1) - 128;  // middle of horizontal screen
			drawStruct.nYPos = (viddef.height >> 1) - 128;  // middle of vertical screen
			drawStruct.fAlpha = cl_theend_alpha;

			// TTD: could scale

			SCR_HUDDrawPic(drawStruct);
		}
	}
}

#define SF_STATUS(val)		( ((val) >> 4) & 15)
#define MK_STATUS(val)		( (val) & 15)
static short weapon_status_mikiko = -1;
static short weapon_status_sf = -1;
void CL_ParseSidekickWeaponStatus()
{
	byte status = MSG_ReadByte(&net_message);
	if (cl.refdef.rdflags & RDF_COOP)
	{
		weapon_status_mikiko = weapon_status_sf = -1;	// inactive!
		return;
	}

	weapon_status_sf = SF_STATUS(status);
	if (weapon_status_sf > 3) weapon_status_sf = -1;

	weapon_status_mikiko = MK_STATUS(status);
	if (weapon_status_mikiko > 3) weapon_status_mikiko = -1;
}

typedef struct team_color_info_s
{
	float r, g, b, a;
} team_color_info_t;
extern team_color_info_t cInfo[];

//---------------------------------------------------------------------------
// SCR_DrawStats()
//---------------------------------------------------------------------------
void SCR_DrawStats(void)
{
	short val, x, y, x1, y1;
	char numstr[64];
	float f;
	int i, x_center, width;
	bool in_the_red;
	void* font;
	CVector rgb;
	DRAWSTRUCT drawStruct;


	if (cls.state != ca_active || !cl.refresh_prepped)
		return;

	// full screen cinematic or letterbox mode.. get out
	if ((CIN_RunningWithView() == 1) || (cl.refdef.rdflags & RDF_LETTERBOX))
	{
		draw_theend();
		draw_gameover();
		return;
	}

	if (scr_draw_progress)
		return;

	// value has changed from user pressing 'show_huds' bind?
	val = cl.frame.playerstate.stats[STAT_LAYOUTS] & LAYOUT_SHOWALLHUDS;

	if (val != override_showhuds)  // check previous state
	{
		override_showhuds = val;
		check_weapon_hud();          // and change weapon info if necessary

		if (override_showhuds)
		{
			if (scr_viewsize->value >= SCR_VIEWSIZE_NOSTATUS) // status hud is not active
				status_hud.direction = 1;                       // so activate it
		}
		else
		{
			if (scr_viewsize->value >= SCR_VIEWSIZE_NOSTATUS) // status hud is not active
			{
				status_hud.direction = -1;                      // fade out
				status_hud.fade_time = cl.time;                 // NOW
			}
		}
	}


	SCR_HUDBegin();

	// update hud settings
	global_hud_updates(&invmode_hud, &inventory_alpha, statusbar_alpha->value);//INVMODE_ALPHA_DEFAULT);  // right-side huds
	global_hud_updates(&skill_hud, &f, statusbar_alpha->value);//INVMODE_ALPHA_DEFAULT);  // skill huds
	global_hud_updates(&status_hud, &f, statusbar_alpha->value); // status bar hud


	// load status bar fonts
	if (sbar_font_g == NULL)
		sbar_font_g = re.LoadFont("mainnums");
	if (sbar_font_r == NULL)
		sbar_font_r = re.LoadFont("mainnumsred");
	if (statbar_font == NULL)
		statbar_font = re.LoadFont("statbar_font");


	// screen size set to show status bar?
	//if (override_showhuds || scr_viewsize->value < SCR_VIEWSIZE_NOSTATUS)
	if (override_showhuds || status_hud.alpha)
	{

		// precalculate
		x_center = (viddef.width - STATUSBAR_POWER2) >> 1; // center horizontally

		rgb.Set(255, 255, 255);

		// draw bottom status bar
		if (statusbar_alpha->value >= 0.01f)  // only show status bar if alpha > 0.1f
		{

			drawStruct.nFlags = DSFLAG_PALETTE | DSFLAG_BLEND | DSFLAG_ALPHA;

			//drawStruct.fAlpha = statusbar_alpha->value;

			drawStruct.fAlpha = status_hud.alpha;
			drawStruct.nYPos = viddef.height - STATUSBAR_POWER2;       // bottom


			drawStruct.nXPos = x_center + bottom_mainbar_offs[0];
			drawStruct.pImage = re.RegisterPic("pics/statusbar/bottom_1.tga", NULL, NULL, RESOURCE_INTERFACE);
			SCR_HUDDrawPic(drawStruct);

			drawStruct.nXPos = x_center + bottom_mainbar_offs[1];
			drawStruct.pImage = re.RegisterPic("pics/statusbar/bottom_3.tga", NULL, NULL, RESOURCE_INTERFACE);
			SCR_HUDDrawPic(drawStruct);

			drawStruct.nXPos = x_center + bottom_mainbar_offs[2];
			drawStruct.pImage = re.RegisterPic("pics/statusbar/bottom_4.tga", NULL, NULL, RESOURCE_INTERFACE);
			SCR_HUDDrawPic(drawStruct);

			// various 'faces' per character, get index
			i = cl.baseclientinfo.character * STATUSBAR_FACES_FRAMES;   // initial face index					 

			// if player is invisible
			if (cl.frame.playerstate.stats[STAT_INVISIBLE])
				i += STATUSBAR_FACES_INVISIBLE;                     // index to 'invisible' face
			else
			{
				if (cl.frame.playerstate.stats[STAT_INVULNERABLE])
					i += STATUSBAR_FACES_INVULNERABLE;                  // index to 'invulnerable' face
				else
					i += calc_faceframe_index(cl.frame.playerstate.stats[STAT_HEALTH]); // index to health face
			}

			drawStruct.nXPos = x_center + bottom_mainbar_offs[3];
			drawStruct.pImage = re.RegisterPic(statusbar_faces[i], NULL, NULL, RESOURCE_INTERFACE);
			SCR_HUDDrawPic(drawStruct);

			if (cl.frame.playerstate.stats[STAT_LAYOUTS] & (LAYOUT_CTF | LAYOUT_DT))
			{
				// draw the cool face hilight.--------------------
				// find the color
				int yourTeam = -1;
				int yourFlap = -1;
				if (cl.frame.playerstate.stats[STAT_MIKIKO_FLAGS] & STAT_CTF_YOUR_TEAM)
				{
					yourTeam = (cl.frame.playerstate.stats[STAT_MIKIKO_FLAGS] >> STAT_CTF_TEAM_CONVERT_SHIFT) & 0x000f;
					yourFlap = STAT_MIKIKO_FLAGS;
				}
				else if (cl.frame.playerstate.stats[STAT_SUPERFLY_FLAGS] & STAT_CTF_YOUR_TEAM)
				{
					yourTeam = (cl.frame.playerstate.stats[STAT_SUPERFLY_FLAGS] >> STAT_CTF_TEAM_CONVERT_SHIFT) & 0x000f;
					yourFlap = STAT_SUPERFLY_FLAGS;
				}

				bool bYouHaveFlag = (yourFlap != -1) && (cl.frame.playerstate.stats[yourFlap] & STAT_CTF_YOU_HAVE_ENEMY_FLAG);
				bool bYourFlagHome = (yourFlap != -1) && (cl.frame.playerstate.stats[yourFlap] & STAT_CTF_YOUR_FLAG_HOME);
				int otherFlap = (yourFlap == STAT_SUPERFLY_FLAGS) ? STAT_MIKIKO_FLAGS : STAT_SUPERFLY_FLAGS;
				bool bOtherFlagHome = (otherFlap != -1) && (cl.frame.playerstate.stats[otherFlap] & STAT_CTF_YOUR_FLAG_HOME);

				if (yourTeam != -1)
				{
					yourTeam--;
					drawStruct.pImage = re.RegisterPic("pics/statusbar/bottom_blu.tga", NULL, NULL, RESOURCE_EPISODE);
					drawStruct.nFlags = DSFLAG_PALETTE | DSFLAG_BLEND | DSFLAG_ALPHA | DSFLAG_COLOR;
					drawStruct.nXPos = viddef.width / 2 - 35;
					drawStruct.nYPos = viddef.height - 65;
					drawStruct.rgbColor = CVector(cInfo[yourTeam].r, cInfo[yourTeam].g, cInfo[yourTeam].b);
					drawStruct.fAlpha = status_hud.alpha;
					SCR_HUDDrawPic(drawStruct);
				}

				// draw the team flaps. --------------------------
				// draw the left flap
				drawStruct.nFlags = DSFLAG_PALETTE | DSFLAG_BLEND | DSFLAG_ALPHA;
				drawStruct.pImage = re.RegisterPic(statusbar_faces[STATUSBAR_BLANK_FLAP], NULL, NULL, RESOURCE_INTERFACE);
				drawStruct.nXPos = 0;
				drawStruct.nYPos = viddef.height - 87;
				drawStruct.fAlpha = status_hud.alpha;
				SCR_HUDDrawPic(drawStruct);

				// draw the right flap
				drawStruct.nXPos = viddef.width - 129;  // right edge
				drawStruct.nFlags |= DSFLAG_PALETTE | DSFLAG_BLEND | DSFLAG_ALPHA | DSFLAG_FLIPX;
				SCR_HUDDrawPic(drawStruct);

				// draw the indicator flags!
				if (cl.frame.playerstate.stats[STAT_LAYOUTS] & LAYOUT_CTF)
				{
					drawStruct.nFlags = DSFLAG_PALETTE | DSFLAG_BLEND | DSFLAG_ALPHA | DSFLAG_COLOR | DSFLAG_SCALE;
					drawStruct.pImage = re.RegisterPic("pics/statusbar/flag_blu.tga", NULL, NULL, RESOURCE_INTERFACE);
					drawStruct.nYPos = viddef.height - 73;
					drawStruct.fScaleX = drawStruct.fScaleY = 2.0f;
					drawStruct.fAlpha = INFO_ALPHA(status_hud.alpha);

					int teamColor;
					// is team 1's flag home?
					if (cl.frame.playerstate.stats[STAT_MIKIKO_FLAGS] & STAT_CTF_YOUR_FLAG_HOME)
					{
						teamColor = ((cl.frame.playerstate.stats[STAT_MIKIKO_FLAGS] >> STAT_CTF_TEAM_CONVERT_SHIFT) & 0x000f) - 1;
						drawStruct.nXPos = 17;
						drawStruct.rgbColor = CVector(cInfo[teamColor].r, cInfo[teamColor].g, cInfo[teamColor].b);
						SCR_HUDDrawPic(drawStruct);
					}

					// is team 2's flag home?
					if (cl.frame.playerstate.stats[STAT_SUPERFLY_FLAGS] & STAT_CTF_YOUR_FLAG_HOME)
					{
						drawStruct.nFlags |= DSFLAG_FLIPX;
						teamColor = ((cl.frame.playerstate.stats[STAT_SUPERFLY_FLAGS] >> STAT_CTF_TEAM_CONVERT_SHIFT) & 0x000f) - 1;
						drawStruct.nXPos = viddef.width - 83;
						drawStruct.rgbColor = CVector(cInfo[teamColor].r, cInfo[teamColor].g, cInfo[teamColor].b);
						SCR_HUDDrawPic(drawStruct);
					}

					// do you have a flag?
					if (bYouHaveFlag)
					{
						drawStruct.nFlags = DSFLAG_PALETTE | DSFLAG_BLEND | DSFLAG_ALPHA | DSFLAG_COLOR;
						drawStruct.nYPos = viddef.height - 60;
						if (yourFlap == STAT_MIKIKO_FLAGS)
						{
							drawStruct.nFlags |= DSFLAG_FLIPX;
							drawStruct.nXPos = 25;
							teamColor = ((cl.frame.playerstate.stats[STAT_SUPERFLY_FLAGS] >> STAT_CTF_TEAM_CONVERT_SHIFT) & 0x000f) - 1;
						}
						else
						{
							drawStruct.nXPos = viddef.width - 60;
							teamColor = ((cl.frame.playerstate.stats[STAT_MIKIKO_FLAGS] >> STAT_CTF_TEAM_CONVERT_SHIFT) & 0x000f) - 1;
						}

						drawStruct.rgbColor = CVector(cInfo[teamColor].r, cInfo[teamColor].g, cInfo[teamColor].b);
						SCR_HUDDrawPic(drawStruct);
					}

					if (cl_dmhints && cl_dmhints->value)
					{
						if (bYouHaveFlag)
							SCR_HelpLine1("YOU HAVE THE FLAG");
						if (!bYourFlagHome)
							SCR_HelpLine2("ENEMY HAS THE FLAG");
					}
				}
				else if (cl.frame.playerstate.stats[STAT_LAYOUTS] & LAYOUT_DT)
				{
					if (cl_dmhints && cl_dmhints->value)
					{
						if (bYouHaveFlag)
							SCR_HelpLine1("YOU HAVE THE PACK");
						if (!bOtherFlagHome)
							SCR_HelpLine2("ENEMY HAS THE PACK");
					}
				}

				// draw the team 1 info bars
				sidekick_draw_healtharmor_bar(78,
					cl.frame.playerstate.stats[STAT_MIKIKO_HEALTH],
					cl.frame.playerstate.stats[STAT_MIKIKO_ARMOR],
					status_hud.alpha);

				// draw the team 2 info bars
				sidekick_draw_healtharmor_bar(viddef.width - 97,
					cl.frame.playerstate.stats[STAT_SUPERFLY_HEALTH],
					cl.frame.playerstate.stats[STAT_SUPERFLY_ARMOR],
					status_hud.alpha);


				// draw the captures
				drawStruct.nFlags = DSFLAG_COLOR | DSFLAG_BLEND | DSFLAG_ALPHA;
				drawStruct.rgbColor = rgb;
				drawStruct.nYPos = viddef.height - 38;
				drawStruct.fAlpha = INFO_ALPHA(status_hud.alpha);

				// team 1
				long captures = (cl.frame.playerstate.stats[STAT_MIKIKO_FLAGS] >> STAT_CTF_TEAM_CAPTURES_SHIFT);
				Com_sprintf(numstr, sizeof(numstr), "%d", captures);
				drawStruct.szString = numstr;
				drawStruct.pImage = /*(cl.frame.playerstate.stats[STAT_MIKIKO_FLAGS] & STAT_CTF_YOUR_TEAM) ? sbar_font_r : */sbar_font_g;
				drawStruct.nXPos = 33;
				drawStruct.nXPos -= ((strlen(numstr) - 1) * STATUSBAR_FONT_POWER2) >> 1;
				SCR_HUDDrawString(drawStruct);

				// team 2
				captures = (cl.frame.playerstate.stats[STAT_SUPERFLY_FLAGS] >> STAT_CTF_TEAM_CAPTURES_SHIFT);
				Com_sprintf(numstr, sizeof(numstr), "%d", captures);
				drawStruct.szString = numstr;
				drawStruct.pImage = /*(cl.frame.playerstate.stats[STAT_SUPERFLY_FLAGS] & STAT_CTF_YOUR_TEAM) ? sbar_font_r : */sbar_font_g;
				drawStruct.nXPos = viddef.width - 43;
				drawStruct.nXPos -= ((strlen(numstr) - 1) * STATUSBAR_FONT_POWER2) >> 1;
				SCR_HUDDrawString(drawStruct);

				// draw status label
				long team = (cl.frame.playerstate.stats[STAT_MIKIKO_FLAGS] >> STAT_CTF_TEAM_CONVERT_SHIFT) & 0x000f;
				sprintf(numstr, "%s", tongue_ctf[T_CTF_RED_TEAM_NAME + (team - 1)]);
				drawStruct.nFlags = DSFLAG_PALETTE | DSFLAG_BLEND | DSFLAG_ALPHA;
				drawStruct.pImage = statbar_font;
				drawStruct.nYPos = viddef.height - 16;
				drawStruct.fAlpha = INFO_ALPHA(status_hud.alpha);
				drawStruct.szString = numstr;
				drawStruct.nXPos = 33;//13;
				drawStruct.nXPos -= ((strlen(numstr) - 1) * STATUSBAR_FONT_POWER2) >> 2;
				SCR_HUDDrawString(drawStruct);

				team = (cl.frame.playerstate.stats[STAT_SUPERFLY_FLAGS] >> STAT_CTF_TEAM_CONVERT_SHIFT) & 0x000f;
				sprintf(numstr, "%s", tongue_ctf[T_CTF_RED_TEAM_NAME + (team - 1)]);
				drawStruct.szString = numstr;
				drawStruct.nXPos = viddef.width - 43;
				drawStruct.nXPos -= ((strlen(numstr) - 1) * STATUSBAR_FONT_POWER2) >> 2;
				SCR_HUDDrawString(drawStruct);
			}
			else
			{
				// show sidekick information (only in single-player/coop)
				if (!(cl.refdef.rdflags & RDF_DEATHMATCH))
				{
					unsigned long flags = cl.frame.playerstate.stats[STAT_SIDEKICKS_EXIST];
					if (cl.refdef.rdflags & RDF_COOP)
					{
						if (flags & SIDEKICK_SUPERFLY)
							flap_superfly.alpha = status_hud.alpha;
						else
							flap_superfly.alpha = 0;

						if (flags & SIDEKICK_MIKIKO)
							flap_mikiko.alpha = status_hud.alpha;
						else
							flap_mikiko.alpha = 0;
					}
					else// if (flags)  // any sidekicks EXIST?
					{
						check_flap_alphas(&flap_mikiko);
						check_flap_alphas(&flap_superfly);
					}

					bool drawingHiro = false;
					// is mikiko info displaying?
					if (flap_mikiko.alpha)
					{
						if (cl.baseclientinfo.character == 1)	// mikiko is the player ... use hiro flap
						{
							drawingHiro = true;
							if (cl.frame.playerstate.stats[STAT_MIKIKO_FLAGS] & STAT_FLAG_INVISIBLE)
								i = STATUSBAR_FACES_FLAP_HIRO + STATUSBAR_FACES_FLAP_INVIS;
							else if (cl.frame.playerstate.stats[STAT_MIKIKO_FLAGS] & STAT_FLAG_INVULNERABLE)
								i = STATUSBAR_FACES_FLAP_HIRO + STATUSBAR_FACES_FLAP_INVUL;
							else
								i = STATUSBAR_FACES_FLAP_HIRO + calc_faceframe_index(cl.frame.playerstate.stats[STAT_MIKIKO_HEALTH]);
						}
						else
						{
							if (cl.frame.playerstate.stats[STAT_MIKIKO_FLAGS] & STAT_FLAG_INVISIBLE)
								i = STATUSBAR_FACES_FLAP_MIKIKO + STATUSBAR_FACES_FLAP_INVIS;
							else if (cl.frame.playerstate.stats[STAT_MIKIKO_FLAGS] & STAT_FLAG_INVULNERABLE)
								i = STATUSBAR_FACES_FLAP_MIKIKO + STATUSBAR_FACES_FLAP_INVUL;
							else
								i = STATUSBAR_FACES_FLAP_MIKIKO + calc_faceframe_index(cl.frame.playerstate.stats[STAT_MIKIKO_HEALTH]);
						}

						drawStruct.pImage = re.RegisterPic(statusbar_faces[i], NULL, NULL, RESOURCE_INTERFACE);
						drawStruct.nFlags |= DSFLAG_PALETTE | DSFLAG_BLEND | DSFLAG_ALPHA;
						drawStruct.nXPos = 0;
						drawStruct.nYPos = viddef.height - 87;
						drawStruct.fAlpha = flap_mikiko.alpha;
						SCR_HUDDrawPic(drawStruct);

						// draw the ammo indicator thingy
						if (!(cl.refdef.rdflags & RDF_COOP) && (weapon_status_mikiko != -1))
						{
							// first the pic
							drawStruct.pImage = re.RegisterPic(sidekick_ammo_icons[SIDEKICK_AMMO_BORDER], NULL, NULL, RESOURCE_INTERFACE);
							drawStruct.nFlags |= DSFLAG_PALETTE | DSFLAG_BLEND | DSFLAG_ALPHA;
							drawStruct.nXPos = 69;
							drawStruct.nYPos = viddef.height - 72;
							drawStruct.fAlpha = flap_mikiko.alpha;
							SCR_HUDDrawPic(drawStruct);

							// then the icon!
							drawStruct.nXPos = 75;
							drawStruct.nYPos = viddef.height - 63;
							drawStruct.fAlpha = INFO_ALPHA(flap_mikiko.alpha);
							drawStruct.pImage = re.RegisterPic(sidekick_ammo_icons[weapon_status_mikiko + SIDEKICK_AMMO_NADA], NULL, NULL, RESOURCE_INTERFACE);
							SCR_HUDDrawPic(drawStruct);
						}

						// draw health/armor bars
						sidekick_draw_healtharmor_bar(78,
							cl.frame.playerstate.stats[STAT_MIKIKO_HEALTH],
							cl.frame.playerstate.stats[STAT_MIKIKO_ARMOR],
							INFO_ALPHA(flap_mikiko.alpha));//flap_mikiko.alpha);
					}

					drawingHiro = false;
					if (flap_superfly.alpha)
					{
						drawStruct.nFlags = 0;
						if (cl.baseclientinfo.character == 2)	// sf is the player ... use hiro flap
						{
							drawingHiro = true;
							drawStruct.nXPos = viddef.width - 129;  // right edge
							drawStruct.nFlags = DSFLAG_FLIPX;
							if (cl.frame.playerstate.stats[STAT_SUPERFLY_FLAGS] & STAT_FLAG_INVISIBLE)
								i = STATUSBAR_FACES_FLAP_HIRO + STATUSBAR_FACES_FLAP_INVIS;
							else if (cl.frame.playerstate.stats[STAT_SUPERFLY_FLAGS] & STAT_FLAG_INVULNERABLE)
								i = STATUSBAR_FACES_FLAP_HIRO + STATUSBAR_FACES_FLAP_INVUL;
							else
								i = STATUSBAR_FACES_FLAP_HIRO + calc_faceframe_index(cl.frame.playerstate.stats[STAT_SUPERFLY_HEALTH]);
						}
						else
						{
							drawStruct.nXPos = viddef.width - 124;  // right edge
							if (cl.frame.playerstate.stats[STAT_SUPERFLY_FLAGS] & STAT_FLAG_INVISIBLE)
								i = STATUSBAR_FACES_FLAP_SUPERFLY + STATUSBAR_FACES_FLAP_INVIS;
							else if (cl.frame.playerstate.stats[STAT_SUPERFLY_FLAGS] & STAT_FLAG_INVULNERABLE)
								i = STATUSBAR_FACES_FLAP_SUPERFLY + STATUSBAR_FACES_FLAP_INVUL;
							else
								i = STATUSBAR_FACES_FLAP_SUPERFLY + calc_faceframe_index(cl.frame.playerstate.stats[STAT_SUPERFLY_HEALTH]);
						}

						drawStruct.pImage = re.RegisterPic(statusbar_faces[i], NULL, NULL, RESOURCE_INTERFACE);
						drawStruct.nFlags |= DSFLAG_PALETTE | DSFLAG_BLEND | DSFLAG_ALPHA;
						//				drawStruct.nXPos  = viddef.width - 123;  // right edge
						drawStruct.nYPos = viddef.height - 87;  // bottom
						drawStruct.fAlpha = flap_superfly.alpha;
						SCR_HUDDrawPic(drawStruct);

						// draw the ammo indicator thingy
						if (!(cl.refdef.rdflags & RDF_COOP) && (weapon_status_sf != -1))
						{
							// first the pic
							drawStruct.pImage = re.RegisterPic(sidekick_ammo_icons[SIDEKICK_AMMO_BORDER], NULL, NULL, RESOURCE_INTERFACE);
							drawStruct.nFlags |= DSFLAG_PALETTE | DSFLAG_BLEND | DSFLAG_ALPHA | DSFLAG_FLIPX;
							drawStruct.nXPos = viddef.width - 135;
							drawStruct.nYPos = viddef.height - 73;
							drawStruct.fAlpha = flap_superfly.alpha;
							SCR_HUDDrawPic(drawStruct);

							// then the icon!
							drawStruct.nXPos = viddef.width - 110;
							drawStruct.nYPos = viddef.height - 64;
							drawStruct.fAlpha = INFO_ALPHA(flap_superfly.alpha);
							drawStruct.pImage = re.RegisterPic(sidekick_ammo_icons[weapon_status_sf + SIDEKICK_AMMO_NADA], NULL, NULL, RESOURCE_INTERFACE);
							SCR_HUDDrawPic(drawStruct);
						}

						// draw health/armor bars
						sidekick_draw_healtharmor_bar(viddef.width - 97,
							cl.frame.playerstate.stats[STAT_SUPERFLY_HEALTH],
							cl.frame.playerstate.stats[STAT_SUPERFLY_ARMOR],
							INFO_ALPHA(flap_superfly.alpha));//flap_superfly.alpha);

						// draw the 'hiro' name tag
						if (drawingHiro)
						{

						}
					}
				}
			}
		}

		// draw floating icons
		for (i = 0;i < 4;i++)
		{
			x = x_center + sb_info_icon[i].x_add;
			y = viddef.height + sb_info_icon[i].y_add;

			x1 = x_center + sb_info_text[i].x_add;
			y1 = viddef.height + sb_info_text[i].y_add;

			if (i == 0)
				x1 += 5;
			if (i == 3)               // frag/level icon
				if (cl.frame.playerstate.stats[STAT_FRAGS] == NO_FRAGS_USED)  // frags not active?
					i++;                  // advance to show exp level icon instead of frag icon

			drawStruct.pImage = re.RegisterPic(statusbar_icons[i], NULL, NULL, RESOURCE_EPISODE);
			drawStruct.nFlags = DSFLAG_PALETTE | DSFLAG_BLEND | DSFLAG_ALPHA;
			drawStruct.nXPos = x;
			drawStruct.nYPos = y;
			drawStruct.fAlpha = INFO_ALPHA(status_hud.alpha);//status_hud.alpha;
			SCR_HUDDrawPic(drawStruct);


#ifdef TONGUE_ENGLISH // only show text in English version

			// draw status label
			if ((i == 3) && (cl.frame.playerstate.stats[STAT_LAYOUTS] & (LAYOUT_CTF)))
			{
				Com_sprintf(numstr, sizeof(numstr), "%s", tongue_scoreboard[T_SCOREBOARD_SCORE]);
				for (int k = 0; k < strlen(numstr); k++)
					numstr[k] = toupper(numstr[k]);
			}
			else
				Com_sprintf(numstr, sizeof(numstr), "%s", tongue_statbar[T_STATBAR_ARMOR + i]);

			drawStruct.szString = numstr;
			drawStruct.nFlags = DSFLAG_PALETTE | DSFLAG_BLEND | DSFLAG_ALPHA;
			drawStruct.pImage = statbar_font;
			drawStruct.nXPos = x1;
			drawStruct.nYPos = y1;
			drawStruct.fAlpha = INFO_ALPHA(status_hud.alpha);;//status_hud.alpha;
			SCR_HUDDrawString(drawStruct);

#endif TONGUE_ENGLISH

		}


		// draw stats on top of status bar
		for (i = 0;i < 4;i++)
		{
			switch (i)
			{
			case 0: val = cl.frame.playerstate.stats[STAT_ARMOR];   break;
			case 1: val = cl.frame.playerstate.stats[STAT_HEALTH];  break;
			case 2: val = cl.frame.playerstate.stats[STAT_AMMO];    break;

			case 3: if (cl.frame.playerstate.stats[STAT_FRAGS] == NO_FRAGS_USED)  // frags not active?
				val = cl.frame.playerstate.stats[STAT_LEVEL];   // show exp level 
				  else
				val = cl.frame.playerstate.stats[STAT_FRAGS];   // show frags
				break;
			}


			// convert stat to string
			itoa(val, numstr, 10);

			// check for values 'in the red' (danger, will robinson!)
			in_the_red = (val >= stat_warning[i][0] && val <= stat_warning[i][1]);  // between floor/ceiling values

			// don't allow health to display below 0
			if (i == 1 && val < 0)
			{
				val = 0;
				//				sprintf(numstr,"0")
				Com_sprintf(numstr, sizeof(numstr), "0");
			}


			x = (viddef.width >> 1) + sb_info[i].x_add;          // set starting x/y
			x -= (strlen(numstr) * STATUSBAR_FONT_POWER2) >> 1;  // center x at this location

			if (i == 0)
				x += 5;

			y = viddef.height + sb_info[i].y_add;

			if (!in_the_red)
				font = sbar_font_g;
			else
				font = sbar_font_r;

			width = strlen(numstr);

			drawStruct.nFlags = DSFLAG_COLOR | DSFLAG_BLEND | DSFLAG_ALPHA;
			drawStruct.rgbColor = rgb;
			drawStruct.szString = numstr;
			drawStruct.pImage = font;
			drawStruct.nXPos = x;
			drawStruct.nYPos = y;
			drawStruct.fAlpha = INFO_ALPHA(status_hud.alpha);;//status_hud.alpha;
			SCR_HUDDrawString(drawStruct);
		}

	}

	draw_skills_info();           // skill levels
	draw_weapon_info();           // weapons
	draw_inventory_info();	       // items/weapons
	draw_sidekickcommand_info();  // sidekick commands
	draw_xplevel_info();          // draw xplevel up

	SCR_HUDEnd();

	//	 draw_gameover();

}

