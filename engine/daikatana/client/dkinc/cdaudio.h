#ifndef DKINC_CDAUDIO_H	//FTE: (included more than once here)
#define DKINC_CDAUDIO_H

int		CDAudio_Init(void);
void	CDAudio_Shutdown(void);
void	CDAudio_Play(int track, qboolean looping);
void	CDAudio_Stop(void);
void	CDAudio_Update(void);
void	CDAudio_Activate (qboolean active);

#endif
