// Every flow of the volatile pack's decisions (src/engine/render/VolatilePackLogic.h).

#include "Check.h"
#include "engine/render/VolatilePackLogic.h"

#include <vector>

using namespace obng::engine::render::volatile_pack;

namespace {

void TestConsistency() {
	Section("The consistency word: the nibble decides, the dirty bits stay");
	Check(IsVolatile(0x8000) && IsVolatile(0x8FFF) && !IsVolatile(0x4000) && !IsVolatile(0x0000) && !IsVolatile(0xC000), "0x8 is volatile, nothing else");
	Check(AsStatic(0x8ABC) == 0x4ABC && AsStatic(0x8000) == 0x4000, "the nibble replaced, the low twelve bits kept");
	Check(AsMutableAllDirty() == 0x0FFF && !IsVolatile(AsMutableAllDirty()) && (AsMutableAllDirty() & kConsistencyMask) != kStatic,
	      "a pack that must run: mutable, every dirty bit");
	Check(BackToVolatile(0x0000) == 0x8000 && BackToVolatile(0x4ABC) == 0x8ABC && BackToVolatile(0x0FFF) == 0x8FFF, "the nibble back, the dirty bits as left");
}

void TestAllocated() {
	Section("The record's allocation as 0x00777F10 answers it");
	const Handle chips[3] = {10, 11, 12};
	const Handle buffers[3] = {20, 21, 22};
	Check(AllStreamsAllocated(3, chips, buffers), "every chip and buffer present");
	Check(!AllStreamsAllocated(0, chips, buffers), "no stream: not allocated");
	const Handle noChip[3] = {10, 0, 12};
	Check(!AllStreamsAllocated(3, noChip, buffers), "a missing chip");
	const Handle noBuffer[3] = {20, 21, 0};
	Check(!AllStreamsAllocated(3, chips, noBuffer), "a chip without its buffer");
}

void TestOwners() {
	Section("Buffer owners: the last writer of a buffer");
	BufferOwners owners;
	Check(!owners.OwnedBy(100, 1), "an unknown buffer is nobody's");
	owners.Wrote(100, 1);
	Check(owners.OwnedBy(100, 1) && !owners.OwnedBy(100, 2), "the record that wrote it owns it");
	owners.Wrote(100, 0);
	Check(!owners.OwnedBy(100, 1) && !owners.OwnedBy(100, 0), "an unlock outside a pack takes it from every record; 0 owns nothing");
	owners.Wrote(100, 2);
	Check(owners.OwnedBy(100, 2) && !owners.OwnedBy(100, 1), "another record's pack takes it");
	owners.Wrote(0, 3);
	Check(owners.Size() == 1, "a null buffer is not tracked");
	owners.Clear();
	Check(owners.Size() == 0 && !owners.OwnedBy(100, 2), "cleared");
}

void TestSource() {
	Section("The kept source: equal arrays, and every way of differing");
	std::vector<unsigned char> positions(36, 1);
	std::vector<unsigned char> normals(36, 2);
	Source kept;
	Check(!SameSource(kept, 5, 3, positions.data(), normals.data()), "nothing kept yet");
	Keep(kept, 5, 3, positions.data(), normals.data());
	Check(SameSource(kept, 5, 3, positions.data(), normals.data()), "the same arrays");
	Check(!SameSource(kept, 6, 3, positions.data(), normals.data()), "another data object");
	Check(!SameSource(kept, 5, 2, positions.data(), normals.data()), "another vertex count");
	Check(!SameSource(kept, 5, 3, nullptr, normals.data()), "no positions");
	std::vector<unsigned char> moved = positions;
	moved[35] = 9;
	Check(!SameSource(kept, 5, 3, moved.data(), normals.data()), "one position byte changed");
	std::vector<unsigned char> turned = normals;
	turned[0] = 9;
	Check(!SameSource(kept, 5, 3, positions.data(), turned.data()), "one normal byte changed");
	Check(!SameSource(kept, 5, 3, positions.data(), nullptr), "the normals gone");
	Source bare;
	Keep(bare, 5, 3, positions.data(), nullptr);
	Check(SameSource(bare, 5, 3, positions.data(), nullptr) && !SameSource(bare, 5, 3, positions.data(), normals.data()),
	      "a data without normals: equal without them, different once they appear");
	Check(!SameSource(kept, 5, 0, positions.data(), normals.data()), "zero vertices is never the same");
}

void TestDecide() {
	Section("The decision, one condition at a time");
	Inputs in;
	in.consistencyWord = 0x4000;
	Check(Decide(in) == Verdict::NotVolatile, "static data: not this lever's");
	in.consistencyWord = 0x8FFF;
	in.force = true;
	Check(Decide(in) == Verdict::Forced, "forced: pack");
	in.force = false;
	Check(Decide(in) == Verdict::NotAllocated, "a stream unallocated: pack");
	in.allocated = true;
	Check(Decide(in) == Verdict::NoSource, "unreadable source: pack");
	in.sourceReadable = true;
	Check(Decide(in) == Verdict::FirstPack, "nothing kept: pack");
	in.haveKept = true;
	Check(Decide(in) == Verdict::SourceChanged, "source differs: pack");
	in.sourceSame = true;
	Check(Decide(in) == Verdict::BufferLost, "a buffer written by another: pack");
	in.buffersOwned = true;
	Check(Decide(in) == Verdict::Skip, "everything holds: skip");
	Check(KeepsSource(Verdict::FirstPack) && KeepsSource(Verdict::SourceChanged) && KeepsSource(Verdict::BufferLost) && KeepsSource(Verdict::NotAllocated) &&
	          KeepsSource(Verdict::Forced) && !KeepsSource(Verdict::Skip) && !KeepsSource(Verdict::NotVolatile) && !KeepsSource(Verdict::NoSource),
	      "the source is kept after every pack of a volatile data that could read it");
}

void TestSameOutsideGap() {
	Section("The gate's comparison: equal outside the unwritten entry");
	std::vector<unsigned char> a(3 * 8, 7);
	std::vector<unsigned char> b = a;
	Check(SameOutsideGap(a.data(), b.data(), a.size(), 8, 8, 0), "no gap, equal");
	b[2 * 8 + 3] = 1;
	Check(!SameOutsideGap(a.data(), b.data(), a.size(), 8, 8, 0), "no gap, one byte differs");
	Check(SameOutsideGap(a.data(), b.data(), a.size(), 8, 2, 3), "the differing byte inside the gap (offset 2, 3 bytes)");
	b[2 * 8 + 5] = 1;
	Check(!SameOutsideGap(a.data(), b.data(), a.size(), 8, 2, 3), "a byte after the gap differs");
	b[2 * 8 + 5] = 7;
	b[1] = 1;
	Check(!SameOutsideGap(a.data(), b.data(), a.size(), 8, 2, 3), "a byte before the gap differs");
	b[1] = 7;
	b[2 * 8 + 3] = 7;
	b[7] = 1;
	Check(SameOutsideGap(a.data(), b.data(), a.size(), 8, 6, 100), "a gap running past the stride is cut at it");
	std::vector<unsigned char> c(20, 3);
	std::vector<unsigned char> d = c;
	d[19] = 0;
	Check(!SameOutsideGap(c.data(), d.data(), c.size(), 8, 2, 3), "bytes beyond the last whole vertex are compared too");
	Check(SameOutsideGap(c.data(), c.data(), c.size(), 0, 0, 0), "stride 0: a plain comparison");
}

}  // namespace

int main() {
	TestConsistency();
	TestAllocated();
	TestOwners();
	TestSource();
	TestDecide();
	TestSameOutsideGap();
	return Failures();
}
