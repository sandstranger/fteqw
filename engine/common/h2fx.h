/*
Hexen II's effects (its starteffect/endeffect builtins), as uHexen2 (https://github.com/sezero/uhexen2, effects.h)
numbers them. GPL v2 or later, as uHexen2.

The server keeps uHexen2's list of them (sv_h2fx.c) and sends it to clients with PEXT2_H2EFFECTS in uHexen2's own
wire format (svc_start_effect, svc_end_effect: svcfte_h2starteffect and svcfte_h2endeffect here); the client runs them
with uHexen2's code (clh2_fx.c). Other clients keep the server's particle-script versions (custom temp entities).
*/

#define MAX_H2EFFECTS		256

/* Types for various chunks */
#define THINGTYPE_GREYSTONE	1
#define THINGTYPE_WOOD		2
#define THINGTYPE_METAL		3
#define THINGTYPE_FLESH		4
#define THINGTYPE_FIRE		5
#define THINGTYPE_CLAY		6
#define THINGTYPE_LEAVES	7
#define THINGTYPE_HAY		8
#define THINGTYPE_BROWNSTONE	9
#define THINGTYPE_CLOTH		10
#define THINGTYPE_WOOD_LEAF	11
#define THINGTYPE_WOOD_METAL	12
#define THINGTYPE_WOOD_STONE	13
#define THINGTYPE_METAL_STONE	14
#define THINGTYPE_METAL_CLOTH	15
#define THINGTYPE_WEBS		16
#define THINGTYPE_GLASS		17
#define THINGTYPE_ICE		18
#define THINGTYPE_CLEARGLASS	19
#define THINGTYPE_REDGLASS	20
#define THINGTYPE_ACID		21
#define THINGTYPE_METEOR	22
#define THINGTYPE_GREENFLESH	23
#define THINGTYPE_BONE		24

#define CE_NONE			0
#define CE_RAIN			1
#define CE_FOUNTAIN		2
#define CE_QUAKE		3
#define CE_WHITE_SMOKE		4	/* whtsmk1.spr */
#define CE_BLUESPARK		5	/* bspark.spr */
#define CE_YELLOWSPARK		6	/* spark.spr */
#define CE_SM_CIRCLE_EXP	7	/* fcircle.spr */
#define CE_BG_CIRCLE_EXP	8	/* fcircle.spr */
#define CE_SM_WHITE_FLASH	9	/* sm_white.spr */
#define CE_WHITE_FLASH		10	/* gryspt.spr */
#define CE_YELLOWRED_FLASH	11	/* yr_flash.spr */
#define CE_BLUE_FLASH		12	/* bluflash.spr */
#define CE_SM_BLUE_FLASH	13	/* bluflash.spr */
#define CE_RED_FLASH		14	/* redspt.spr */
#define CE_SM_EXPLOSION		15	/* sm_expld.spr */
#define CE_LG_EXPLOSION		16	/* bg_expld.spr */
#define CE_FLOOR_EXPLOSION	17	/* fl_expld.spr */
#define CE_RIDER_DEATH		18
#define CE_BLUE_EXPLOSION	19	/* xpspblue.spr */
#define CE_GREEN_SMOKE		20	/* grnsmk1.spr */
#define CE_GREY_SMOKE		21	/* grysmk1.spr */
#define CE_RED_SMOKE		22	/* redsmk1.spr */
#define CE_SLOW_WHITE_SMOKE	23	/* whtsmk1.spr */
#define CE_REDSPARK		24	/* rspark.spr */
#define CE_GREENSPARK		25	/* gspark.spr */
#define CE_TELESMK1		26	/* telesmk1.spr */
#define CE_TELESMK2		27	/* telesmk2.spr */
#define CE_ICEHIT		28	/* icehit.spr */
#define CE_MEDUSA_HIT		29	/* medhit.spr */
#define CE_MEZZO_REFLECT	30	/* mezzoref.spr */
#define CE_FLOOR_EXPLOSION2	31	/* flrexpl2.spr */
#define CE_XBOW_EXPLOSION	32	/* xbowexpl.spr */
#define CE_NEW_EXPLOSION	33	/* gen_expl.spr */
#define CE_MAGIC_MISSILE_EXPLOSION 34	/* mm_expld.spr */
#define CE_GHOST		35	/* ghost.spr */
#define CE_BONE_EXPLOSION	36
#define CE_REDCLOUD		37
#define CE_TELEPORTERPUFFS	38
#define CE_TELEPORTERBODY	39
#define CE_BONESHARD		40
#define CE_BONESHRAPNEL		41

/* New for Mission Pack... */
#define CE_FLAMESTREAM		42	/* Flamethrower */
#define CE_SNOW			43
#define CE_GRAVITYWELL		44
#define CE_BLDRN_EXPL		45
#define CE_ACID_MUZZFL		46
#define CE_ACID_HIT		47
#define CE_FIREWALL_SMALL	48
#define CE_FIREWALL_MEDIUM	49
#define CE_FIREWALL_LARGE	50
#define CE_LBALL_EXPL		51
#define CE_ACID_SPLAT		52
#define CE_ACID_EXPL		53
#define CE_FBOOM		54
#define CE_CHUNK		55
#define CE_BOMB			56
#define CE_BRN_BOUNCE		57
#define CE_LSHOCK		58
#define CE_FLAMEWALL		59
#define CE_FLAMEWALL2		60
#define CE_FLOOR_EXPLOSION3	61
#define CE_ONFIRE		62
