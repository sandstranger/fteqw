/*
Persistent GLSL program binary cache, bounded RAM binary cache and bounded GPU program residency.

Three different resources, kept apart on purpose:

  1. GLSLWarm (gl_shader.c)   - WHICH permutations to build at map load. A list of names, no binaries.
  2. Disk + RAM binary cache   - WHAT the driver produced for an exact source text (glGetProgramBinary).
                                 Disk: one file per program, versioned header, CRC32, atomic rename.
                                 RAM:  small LRU of recently used binaries (bytes AND entry limits).
  3. GPU program residency     - linked gl program objects. LRU-bounded; a permutation may only be evicted
                                 when its binary is persisted, it was not used for r_shadercache_gpu_minage
                                 frames, and it is not permutation 0 (the program's fallback). The next use
                                 reloads it from the RAM/disk binary instead of recompiling.

Keys: sha256 of every string handed to the driver for every stage (gl_vidcommon.c GLSlang_HashProgram), so
permutation defines, includes, cvar-driven #defines and the version line are all part of it. Files live in
<dir>/<driverhash>/<key>.bin; the driver hash covers vendor, renderer, GL/GLSL version strings (Mali and Adreno
put the driver build there), the capability profile, the pointer size and this build. A binary from another
driver or build is never even looked at, and glProgramBinary's link status is still checked on every load.

The directory comes from outside (Android: the app's code cache dir via JNA, FTE_SetShaderCacheDirectory),
falling back to <homedir>/shadercache. No gl call is made from the setter; it only records and prepares the path.
*/
#include "quakedef.h"
#ifdef GLQUAKE
#include "glquake.h"
#include "shader.h"
#include <stdio.h>
#include <errno.h>
#ifndef _WIN32
#include <unistd.h>
#include <sys/stat.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
#define GLSC_EXPORT __attribute__((used)) __attribute__((visibility("default")))
#else
#define GLSC_EXPORT
#endif

//per-ABI defaults: armeabi-v7a has a 32-bit address space and usually little RAM, so it gets small caches
#if defined(__arm__) && !defined(__aarch64__)
	#define GLSC_DEF_RAMKB		"768"
	#define GLSC_DEF_RAMENTRIES	"48"
	#define GLSC_DEF_GPUPROGS	"160"
	#define GLSC_DEF_WRITEKB	"2048"
	#define GLSC_ABI			"armv7"
#elif defined(__aarch64__)
	#define GLSC_DEF_RAMKB		"4096"
	#define GLSC_DEF_RAMENTRIES	"192"
	#define GLSC_DEF_GPUPROGS	"384"
	#define GLSC_DEF_WRITEKB	"8192"
	#define GLSC_ABI			"arm64"
#else
	#define GLSC_DEF_RAMKB		"8192"
	#define GLSC_DEF_RAMENTRIES	"512"
	#define GLSC_DEF_GPUPROGS	"0"		//desktop: unbounded unless asked
	#define GLSC_DEF_WRITEKB	"16384"
	#define GLSC_ABI			"other"
#endif

static cvar_t r_shadercache = CVARFD("r_shadercache", "1", CVAR_ARCHIVE, "Keep linked GLSL programs as driver binaries (glProgramBinary) on disk, so later runs and later loads skip compiling and linking. 0 compiles from source every time.");
static cvar_t r_shadercache_ram_kb = CVARD("r_shadercache_ram_kb", GLSC_DEF_RAMKB, "RAM budget (KB) for recently used program binaries kept in memory. Least recently used binaries are evicted first; they stay on disk.");
static cvar_t r_shadercache_ram_entries = CVARD("r_shadercache_ram_entries", GLSC_DEF_RAMENTRIES, "Maximum number of program binaries kept in RAM.");
static cvar_t r_shadercache_gpu_programs = CVARD("r_shadercache_gpu_programs", GLSC_DEF_GPUPROGS, "Maximum number of linked shader permutations kept as GL program objects (0 unbounded). Unused ones whose binary is cached are released and reloaded from the binary on their next use.");
static cvar_t r_shadercache_gpu_minage = CVARD("r_shadercache_gpu_minage", "900", "Frames a permutation must go unused before it may be released (protects everything the current frames draw, and stops evict/reload cycles).");
static cvar_t r_shadercache_write_kb = CVARD("r_shadercache_write_kb", GLSC_DEF_WRITEKB, "Most binary data (KB) queued for the background writer at once. Beyond it a write happens immediately.");

typedef struct glsc_ramentry_s
{
	struct glsc_ramentry_s *prev, *next;	//lru: head = most recent
	unsigned char key[32];
	GLenum format;
	unsigned int flags;
	size_t size;
	void *data;
} glsc_ramentry_t;

static struct
{
	qboolean inited;
	qboolean usable;			//this context can store/load binaries and the directory is writable
	char basedir[MAX_OSPATH];	//from FTE_SetShaderCacheDirectory, or derived
	char dir[MAX_OSPATH];		//basedir/<driverhash>
	unsigned char driverhash[32];
	unsigned char lastdriverhash[32];	//the ram cache belongs to this driver
	void *mutex;				//protects pendingwrite (worker jobs)

	//ram binary cache
	glsc_ramentry_t *ramhead, *ramtail;
	size_t rambytes, rampeak;
	unsigned int ramcount;

	//gpu residency
	struct programpermu_s *reshead, *restail;
	unsigned int rescount, respeak;
	unsigned int frame;
	qboolean lowmem;

	//writer
	size_t pendingwrite;

	//stats
	unsigned int diskhits, diskmisses, diskinvalid, ramhits, rammisses, ramevictions;
	size_t ramevictedbytes;
	unsigned int binaryfail, binaryreloads, sourcecompiles, stores, storefail, allocfail;
	unsigned int gpuevict_budget, gpuevict_lowmem, gpuevict_manual;
	unsigned int writes, writefail, syncwrites;
} glsc;

static char glsc_external_dir[MAX_OSPATH];	//written by the JNA setter, read at renderer init

static void GLSC_Lock(void)
{
	if (glsc.mutex)
		Sys_LockMutex(glsc.mutex);
}
static void GLSC_Unlock(void)
{
	if (glsc.mutex)
		Sys_UnlockMutex(glsc.mutex);
}

/*----------------------------------------------------------------------------------------------------------*/
//crc32 (ieee 802.3)
static unsigned int glsc_crctable[256];
static unsigned int GLSC_CRC32(const void *data, size_t len)
{
	const unsigned char *p = data;
	unsigned int c = 0xffffffffu;
	if (!glsc_crctable[1])
	{
		unsigned int i, j, v;
		for (i = 0; i < 256; i++)
		{
			v = i;
			for (j = 0; j < 8; j++)
				v = (v & 1) ? (0xedb88320u ^ (v >> 1)) : (v >> 1);
			glsc_crctable[i] = v;
		}
	}
	while (len--)
		c = glsc_crctable[(c ^ *p++) & 0xff] ^ (c >> 8);
	return c ^ 0xffffffffu;
}

static void GLSC_Hex(char *out, const unsigned char *in, size_t bytes)
{
	static const char hex[] = "0123456789abcdef";
	size_t i;
	for (i = 0; i < bytes; i++)
	{
		out[i*2+0] = hex[in[i]>>4];
		out[i*2+1] = hex[in[i]&15];
	}
	out[bytes*2] = 0;
}

/*----------------------------------------------------------------------------------------------------------*/
//directory handling (native paths: the directory comes from the os, not from the game filesystem)
static qboolean GLSC_MakeDirs(const char *path)
{
	char tmp[MAX_OSPATH];
	char *s;
	Q_strncpyz(tmp, path, sizeof(tmp));
	if (!*tmp)
		return false;
	for (s = tmp+1; *s; s++)
	{
		if (*s == '/' || *s == '\\')
		{
			char c = *s;
			*s = 0;
			if (s[-1] != ':')	//windows drive letters
				Sys_mkdir(tmp);
			*s = c;
		}
	}
	Sys_mkdir(tmp);
	return true;
}
static qboolean GLSC_DirWritable(const char *dir)
{
	char probe[MAX_OSPATH];
	FILE *f;
	Q_snprintfz(probe, sizeof(probe), "%s/.probe", dir);
	f = fopen(probe, "wb");
	if (!f)
		return false;
	if (fwrite("ok", 1, 2, f) != 2)
	{
		fclose(f);
		remove(probe);
		return false;
	}
	fclose(f);
	remove(probe);
	return true;
}

//JNA / native C ABI. Safe to call before the renderer exists, from any thread, any number of times; it never
//touches gl. The path is UTF-8 (android paths are bytes, so it is used as it is). The directory is created if
//needed and checked for writability when the renderer next initialises.
GLSC_EXPORT void FTE_SetShaderCacheDirectory(const char *path)
{
	size_t l;
	if (!path || !*path)
	{
		*glsc_external_dir = 0;
		return;
	}
	l = strlen(path);
	if (l >= sizeof(glsc_external_dir) - 80)
	{	//leave room for "/<driverhash>/<key>.bin.tmpXXXXXXXX"
		*glsc_external_dir = 0;
		return;
	}
	memcpy(glsc_external_dir, path, l+1);
	while (l > 1 && (glsc_external_dir[l-1] == '/' || glsc_external_dir[l-1] == '\\'))
		glsc_external_dir[--l] = 0;
	GLSC_MakeDirs(glsc_external_dir);
}

/*----------------------------------------------------------------------------------------------------------*/
//ram binary cache
static void GLSC_RamUnlink(glsc_ramentry_t *e)
{
	if (e->prev)
		e->prev->next = e->next;
	else
		glsc.ramhead = e->next;
	if (e->next)
		e->next->prev = e->prev;
	else
		glsc.ramtail = e->prev;
	e->prev = e->next = NULL;
}
static void GLSC_RamLinkHead(glsc_ramentry_t *e)
{
	e->prev = NULL;
	e->next = glsc.ramhead;
	if (glsc.ramhead)
		glsc.ramhead->prev = e;
	glsc.ramhead = e;
	if (!glsc.ramtail)
		glsc.ramtail = e;
}
static void GLSC_RamFree(glsc_ramentry_t *e, qboolean evicted)
{
	GLSC_RamUnlink(e);
	glsc.rambytes -= e->size;
	glsc.ramcount--;
	if (evicted)
	{
		glsc.ramevictions++;
		glsc.ramevictedbytes += e->size;
	}
	BZ_Free(e->data);
	Z_Free(e);
}
static void GLSC_RamClear(void)
{
	while (glsc.ramtail)
		GLSC_RamFree(glsc.ramtail, true);
}
static size_t GLSC_RamBudget(void)
{
	return (size_t)max(0, r_shadercache_ram_kb.ival) * 1024;
}
static glsc_ramentry_t *GLSC_RamFind(const unsigned char key[32])
{
	glsc_ramentry_t *e;
	for (e = glsc.ramhead; e; e = e->next)
		if (!memcmp(e->key, key, 32))
			return e;
	return NULL;
}
//takes ownership of data when it returns true
static qboolean GLSC_RamInsert(const unsigned char key[32], GLenum format, unsigned int flags, void *data, size_t size)
{
	glsc_ramentry_t *e;
	size_t budget = GLSC_RamBudget();
	unsigned int maxentries = max(0, r_shadercache_ram_entries.ival);
	if (!budget || !maxentries || size > budget)
		return false;	//bigger than the whole cache: used directly, never cached (the disk keeps it)
	if ((e = GLSC_RamFind(key)))
		GLSC_RamFree(e, false);
	//make room first: least recently used out
	while (glsc.ramtail && (glsc.rambytes + size > budget || glsc.ramcount + 1 > maxentries))
		GLSC_RamFree(glsc.ramtail, true);
	e = Z_Malloc(sizeof(*e));
	memcpy(e->key, key, 32);
	e->format = format;
	e->flags = flags;
	e->data = data;
	e->size = size;
	GLSC_RamLinkHead(e);
	glsc.rambytes += size;
	glsc.ramcount++;
	if (glsc.rambytes > glsc.rampeak)
		glsc.rampeak = glsc.rambytes;
	return true;
}
static void GLSC_RamEnforce(void)
{
	size_t budget = GLSC_RamBudget();
	unsigned int maxentries = max(0, r_shadercache_ram_entries.ival);
	while (glsc.ramtail && (glsc.rambytes > budget || glsc.ramcount > maxentries))
		GLSC_RamFree(glsc.ramtail, true);
}

/*----------------------------------------------------------------------------------------------------------*/
//disk format
#define GLSC_MAGIC		"FTEGLSC"
#define GLSC_VERSION	2
#define GLSC_MAXBINARY	(16*1024*1024)
typedef struct
{
	char magic[8];
	unsigned int version;
	unsigned int headersize;
	unsigned char key[32];
	unsigned char driver[32];
	unsigned int format;
	unsigned int length;
	unsigned int flags;		//1: usetesselation
	unsigned int payloadcrc;
	unsigned int headercrc;	//of everything above
} glsc_header_t;

static void GLSC_PathForKey(char *out, size_t outsize, const unsigned char key[32])
{
	char hex[65];
	GLSC_Hex(hex, key, 32);
	Q_snprintfz(out, outsize, "%s/%s.bin", glsc.dir, hex);
}

typedef struct
{
	char path[MAX_OSPATH];
	size_t size;	//header + payload
	qbyte data[1];
} glsc_writejob_t;

static qboolean GLSC_WriteFile(const char *path, const void *data, size_t size)
{
	char tmp[MAX_OSPATH+32];
	FILE *f;
	qboolean ok;
	unsigned int salt = (unsigned int)(size_t)data ^ (unsigned int)(Sys_DoubleTime()*1000000);
	Q_snprintfz(tmp, sizeof(tmp), "%s.tmp%08x", path, salt);
	f = fopen(tmp, "wb");
	if (!f)
		return false;
	ok = fwrite(data, 1, size, f) == size;
	if (fflush(f))
		ok = false;
#ifndef _WIN32
	if (ok && fsync(fileno(f)))	//the rename below must not publish a file whose blocks never reached storage
		ok = false;
#endif
	if (fclose(f))
		ok = false;
	if (ok)
	{
#ifdef _WIN32
		remove(path);	//windows rename does not replace
#endif
		if (rename(tmp, path))
			ok = false;
	}
	if (!ok)
		remove(tmp);
	return ok;
}
static void GLSC_WriteJob(void *ctx, void *data, size_t a, size_t b)
{	//runs on a loader thread (or inline when there are none). no gl, no glsc state but the counters.
	glsc_writejob_t *job = data;
	qboolean ok = GLSC_WriteFile(job->path, job->data, job->size);
	GLSC_Lock();
	glsc.pendingwrite -= job->size;
	glsc.writes++;
	if (!ok)
		glsc.writefail++;
	GLSC_Unlock();
	BZ_Free(job);
}

/*----------------------------------------------------------------------------------------------------------*/
//loading a binary into a fresh program object. false (and nothing left behind) when the driver refuses it.
static qboolean GLSC_ProgramFromBinary(GLenum format, const void *data, size_t size, GLuint *outprogram)
{
	GLint linked = 0;
	GLuint prog;
	if (!qglProgramBinary || !size || size > GLSC_MAXBINARY)
		return false;
	while (qglGetError())
		;
	prog = qglCreateProgramObjectARB();
	if (!prog)
		return false;
	qglProgramBinary(prog, format, data, (GLint)size);
	if (qglGetError() == GL_NO_ERROR)
		qglGetProgramParameteriv_(prog, GL_OBJECT_LINK_STATUS_ARB, &linked);
	if (!linked)
	{
		qglDeleteProgramObject_(prog);
		return false;
	}
	*outprogram = prog;
	return true;
}

qboolean GLSC_Available(void)
{
	return glsc.usable && r_shadercache.ival && qglProgramBinary && qglGetProgramBinary;
}

void GLSC_NoteSourceCompile(void)
{
	glsc.sourcecompiles++;
}

qboolean GLSC_TryLoad(const unsigned char key[32], GLuint *outprogram, qboolean *usetesselation)
{
	glsc_ramentry_t *e;
	char path[MAX_OSPATH];
	FILE *f;
	glsc_header_t h;
	void *payload;
	if (!GLSC_Available())
		return false;

	//1: ram
	e = GLSC_RamFind(key);
	if (e)
	{
		if (GLSC_ProgramFromBinary(e->format, e->data, e->size, outprogram))
		{
			*usetesselation = !!(e->flags & 1);
			GLSC_RamUnlink(e);
			GLSC_RamLinkHead(e);
			glsc.ramhits++;
			glsc.binaryreloads++;
			return true;
		}
		//the driver no longer takes it (driver update while running?): forget it everywhere
		glsc.binaryfail++;
		GLSC_RamFree(e, false);
		GLSC_PathForKey(path, sizeof(path), key);
		remove(path);
		return false;
	}
	glsc.rammisses++;

	//2: disk
	GLSC_PathForKey(path, sizeof(path), key);
	f = fopen(path, "rb");
	if (!f)
	{
		glsc.diskmisses++;
		return false;
	}
	if (fread(&h, 1, sizeof(h), f) != sizeof(h) || memcmp(h.magic, GLSC_MAGIC, 8) || h.version != GLSC_VERSION ||
		h.headersize != sizeof(h) || memcmp(h.key, key, 32) || memcmp(h.driver, glsc.driverhash, 32) ||
		h.headercrc != GLSC_CRC32(&h, offsetof(glsc_header_t, headercrc)) || !h.length || h.length > GLSC_MAXBINARY)
	{	//another build's layout, another driver, a different program in a colliding name, or plain corruption
		fclose(f);
		remove(path);
		glsc.diskinvalid++;
		glsc.diskmisses++;
		return false;
	}
	payload = BZ_Malloc(h.length);
	if (!payload)
	{
		fclose(f);
		glsc.allocfail++;
		return false;
	}
	if (fread(payload, 1, h.length, f) != h.length || GLSC_CRC32(payload, h.length) != h.payloadcrc)
	{	//truncated (killed mid-write before the rename existed?) or damaged: only this entry is lost
		fclose(f);
		BZ_Free(payload);
		remove(path);
		glsc.diskinvalid++;
		glsc.diskmisses++;
		return false;
	}
	fclose(f);
	if (!GLSC_ProgramFromBinary(h.format, payload, h.length, outprogram))
	{	//the driver refused it: free the program, drop the entry; the caller recompiles and stores a new one
		BZ_Free(payload);
		remove(path);
		glsc.binaryfail++;
		return false;
	}
	*usetesselation = !!(h.flags & 1);
	glsc.diskhits++;
	if (!GLSC_RamInsert(key, h.format, h.flags, payload, h.length))
		BZ_Free(payload);
	return true;
}

void GLSC_Reject(const unsigned char key[32])
{
	char path[MAX_OSPATH];
	glsc_ramentry_t *e = GLSC_RamFind(key);
	if (e)
		GLSC_RamFree(e, false);
	if (glsc.usable)
	{
		GLSC_PathForKey(path, sizeof(path), key);
		remove(path);
	}
}

qboolean GLSC_Store(const unsigned char key[32], GLuint program, qboolean usetesselation)
{
	GLint len = 0, outlen = 0;
	GLenum format = 0;
	glsc_writejob_t *job;
	glsc_header_t *h;
	size_t total, pending, writebudget;
	void *ramcopy;
	if (!GLSC_Available() || !program)
		return false;
	while (qglGetError())
		;
	qglGetProgramParameteriv_(program, GL_PROGRAM_BINARY_LENGTH, &len);
	if (qglGetError() != GL_NO_ERROR || len <= 0 || len > GLSC_MAXBINARY)
	{
		glsc.storefail++;
		return false;
	}
	total = sizeof(glsc_header_t) + (size_t)len;
	job = BZ_Malloc(offsetof(glsc_writejob_t, data) + total);
	if (!job)
	{
		glsc.allocfail++;
		return false;
	}
	h = (glsc_header_t*)job->data;
	qglGetProgramBinary(program, len, &outlen, &format, job->data + sizeof(glsc_header_t));
	if (qglGetError() != GL_NO_ERROR || outlen <= 0 || outlen > len)
	{	//no binary after all (some drivers without the retrievable hint)
		BZ_Free(job);
		glsc.storefail++;
		return false;
	}
	total = sizeof(glsc_header_t) + (size_t)outlen;
	memset(h, 0, sizeof(*h));
	memcpy(h->magic, GLSC_MAGIC, 8);
	h->version = GLSC_VERSION;
	h->headersize = sizeof(*h);
	memcpy(h->key, key, 32);
	memcpy(h->driver, glsc.driverhash, 32);
	h->format = format;
	h->length = outlen;
	h->flags = usetesselation?1:0;
	h->payloadcrc = GLSC_CRC32(job->data + sizeof(glsc_header_t), outlen);
	h->headercrc = GLSC_CRC32(h, offsetof(glsc_header_t, headercrc));
	job->size = total;
	GLSC_PathForKey(job->path, sizeof(job->path), key);
	glsc.stores++;

	//the ram cache gets its own copy only when it fits its budget
	if ((size_t)outlen <= GLSC_RamBudget() && r_shadercache_ram_entries.ival > 0)
	{
		ramcopy = BZ_Malloc(outlen);
		if (ramcopy)
		{
			memcpy(ramcopy, job->data + sizeof(glsc_header_t), outlen);
			if (!GLSC_RamInsert(key, format, h->flags, ramcopy, outlen))
				BZ_Free(ramcopy);
		}
		else
			glsc.allocfail++;
	}

	//the disk write goes to a loader thread, bounded: past the budget it is written now instead of queued
	writebudget = (size_t)max(0, r_shadercache_write_kb.ival) * 1024;
	GLSC_Lock();
	pending = glsc.pendingwrite;
	if (pending + total <= writebudget && COM_HasWorkers(WG_LOADER))
		glsc.pendingwrite += total;
	else
		pending = (size_t)-1;
	GLSC_Unlock();
	if (pending == (size_t)-1)
	{
		qboolean ok = GLSC_WriteFile(job->path, job->data, job->size);
		glsc.syncwrites++;
		glsc.writes++;
		if (!ok)
			glsc.writefail++;
		BZ_Free(job);
		return ok || GLSC_RamFind(key);
	}
	COM_AddWork(WG_LOADER, GLSC_WriteJob, NULL, job, 0, 0);
	return true;
}

/*----------------------------------------------------------------------------------------------------------*/
//gpu program residency
static void GLSC_ResUnlink(struct programpermu_s *pp)
{
	if (pp->lru_prev)
		pp->lru_prev->lru_next = pp->lru_next;
	else
		glsc.reshead = pp->lru_next;
	if (pp->lru_next)
		pp->lru_next->lru_prev = pp->lru_prev;
	else
		glsc.restail = pp->lru_prev;
	pp->lru_prev = pp->lru_next = NULL;
}
static void GLSC_ResLinkHead(struct programpermu_s *pp)
{
	pp->lru_prev = NULL;
	pp->lru_next = glsc.reshead;
	if (glsc.reshead)
		glsc.reshead->lru_prev = pp;
	glsc.reshead = pp;
	if (!glsc.restail)
		glsc.restail = pp;
}
void GLSC_ProgramResident(struct programpermu_s *pp, struct programshared_s *prog)
{
	if (!pp || pp->resident)
		return;
	pp->owner = prog;
	pp->lastframe = glsc.frame;
	pp->resident = true;
	GLSC_ResLinkHead(pp);
	glsc.rescount++;
	if (glsc.rescount > glsc.respeak)
		glsc.respeak = glsc.rescount;
}
void GLSC_ProgramForget(struct programpermu_s *pp)
{
	if (!pp || !pp->resident)
		return;
	GLSC_ResUnlink(pp);
	pp->resident = false;
	pp->owner = NULL;
	glsc.rescount--;
}
//BE_RenderMeshProgram, once per frame per permutation at most
void GLSC_ProgramTouch(struct programpermu_s *pp)
{
	if (pp->lastframe == glsc.frame || !pp->resident)
		return;
	pp->lastframe = glsc.frame;
	if (glsc.reshead != pp)
	{
		GLSC_ResUnlink(pp);
		GLSC_ResLinkHead(pp);
	}
}
unsigned int GLSC_Frame(void)
{
	return glsc.frame;
}

void GLBE_ForgetProgramCache(void);
//releases permutations from the cold end until 'target' remain. Only ones that can come back without a compile.
static unsigned int GLSC_EvictPrograms(unsigned int target, unsigned int minage, unsigned int *reasoncounter)
{
	struct programpermu_s *pp, *prev;
	unsigned int evicted = 0;
	for (pp = glsc.restail; pp && glsc.rescount > target; pp = prev)
	{
		program_t *owner = pp->owner;
		unsigned int perm = pp->permutation;
		prev = pp->lru_prev;
		if (glsc.frame - pp->lastframe < minage)
			break;	//everything closer to the head is younger still
		if (!pp->persisted || !perm || !owner || perm >= PERMUTATIONS || owner->permu[perm] != pp || owner->permu[0] == pp)
			continue;	//no binary to come back from, the fallback, or not where the program looks for it
		owner->permu[perm] = NULL;	//BE_RenderMeshProgram reloads it (from the binary) on its next use
		GLSC_ProgramForget(pp);
		if (pp->h.glsl.handle)
			qglDeleteProgramObject_(pp->h.glsl.handle);	//gl defers the delete while a queued draw uses it
		BZ_Free(pp->parm);
		Z_Free(pp);
		evicted++;
	}
	if (evicted)
	{
		GLBE_ForgetProgramCache();	//the backend caches program names; a new program may reuse a name
		*reasoncounter += evicted;
	}
	return evicted;
}

void GLSC_LowMemory(void)
{
	glsc.lowmem = true;	//acted on at the next frame end, on the render thread with the context current
}

void GLSC_FrameEnd(void)
{
	int budget = r_shadercache_gpu_programs.ival;
	unsigned int minage = max(2, r_shadercache_gpu_minage.ival);
	glsc.frame++;
	if (!glsc.inited)
		return;
	if (r_shadercache_ram_kb.modified || r_shadercache_ram_entries.modified)
	{
		r_shadercache_ram_kb.modified = r_shadercache_ram_entries.modified = false;
		GLSC_RamEnforce();
	}
	if (glsc.lowmem)
	{
		glsc.lowmem = false;
		GLSC_RamClear();
		//under pressure: down to half the budget (or half of what is resident), anything idle for a few frames
		GLSC_EvictPrograms((budget > 0 ? (unsigned int)budget : glsc.rescount) / 2, 4, &glsc.gpuevict_lowmem);
		GLRT_Trim(true);
		return;
	}
	if (budget > 0 && glsc.rescount > (unsigned int)budget)
		GLSC_EvictPrograms(budget, minage, &glsc.gpuevict_budget);
}

/*----------------------------------------------------------------------------------------------------------*/
static void GLSC_Stats_f(void)
{
	size_t pending;
	GLSC_Lock();
	pending = glsc.pendingwrite;
	GLSC_Unlock();
	Con_Printf("shader binary cache (%s): %s\n", GLSC_ABI, GLSC_Available()?glsc.dir:"disabled");
	if (!GLSC_Available())
		Con_Printf("  reason: %s\n", !r_shadercache.ival?"r_shadercache 0":!qglProgramBinary?"driver has no program binary format":"no writable directory");
	Con_Printf("  disk: %u hits, %u misses (%u invalid entries removed), %u stores, %u store failures\n", glsc.diskhits, glsc.diskmisses, glsc.diskinvalid, glsc.stores, glsc.storefail);
	Con_Printf("  writer: %u written, %u failed, %u immediate, %u KB queued\n", glsc.writes, glsc.writefail, glsc.syncwrites, (unsigned int)(pending/1024));
	Con_Printf("  ram: %u entries, %u KB (peak %u KB, budget %i KB / %i entries), %u hits, %u misses, %u evicted (%u KB)\n",
		glsc.ramcount, (unsigned int)(glsc.rambytes/1024), (unsigned int)(glsc.rampeak/1024), r_shadercache_ram_kb.ival, r_shadercache_ram_entries.ival,
		glsc.ramhits, glsc.rammisses, glsc.ramevictions, (unsigned int)(glsc.ramevictedbytes/1024));
	Con_Printf("  gpu programs: %u resident (peak %u, budget %i, min age %i frames), evicted %u by budget, %u by memory pressure, %u by command\n",
		glsc.rescount, glsc.respeak, r_shadercache_gpu_programs.ival, r_shadercache_gpu_minage.ival, glsc.gpuevict_budget, glsc.gpuevict_lowmem, glsc.gpuevict_manual);
	Con_Printf("  compiles from source: %u, binary loads: %u from disk + %u from ram, refused binaries: %u, allocation failures: %u\n",
		glsc.sourcecompiles, glsc.diskhits, glsc.binaryreloads, glsc.binaryfail, glsc.allocfail);
}
static void GLSC_Trim_f(void)
{
	unsigned int before = glsc.rescount;
	GLSC_RamClear();
	GLSC_EvictPrograms(0, 2, &glsc.gpuevict_manual);
	Con_Printf("released %u gpu programs; ram cache cleared\n", before - glsc.rescount);
}

static void GLSC_DriverHash(void)
{
	void *ctx = alloca(hash_sha2_256.contextsize);
	const char *s;
	unsigned int v;
	extern const char *gl_vendor, *gl_renderer, *gl_version;
	hash_sha2_256.init(ctx);
#define HS(str) do { s = (str); if (!s) s = ""; hash_sha2_256.process(ctx, s, strlen(s)+1); } while(0)
#define HV(val) do { v = (unsigned int)(val); hash_sha2_256.process(ctx, &v, sizeof(v)); } while(0)
	HS(gl_vendor);
	HS(gl_renderer);
	HS(gl_version);		//mali/adreno/powervr put the driver build here
	HS((const char*)qglGetString(0x8B8C/*GL_SHADING_LANGUAGE_VERSION*/));
	HV(gl_config.gles);
	HV(sh_config.maxver);
	HV(gl_config.num_program_binary_formats);
	HV(gl_config.program_binary_core);
	HV(gl_config.maxattribs);
	HV(gl_config.msrtt);
	HV(gl_config.fbo_invalidate);
	HV(sizeof(void*));
	HS(GLSC_ABI);
	HS(version_string());
	HS(__DATE__ " " __TIME__);	//this build
#undef HS
#undef HV
	hash_sha2_256.terminate(glsc.driverhash, ctx);
}

void GLSC_Init(void)
{
	static qboolean registered;
	char hex[65];
	if (!registered)
	{
		registered = true;
		Cvar_Register(&r_shadercache, "GLSL program cache");
		Cvar_Register(&r_shadercache_ram_kb, "GLSL program cache");
		Cvar_Register(&r_shadercache_ram_entries, "GLSL program cache");
		Cvar_Register(&r_shadercache_gpu_programs, "GLSL program cache");
		Cvar_Register(&r_shadercache_gpu_minage, "GLSL program cache");
		Cvar_Register(&r_shadercache_write_kb, "GLSL program cache");
		Cmd_AddCommandD("r_shadercache_stats", GLSC_Stats_f, "Show the GLSL program binary cache, the RAM binary cache and gpu program residency statistics.");
		Cmd_AddCommandD("r_shadercache_trim", GLSC_Trim_f, "Release every unused gpu program that can be reloaded from a cached binary, and empty the RAM binary cache.");
	}
	if (!glsc.mutex)
		glsc.mutex = Sys_CreateMutex();	//kept for the process lifetime: writer jobs may still be finishing

	glsc.usable = false;
	glsc.inited = true;
	GLSC_DriverHash();
	if (memcmp(glsc.lastdriverhash, glsc.driverhash, 32))
	{	//another driver (or renderer) than the ram cache's binaries were made by
		GLSC_RamClear();
		memcpy(glsc.lastdriverhash, glsc.driverhash, 32);
	}
	if (!qglProgramBinary || !qglGetProgramBinary)
	{
		Con_DPrintf("shader binary cache: the driver offers no program binary format\n");
		return;
	}

	if (*glsc_external_dir)
		Q_strncpyz(glsc.basedir, glsc_external_dir, sizeof(glsc.basedir));
	else if (*com_homepath)
		Q_snprintfz(glsc.basedir, sizeof(glsc.basedir), "%sshadercache", com_homepath);
	else
		*glsc.basedir = 0;
	if (!*glsc.basedir)
	{
		Con_Printf(CON_WARNING "shader binary cache: no directory (FTE_SetShaderCacheDirectory was not called and there is no home dir); compiling from source\n");
		return;
	}
	GLSC_Hex(hex, glsc.driverhash, 8);	//64 bits name the driver's directory; the full hash is in every header
	Q_snprintfz(glsc.dir, sizeof(glsc.dir), "%s/%s", glsc.basedir, hex);
	GLSC_MakeDirs(glsc.dir);
	if (!GLSC_DirWritable(glsc.dir))
	{
		Con_Printf(CON_WARNING "shader binary cache: %s is not writable; compiling from source\n", glsc.dir);
		return;
	}
	glsc.usable = true;
	Con_DPrintf("shader binary cache: %s (%i binary formats)\n", glsc.dir, gl_config.num_program_binary_formats);
}

void GLSC_Shutdown(void)
{	//gl programs are released by Shader_Shutdown (GLSC_ProgramForget); the ram cache is cpu memory and survives a
	//vid_restart onto the same driver (GLSC_Init drops it when the driver changes)
	glsc.usable = false;
	glsc.inited = false;
	glsc.reshead = glsc.restail = NULL;
	glsc.rescount = 0;
}

//SDL_EVENT_LOW_MEMORY / onTrimMemory: everything that can be rebuilt goes at the next frame end
void GL_LowMemory(void)
{
	GLSC_LowMemory();
}
#endif
