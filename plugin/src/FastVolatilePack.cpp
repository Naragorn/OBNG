// The volatile pack's skip in the game: see FastVolatilePack.h.

#include "FastVolatilePack.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "ShadowRecorder.h"
#include "engine/render/VolatilePackLogic.h"

namespace obng::plugin {
namespace {

namespace vp = obng::engine::render::volatile_pack;

constexpr UInt32 kPacker = 0x00763130;
constexpr UInt8 kPackerBytes[] = {0x83, 0xEC, 0x1C, 0x53, 0x56};  // sub esp,1Ch; push ebx; push esi (read)
constexpr UInt32 kUnlock = 0x00776D80;
constexpr UInt8 kUnlockBytes[] = {0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x4C};  // push esi; mov esi,ecx; mov eax,[esi+4Ch] (read)

// Offsets (read: re/functions/0x00763130.md, 0x0076FE20.md, 0x00776D80.md, 0x0076F590.md).
constexpr UInt32 kDataVertices = 0x08;      // u16
constexpr UInt32 kDataPositions = 0x1C;
constexpr UInt32 kDataNormals = 0x20;
constexpr UInt32 kDataColours = 0x24;
constexpr UInt32 kDataConsistency = 0x2E;   // u16: nibble and dirty bits
constexpr UInt32 kRecordStreamCount = 0x1C;
constexpr UInt32 kRecordChips = 0x24;
constexpr UInt32 kChipBuffer = 0x08;
constexpr UInt32 kManagerStaging = 0x40;
constexpr UInt32 kManagerBytes = 0x4C;
constexpr UInt32 kDeclEntryCount = 0x1C;
constexpr UInt32 kDeclStreams = 0x24;       // 0x10-byte records: +0x08 entries, +0x0C stride
constexpr UInt32 kEntrySize = 0x1C;         // +0x04 offset, +0x08 selector
constexpr UInt32 kSelectorColour = 4;
constexpr UInt32 kColourBytes = 16;
constexpr UInt32 kMaxStreams = 8;

using PackerFn = UInt32(__fastcall*)(void* self, void* edx, UInt32 record, UInt32 data, UInt32 skin, UInt32 partition, UInt32 decl, UInt32 force);
using UnlockFn = UInt32(__fastcall*)(void* self, void* edx, UInt32 buffer);

template <class T>
T At(UInt32 address) {
	return *reinterpret_cast<volatile const T*>(static_cast<std::uintptr_t>(address));
}

enum class Mode { Off, Route, Shadow };
Mode g_mode = Mode::Off;
PackerFn g_packer = nullptr;
UnlockFn g_unlock = nullptr;
LogFn g_log = nullptr;

struct Kept {
	vp::Source source;
	std::vector<unsigned char> streams[kMaxStreams];  // the shadow's: each stream's bytes of the last pack
};

SRWLOCK g_lock = SRWLOCK_INIT;
vp::BufferOwners g_owners;
std::unordered_map<UInt32, Kept> g_kept;  // by record
constexpr std::size_t kKeptAtMost = 4096;

// The pack running on the render thread (the packer does not nest: the
// unlocks inside it are its streams').
DWORD g_packThread = 0;
UInt32 g_packRecord = 0;
UInt32 g_packDecl = 0;
UInt32 g_packData = 0;
bool g_packVerify = false;      // the shadow: compare this pack's streams with the kept ones
bool g_packKeepStreams = false; // the shadow: keep this pack's streams
bool g_verifyMismatch = false;
char g_verifyDetail[200];

volatile LONG64 g_counts[8] = {};  // by Verdict
volatile LONG64 g_skippedBytes = 0;

void VerifyOrKeepStream(UInt32 manager, UInt32 buffer) {
	// Which stream of the record is this buffer (the chips were replaced
	// before the lock, so the record names them already)?
	const UInt32 count = At<UInt32>(g_packRecord + kRecordStreamCount);
	const UInt32 chips = At<UInt32>(g_packRecord + kRecordChips);
	UInt32 stream = kMaxStreams;
	for (UInt32 i = 0; i < count && i < kMaxStreams && chips != 0; ++i) {
		const UInt32 chip = At<UInt32>(chips + 4 * i);
		if (chip != 0 && At<UInt32>(chip + kChipBuffer) == buffer) {
			stream = i;
			break;
		}
	}
	if (stream == kMaxStreams) {
		return;  // the index buffer or another manager's
	}
	const unsigned char* staging = At<const unsigned char*>(manager + kManagerStaging);
	const UInt32 bytes = At<UInt32>(manager + kManagerBytes);
	if (staging == nullptr || bytes == 0) {
		return;
	}
	Kept& kept = g_kept[g_packRecord];
	if (g_packVerify && !g_verifyMismatch) {
		std::uint32_t stride = 0;
		std::uint32_t gap = 0xFFFFFFFFu;
		if (g_packDecl != 0) {
			const UInt32 streamRecord = At<UInt32>(g_packDecl + kDeclStreams) + stream * 16;
			stride = At<UInt32>(streamRecord + 0x0C);
			if (At<UInt32>(g_packData + kDataColours) == 0) {
				const UInt32 entries = At<UInt32>(streamRecord + 0x08);
				const UInt32 entryCount = At<UInt32>(g_packDecl + kDeclEntryCount);
				for (UInt32 e = 0; e < entryCount && entries != 0; ++e) {
					if (At<UInt32>(entries + e * kEntrySize + 0x08) == kSelectorColour) {
						gap = At<UInt32>(entries + e * kEntrySize + 0x04);
						break;
					}
				}
			}
		}
		const std::vector<unsigned char>& last = kept.streams[stream];
		if (last.size() != bytes) {
			g_verifyMismatch = true;
			std::snprintf(g_verifyDetail, sizeof(g_verifyDetail), " stream %u: %u bytes now, %u kept", stream, bytes, static_cast<unsigned>(last.size()));
		} else if (!vp::SameOutsideGap(last.data(), staging, bytes, stride, gap, kColourBytes)) {
			g_verifyMismatch = true;
			std::snprintf(g_verifyDetail, sizeof(g_verifyDetail), " stream %u: %u bytes, stride %u, unwritten entry at %d - bytes outside it differ from the kept pack",
			              stream, bytes, stride, static_cast<int>(gap));
		}
	}
	if (g_packKeepStreams) {
		kept.streams[stream].assign(staging, staging + bytes);
	}
}

UInt32 __fastcall UnlockReplacement(void* self, void* /*edx*/, UInt32 buffer) {
	AcquireSRWLockExclusive(&g_lock);
	const bool inPack = g_packRecord != 0 && GetCurrentThreadId() == g_packThread;
	g_owners.Wrote(buffer, inPack ? g_packRecord : 0);
	if (inPack && (g_packVerify || g_packKeepStreams)) {
		VerifyOrKeepStream(reinterpret_cast<UInt32>(self), buffer);
	}
	ReleaseSRWLockExclusive(&g_lock);
	return g_unlock(self, nullptr, buffer);
}

UInt32 __fastcall PackerReplacement(void* self, void* /*edx*/, UInt32 record, UInt32 data, UInt32 skin, UInt32 partition, UInt32 decl, UInt32 force) {
	if (record == 0 || data == 0) {
		return g_packer(self, nullptr, record, data, skin, partition, decl, force);
	}
	vp::Inputs in;
	in.consistencyWord = At<UInt16>(data + kDataConsistency);
	if (!vp::IsVolatile(in.consistencyWord)) {
		return g_packer(self, nullptr, record, data, skin, partition, decl, force);
	}
	in.force = (force & 0xFF) != 0;
	const UInt32 streamCount = At<UInt32>(record + kRecordStreamCount);
	const UInt32 chipArray = At<UInt32>(record + kRecordChips);
	vp::Handle chips[kMaxStreams] = {};
	vp::Handle buffers[kMaxStreams] = {};
	if (streamCount <= kMaxStreams && chipArray != 0) {
		for (UInt32 i = 0; i < streamCount; ++i) {
			chips[i] = At<UInt32>(chipArray + 4 * i);
			buffers[i] = chips[i] != 0 ? At<UInt32>(chips[i] + kChipBuffer) : 0;
		}
		in.allocated = vp::AllStreamsAllocated(streamCount, chips, buffers);
	}
	const UInt32 vertices = At<UInt16>(data + kDataVertices);
	const unsigned char* positions = At<const unsigned char*>(data + kDataPositions);
	const unsigned char* normals = At<const unsigned char*>(data + kDataNormals);
	in.sourceReadable = positions != nullptr && vertices != 0;

	AcquireSRWLockExclusive(&g_lock);
	if (g_kept.size() >= kKeptAtMost) {
		g_kept.clear();
	}
	auto it = g_kept.find(record);
	in.haveKept = it != g_kept.end() && it->second.source.data != 0;
	if (in.haveKept && in.sourceReadable) {
		in.sourceSame = vp::SameSource(it->second.source, data, vertices, positions, normals);
	}
	if (in.allocated) {
		in.buffersOwned = true;
		for (UInt32 i = 0; i < streamCount; ++i) {
			if (!g_owners.OwnedBy(buffers[i], record)) {
				in.buffersOwned = false;
			}
		}
	}
	const vp::Verdict verdict = vp::Decide(in);
	InterlockedIncrement64(&g_counts[static_cast<int>(verdict)]);

	if (verdict == vp::Verdict::Skip && g_mode == Mode::Route) {
		ReleaseSRWLockExclusive(&g_lock);
		// The packer's own early-out: static, allocated, not forced (read
		// 0x00763153-0x0076317C). The nibble goes back whatever the word is
		// afterwards.
		volatile UInt16* word = reinterpret_cast<volatile UInt16*>(static_cast<std::uintptr_t>(data + kDataConsistency));
		*word = vp::AsStatic(in.consistencyWord);
		const UInt32 eax = g_packer(self, nullptr, record, data, skin, partition, decl, force);
		*word = vp::BackToVolatile(*word);
		InterlockedAdd64(&g_skippedBytes, static_cast<LONG64>(vertices) * 88);
		return eax;
	}

	// A pack runs: the unlocks inside it are this record's.
	g_packThread = GetCurrentThreadId();
	g_packRecord = record;
	g_packDecl = decl;
	g_packData = data;
	g_packVerify = g_mode == Mode::Shadow && verdict == vp::Verdict::Skip;
	g_packKeepStreams = g_mode == Mode::Shadow;
	g_verifyMismatch = false;
	ReleaseSRWLockExclusive(&g_lock);

	// The pack runs as a mutable one with every dirty bit (VolatilePackLogic.h
	// AsMutableAllDirty): the record keeps its own chips and buffers.
	volatile UInt16* packWord = reinterpret_cast<volatile UInt16*>(static_cast<std::uintptr_t>(data + kDataConsistency));
	*packWord = vp::AsMutableAllDirty();
	const UInt32 eax = g_packer(self, nullptr, record, data, skin, partition, decl, force);
	*packWord = vp::BackToVolatile(*packWord);

	AcquireSRWLockExclusive(&g_lock);
	const bool verified = g_packVerify;
	const bool mismatch = g_verifyMismatch;
	g_packRecord = 0;
	g_packVerify = false;
	g_packKeepStreams = false;
	if (in.sourceReadable && vp::KeepsSource(verdict)) {
		vp::Keep(g_kept[record].source, data, vertices, positions, normals);
	}
	ReleaseSRWLockExclusive(&g_lock);
	if (verified) {
		if (mismatch) {
			char inputs[96];
			std::snprintf(inputs, sizeof(inputs), "record=%08X data=%08X vertices=%u decl=%08X", record, data, vertices, decl);
			RecordCall(Shadowed::VolatilePack, true, g_verifyDetail, inputs);
		} else {
			RecordCall(Shadowed::VolatilePack, false, "", "");
		}
	}
	return eax;
}

UInt32 Install(Mode mode, LogFn log) {
	static bool s_done = false;
	if (s_done) {
		return 0;
	}
	s_done = true;
	g_log = log;
	const DetourRole role = mode == Mode::Route ? DetourRole::Route : DetourRole::Shadow;
	UInt32 made = 0;
	made += InstallEntryDetour("dynamic buffer unlock 0x00776D80 (volatile pack)", kUnlock, kUnlockBytes, sizeof(kUnlockBytes),
	                           reinterpret_cast<const void*>(&UnlockReplacement), reinterpret_cast<void**>(&g_unlock), log, DetourRole::Shadow) ? 1 : 0;
	if (made == 1) {
		made += InstallEntryDetour("PackSkinnedGeometryBuffer 0x00763130 (volatile pack)", kPacker, kPackerBytes, sizeof(kPackerBytes),
		                           reinterpret_cast<const void*>(&PackerReplacement), reinterpret_cast<void**>(&g_packer), log, role) ? 1 : 0;
	}
	if (made == 2) {
		g_mode = mode;
		if (mode == Mode::Route) {
			log("Route volatile pack: a volatile geometry with unchanged positions and normals and untouched buffers takes the packer's static "
			    "early-out (VolatilePackLogic.h)");
		}
	}
	return made;
}

}  // namespace

UInt32 InstallRouteVolatilePack(const RouteSettings& settings, LogFn log) { return settings.volatilePack ? Install(Mode::Route, log) : 0; }
UInt32 InstallShadowVolatilePack(const ShadowSettings& settings, LogFn log) { return settings.volatilePack ? Install(Mode::Shadow, log) : 0; }

void LogVolatilePackSummary() {
	if (g_mode == Mode::Off || g_log == nullptr) {
		return;
	}
	LONG64 c[8];
	for (int i = 0; i < 8; ++i) {
		c[i] = InterlockedExchange64(&g_counts[i], 0);
	}
	const LONG64 bytes = InterlockedExchange64(&g_skippedBytes, 0);
	g_log("%s: volatile pack - %lld %s, packed: %lld forced, %lld unallocated, %lld unreadable, %lld first, %lld source changed, %lld buffer written by "
	      "another; %.1f MB not copied",
	      g_mode == Mode::Route ? "Route" : "Shadow", c[static_cast<int>(vp::Verdict::Skip)], g_mode == Mode::Route ? "skipped" : "would skip (verified)",
	      c[static_cast<int>(vp::Verdict::Forced)], c[static_cast<int>(vp::Verdict::NotAllocated)], c[static_cast<int>(vp::Verdict::NoSource)],
	      c[static_cast<int>(vp::Verdict::FirstPack)], c[static_cast<int>(vp::Verdict::SourceChanged)], c[static_cast<int>(vp::Verdict::BufferLost)],
	      static_cast<double>(bytes) / (1024.0 * 1024.0));
}

}  // namespace obng::plugin
