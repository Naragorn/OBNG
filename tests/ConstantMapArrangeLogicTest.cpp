// Every flow of the constant map arranging's test (src/engine/render/ConstantMapArrangeLogic.h).

#include "Check.h"
#include "engine/render/ConstantMapArrangeLogic.h"

#include <vector>

using namespace obng::engine::render::constant_map_arrange;

namespace {

// Entries as kinds: -1 is a null entry, anything else the byte at +8.
struct FakePort {
	std::vector<int> kinds;
	Handle Entry(std::uint32_t index) const { return kinds[index] < 0 ? 0u : index + 100u; }
	std::uint8_t Kind(Handle entry) const { return static_cast<std::uint8_t>(kinds[entry - 100u]); }
};

bool Is(std::vector<int> kinds) {
	FakePort port{kinds};
	return Arranged(port, static_cast<std::uint32_t>(kinds.size()));
}

}  // namespace

int main() {
	Section("Arranged: nothing to swap");
	Check(Is({}) && Is({1}) && Is({0}) && Is({-1}), "no entry or one: the original returns at once");
	Check(Is({1, 1, 1}), "only entries of kind 1");
	Check(Is({0, 0, 2}), "only other entries");
	Check(Is({1, 1, 0, 0}), "the kind 1 entries first");
	Check(Is({1, -1, 1, 0, -1, 2}), "nulls between them are stepped over");
	Check(Is({-1, -1, -1}), "only nulls");
	Section("Not arranged: a kind 1 entry behind another kind");
	Check(!Is({0, 1}), "the smallest case");
	Check(!Is({1, 0, 1}), "one out of place at the end");
	Check(!Is({2, -1, 1}), "another kind, a null, then kind 1");
	Check(!Is({-1, 3, 0, 1, 1}), "a null first does not hide it");
	Check(Is({-1, 1, 0}), "a null first, then in order");
	Section("The kind is the byte's value: only 1 counts as the first group");
	Check(!Is({255, 1}) && Is({1, 255}) && Is({2, 0, 3}), "0, 2 and 255 are all 'other'");
	return Failures();
}
