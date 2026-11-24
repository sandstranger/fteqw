#ifndef _DK_THREAD_H
#define _DK_THREAD_H

#ifdef _WIN32
#include "CmnHdr.H"
#include <windows.h>
#include "SWMRG.H"

class CThreadSync
{
public:
    CThreadSync();
    CThreadSync( LPCSTR lpFileMappingName, LPCSTR lpObjectName );
    ~CThreadSync();
    
    BOOL Init( LPCSTR lpFileMappingName, LPCSTR lpObjectName );
    BOOL CreateFileMap();
    BOOL OpenFileForRead();
    BOOL OpenFileForWrite();
    void CreateEvent();
    void OpenEvent();
    void CreateThread( PTHREAD_START lpStartAddr );
    void Destroy();
    int WaitToRead( DWORD dwTimeOut );
    void DoneReading();
    int WaitToWrite( DWORD dwTimeOut );
    void DoneWriting();
    void WriteToSharedData( const char *pData );
    void ReadSharedData( char *pData );
    int CreateFileMap_OpenWrite_CreateEvent();
    int CreateFileMap_OpenRead_CreateEvent();
    int OpenFileMapForRead_OpenEvent();
    int OpenFileMapForWrite_OpenEvent();

private:
    // single-writer/multiple-reader guard synchronization object
    SWMRG g_SWMRG;

    // Array of thread handles needed for process termination
    HANDLE g_hThread;

    // Flag indicating when the threads should terminate
    // The flag is volatile because it is changed asynchronously
    long volatile g_lTerminate;

    HANDLE SharedFile;
    LPVOID SharedMem;
    LPSTR  String;

    char szFileMappingName[64];
    char szObjectName[64];
};

#else
//FTE, elsewhere: no threads. What one would run runs then and there, and there's never anyone else to wait for.
#include <string.h>
typedef unsigned (*PTHREAD_START)(void *);
class CThreadSync
{
public:
	CThreadSync() {shared[0] = 0;}
	CThreadSync(const char *a, const char *b) {shared[0] = 0;}
	int Init(const char *a, const char *b) {return 1;}
	int CreateFileMap() {return 1;}
	int OpenFileForRead() {return 1;}
	int OpenFileForWrite() {return 1;}
	void CreateEvent() {}
	void OpenEvent() {}
	void CreateThread(PTHREAD_START start) {start(0);}
	void Destroy() {}
	int WaitToRead(unsigned long timeout) {return 1;}
	void DoneReading() {}
	int WaitToWrite(unsigned long timeout) {return 1;}
	void DoneWriting() {}
	void WriteToSharedData(const char *data) {strncpy(shared, data, sizeof(shared)-1); shared[sizeof(shared)-1] = 0;}
	void ReadSharedData(char *data) {strcpy(data, shared);}
	int CreateFileMap_OpenWrite_CreateEvent() {return 1;}
	int CreateFileMap_OpenRead_CreateEvent() {return 1;}
	int OpenFileMapForRead_OpenEvent() {return 1;}
	int OpenFileMapForWrite_OpenEvent() {return 1;}
private:
	char shared[256];
};
#endif
#endif 
