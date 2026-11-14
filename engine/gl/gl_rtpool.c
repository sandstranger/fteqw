/*
Render target pool.

Transient render targets (post-process ping-pong, multisample storage, depth renderbuffers) are taken from here
and given back when the pass that needed them is over. A target is only ever allocated on first real use
(lazy), and a released one is reused by the next pass that asks for an identical one (purpose, size, format,
filtering, sample count) before anything new is allocated. Targets whose lifetimes do not overlap therefore
share storage: the old post-process chain kept one full-screen texture per stage name (up to five), the pool
needs two.

Entry states:  (not allocated) -> ACTIVE (allocated, handed out: attached/rendered/sampled by its owner)
               -> FREE (allocated, available for reuse) -> ACTIVE ... or -> destroyed (evicted)
Only FREE entries are ever evicted, oldest release first, when the pool is over its memory budget, when they
have not been reused for r_rtpool_maxage frames, on map change, on memory pressure and at shutdown. GL orders a
later write after earlier reads of the same object, so a target released after the draw that read it may be
reacquired in the same frame.

Budgets are per ABI: armeabi-v7a keeps far less around than arm64.
*/
#include "quakedef.h"
#ifdef GLQUAKE
#include "glquake.h"
#include "shader.h"

#if defined(__arm__) && !defined(__aarch64__)
	#define RTP_DEF_BUDGETMB	"40"
	#define RTP_ABI				"armv7"
#elif defined(__aarch64__)
	#define RTP_DEF_BUDGETMB	"128"
	#define RTP_ABI				"arm64"
#else
	#define RTP_DEF_BUDGETMB	"512"
	#define RTP_ABI				"other"
#endif

cvar_t r_rtpool_budget_mb = CVARD("r_rtpool_budget_mb", RTP_DEF_BUDGETMB, "Memory (MB) that released render targets may keep for reuse. Beyond it, the least recently released are destroyed.");
static cvar_t r_rtpool_maxage = CVARD("r_rtpool_maxage", "180", "Frames a released render target is kept for reuse before it is destroyed.");

enum { RTE_TEXTURE, RTE_RENDERBUFFER };
enum { RTS_ACTIVE, RTS_FREE };
typedef struct rtentry_s
{
	struct rtentry_s *next;
	int kind;
	int state;
	enum rtpool_purpose_e purpose;
	int width, height;
	int samples;
	int msrtt;
	uploadfmt_t fmt;
	unsigned int flags;
	GLenum ifmt;
	texid_t tex;
	GLuint rb;
	size_t bytes;
	unsigned int releasedframe;
} rtentry_t;

static struct
{
	qboolean inited;
	rtentry_t *entries;
	unsigned int frame;
	unsigned int count, activecount;
	size_t bytes, freebytes, peakbytes;
	size_t tracked[RTP_MAX];	//storage owned elsewhere (fbo depth renderbuffers, captures), counted here
	size_t trackedpeak;
	unsigned int hits, misses, evictions, reuses, allocfails, fborecreates;
	size_t evictedbytes;
} rtp;

static const char *rtp_purposenames[RTP_MAX] = {"postproc", "scene", "msaa", "depth", "capture", "other"};

size_t GLRT_FormatBytes(GLenum internalformat)
{
	switch(internalformat)
	{
	case GL_DEPTH_COMPONENT16_ARB:
	case 0x8D62:	//GL_RGB565
	case GL_RGBA4:
	case GL_RGB5_A1:
	case 0x822B:	//GL_RG8
		return 2;
	case GL_DEPTH_COMPONENT24_ARB:
	case GL_DEPTH24_STENCIL8_EXT:
	case GL_DEPTH_COMPONENT32_ARB:
	case GL_RGBA8:
	case GL_RGB8:	//padded in practice
	case GL_SRGB8_ALPHA8_EXT:
	case 0x8C3A:	//GL_R11F_G11F_B10F
	case 0x8059:	//GL_RGB10_A2
	case 0x8CAC:	//GL_DEPTH_COMPONENT32F
		return 4;
	case 0x8CAD:	//GL_DEPTH32F_STENCIL8
	case GL_RGBA16F:
		return 8;
	case GL_RGBA32F:
		return 16;
	case GL_STENCIL_INDEX8_EXT:
	case 0x8229:	//GL_R8
		return 1;
	default:
		return 4;
	}
}
static size_t GLRT_TextureBytes(uploadfmt_t fmt, int w, int h)
{
	unsigned int bb, bw, bh, bd;
	Image_BlockSizeForEncoding(fmt, &bb, &bw, &bh, &bd);
	if (!bw || !bh)
		return (size_t)w*h*4;
	return (size_t)((w+bw-1)/bw) * ((h+bh-1)/bh) * bb;
}

static void GLRT_Account(void)
{
	size_t t = rtp.bytes, i;
	for (i = 0; i < RTP_MAX; i++)
		t += rtp.tracked[i];
	if (t > rtp.peakbytes)
		rtp.peakbytes = t;
}

void GLRT_TrackAllocation(enum rtpool_purpose_e purpose, size_t bytes, int delta)
{
	if ((unsigned)purpose >= RTP_MAX)
		purpose = RTP_OTHER;
	if (delta > 0)
		rtp.tracked[purpose] += bytes;
	else if (rtp.tracked[purpose] >= bytes)
		rtp.tracked[purpose] -= bytes;
	else
		rtp.tracked[purpose] = 0;
	GLRT_Account();
}
void GLRT_NoteFBORecreate(void)
{
	rtp.fborecreates++;
}

static void GLRT_DestroyEntry(rtentry_t **link, qboolean contextalive)
{
	rtentry_t *e = *link;
	*link = e->next;
	if (e->kind == RTE_TEXTURE)
	{
		if (contextalive && e->tex)
			Image_DestroyTexture(e->tex);	//also forgets any texture-unit binding of it
	}
	else if (contextalive && e->rb)
		qglDeleteRenderbuffersEXT(1, &e->rb);
	rtp.count--;
	rtp.bytes -= e->bytes;
	if (e->state == RTS_FREE)
		rtp.freebytes -= e->bytes;
	else
		rtp.activecount--;
	Z_Free(e);
}

static void GLRT_Evict(rtentry_t **link)
{
	rtp.evictions++;
	rtp.evictedbytes += (*link)->bytes;
	GLRT_DestroyEntry(link, true);
}

//frees FREE entries: all of them, or the oldest until the budget is met plus anything older than maxage
static void GLRT_EnforceBudget(qboolean all)
{
	size_t budget = (size_t)max(0, r_rtpool_budget_mb.ival) * 1024*1024;
	unsigned int maxage = max(1, r_rtpool_maxage.ival);
	rtentry_t **link, **oldest;
	for (link = &rtp.entries; *link; )
	{
		rtentry_t *e = *link;
		if (e->state == RTS_FREE && (all || rtp.frame - e->releasedframe > maxage))
			GLRT_Evict(link);
		else
			link = &e->next;
	}
	while (rtp.bytes > budget && rtp.freebytes)
	{
		oldest = NULL;
		for (link = &rtp.entries; *link; link = &(*link)->next)
			if ((*link)->state == RTS_FREE && (!oldest || (*link)->releasedframe < (*oldest)->releasedframe))
				oldest = link;
		if (!oldest)
			break;
		GLRT_Evict(oldest);
	}
}

texid_t GLRT_AcquireTexture(enum rtpool_purpose_e purpose, const char *name, int width, int height, uploadfmt_t fmt, unsigned int imageflags)
{
	rtentry_t *e;
	static unsigned int seq;
	if (width <= 0 || height <= 0 || !gl_config.ext_framebuffer_objects)
		return r_nulltex;
	imageflags |= IF_NOPURGE|IF_RENDERTARGET|IF_NOMIPMAP|IF_CLAMP;
	for (e = rtp.entries; e; e = e->next)
	{
		if (e->kind == RTE_TEXTURE && e->state == RTS_FREE && e->purpose == purpose &&
			e->width == width && e->height == height && e->fmt == fmt && e->flags == imageflags &&
			e->tex && e->tex->status == TEX_LOADED)
		{
			e->state = RTS_ACTIVE;
			rtp.activecount++;
			rtp.freebytes -= e->bytes;
			rtp.hits++;
			rtp.reuses++;
			return e->tex;
		}
	}
	//nothing compatible is free: allocate now (first real use)
	rtp.misses++;
	e = Z_Malloc(sizeof(*e));
	e->kind = RTE_TEXTURE;
	e->purpose = purpose;
	e->width = width;
	e->height = height;
	e->fmt = fmt;
	e->flags = imageflags;
	e->tex = Image_CreateTexture(va("***rtpool_%s_%u***", name?name:rtp_purposenames[purpose], ++seq), NULL, imageflags);
	if (!e->tex)
	{
		rtp.allocfails++;
		Z_Free(e);
		return r_nulltex;
	}
	Image_Upload(e->tex, fmt, NULL, NULL, width, height, 1, imageflags);
	if (e->tex->status != TEX_LOADED)
	{
		rtp.allocfails++;
		Image_DestroyTexture(e->tex);
		Z_Free(e);
		return r_nulltex;
	}
	e->bytes = GLRT_TextureBytes(fmt, width, height);
	e->state = RTS_ACTIVE;
	e->next = rtp.entries;
	rtp.entries = e;
	rtp.count++;
	rtp.activecount++;
	rtp.bytes += e->bytes;
	GLRT_Account();
	return e->tex;
}

void GLRT_ReleaseTexture(texid_t tex)
{
	rtentry_t *e;
	if (!tex)
		return;
	for (e = rtp.entries; e; e = e->next)
	{
		if (e->kind == RTE_TEXTURE && e->tex == tex)
		{
			if (e->state == RTS_ACTIVE)
			{
				e->state = RTS_FREE;
				e->releasedframe = rtp.frame;
				rtp.activecount--;
				rtp.freebytes += e->bytes;
			}
			return;
		}
	}
}

GLuint GLRT_AcquireRenderbuffer(enum rtpool_purpose_e purpose, GLenum internalformat, int width, int height, int samples, int msrtt)
{
	rtentry_t *e;
	GLenum err;
	if (width <= 0 || height <= 0 || !gl_config.ext_framebuffer_objects)
		return 0;
	if (samples < 2)
		samples = 0;
	for (e = rtp.entries; e; e = e->next)
	{
		if (e->kind == RTE_RENDERBUFFER && e->state == RTS_FREE && e->purpose == purpose &&
			e->width == width && e->height == height && e->ifmt == internalformat && e->samples == samples && e->msrtt == msrtt)
		{
			e->state = RTS_ACTIVE;
			rtp.activecount++;
			rtp.freebytes -= e->bytes;
			rtp.hits++;
			rtp.reuses++;
			return e->rb;
		}
	}
	rtp.misses++;
	e = Z_Malloc(sizeof(*e));
	e->kind = RTE_RENDERBUFFER;
	e->purpose = purpose;
	e->width = width;
	e->height = height;
	e->ifmt = internalformat;
	e->samples = samples;
	e->msrtt = msrtt;
	while (qglGetError())
		;
	qglGenRenderbuffersEXT(1, &e->rb);
	qglBindRenderbufferEXT(GL_RENDERBUFFER_EXT, e->rb);
	if (samples && msrtt && qglRenderbufferStorageMultisampleMSRTT)
		qglRenderbufferStorageMultisampleMSRTT(GL_RENDERBUFFER_EXT, samples, internalformat, width, height);
	else if (samples && qglRenderbufferStorageMultisampleEXT)
		qglRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER_EXT, samples, internalformat, width, height);
	else
		qglRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, internalformat, width, height);
	qglBindRenderbufferEXT(GL_RENDERBUFFER_EXT, 0);
	err = qglGetError();
	if (err != GL_NO_ERROR || !e->rb)
	{
		if (e->rb)
			qglDeleteRenderbuffersEXT(1, &e->rb);
		rtp.allocfails++;
		Z_Free(e);
		return 0;
	}
	//render-to-texture multisample storage lives in tile memory on the gpus that offer it; count one sample
	e->bytes = (size_t)width*height*GLRT_FormatBytes(internalformat) * ((samples && !msrtt)?samples:1);
	e->state = RTS_ACTIVE;
	e->next = rtp.entries;
	rtp.entries = e;
	rtp.count++;
	rtp.activecount++;
	rtp.bytes += e->bytes;
	GLRT_Account();
	return e->rb;
}

void GLRT_ReleaseRenderbuffer(GLuint rb)
{
	rtentry_t *e;
	if (!rb)
		return;
	for (e = rtp.entries; e; e = e->next)
	{
		if (e->kind == RTE_RENDERBUFFER && e->rb == rb)
		{
			if (e->state == RTS_ACTIVE)
			{
				e->state = RTS_FREE;
				e->releasedframe = rtp.frame;
				rtp.activecount--;
				rtp.freebytes += e->bytes;
			}
			return;
		}
	}
}

void GLRT_FrameEnd(void)
{
	rtp.frame++;
	if (r_rtpool_budget_mb.modified)
	{
		r_rtpool_budget_mb.modified = false;
		GLRT_EnforceBudget(false);
	}
	else if (!(rtp.frame & 15))	//age checks need no per-frame walk
		GLRT_EnforceBudget(false);
}

void GLRT_Trim(qboolean all)
{
	GLRT_EnforceBudget(all);
}

void GLBE_PassStats_Print(void);
static void GLRT_Stats_f(void)
{
	rtentry_t *e;
	size_t i, tracked = 0;
	for (i = 0; i < RTP_MAX; i++)
		tracked += rtp.tracked[i];
	Con_Printf("render target pool (%s, budget %i MB, max age %i frames):\n", RTP_ABI, r_rtpool_budget_mb.ival, r_rtpool_maxage.ival);
	Con_Printf("  %u allocated (%u active, %u available), %u KB (%u KB available), peak %u KB incl. tracked\n",
		rtp.count, rtp.activecount, rtp.count-rtp.activecount, (unsigned int)(rtp.bytes/1024), (unsigned int)(rtp.freebytes/1024), (unsigned int)(rtp.peakbytes/1024));
	Con_Printf("  pool hits %u (reused textures/renderbuffers), misses %u (new allocations), evicted %u (%u KB), failed allocations %u, fbo (re)creations %u\n",
		rtp.hits, rtp.misses, rtp.evictions, (unsigned int)(rtp.evictedbytes/1024), rtp.allocfails, rtp.fborecreates);
	Con_Printf("  tracked outside the pool: %u KB", (unsigned int)(tracked/1024));
	for (i = 0; i < RTP_MAX; i++)
		if (rtp.tracked[i])
			Con_Printf(" %s=%uKB", rtp_purposenames[i], (unsigned int)(rtp.tracked[i]/1024));
	Con_Printf("\n");
	for (e = rtp.entries; e; e = e->next)
	{
		if (e->kind == RTE_TEXTURE)
			Con_Printf("   tex %-8s %4ix%-4i %-10s %6u KB %s\n", rtp_purposenames[e->purpose], e->width, e->height, Image_FormatName(e->fmt),
				(unsigned int)(e->bytes/1024), e->state==RTS_ACTIVE?"active":va("available (released %u frames ago)", rtp.frame-e->releasedframe));
		else
			Con_Printf("   rb  %-8s %4ix%-4i fmt 0x%04x x%i%s %6u KB %s\n", rtp_purposenames[e->purpose], e->width, e->height, e->ifmt, max(1,e->samples),
				e->msrtt?" (render-to-texture)":"", (unsigned int)(e->bytes/1024), e->state==RTS_ACTIVE?"active":"available");
	}
	{
		void Image_QueueStats(size_t *queued, size_t *peak, unsigned int *waits, unsigned int *timeouts);
		size_t q, p;
		unsigned int w, t;
		Image_QueueStats(&q, &p, &w, &t);
		Con_Printf("texture upload queue: %u KB decoded and waiting (peak %u KB), loader waits %u (%u gave up after 2s)\n",
			(unsigned int)(q/1024), (unsigned int)(p/1024), w, t);
	}
	GLBE_PassStats_Print();
}

void GLRT_Init(void)
{
	static qboolean registered;
	if (!registered)
	{
		registered = true;
		Cvar_Register(&r_rtpool_budget_mb, "Render target pool");
		Cvar_Register(&r_rtpool_maxage, "Render target pool");
		Cmd_AddCommandD("r_rtpool_stats", GLRT_Stats_f, "Show render target pool, tracked render target memory and render pass statistics.");
	}
	rtp.inited = true;
}

void GLRT_Shutdown(qboolean contextalive)
{
	while (rtp.entries)
		GLRT_DestroyEntry(&rtp.entries, contextalive);
	memset(rtp.tracked, 0, sizeof(rtp.tracked));
	rtp.count = rtp.activecount = 0;
	rtp.bytes = rtp.freebytes = 0;
}
#endif
