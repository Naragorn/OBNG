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
		Check(n.compared == 4, "within the mesh groups four draws compared with their group's first (four in the group of five)");
	}

	Section("Count: which property word differs within a mesh group");
	{
		Key first = K(10, 1, 7, 9);
		first.words[4] = 0x100;
		first.words[6] = 0x600;
		first.words[10] = 0xA0;
		Key sameTextureOtherShaderProperty = first;
		sameTextureOtherShaderProperty.words[4] = 0x101;  // its own shader property object, the same class
		Key otherTexture = first;
		otherTexture.words[6] = 0x601;
		std::vector<Key> keys = {first, sameTextureOtherShaderProperty, otherTexture};
		const Counts n = Count(keys);
		Check(n.compared == 2 && n.wordDiffers[4] == 1 && n.wordDiffers[6] == 1 && n.wordDiffers[10] == 0 && n.wordDiffers[0] == 0,
		      "slot 4 differs once, slot 6 once, the shader property's class never");
		Totals t;
		t.Add(n);
		char line[900];
		Format(t, line, sizeof(line));
		Check(std::string(line).find("within the mesh groups 2 draws compared with their group's first, the word differing in %: 0:0 1:0 2:0 3:0 4:50 5:0 6:50") !=
		          std::string::npos,
		      "the share of each word written");
	}

	Section("Coverage: the grouped draws' pass objects");
	{
		std::vector<Key> none;
		const PassCoverage z = Coverage(none);
		Check(z.grouped == 0 && z.passes == 0 && z.forEighty == 0, "nothing grouped");
		std::vector<Key> keys;
		for (int i = 0; i < 8; ++i) {
			keys.push_back(K(1, 100, 0, 0));  // eight copies of mesh 1 in pass 100
		}
		keys.push_back(K(2, 200, 0, 0));
		keys.push_back(K(2, 200, 0, 0));  // two of mesh 2 in pass 200
		keys.push_back(K(3, 300, 0, 0));  // a single draw: not grouped
		const PassCoverage c = Coverage(keys);
		Check(c.grouped == 10 && c.passes == 2, "ten grouped draws in two pass objects; the single draw left out");
		Check(c.forEighty == 1 && c.forNinety == 2 && c.topShare[0] == 8 && c.topShare[1] == 2 && c.topShare[2] == 0,
		      "the largest pass covers 80 %, both are needed for 90 %");
		Check(c.topPass[0] == 100 && c.topDraws[0] == 8 && c.topPass[1] == 200 && c.topDraws[1] == 2 && c.topPass[2] == 0, "the largest passes named, in order");
		const std::uint8_t bytes[2] = {0x61, 0x62};  // "ab"
		Check(HashBytes(bytes, 2) == 0x4D2505CAu && HashBytes(bytes, 0) == 2166136261u, "FNV-1a over bytes, as the offline matcher computes it");
		Totals t;
		t.Add(Count(keys));
		t.AddCoverage(c);
		char line[1200];
		Format(t, line, sizeof(line));
		Check(std::string(line).find("the grouped draws' pass objects: up to 2 a pass, 1 cover 80 % and 2 cover 90 % of them, the three largest 80 / 20 / 0 %") !=
		          std::string::npos,
		      "the coverage written");
	}

	Section("Runs: groups only inside a run of one pass object, in draw order");
	{
		// pass 1: meshes 1 1 2 | pass 2: 1 | pass 1: 1 2 2
		std::vector<Key> keys = {K(1, 1, 0, 0), K(1, 1, 0, 0), K(2, 1, 0, 0), K(1, 2, 0, 0), K(1, 1, 0, 0), K(2, 1, 0, 0), K(2, 1, 0, 0)};
		const Counts n = Count(keys);
		Check(n.runs == 3 && n.longestRun == 3, "three runs, the longest three draws");
		Check(n.runGroups == 5, "groups within the runs: 2 + 1 + 2 = 5 (against 3 by mesh and pass over the whole pass)");
		Check(n.groups[0] == 3, "the whole-pass count is still three");
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
