/*
What FTE's Quake II server and client have on top of the protocol for Daikatana (daikatana/fte/svdk_game.cpp
writes it, daikatana/client reads it). Plain defines: C and C++, FTE's side and the game's.
*/
#ifndef DK_FTEPROTO_H
#define DK_FTEPROTO_H

//an svc of FTE's (common/protocol.h has it too): [short length][that many bytes of Daikatana's own protocol,
//its svcs (SVC_TEMP_ENTITY, SVC_INVENTORY...) as its server wrote them]
#define svcq2dk_message		64

//in those, ops of FTE's past Daikatana's svcs
#define svcdk_fte_stats		240		//[MAX_STATS longs][long pm_flags][byte pm_type][float max_speed]: the player's stats
									//(Quake II's are shorts), its pmove's flags, type and speed (Daikatana's)
#define svcdk_fte_entstate	241		//[short entnum][short DKX_ bits][the fields they say]: what of an entity's state
									//Quake II's has no room for, as it changes (reliably, to everyone)

//svcdk_fte_entstate's fields, in this order
#define DKX_RESET			0x0001	//everything back to nothing, before the rest
#define DKX_MODEL2			0x0002	//[long]
#define DKX_MODEL3			0x0004	//[long]
#define DKX_MODEL4			0x0008	//[long]
#define DKX_EFFECTS2		0x0010	//[long]
#define DKX_FLAGS			0x0020	//[long]
#define DKX_ALPHA			0x0040	//[float]
#define DKX_COLOR			0x0080	//[3 floats]
#define DKX_SCALE			0x0100	//[3 floats] render_scale
#define DKX_MINSMAXS		0x0200	//[6 floats]
#define DKX_ANGLEDELTA		0x0400	//[3 floats]
#define DKX_ORIGINHACK		0x0800	//[3 floats]
#define DKX_FRAMEINFO		0x1000	//[short flags, start, end, startloop, endloop, loopcount, sound1frame, sound2frame]
									//[byte sound1index, sound2index, frameinc][float animspeed]
#define DKX_SYNC			0x2000	//[short frame]: the frame its client's animation goes on from
#define DKX_CLUSTERS		0x4000	//[byte count][count of: byte index, 3 floats angles]
#define DKX_SOUND			0x8000	//[byte flags][float volume, min distance, max distance]

#endif
