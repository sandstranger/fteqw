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
#define svcdk_fte_stats		240		//[MAX_STATS longs]: the player's stats (Quake II's are shorts)

#endif
