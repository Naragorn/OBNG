// Every flow of the instance group count (plugin/src/InstanceGroupLogic.h).

#include "Check.h"
#include "InstanceGroupLogic.h"

#include <cstring>
#include <string>

using namespace obng::plugin::instance_groups;

namespace {

Key K(std::uint32_t data, std::uint32_t pass, std::uint32_t props, std::uint32_t effects) {
	Key k;
	k.data = data;
	k.pass = pass;
	k.props = props;
	k.effects = effects;
	return k;
}

}  // namespace

int main() {
	Section("The hash: equal contents, equal hashes");
	const std::uint32_t a[3] = {1, 2, 3};
	const std::uint32_t b[3] = {1, 2, 3};
	const std::uint32_t c[3] = {1, 2, 4};
	Check(HashWords(a, 3) == HashWords(b, 3) && HashWords(a, 3) != HashWords(c, 3), "the same words hash the same, a changed word differently");
	Check(HashWords(a, 0) == 2166136261u, "no words: the basis");

	Section("Count: an empty pass");
	{
		std::vector<Key> none;
		const Counts n = Count(none);
		Check(n.draws == 0 && n.groups[0] == 0 && n.inFour[2] == 0 && n.largest[1] == 0, "nothing counted");
	}

	Section("Count: each level finer than the one before");
	{
		std::vector<Key> keys = {
		    K(10, 1, 7, 9), K(10, 1, 7, 9), K(10, 1, 7, 9), K(10, 1, 7, 8),  // one mesh and pass: four draws, one of them other lights
		    K(10, 1, 6, 9),                                                   // the same mesh and pass, other properties
		    K(10, 2, 7, 9),                                                   // the same mesh, another pass
		    K(20, 1, 7, 9),                                                   // another mesh
		};
		const Counts n = Count(keys);
		Check(n.draws == 7, "seven draws");
		Check(n.groups[0] == 3 && n.largest[0] == 5 && n.inFour[0] == 5, "by mesh and pass: three groups, five draws in the largest");
		Check(n.groups[1] == 4 && n.largest[1] == 4 && n.inFour[1] == 4, "and properties: four groups, the largest four");
		Check(n.groups[2] == 5 && n.largest[2] == 3 && n.inFour[2] == 0, "and effect state: five groups, none of four or more");
	}

	Section("Totals and the line");
	{
		Totals t;
		char line[600];
		Format(t, line, sizeof(line));
		Check(std::string(line) == "Route: stereo draw instance groups - none noted", "no pass: said so");
		std::vector<Key> one = {K(1, 1, 1, 1), K(1, 1, 1, 1), K(1, 1, 1, 1), K(1, 1, 1, 1)};
		std::vector<Key> two = {K(2, 1, 1, 1), K(3, 1, 1, 1)};
		t.Add(Count(one));
		t.Add(Count(two));
		Check(t.passes == 2 && t.sum.draws == 6 && t.sum.groups[0] == 3 && t.sum.inFour[0] == 4 && t.sum.largest[0] == 4, "summed over passes, the largest kept");
		Format(t, line, sizeof(line));
		const std::string text(line);
		Check(text.rfind("Route: stereo draw instance groups - 2 passes noted, 3 pass setups a pass;", 0) == 0, "passes and setups a pass");
		Check(text.find("by mesh and pass 2 groups (50 % fewer draws), 2 draws in groups of four or more, the largest 4;") != std::string::npos,
		      "a level's groups, the share of draws saved, the draws in big groups and the largest");
		Check(text.find("and effect state") != std::string::npos, "all three levels written");
	}
	return Failures();
}
