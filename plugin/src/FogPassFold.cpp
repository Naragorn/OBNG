#include "FogPassFold.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>

#include "Memory.h"
#include "engine/render/FogPassFoldLogic.h"

namespace obng::plugin {

namespace {

namespace fold = obng::engine::render::fog_pass_fold;

// The lit shader property as the builder reads it (0x0085B9A3 - 0x0085B9C1, 0x0085B9CC): the pass list's last node
// at +0x30 and its count at +0x34, the object 0x0085A200 draws for at +0x8C. A node is {next, prev, record}; a
// record has its pass id at +4 (u16) and the byte the builder marks at +7.
constexpr std::size_t kLastNode = 0x30, kCount = 0x34, kExtra = 0x8C;
constexpr std::size_t kNodePrev = 0x4, kNodeRecord = 0x8, kRecordPass = 0x4, kRecordMark = 0x7;

using AppendPassFn = void(__thiscall*)(void* self, UInt32 geometry, short* count, UInt32 building, UInt8* first, UInt32 a, UInt32 b, UInt32 c);

LogFn g_log = nullptr;
bool g_on = false;
fold::Mode g_mode = fold::Mode::Off;
fold::Counts g_counts;  // the render thread alone builds pass lists
bool g_alternate = false;
std::atomic<bool> g_active{true};
std::uint64_t g_switches = 0;  // the summary's thread alone

template <typename T>
T At(UInt32 base, std::size_t offset) {
	T value;
	std::memcpy(&value, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(base) + offset), sizeof(T));
	return value;
}

UInt16 PassOf(UInt32 node) {
	const UInt32 record = At<UInt32>(node, kNodeRecord);
	return record != 0 ? At<UInt16>(record, kRecordPass) : UInt16{0};
}

// In place of the builder's call of 0x00854190.
void __fastcall AppendPass(void* self, void*, UInt32 geometry, short* count, UInt32 building, UInt8* first, UInt32 a, UInt32 b, UInt32 c) {
	const UInt32 property = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(self));
	const UInt32 last = At<UInt32>(property, kLastNode);
	const fold::Shape shape = fold::ShapeOf(
	    static_cast<UInt8>(building) == 1, At<UInt32>(property, kExtra) != 0, At<UInt32>(property, kCount), last,
	    [](UInt32 node) { return At<UInt32>(node, kNodePrev); }, &PassOf);
	const bool folds = g_active.load(std::memory_order_relaxed) && fold::Folds(g_mode, shape);
	bool marked = false;
	if (folds) {
		// The builder's other ending (0x0085B9A9 - 0x0085B9C1): the last record takes the mark.
		const UInt32 record = At<UInt32>(last, kNodeRecord);
		if (record != 0 && fold::MarksLast(At<UInt16>(record, kRecordPass))) {
			*reinterpret_cast<UInt8*>(static_cast<std::uintptr_t>(record) + kRecordMark) = 1;
			marked = true;
		}
	} else {
		reinterpret_cast<AppendPassFn>(static_cast<std::uintptr_t>(fold::kAppendPass))(self, geometry, count, building, first, a, b, c);
	}
	fold::Count(g_counts, shape, folds, marked);
}

}  // namespace

void InstallFogPassFold(const char* iniPath, LogFn log) {
	static bool s_done = false;
	if (s_done || iniPath == nullptr) {
		return;
	}
	s_done = true;
	g_mode = fold::ModeOf(static_cast<int>(GetPrivateProfileIntA("Route", "FogPassFold", fold::Shipped(), iniPath)));
	if (g_mode == fold::Mode::Off) {
		log("Route: fog pass fold - off in OBNG.ini; every lit geometry with all four lighting passes gets its pass without lights, as the game builds it");
		return;
	}
	g_log = log;
	UInt8 expected[5] = {0xE8}, patch[5] = {0xE8};
	const UInt32 toOriginal = fold::kAppendPass - (fold::kCallSite + 5);
	const UInt32 toRoute = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(&AppendPass)) - (fold::kCallSite + 5);
	std::memcpy(expected + 1, &toOriginal, 4);
	std::memcpy(patch + 1, &toRoute, 4);
	if (!Verify(fold::kCallSite, expected, sizeof(expected))) {
		log("Route: fog pass fold - the builder's call at %08X is not the game's own; not routed", fold::kCallSite);
		g_mode = fold::Mode::Off;
		return;
	}
	if (!SafeWrite(fold::kCallSite, patch, sizeof(patch))) {
		log("Route: fog pass fold - the builder's call at %08X could not be written; not routed", fold::kCallSite);
		g_mode = fold::Mode::Off;
		return;
	}
	g_on = true;
	g_alternate = g_mode != fold::Mode::Census && GetPrivateProfileIntA("Route", "FogPassFoldAlternate", 0, iniPath) != 0;
	g_active.store(fold::Active(g_alternate, g_switches), std::memory_order_relaxed);
	if (g_alternate) {
		log("Route: fog pass fold - alternating: every summary switches between the game's ending and the route's and makes every pass list build anew "
		    "(bit %02X of the pass mask at %08X flipped); the run starts with the game's", fold::kRebuildBit, fold::kPassMask);
	}
	log("Route: fog pass fold (%s) - the pass without lights that ends a lit geometry's list (0x00854190 from %08X) %s",
	    g_mode == fold::Mode::Census ? "census" : g_mode == fold::Mode::FoldAll ? "every list" : "single lists", fold::kCallSite,
	    g_mode == fold::Mode::Census    ? "is counted by the list's shape and left to the game"
	    : g_mode == fold::Mode::FoldAll ? "is left out for every list without a specular light pass, the last record marked instead"
	                                    : "is left out for a list of one record that is no specular light pass, that record marked instead");
}

void LogFogPassFoldSummary() {
	if (!g_on) {
		return;
	}
	const auto of = [](fold::Shape s) { return g_counts.byShape[static_cast<unsigned>(s)]; };
	g_log("Route: fog pass fold (%s) - since the start %llu lists ended with the pass without lights asked for: counted only %llu, empty %llu, "
	      "with the extra object %llu, with a specular light pass %llu, one record %llu, several records without a specular light pass %llu; "
	      "folded %llu (last record marked %llu)",
	      g_mode == fold::Mode::Census ? "census" : g_mode == fold::Mode::FoldAll ? "every list" : "single lists", g_counts.calls,
	      of(fold::Shape::Counted), of(fold::Shape::Empty), of(fold::Shape::Extra), of(fold::Shape::Specular), of(fold::Shape::Single),
	      of(fold::Shape::Several), g_counts.folded, g_counts.marked);
	if (g_alternate) {
		++g_switches;
		const bool active = fold::Active(g_alternate, g_switches);
		g_active.store(active, std::memory_order_relaxed);
		// Every list's key holds the mask: one bit changed and each is built anew at its next use.
		auto* mask = reinterpret_cast<volatile UInt32*>(static_cast<std::uintptr_t>(fold::kPassMask));
		*mask = *mask ^ fold::kRebuildBit;
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		g_log("Route: fog pass fold - switch %llu at tick %lld: %s from here", g_switches, now.QuadPart, active ? "the route's ending" : "the game's ending");
	}
}

}  // namespace obng::plugin
