#include "VoiceDecode.h"

#include <windows.h>

#include <intrin.h>
#include <xmmintrin.h>

#include <cstring>

#include "engine/sound/Mp3Frames.h"

namespace obng::plugin {

namespace {

namespace vd = obng::engine::sound::voice_decode;

// ---- The game's side (read 2026-11-13 in the instructions; knowledge/voice-lines.md) -----------------------------
constexpr UInt32 kPlay = 0x006A9E00;  // __thiscall(manager; IDirectSoundBuffer** out, const char* path, byte flags), RET 0Ch; AX = ms, 0 = failed
constexpr UInt8 kPlayBytes[] = {0x6A, 0xFF, 0x68, 0xEC, 0x62, 0x9C, 0x00};  // push -1; push 009C62ECh
constexpr UInt32 kSoundDtor = 0x006B6700;  // __thiscall(sound): releases the buffer at +0x50 until its count is 0
constexpr UInt8 kSoundDtorBytes[] = {0x56, 0x8B, 0xF1, 0xF7, 0x06, 0x00, 0x40, 0x00, 0x00};  // push esi; mov esi,ecx; test dword [esi],4000h
constexpr UInt32 kNotFoundSite = 0x006A9EB7;   // "MP3 file '%s' not found": lea ecx,[esp+64h]; push ecx; push ... (read)
constexpr UInt32 kOpenFileSite = 0x006A9ECE;   // where the function opens the file: push 1Ch; call the allocator (read)
constexpr UInt32 kFlagMasksSite = 0x006A9FBE;  // and eax,20010h; or eax,0A0h (read)
constexpr UInt32 kExistsPointer = 0x00B27E50;  // bool __cdecl(const char* path, int 0), loose or in an archive (0x00748160 jumps through it)
constexpr UInt32 kAlloc = 0x00401F00;          // void* __cdecl(size)
constexpr UInt32 kFree = 0x00401F20;           // void __cdecl(void*)
constexpr UInt32 kFileSize = 0x1C;
constexpr UInt32 kFileCtor = 0x006B45C0;       // __thiscall(file; path): reads the whole file, counts its frames, parses the first header
constexpr UInt32 kFileDtor = 0x006B31D0;       // __thiscall(file): frees what it holds, not itself
constexpr UInt32 kDecoderSize = 0x413C;
constexpr UInt32 kDecoderCtor = 0x006B1D40;    // __thiscall(decoder; file)
constexpr UInt32 kDecoderDtor = 0x006B3500;    // __thiscall(decoder)
constexpr UInt32 kDecodeFrame = 0x006B3AE0;    // __thiscall(decoder): 0x900 bytes of its own, or null at the end
constexpr UInt32 kManagerDirectSound = 0x08;
constexpr UInt32 kFileFrames = 0x00;
constexpr UInt32 kFileHeader = 0x04;   // its +8: the first frame's sample rate
constexpr UInt32 kHeaderRate = 0x08;
constexpr UInt32 kFileData = 0x08;
constexpr UInt32 kFileBytes = 0x0C;
constexpr UInt32 kFileValid = 0x18;    // a byte
constexpr UInt32 kSoundBuffer = 0x50;  // IDirectSoundBuffer8 (0x006B67F0 asks the buffer for it)

using PlayFn = UInt32(__fastcall*)(void* self, void* edx, void** out, const char* path, UInt32 flags);
using SoundDtorFn = UInt32(__fastcall*)(void* self, void* edx);
using ExistsFn = bool(__cdecl*)(const char* path, int mode);
using AllocFn = void*(__cdecl*)(UInt32 size);
using FreeFn = void(__cdecl*)(void* block);
using FileCtorFn = void*(__fastcall*)(void* self, void* edx, const char* path);
using DecoderCtorFn = void*(__fastcall*)(void* self, void* edx, void* file);
using ThisFn = void(__fastcall*)(void* self, void* edx);
using DecodeFn = const void*(__fastcall*)(void* self, void* edx);

// ---- DirectSound, by its vtable (dsound.h is not among the plugin's headers) ---------------------------------------
#pragma pack(push, 1)
struct WaveFormat {
	UInt16 tag;
	UInt16 channels;
	UInt32 rate;
	UInt32 bytesPerSecond;
	UInt16 blockAlign;
	UInt16 bits;
	UInt16 extra;
};
#pragma pack(pop)
struct BufferDesc {
	UInt32 size;
	UInt32 flags;
	UInt32 bytes;
	UInt32 reserved;
	const WaveFormat* format;
	UInt8 algorithm[16];  // GUID_NULL, as the original's copy from 0x00A78FC4
};
static_assert(sizeof(WaveFormat) == 18);
static_assert(sizeof(BufferDesc) == 0x24);
constexpr UInt32 kQueryInterface = 0x00;
constexpr UInt32 kRelease = 0x08;
constexpr UInt32 kCreateSoundBuffer = 0x0C;  // IDirectSound
constexpr UInt32 kLock = 0x2C;               // IDirectSoundBuffer
constexpr UInt32 kUnlock = 0x4C;
constexpr DWORD kLockEntire = 2;
using QueryFn = HRESULT(__stdcall*)(void* self, const GUID* iid, void** out);
using ReleaseFn = ULONG(__stdcall*)(void* self);
using CreateBufferFn = HRESULT(__stdcall*)(void* self, const BufferDesc* desc, void** out, void* outer);
using LockFn = HRESULT(__stdcall*)(void* self, DWORD offset, DWORD bytes, void** first, DWORD* firstBytes, void** second, DWORD* secondBytes, DWORD flags);
using UnlockFn = HRESULT(__stdcall*)(void* self, void* first, DWORD firstBytes, void* second, DWORD secondBytes);
constexpr GUID kIidUnknown = {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

template <typename Fn>
Fn Method(void* object, UInt32 offset) {
	return *reinterpret_cast<Fn*>(*reinterpret_cast<UInt32*>(object) + offset);
}
UInt32 Dword(const void* at, UInt32 offset) { return *reinterpret_cast<const UInt32*>(reinterpret_cast<UInt32>(at) + offset); }

// ---- State ---------------------------------------------------------------------------------------------------------
constexpr UInt32 kChunkFrames = 8;

struct Job {
	CRITICAL_SECTION guard;  // held by the worker while it writes the buffer; taken once by whoever cancels
	volatile LONG cancel = 0;
	void* buffer = nullptr;  // the line's IDirectSoundBuffer; not ours to keep alive (the sound's destructor releases it to 0)
	UInt32 done = 0;         // frames in the buffer
	UInt32 total = 0;
	void* file = nullptr;    // the game's file object: the MP3's bytes
	void* decoder = nullptr; // the game's decoder (mode 1, and the check)
	vd::Mp3Stream stream;    // mode 2
	bool check = false;
	UInt16 controlWord = 0;  // the calling thread's x87 control word and MXCSR: the game's decoder is x87 code
	UInt32 mxcsr = 0;
};

void* g_playOriginal = nullptr;
void* g_dtorOriginal = nullptr;
bool g_installed = false;
LogFn g_log = nullptr;
bool g_standIn = true;        // [Route] VoiceDecodeStandIn: a line without a voice file gets the silent voice plugin's file through the route
UInt32 g_standInLines = 3;    // lines that say what the not-found place is
UInt32 g_standInPathLines = 3;
bool g_testMissing = false;   // [Route] VoiceDecodeTestMissing: at the first voice call, what a line without a voice file would get
UInt32 g_leftLines = 60;  // lines for calls left to the game, a session ([Route] VoiceDecodeLeftLines)
vd::Mode g_mode = vd::Mode::kOwnDecoder;
UInt32 g_leadMs = 150;
bool g_check = false;
LARGE_INTEGER g_frequency{};
CRITICAL_SECTION g_lock;  // the slots and the counts
vd::Slots g_slots;
vd::Counts g_counts;
Job g_jobs[vd::Slots::kCount];
HANDLE g_wake = nullptr;
volatile LONG g_checkLines = 0;

void FreeBlock(void* block) { reinterpret_cast<FreeFn>(kFree)(block); }

void DestroyFile(void* file) {
	if (file != nullptr) {
		reinterpret_cast<ThisFn>(kFileDtor)(file, nullptr);
		FreeBlock(file);
	}
}
void DestroyDecoder(void* decoder) {
	if (decoder != nullptr) {
		reinterpret_cast<ThisFn>(kDecoderDtor)(decoder, nullptr);
		FreeBlock(decoder);
	}
}
void* MakeDecoder(void* file) {
	void* block = reinterpret_cast<AllocFn>(kAlloc)(kDecoderSize);
	return block != nullptr ? reinterpret_cast<DecoderCtorFn>(kDecoderCtor)(block, nullptr, file) : nullptr;
}

UInt16 ControlWord() {
	UInt16 control = 0;
	__asm fnstcw control
	return control;
}
void SetControlWord(UInt16 control) {
	__asm fldcw control
}

// One frame of the line into `out` (0x900 bytes); false when the decoder has no further frame.
bool DecodeFrame(Job& job, vd::Mode mode, std::int16_t* out) {
	if (mode == vd::Mode::kOwnDecoder) {
		return vd::NextFrame(job.stream, out);
	}
	const void* frame = reinterpret_cast<DecodeFn>(kDecodeFrame)(job.decoder, nullptr);
	if (frame == nullptr) {
		return false;
	}
	std::memcpy(out, frame, vd::kFrameBytes);
	return true;
}

// What a slot holds is freed by the thread that calls the voice player or destroys a sound - never by the worker.
void CleanDone() {
	for (;;) {
		// The slot's pointers are taken out under the lock, before the slot can be claimed again.
		EnterCriticalSection(&g_lock);
		const UInt32 slot = g_slots.TakeDone();
		void* decoder = nullptr;
		void* file = nullptr;
		if (slot != vd::Slots::kNone) {
			Job& job = g_jobs[slot];
			decoder = job.decoder;
			file = job.file;
			job.decoder = nullptr;
			job.file = nullptr;
			job.buffer = nullptr;
		}
		LeaveCriticalSection(&g_lock);
		if (slot == vd::Slots::kNone) {
			return;
		}
		DestroyDecoder(decoder);
		DestroyFile(file);
	}
}

// ---- The worker ----------------------------------------------------------------------------------------------------

// The own decoder against the game's over the whole file, written as one line ([Route] VoiceDecodeCheck).
void CheckLine(Job& job) {
	void* decoder = job.decoder;
	if (decoder == nullptr || job.file == nullptr) {
		return;
	}
	vd::Mp3Stream own;
	vd::Open(own, reinterpret_cast<const std::uint8_t*>(Dword(job.file, kFileData)), Dword(job.file, kFileBytes));
	vd::Difference difference;
	std::int16_t mine[vd::kFrameSamples];
	for (UInt32 i = 0; i < job.total && job.cancel == 0; ++i) {
		const void* theirs = reinterpret_cast<DecodeFn>(kDecodeFrame)(decoder, nullptr);
		if (theirs == nullptr || !vd::NextFrame(own, mine)) {
			break;
		}
		difference.Add(mine, reinterpret_cast<const std::int16_t*>(theirs), vd::kFrameSamples);
	}
	if (InterlockedIncrement(&g_checkLines) <= 200) {
		g_log("Route: voice decode check - %u of %u frames compared, the largest difference %u of 32768, the mean square %.2f", difference.frames,
		      job.total, difference.largest, difference.MeanSquare());
	}
}

void RunJob(Job& job) {
	const UInt16 control = ControlWord();
	const UInt32 mxcsr = _mm_getcsr();
	SetControlWord(job.controlWord);
	_mm_setcsr(job.mxcsr);
	static std::int16_t chunk[kChunkFrames * vd::kFrameSamples];  // one worker: one piece at a time
	UInt64 frames = 0;
	bool lockFailed = false;
	while (job.cancel == 0) {
		const UInt32 want = vd::NextChunk(job.done, job.total, kChunkFrames);
		UInt32 got = 0;
		while (got < want && DecodeFrame(job, g_mode, chunk + got * vd::kFrameSamples)) {
			++got;
		}
		if (got == 0) {
			break;  // the line is through, or its decoder is: what is left of the buffer stays silent
		}
		EnterCriticalSection(&job.guard);
		if (job.cancel == 0) {
			void* first = nullptr;
			DWORD firstBytes = 0;
			const DWORD bytes = got * vd::kFrameBytes;
			if (Method<LockFn>(job.buffer, kLock)(job.buffer, job.done * vd::kFrameBytes, bytes, &first, &firstBytes, nullptr, nullptr, 0) >= 0 &&
			    first != nullptr) {
				std::memcpy(first, chunk, firstBytes < bytes ? firstBytes : bytes);
				Method<UnlockFn>(job.buffer, kUnlock)(job.buffer, first, firstBytes, nullptr, 0);
			} else {
				lockFailed = true;
			}
		}
		LeaveCriticalSection(&job.guard);
		job.done += got;
		frames += got;
		if (lockFailed || got < want) {
			break;
		}
	}
	if (job.check && job.cancel == 0) {
		CheckLine(job);
	}
	SetControlWord(control);
	_mm_setcsr(mxcsr);
	EnterCriticalSection(&g_lock);
	g_counts.workerFrames += frames;
	g_counts.lockFailures += lockFailed ? 1 : 0;
	LeaveCriticalSection(&g_lock);
}

DWORD WINAPI Worker(void*) {
	for (;;) {
		WaitForSingleObject(g_wake, INFINITE);
		EnterCriticalSection(&g_lock);
		const UInt32 slot = g_slots.Take();
		LeaveCriticalSection(&g_lock);
		if (slot == vd::Slots::kNone) {
			continue;
		}
		RunJob(g_jobs[slot]);
		EnterCriticalSection(&g_lock);
		g_slots.Finish(slot);
		LeaveCriticalSection(&g_lock);
	}
}

// ---- The call --------------------------------------------------------------------------------------------------------

struct Line {
	void* file = nullptr;
	UInt32 frames = 0;
	UInt32 rate = 0;
	void* buffer = nullptr;
	void* locked = nullptr;
	DWORD lockedBytes = 0;
	bool standIn = false;  // no voice file: the silent voice plugin's file instead
};

// Bytes that may not be readable; false then.
bool ReadBytes(UInt32 address, UInt8* out, UInt32 count) {
	__try {
		std::memcpy(out, reinterpret_cast<const void*>(address), count);
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool CallStub(const vd::SilentStub& stub, char* path) {
	__try {
		*reinterpret_cast<char**>(stub.pathSlot) = path;
		reinterpret_cast<void(__cdecl*)()>(stub.routine)();
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

// A line with neither MP3 nor WAV: when the function's not-found place 0x006A9EB7 is the silent voice plugin's
// stub (VoiceDecodeLogic.h SilentStub), its routine rewrites `name` as it would the function's own buffer. True:
// `name` was handed to it.
bool StandInPath(char name[vd::kPathCapacity]) {
	if (!g_standIn) {
		return false;
	}
	UInt8 site[vd::kSilentSiteBytes] = {};
	UInt8 code[vd::kSilentStubBytes] = {};
	UInt8 slot[4] = {};
	std::memcpy(site, reinterpret_cast<const void*>(kNotFoundSite), sizeof(site));
	const UInt32 jumpSlot = vd::SilentJumpSlot(site);
	vd::SilentStub stub;
	UInt32 target = 0;
	if (jumpSlot != 0 && ReadBytes(jumpSlot, slot, sizeof(slot))) {
		std::memcpy(&target, slot, sizeof(target));
		if (ReadBytes(target, code, sizeof(code))) {
			stub = vd::ParseSilentStub(code, target);
		}
	}
	UInt32 goesOn = 0;
	const bool usable = stub.found && ReadBytes(stub.continueSlot, slot, sizeof(slot)) && (std::memcpy(&goesOn, slot, 4), goesOn == kOpenFileSite);
	if (g_standInLines != 0) {
		--g_standInLines;
		g_log("Route: voice decode - a line without a voice file: the not-found place 0x006A9EB7 %s (stub at %08X, its routine %08X, it goes on at %08X)",
		      usable ? "is the silent voice plugin's stub - the route asks its routine for the file" : jumpSlot != 0 ? "is patched, but not by a stub the route knows - left to the game"
		                                                                                                             : "is the game's own - there is no stand-in",
		      target, stub.routine, goesOn);
	}
	if (!usable) {
		return false;
	}
	if (!CallStub(stub, name)) {
		g_standIn = false;
		g_log("Route: voice decode - the silent voice plugin's routine faulted; such lines are left to the game from now on");
		return false;
	}
	name[vd::kPathCapacity - 1] = 0;
	if (g_standInPathLines != 0) {
		--g_standInPathLines;
		g_log("Route: voice decode - the silent voice plugin's routine gave \"%s\"", name);
	}
	return true;
}

// The file as the original opens it. Not kTaken: the call is the game's (no such MP3, not one its decoder takes) -
// nothing is left behind, the original does its own tries and writes its own messages.
vd::Left OpenLine(const char* path, Line* line) {
	char name[vd::kPathCapacity];
	if (!vd::Mp3Path(path, name, sizeof(name))) {
		return vd::Left::kPath;
	}
	const ExistsFn exists = *reinterpret_cast<ExistsFn*>(kExistsPointer);
	if (exists == nullptr) {
		return vd::Left::kNoFile;
	}
	if (!exists(name, 0)) {
		// The original: with the WAV there it returns quietly (its caller plays the WAV), without it the not-found place runs.
		if (exists(path, 0) || !StandInPath(name) || !exists(name, 0)) {
			return vd::Left::kNoFile;
		}
		line->standIn = true;
	}
	void* block = reinterpret_cast<AllocFn>(kAlloc)(kFileSize);
	if (block == nullptr) {
		return vd::Left::kNoMemory;
	}
	void* file = reinterpret_cast<FileCtorFn>(kFileCtor)(block, nullptr, name);
	const UInt32 frames = Dword(file, kFileFrames);
	const UInt32 header = Dword(file, kFileHeader);
	const UInt32 rate = header != 0 ? Dword(reinterpret_cast<const void*>(header), kHeaderRate) : 0;
	const bool valid = *reinterpret_cast<const UInt8*>(reinterpret_cast<UInt32>(file) + kFileValid) != 0;
	if (const vd::Left why = vd::LeftOfFile(valid, frames, rate, Dword(file, kFileData) != 0); why != vd::Left::kTaken) {
		DestroyFile(file);
		return why;
	}
	line->file = file;
	line->frames = frames;
	line->rate = rate;
	return vd::Left::kTaken;
}

// The buffer as the original makes it, locked whole. False: no buffer (the original returns 0 then).
bool MakeBuffer(void* manager, void** out, UInt32 flags, Line* line) {
	void* directSound = reinterpret_cast<void*>(Dword(manager, kManagerDirectSound));
	if (directSound == nullptr) {
		return false;
	}
	WaveFormat format{};
	format.tag = 1;
	format.channels = 1;
	format.rate = line->rate;
	format.bytesPerSecond = line->rate * 2;
	format.blockAlign = 2;
	format.bits = 16;
	BufferDesc desc{};
	desc.size = sizeof(desc);
	UInt8 code[10];
	std::memcpy(code, reinterpret_cast<const void*>(kFlagMasksSite), sizeof(code));
	const vd::Masks masks = vd::FlagMasks(code);
	desc.flags = vd::BufferFlags(flags, masks.always, masks.positional);
	desc.bytes = vd::BufferBytes(line->frames);
	desc.format = &format;
	if (Method<CreateBufferFn>(directSound, kCreateSoundBuffer)(directSound, &desc, out, nullptr) < 0 || *out == nullptr) {
		return false;
	}
	line->buffer = *out;
	// A lock that fails leaves the buffer in *out unreleased, as the original does (0x006AA0A0).
	return Method<LockFn>(line->buffer, kLock)(line->buffer, 0, 0, &line->locked, &line->lockedBytes, nullptr, nullptr, kLockEntire) >= 0 &&
	       line->locked != nullptr;
}

// The line's first frames into the locked buffer, the rest silence; the frames decoded. `ended`: the decoder had
// no further frame - the line is through (the original's loop ends at the first frame it does not get).
UInt32 FillLead(Job& job, const Line& line, UInt32 lead, bool* ended) {
	auto* at = static_cast<std::int16_t*>(line.locked);
	const UInt32 room = line.lockedBytes / vd::kFrameBytes;
	const UInt32 most = lead < room ? lead : room;
	UInt32 done = 0;
	while (done < most && DecodeFrame(job, g_mode, at + done * vd::kFrameSamples)) {
		++done;
	}
	*ended = done < most;
	std::memset(at + done * vd::kFrameSamples, 0, line.lockedBytes - done * vd::kFrameBytes);
	return done;
}

// The route's version of the call. `why` not kTaken: nothing was done, the game's function is to run.
UInt32 Rebuilt(void* manager, void** out, const char* path, UInt32 flags, vd::Left* why) {
	Line line;
	*why = OpenLine(path, &line);
	if (*why != vd::Left::kTaken) {
		return 0;
	}
	EnterCriticalSection(&g_lock);
	const UInt32 slot = g_slots.Claim();
	LeaveCriticalSection(&g_lock);
	Job local;
	Job& job = slot != vd::Slots::kNone ? g_jobs[slot] : local;
	job.cancel = 0;
	job.file = line.file;
	job.decoder = g_mode == vd::Mode::kEngineDecoder || g_check ? MakeDecoder(line.file) : nullptr;
	job.check = g_check && g_mode == vd::Mode::kOwnDecoder && job.decoder != nullptr;
	job.total = line.frames;
	job.done = 0;
	job.controlWord = ControlWord();
	job.mxcsr = _mm_getcsr();
	vd::Open(job.stream, reinterpret_cast<const std::uint8_t*>(Dword(line.file, kFileData)), Dword(line.file, kFileBytes));
	const bool decoderMissing = g_mode == vd::Mode::kEngineDecoder && job.decoder == nullptr;
	const bool made = !decoderMissing && MakeBuffer(manager, out, flags, &line);
	UInt32 lead = 0;
	bool ended = false;
	if (made) {
		job.buffer = line.buffer;
		const bool whole = slot == vd::Slots::kNone;
		lead = FillLead(job, line, whole ? line.frames : vd::LeadFrames(g_leadMs, line.rate, line.frames), &ended);
		job.done = ended ? line.frames : lead;  // an ended line has nothing left for the worker
		Method<UnlockFn>(line.buffer, kUnlock)(line.buffer, line.locked, line.lockedBytes, nullptr, 0);
	}
	const bool queued = made && slot != vd::Slots::kNone && (job.done < line.frames || job.check);
	EnterCriticalSection(&g_lock);
	g_counts.lines += made ? 1 : 0;
	g_counts.standIn += made && line.standIn ? 1 : 0;
	g_counts.failed += made ? 0 : 1;
	g_counts.leadFrames += lead;
	g_counts.whole += made && !queued ? 1 : 0;
	g_counts.noSlot += made && slot == vd::Slots::kNone ? 1 : 0;
	if (queued) {
		g_slots.Queue(slot);
	} else {
		g_slots.Drop(slot);  // cleaned with the done ones; nothing when there was no slot
	}
	LeaveCriticalSection(&g_lock);
	if (queued) {
		ReleaseSemaphore(g_wake, 1, nullptr);
	} else if (slot == vd::Slots::kNone) {
		DestroyDecoder(local.decoder);
		DestroyFile(local.file);
	}
	return made ? vd::DurationMs(line.frames, line.rate) : 0;
}

// [Route] VoiceDecodeTestMissing: a path with no voice file opened as the route would, and closed again - whether
// another plugin's stand-in is found and what it is.
void TestMissing() {
	Line line;
	const vd::Left why = OpenLine("Data\\Sound\\Voice\\OBNG-test.esp\\none\\m\\obng_missing_line.wav", &line);
	g_log("Route: voice decode test - a line without a voice file: %s%s, %u frames at %u Hz", why == vd::Left::kTaken ? "taken" : vd::LeftName(why),
	      line.standIn ? " on the stand-in" : "", line.frames, line.rate);
	if (why == vd::Left::kTaken) {
		DestroyFile(line.file);
	}
}

UInt32 __fastcall Play(void* self, void* /*edx*/, void** out, const char* path, UInt32 flags) {
	LARGE_INTEGER start;
	LARGE_INTEGER end;
	QueryPerformanceCounter(&start);
	CleanDone();
	if (g_testMissing) {
		g_testMissing = false;
		TestMissing();
	}
	vd::Left why = vd::Left::kTaken;
	UInt32 result = Rebuilt(self, out, path, flags, &why);
	const bool taken = why == vd::Left::kTaken;
	if (!taken) {
		result = reinterpret_cast<PlayFn>(g_playOriginal)(self, nullptr, out, path, flags);
	}
	QueryPerformanceCounter(&end);
	const UInt64 ticks = static_cast<UInt64>(end.QuadPart - start.QuadPart);
	if (!taken && g_leftLines != 0) {
		--g_leftLines;
		char text[480];
		vd::FormatLeft(path, why, result & 0xFFFFu, static_cast<double>(ticks) * 1000.0 / static_cast<double>(g_frequency.QuadPart != 0 ? g_frequency.QuadPart : 1), text, sizeof(text));
		g_log("%s", text);
	}
	EnterCriticalSection(&g_lock);
	g_counts.left += taken ? 0 : 1;
	g_counts.callTicks += ticks;
	g_counts.longestCallTicks = ticks > g_counts.longestCallTicks ? ticks : g_counts.longestCallTicks;
	LeaveCriticalSection(&g_lock);
	return result;
}

// ---- A sound is destroyed: its line's worker must be off the buffer first -------------------------------------------

bool SameObject(void* a, void* b) {
	if (a == b) {
		return true;
	}
	void* first = nullptr;
	void* second = nullptr;
	const bool both = Method<QueryFn>(a, kQueryInterface)(a, &kIidUnknown, &first) >= 0 && first != nullptr &&
	                  Method<QueryFn>(b, kQueryInterface)(b, &kIidUnknown, &second) >= 0 && second != nullptr;
	const bool same = both && first == second;
	if (first != nullptr) {
		Method<ReleaseFn>(first, kRelease)(first);
	}
	if (second != nullptr) {
		Method<ReleaseFn>(second, kRelease)(second);
	}
	return same;
}

void CancelFor(void* buffer) {
	for (UInt32 i = 0; i < vd::Slots::kCount; ++i) {
		EnterCriticalSection(&g_lock);
		const vd::Slots::State state = g_slots.Of(i);
		LeaveCriticalSection(&g_lock);
		Job& job = g_jobs[i];
		// A cancelled line's buffer may be gone already: it is never asked again.
		if ((state != vd::Slots::State::kQueued && state != vd::Slots::State::kRunning) || job.cancel != 0 || job.buffer == nullptr ||
		    !SameObject(job.buffer, buffer)) {
			continue;
		}
		EnterCriticalSection(&job.guard);
		InterlockedExchange(&job.cancel, 1);
		LeaveCriticalSection(&job.guard);
		EnterCriticalSection(&g_lock);
		++g_counts.cancelled;
		LeaveCriticalSection(&g_lock);
	}
}

UInt32 __fastcall SoundDtor(void* self, void* /*edx*/) {
	EnterCriticalSection(&g_lock);
	const UInt32 inWork = g_slots.InWork();
	LeaveCriticalSection(&g_lock);
	void* buffer = reinterpret_cast<void*>(Dword(self, kSoundBuffer));
	if (inWork != 0 && buffer != nullptr) {
		CancelFor(buffer);
	}
	CleanDone();
	return reinterpret_cast<SoundDtorFn>(g_dtorOriginal)(self, nullptr);
}

}  // namespace

void InstallVoiceDecode(const char* iniPath, LogFn log) {
	if (g_installed || iniPath == nullptr) {
		return;
	}
	g_mode = vd::ModeOf(static_cast<int>(GetPrivateProfileIntA("Route", "VoiceDecode", 2, iniPath)), vd::Mode::kOwnDecoder);
	if (g_mode == vd::Mode::kOff) {
		return;
	}
	g_log = log;
	const UINT lead = GetPrivateProfileIntA("Route", "VoiceDecodeLeadMs", 150, iniPath);
	g_leadMs = lead > 5000 ? 5000 : lead;
	g_check = GetPrivateProfileIntA("Route", "VoiceDecodeCheck", 0, iniPath) != 0;
	g_leftLines = GetPrivateProfileIntA("Route", "VoiceDecodeLeftLines", 60, iniPath);
	g_standIn = GetPrivateProfileIntA("Route", "VoiceDecodeStandIn", 1, iniPath) != 0;
	g_testMissing = GetPrivateProfileIntA("Route", "VoiceDecodeTestMissing", 0, iniPath) != 0;
	QueryPerformanceFrequency(&g_frequency);
	InitializeCriticalSection(&g_lock);
	for (Job& job : g_jobs) {
		InitializeCriticalSection(&job.guard);
	}
	g_wake = CreateSemaphoreA(nullptr, 0, 0x7FFFFFFF, nullptr);
	HANDLE thread = g_wake != nullptr ? CreateThread(nullptr, 0, &Worker, nullptr, 0, nullptr) : nullptr;
	if (thread == nullptr) {
		log("Route: voice decode - no worker thread; the game's function stays");
		return;
	}
	SetThreadPriority(thread, THREAD_PRIORITY_ABOVE_NORMAL);
	CloseHandle(thread);
	// The destructor first: a line must never be in work without it.
	if (!InstallEntryDetour("sound destructor 0x006B6700 (voice decode)", kSoundDtor, kSoundDtorBytes, sizeof(kSoundDtorBytes),
	                        reinterpret_cast<const void*>(&SoundDtor), &g_dtorOriginal, log, DetourRole::Route)) {
		return;
	}
	g_installed = InstallEntryDetour("voice player 0x006A9E00 (voice decode)", kPlay, kPlayBytes, sizeof(kPlayBytes),
	                                 reinterpret_cast<const void*>(&Play), &g_playOriginal, log, DetourRole::Route);
	if (g_installed) {
		log("Route: voice decode - %s, the line's first %u ms at the call, the rest on a worker thread%s",
		    g_mode == vd::Mode::kEngineDecoder ? "the game's decoder" : "own decoder (minimp3)", g_leadMs,
		    g_check ? "; every line checked against the game's decoder" : "");
	}
}

void LogVoiceDecodeSummary() {
	if (!g_installed) {
		return;
	}
	EnterCriticalSection(&g_lock);
	const vd::Counts counts = g_counts;
	g_counts = vd::Counts{};
	LeaveCriticalSection(&g_lock);
	InterlockedExchange(&g_checkLines, 0);
	char line[600];
	vd::FormatSummary(counts, g_mode, g_leadMs, static_cast<std::uint64_t>(g_frequency.QuadPart), line, sizeof(line));
	g_log("%s", line);
}

}  // namespace obng::plugin
