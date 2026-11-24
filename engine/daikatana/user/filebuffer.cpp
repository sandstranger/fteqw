
#include <stdio.h>
#include "memmgrcommon.h"
#include "filebuffer.h"

CFileBuffer::CFileBuffer( int nNum )
{
	nPos = 0;
	nCounter = 0;
	nNumChars = nNum;
	pBuffer = (char*)memmgr.X_Malloc(sizeof(char)*nNum,MEM_TAG_FILE);
}

CFileBuffer::~CFileBuffer()
{
	memmgr.X_Free(pBuffer);
	pBuffer = NULL;
}

// get next line
//FTE: bounded by the buffer (it tested nPos, which doesn't move, and read past the end) and by n; its EOF test is
//on a signed char, as on x86 (char's unsigned on arm, where 0xff never equalled EOF)
char *CFileBuffer::FGetLine( char *string, int n )
{
	int nStringCounter = 0;
	int nCounter = nPos;
	if ( !string || n <= 0 || !pBuffer )
	{
		return NULL;
	}
	while ( nCounter < nNumChars && pBuffer[nCounter] != '\n' && pBuffer[nCounter] != '\r' && pBuffer[nCounter] != '\0' &&
			(signed char)pBuffer[nCounter] != EOF )
	{
		if ( nStringCounter < n-1 )
			string[nStringCounter++] = pBuffer[nCounter];
		nCounter++;
	}

	if ( nStringCounter == 0 )
	{
		return NULL;
	}

	// get rest of the line including line feed
	while ( nCounter < nNumChars && (pBuffer[nCounter] == ' ' || pBuffer[nCounter] == '\n' || pBuffer[nCounter] == '\r') )
	{
		if ( nStringCounter < n-1 )
			string[nStringCounter++] = pBuffer[nCounter];
		nCounter++;
	}
	string[nStringCounter] = '\0';

	nPos = nCounter;

	return string;
}

CFileBuffer *FILEBUFFER_Delete( CFileBuffer *pFileBuffer )
{
	delete pFileBuffer;
	// SCG[2/16/00]: memmgr.X_Free(pFileBuffer);

	return NULL;
}
