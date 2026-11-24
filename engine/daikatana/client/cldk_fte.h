/*
What Daikatana's client code (daikatana/client) has of FTE's client (client/cldk_fte.c), and what FTE's client calls
of it. Plain C: the game's headers and FTE's don't mix.
*/
#ifndef CLDK_FTE_H
#define CLDK_FTE_H

#ifdef __cplusplus
extern "C" {
#endif

//FTE's, for the game's client code
void		*CLDKF_RegisterPic(const char *name, int *width, int *height);	//null if there's no such image
void		CLDKF_DrawPic(void *pic, float x, float y, float w, float h, float s1, float t1, float s2, float t2, const float *rgba);
void		CLDKF_DrawFill(float x, float y, float w, float h, const float *rgba);
void		CLDKF_VidSize(int *width, int *height);		//the virtual screen the HUD's drawn on
int			CLDKF_Milliseconds(void);					//the client's time
float		CLDKF_CvarValue(const char *name, const char *defaultvalue);
void		CLDKF_CvarSet(const char *name, const char *value);
void		CLDKF_LocalSound(const char *name);
void		CLDKF_AddCommand(const char *text);			//to the console (forwarded to the server, as the game's are)
int			CLDKF_Stat(int i);							//Quake II's (short) stats
int			CLDKF_RDFlags(void);
const char	*CLDKF_MapName(void);
int			CLDKF_ModelIndex(const char *name);			//0 if the server hasn't precached it
const char	*CLDKF_ModelName(int modelindex);			//"" if there's none
void		*CLDKF_LoadFile(const char *name, int *length);
void		CLDKF_FreeFile(void *buf);
void		CLDKF_Music(const char *name, int loop);		//null: stop

//the game's client code, for FTE's
void		CLDK_ParseMessage(const unsigned char *data, int length);	//an svcq2dk_message's
void		CLDK_DrawHUD(void);
void		CLDK_Reset(void);							//new connection, new level

#ifdef __cplusplus
}
#endif
#endif
