/*
The models its renderer drew on the HUD (ref_gl/gl_inventory.cpp, gl_icons.cpp): the weapons' and items' down the
right as they're picked from, the boosts' (artifacts) down the left, turning; over FTE's (CLDKF_DrawIcons).

GPL v2 or later.
*/
#include "cldk_local.h"

extern invenIcon_t			inventoryIcons[];	//cl_hudmsg.cpp's
extern int					numInventoryIcons;
extern int					inventory_mode;
extern float				inventory_alpha;
extern onscreen_icons_info_t onscreen_icons_info;

static float	iconrotation;	//its ref_laserRotation: 60 degrees a second

//what the HUD has (its models' indexes before the level's drawn, FTE's models after)
static void *IconModel(void *m, int modelindex)
{
	if (modelindex > 0)
		return CLDKF_ModelForIndex(modelindex);
	if ((size_t)m < MAX_MODELS)
		return CLDKF_ModelForIndex((int)(size_t)m);
	return m;
}

#define	ICON_WIDTH				80
#define ICON_ITEM_XOFFSET		54
#define ICON_ITEM_YOFFSET		250
#define ICON_ITEM_HEIGHT		58
#define ICON_WEAPON_XOFFSET		38
#define ICON_WEAPON_YOFFSET		436
#define ICON_WEAPON_HEIGHT		61

//inventory_DrawIcons
static void CLDK_DrawInventoryIcons(void)
{
	cldk_icon_t icons[CL_MAX_INVENTORY_ICONS];
	float hs = hud_scale?hud_scale->value:1, scale, w, x, y, h;
	int i, n = 0;
	if (inventory_mode != INVMODE_WEAPON_ICONS && inventory_mode != INVMODE_ITEM_ICONS)
		return;
	if (inventory_alpha == 0)
		return;
	hs = (hs < 1)?1:((hs > 3)?3:hs);
	if (inventory_mode == INVMODE_ITEM_ICONS)
	{
		x = (int)(ICON_ITEM_XOFFSET * hs + 0.5f);
		h = (int)(ICON_ITEM_HEIGHT * hs + 0.5f);
		y = viddef.height - (int)(ICON_ITEM_YOFFSET * hs + 0.5f);
		scale = 1.4f * hs;
	}
	else
	{
		x = (int)(ICON_WEAPON_XOFFSET * hs + 0.5f);
		h = (int)(ICON_WEAPON_HEIGHT * hs + 0.5f);
		y = viddef.height - (int)(ICON_WEAPON_YOFFSET * hs + 0.5f);
		scale = 2.5f * hs;
	}
	w = (int)(ICON_WIDTH * hs + 0.5f);
	if (w > viddef.width)
		w = viddef.width;
	for (i = 0; i < numInventoryIcons && i < CL_MAX_INVENTORY_ICONS; i++, y += h)
	{
		cldk_icon_t *ic = &icons[n];
		ic->model = IconModel(inventoryIcons[i].model, inventoryIcons[i].modelindex);
		if (!ic->model)
			continue;
		ic->x = x;
		ic->y = y;
		ic->angles[0] = iconrotation * 2;
		ic->angles[1] = 0;
		ic->angles[2] = -45;
		ic->frame = ic->oldframe = 2;
		ic->backlerp = 0;
		ic->alpha = inventory_alpha;
		ic->scale = scale;
		n++;
	}
	CLDKF_DrawIcons(viddef.width - w, 0, w, viddef.height, n, icons);
}

#define BICON_SPACING	36
#define BICON_TOP		230
#define	BICON_WIDTH		160

//GL_Boost_Icons_Display
static void CLDK_DrawBoostIcons(void)
{
	cldk_icon_t icons[MAX_ONSCREEN_ICONS*2];
	float y = viddef.height - BICON_TOP - 16.0f;
	int i, k, n = 0;
	if (onscreen_icons_info.num_icons <= 0)
		return;
	for (i = 0; i < MAX_ONSCREEN_ICONS; i++, y += BICON_SPACING)
	{
		onscreen_icons_t *icon = &onscreen_icons_info.icons[i];
		for (k = 0; k < 2; k++)
		{
			cldk_icon_t *ic = &icons[n];
			if (!icon->models[k] || icon->alpha == 0)
				continue;
			ic->model = IconModel(icon->models[k], 0);
			if (!ic->model)
				continue;
			ic->x = BICON_WIDTH/2;
			ic->y = y;
			ic->angles[0] = (k & 1)?iconrotation * 8:-iconrotation * 8;	//(the inner one the other way)
			ic->angles[1] = 0;
			ic->angles[2] = -90;
			ic->frame = 1;
			ic->oldframe = 0;
			ic->backlerp = 0;
			ic->alpha = icon->alpha;
			ic->scale = 1;
			n++;
		}
	}
	CLDKF_DrawIcons(0, 0, BICON_WIDTH, viddef.height, n, icons);
}

void CLDK_DrawHUDModels(void)
{
	iconrotation += 60 * cls.frametime;
	while (iconrotation > 360)
		iconrotation -= 360;
	if (CLDKF_RDFlags() & (RDF_LETTERBOX|RDF_NO_ICONS))
		return;
	CLDK_DrawInventoryIcons();
	CLDK_DrawBoostIcons();
}
