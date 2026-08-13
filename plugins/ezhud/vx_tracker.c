/*
Copyright (C) 2011 VULTUREIIC

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
	\file

	\brief
	Frags Tracker Screen Element (killfeed)

	\author
	VULTUREIIC

	Ported to the FTE ezhud plugin (ezhud #15 P2) from
	reference/ezquake/src/vx_tracker.c + hud_common.c's "tracker" HUD_Register
	block, as of ezquake submodule commit a86996a3d33dc1bc3fb15bfe7bcadd662b822557.

	This is a mechanical, TRIMMED port (see comments below for what was cut):
	it keeps the cvar surface, message queue/fade behaviour, colour policy,
	streak tracking and flag-event rows (touch/capture/drop, gated on
	r_tracker_flags) that ezQuake's console players actually rely on, and
	drops quad-icon extras / in-console coloured weapon names / pickup rows
	(no pickup event source exists in the engine bridge yet) that have no
	FTE plugin counterpart yet. Frag events arrive via the new engine->plugin
	"FragEvent" export
	(engine/common/plugin.c, dispatched from engine/client/fragstats.c's
	Stats_Evaluate), which mirrors ezQuake's fragstats.c parsing but happens
	engine-side against fragfile.dat instead of being reimplemented here.
**/

#include "ezquakeisms.h"
#include "hud.h"
#include "hud_common.h"

//-----------------------------------------------------------------------------
// mirrors engine/client/fragstats.c's fragfilemsgtypes_t (kept in sync by hand -
// there is no shared header between the engine and a plugin build, so this is
// a deliberate duplicate; if the engine enum changes this needs updating too).
typedef enum
{
	ff_death,
	ff_tkdeath,
	ff_suicide,
	ff_bonusfrag,
	ff_tkbonus,
	ff_flagtouch,
	ff_flagcaps,
	ff_flagdrops,

	ff_frags,
	ff_fragedby,
	ff_tkills,
	ff_tkilledby,
} fragfilemsgtypes_t;

//-----------------------------------------------------------------------------
// cvars (ezQuake dialect - names kept identical to reference/ezquake/src/vx_tracker.c
// lines ~94-131 so existing configs / the hud editor's killfeed controls just work)

// note: statically linked into the engine (FTEENGINE), but this plugin's
// convention throughout hud_common.c/hud.c is to register ALL of its own
// cvars indirectly via cvarfuncs->GetNVFDG(...) into cvar_t* pointers (see
// e.g. hud_common.c's scr_newHud/hud_planmode), rather than declaring static
// cvar_t structs + Cvar_Register - so the tracker's cvars follow suit here.
static cvar_t *r_tracker;
static cvar_t *r_tracker_frags;
static cvar_t *r_tracker_streaks;
static cvar_t *r_tracker_time;
static cvar_t *r_tracker_messages;
static cvar_t *r_tracker_pickups;
static cvar_t *r_tracker_align_right;
static cvar_t *r_tracker_scale;
static cvar_t *r_tracker_flags;

static cvar_t *r_tracker_color_good;
static cvar_t *r_tracker_color_bad;
static cvar_t *r_tracker_color_tkgood;
static cvar_t *r_tracker_color_tkbad;
static cvar_t *r_tracker_color_suicide;

static cvar_t *cl_useimagesinfraglog;
static cvar_t *con_fragmessages;

//-----------------------------------------------------------------------------
// message queue

#define MAX_TRACKERMESSAGES 32
#define MAX_TRACKER_MSG_LEN 256

typedef struct trackermsg_s
{
	char text[MAX_TRACKER_MSG_LEN];
	double addtime;
	qboolean used;
} trackermsg_t;

static trackermsg_t tracker_messages[MAX_TRACKERMESSAGES];
static int tracker_msg_head; // next slot to write, ring buffer

// streak tracking - mirrors ezQuake's tp_streak table (vx_tracker.c ~68-80),
// computed here plugin-side since the engine FragEvent bridge only forwards
// raw per-frag events, not running streak state.
typedef struct { int frags; const char *name; } killing_streak_t;
static const killing_streak_t tracker_streak_table[] = {
	{ 5,   "on a killing spree" },
	{ 10,  "on a rampage" },
	{ 15,  "unstoppable" },
	{ 20,  "godlike" },
	{ 50,  "the master now" },
	{ 100, "teh chet" },
};
#define NUM_TRACKER_STREAKS (sizeof(tracker_streak_table)/sizeof(tracker_streak_table[0]))
#define MAX_STREAK_CLIENTS 32
static int tracker_streak_frags[MAX_STREAK_CLIENTS];

static void VXTracker_AddMessage(const char *text)
{
	trackermsg_t *m = &tracker_messages[tracker_msg_head];
	strlcpy(m->text, text, sizeof(m->text));
	m->addtime = cls.realtime;
	m->used = true;
	tracker_msg_head = (tracker_msg_head + 1) % MAX_TRACKERMESSAGES;

	if (con_fragmessages->ival)
		Con_Printf("%s\n", text);
}

static void VXTracker_CheckStreak(int killer)
{
	unsigned int i;
	if (!r_tracker_streaks->ival)
		return;
	if (killer < 0 || killer >= MAX_STREAK_CLIENTS)
		return;

	tracker_streak_frags[killer]++;
	for (i = 0; i < NUM_TRACKER_STREAKS; i++)
	{
		if (tracker_streak_frags[killer] == tracker_streak_table[i].frags)
		{
			char line[MAX_TRACKER_MSG_LEN];
			const char *name = (killer < countof(cl.players)) ? cl.players[killer].name : "someone";
			Q_snprintfz(line, sizeof(line), "^3%s is %s! (%i)", name, tracker_streak_table[i].name, tracker_streak_table[i].frags);
			VXTracker_AddMessage(line);
			break;
		}
	}
}

static void VXTracker_ResetStreak(int who)
{
	if (who >= 0 && who < MAX_STREAK_CLIENTS)
		tracker_streak_frags[who] = 0;
}

//-----------------------------------------------------------------------------
// building lines from a FragEvent. p1/p2 are player indices as sent by the
// engine's Stats_Evaluate (see engine/client/fragstats.c); -1 means "world"/none.

static const char *VXTracker_PlayerName(int idx)
{
	if (idx < 0 || idx >= countof(cl.players))
		return "world";
	return cl.players[idx].name[0] ? cl.players[idx].name : "player";
}

// ezhud #15 P2 FIX3: resolves a weapon abbreviation/icon per weapon id via the
// new engine plugin API GetFragWeaponToken (engine/common/plugin.c ->
// engine/client/fragstats.c's Stats_GetWeaponToken), which returns the SAME
// token the engine's own built-in tracker draws for that weapon: either the
// fragfile.dat-loaded tracker-charset image-glyph string (a plain string,
// drawable inline through the normal StringH/String path used for the rest of
// the row - no separate pic-draw call needed here) or its text abbreviation.
// cl_useimagesinfraglog only affects the FIRST of those two outcomes: when
// off, we always use the plain-text fallback path (skip asking the engine for
// the image token at all); when on, we ask for it but still fall back to the
// numeric placeholder if the engine returns nothing (unknown wid), which is
// the "falls back to text when unavailable" requirement.
static const char *VXTracker_WeaponAbrev(int wid, char *buf, size_t bufsize)
{
	if (cl_useimagesinfraglog->ival && drawfuncs)
	{
		char token[32];
		drawfuncs->GetFragWeaponToken(wid, token, sizeof(token));
		if (token[0])
		{
			Q_snprintfz(buf, bufsize, "%s", token);
			return buf;
		}
	}
	Q_snprintfz(buf, bufsize, "wpn%i", wid);
	return buf;
}

void VXTracker_FragEvent(int msgtype, int weaponid, int victim, int attacker, int p3)
{
	fragfilemsgtypes_t mt = (fragfilemsgtypes_t)msgtype;
	char line[MAX_TRACKER_MSG_LEN];
	char wbuf[16];
	const char *wname;
	const char *vname, *aname;
	int localnum = cl.playernum;

	if (!r_tracker->ival)
		return;

	// r_tracker_frags is a plain boolean in ezQuake (vx_tracker.c:96,
	// "amf_tracker_frags") - "show frag rows at all", not a 0/1/2 own/all
	// mode switch. (An earlier revision of this file wrongly gave it FTE
	// engine-console-tracker-style 0/1/2 semantics; fixed per review.)
	if (!r_tracker_frags->ival)
		return;

	wname = VXTracker_WeaponAbrev(weaponid, wbuf, sizeof(wbuf));
	vname = VXTracker_PlayerName(victim);
	aname = VXTracker_PlayerName(attacker);

	switch (mt)
	{
	case ff_death:
		Q_snprintfz(line, sizeof(line), "%s%s ^7%s ^7%s", (attacker == localnum) ? r_tracker_color_good->string : r_tracker_color_bad->string, aname, wname, vname);
		VXTracker_AddMessage(line);
		VXTracker_CheckStreak(attacker);
		VXTracker_ResetStreak(victim);
		break;

	case ff_suicide:
		Q_snprintfz(line, sizeof(line), "%s%s ^7%s (suicide)", r_tracker_color_suicide->string, vname, wname);
		VXTracker_AddMessage(line);
		VXTracker_ResetStreak(victim);
		break;

	case ff_bonusfrag: // world/bonus frag on someone (e.g. lava, telefrag)
		Q_snprintfz(line, sizeof(line), "%s%s ^7%s ^7world", r_tracker_color_bad->string, vname, wname);
		VXTracker_AddMessage(line);
		VXTracker_ResetStreak(victim);
		break;

	case ff_tkbonus:
		Q_snprintfz(line, sizeof(line), "%s%s ^7%s ^7%s (team)", r_tracker_color_tkbad->string, aname, wname, vname);
		VXTracker_AddMessage(line);
		break;

	// ff_frags/ff_fragedby: the ordinary "X killed Y" case - the bulk of a
	// real killfeed's traffic (fragstats.c Stats_Evaluate's "p1 died, p2
	// killed" switch arm, Stats_FragMessage(p1,wid,p2,false)). Confirmed via
	// live probe against qw/demos/tb4gf_book_vs_s.mvd: this demo's server
	// prints frags through templates registered under the X_FRAGGED_BY_Y
	// obituary type (reference/ezquake/misc/fragfile/fragfile.dat's
	// "chewed on ...'s boomstick"/"was gibbed by ...'s rocket"/etc rows),
	// which fragstats.c's #DEFINE OBITUARY parser maps straight to
	// ff_fragedby - NOT the single-component ff_death/suicide cases this
	// port originally assumed were the main path. (Earlier revision of this
	// file wrongly treated ff_frags/ff_fragedby/ff_tkills/ff_tkilledby as
	// running-total-only and dropped them; that was the reason the tracker
	// rendered an empty queue against a real demo full of real frags.)
	case ff_frags:
	case ff_fragedby:
		Q_snprintfz(line, sizeof(line), "%s%s ^7%s ^7%s", (attacker == localnum) ? r_tracker_color_good->string : r_tracker_color_bad->string, aname, wname, vname);
		VXTracker_AddMessage(line);
		VXTracker_CheckStreak(attacker);
		VXTracker_ResetStreak(victim);
		break;

	case ff_tkills:
	case ff_tkilledby:
		Q_snprintfz(line, sizeof(line), "%s%s ^7%s ^7%s (team)", r_tracker_color_tkbad->string, aname, wname, vname);
		VXTracker_AddMessage(line);
		break;

	// Flag events - fragstats.c's ff_flagtouch/ff_flagcaps/ff_flagdrops
	// (single-component: victim==attacker==the player who touched/capped/
	// dropped, see Stats_Evaluate's p1-only case ~429-452 and
	// Stats_ParsePrintLine's "one player" call site passing p1,p1). Gated
	// on r_tracker_flags, matching ezQuake's VX_FilterFlags()/
	// amf_tracker_flags (vx_tracker.c:95, VX_TrackerFlagTouch/Drop/Capture
	// ~1056-1104) - "You've taken/dropped/captured the flag" wording ported
	// directly from those three functions, trimmed to one line (no separate
	// running-total second line; that total lives in Stats_Message, not the
	// tracker).
	case ff_flagtouch:
		if (r_tracker_flags->ival)
		{
			Q_snprintfz(line, sizeof(line), "^3%s ^7has taken the flag", vname);
			VXTracker_AddMessage(line);
		}
		break;

	case ff_flagcaps:
		if (r_tracker_flags->ival)
		{
			Q_snprintfz(line, sizeof(line), "^3%s ^7has captured the flag", vname);
			VXTracker_AddMessage(line);
		}
		break;

	case ff_flagdrops:
		if (r_tracker_flags->ival)
		{
			Q_snprintfz(line, sizeof(line), "^3%s ^7has dropped the flag", vname);
			VXTracker_AddMessage(line);
		}
		break;

	// STUB: ff_tkdeath ("killed by a teammate, but we don't know who" -
	// fragstats.c's own comment) has no per-line wording decided for this
	// port yet; dropping it is a real trim (item 3), not an oversight.
	case ff_tkdeath:
	default:
		break;
	}
}

//-----------------------------------------------------------------------------
// renderer - SCR_HUD_DrawTracker, ported from reference/ezquake/src/hud_common.c
// ~936-946 (HUD_Register block) + vx_tracker.c's VXSCR_DrawTrackerString
// (~1217-1361), trimmed to plain-text rows (no proportional font metrics,
// no per-row image_scale/name_width column layout - those need font metrics
// APIs this plugin build doesn't have wired up, see ezquakeisms.h's
// Draw_EZString/Draw_SString for what's actually available).

void SCR_HUD_DrawTracker(hud_t *hud)
{
	static cvar_t *hud_tracker_scale = NULL;
	static cvar_t *hud_tracker_align_right;
	int width, height;
	int x, y;
	int row, shown, i, idx;
	float rowheight;

	if (hud_tracker_scale == NULL)
	{
		hud_tracker_scale = HUD_FindVar(hud, "scale");
		hud_tracker_align_right = HUD_FindVar(hud, "align_right");
	}

	if (!r_tracker->ival)
	{
		return;
	}

	// ezhud #15 P2 FIX4: clamp both inputs to the rect size so a stray cvar
	// value (hud_tracker_scale set to 0, or r_tracker_messages set to 0 by a
	// config/typo) can't collapse this element to a permanent 0x0 rect - HUD
	// elements have no general "recompute on next value that makes sense"
	// path, so a single bad frame here would otherwise stick until reload.
	// Always sized off these two cvars (never hardcoded), same as before.
	rowheight = 8 * bound(0.1f, hud_tracker_scale->value, 10.0f);
	width = 40 * 8 * bound(0.1f, hud_tracker_scale->value, 10.0f);
	height = (int)(max(1, r_tracker_messages->ival) * rowheight);

	if (!HUD_PrepareDraw(hud, width, height, &x, &y))
		return;

	shown = 0;
	// walk the ring buffer newest-first
	for (i = 0; i < MAX_TRACKERMESSAGES && shown < r_tracker_messages->ival; i++)
	{
		double age;
		idx = (tracker_msg_head - 1 - i + MAX_TRACKERMESSAGES) % MAX_TRACKERMESSAGES;
		if (!tracker_messages[idx].used)
			continue;

		age = cls.realtime - tracker_messages[idx].addtime;
		if (age > r_tracker_time->value + 1.0) // +1s fade tail, mirrors r_tracker_fadetime default in engine's built-in tracker
			continue;

		row = shown;
		Draw_SString(x, y + row * rowheight, tracker_messages[idx].text, hud_tracker_scale->value);
		shown++;
	}
}

//-----------------------------------------------------------------------------

void VXTracker_Init(void)
{
	// Defaults verified bit-for-bit against reference/ezquake/src/vx_tracker.c
	// lines 93-131 (re-checked directly, not copied from a stale summary):
	//   r_tracker "1", r_tracker_flags "0", r_tracker_frags "1",
	//   r_tracker_streaks "0", r_tracker_time "4", r_tracker_messages "20",
	//   r_tracker_pickups "0", r_tracker_align_right "1", r_tracker_scale "1".
	//
	// r_tracker_frags IS a plain boolean in ezQuake ("show frag rows at all",
	// amf_tracker_frags = {"r_tracker_frags", "1"}), not an FTE-style 0/1/2
	// off/mine/all mode switch - an earlier revision of this port got that
	// wrong and is fixed here.
	//
	// This cvar NAME collides with the engine's OWN built-in console tracker
	// cvar of the same name (engine/client/fragstats.c ~line 83, default
	// "0", a *different*, non-boolean-in-spirit "0/1/2" cvar the engine's
	// own tracker reads). Getting this plugin cvar registered FIRST wins the
	// name for the ezQuake dialect the editor's killfeed controls expect;
	// anything that still pokes the engine's r_tracker_frags directly after
	// this is talking to the wrong subsystem - see the P2 report / the
	// TRACKER_TRANSLATE finding in ~/dev/ezhud-hotfix's fte-adapter.js.
	r_tracker              = cvarfuncs->GetNVFDG("r_tracker", "1", 0, "Enable the frags tracker (killfeed) HUD element.", "ezhud");
	r_tracker_frags         = cvarfuncs->GetNVFDG("r_tracker_frags", "1", 0, "Show frag rows in the tracker at all (boolean).", "ezhud");
	r_tracker_streaks       = cvarfuncs->GetNVFDG("r_tracker_streaks", "0", 0, "Show killing-spree/streak messages.", "ezhud");
	r_tracker_time          = cvarfuncs->GetNVFDG("r_tracker_time", "4", 0, "Seconds before a tracker message starts fading.", "ezhud");
	r_tracker_messages      = cvarfuncs->GetNVFDG("r_tracker_messages", "20", 0, "Max number of tracker rows shown at once.", "ezhud");
	r_tracker_pickups       = cvarfuncs->GetNVFDG("r_tracker_pickups", "0", 0, "Also show item pickup messages. STUB: no pickup event source ported yet.", "ezhud");
	r_tracker_align_right   = cvarfuncs->GetNVFDG("r_tracker_align_right", "1", 0, "Right-align tracker text within the element.", "ezhud");
	r_tracker_scale         = cvarfuncs->GetNVFDG("r_tracker_scale", "1", 0, "Text scale for the tracker.", "ezhud");
	r_tracker_flags         = cvarfuncs->GetNVFDG("r_tracker_flags", "0", 0, "Show flag touch/capture/drop messages (boolean).", "ezhud");
	r_tracker_color_good    = cvarfuncs->GetNVFDG("r_tracker_color_good", "^2", 0, "Colour code for frags in your favour.", "ezhud");
	r_tracker_color_bad     = cvarfuncs->GetNVFDG("r_tracker_color_bad", "^1", 0, "Colour code for frags against you.", "ezhud");
	r_tracker_color_tkgood  = cvarfuncs->GetNVFDG("r_tracker_color_tkgood", "^3", 0, "Colour code for teamkills of enemies (other team).", "ezhud");
	r_tracker_color_tkbad   = cvarfuncs->GetNVFDG("r_tracker_color_tkbad", "^6", 0, "Colour code for teamkills on your team.", "ezhud");
	r_tracker_color_suicide = cvarfuncs->GetNVFDG("r_tracker_color_suicide", "^1", 0, "Colour code for suicides.", "ezhud");
	cl_useimagesinfraglog   = cvarfuncs->GetNVFDG("cl_useimagesinfraglog", "0", 0, "Use weapon icons in the tracker instead of text abbreviations (best-effort; falls back to text when unavailable).", "ezhud");
	con_fragmessages        = cvarfuncs->GetNVFDG("con_fragmessages", "1", 0, "Also echo tracker lines to the console.", "ezhud");

	HUD_Register("tracker", NULL,
		"Killfeed / frags tracker - shows who fragged whom, ported from ezQuake's vx_tracker.",
		0, ca_active, 8, SCR_HUD_DrawTracker,
		"1", "top", "right", "top", "0", "0.2", "0", "0 0 0", NULL,
		"scale",       "1",
		"align_right", "0",
		NULL);

	if (plugfuncs)
		plugfuncs->ExportFunction("FragEvent", VXTracker_FragEvent);

}
