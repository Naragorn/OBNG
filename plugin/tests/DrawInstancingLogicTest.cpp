// Every flow of the draw instancing's decisions, grouping, predictions and
// matrix work (plugin/src/DrawInstancingLogic.h).

#include "Check.h"
#include "DrawInstancingLogic.h"

#include <cmath>
#include <cstring>
#include <string>

using namespace obng::plugin::draw_instancing;

namespace {

// A record that may wait: in the walk, not the first, a whole shape with
// opaque alpha, the depth written, its pass predicted.
Facts Waiting() {
	Facts f;
	f.inList = true;
	f.inWalk = true;
	f.first = false;
	f.readable = true;
	f.geometry = true;
	f.skinned = false;
	f.shaderAlpha = 1.0f;
	f.materialAlpha = 1.0f;
	f.hasAlpha = false;
	f.hasZ = false;
	f.predicted = true;
	return f;
}

Key KeyOf(std::uint32_t data, std::uint32_t pass) {
	Key k;
	k.w[kWordData] = data;
	k.w[kWordPass] = pass;
	return k;
}

// A rotation about z by `angle` radians, NiMatrix33 order (row-major, acting
// on a column vector).
void RotationZ(double angle, float out[9]) {
	const double c = std::cos(angle);
	const double s = std::sin(angle);
	const double m[9] = {c, -s, 0, s, c, 0, 0, 0, 1};
	for (int i = 0; i < 9; ++i) {
		out[i] = static_cast<float>(m[i]);
	}
}

// A view-projection and the registers the engine uploads for one draw:
// ModelViewProj = VP * W in c0..c3, the light direction in model space in
// c13 (the world direction taken through W's inverse rotation and scale), a
// shadow matrix S * W in c28..c31, the other registers filled with markers.
void Registers(const Mat4& vp, const Mat4& shadow, const double lightWorld[3], const InstanceRows& w, float out[kVertexRegisters][4]) {
	for (int r = 0; r < kVertexRegisters; ++r) {
		for (int c = 0; c < 4; ++c) {
			out[r][c] = static_cast<float>(100 * r + c);
		}
	}
	const Mat4 world = ToMat4(w);
	const Mat4 mvp = Product(vp, world);
	const Mat4 sw = Product(shadow, world);
	for (int r = 0; r < 4; ++r) {
		for (int c = 0; c < 4; ++c) {
			out[kRegisterViewProj + r][c] = static_cast<float>(mvp.m[r][c]);
			out[kRegisterShadow + r][c] = static_cast<float>(sw.m[r][c]);
		}
	}
	Mat4 inverse;
	InvertAffine(w, &inverse);
	for (int r = 0; r < 3; ++r) {
		out[kRegisterLight][r] = static_cast<float>(inverse.m[r][0] * lightWorld[0] + inverse.m[r][1] * lightWorld[1] + inverse.m[r][2] * lightWorld[2]);
	}
	out[kRegisterLight][3] = 0.0f;
}

Mat4 SomeViewProj() {
	Mat4 m{};
	const double v[4][4] = {{1.2, 0.1, -0.3, 5.0}, {0.05, 1.7, 0.2, -3.0}, {0.3, -0.2, 1.0, 40.0}, {0.3, -0.2, 1.0, 42.0}};
	std::memcpy(m.m, v, sizeof(v));
	return m;
}

Mat4 SomeShadow() {
	Mat4 m{};
	const double v[4][4] = {{0.01, 0.002, 0.0, 0.5}, {0.0, 0.01, 0.003, 0.25}, {0.1, 0.1, 0.1, 0.1}, {0.0, 0.0, 0.0001, 1.0}};
	std::memcpy(m.m, v, sizeof(v));
	return m;
}

}  // namespace

int main() {
	Section("ModeFromIni: 0, 1 and 2 known, anything else off and said");
	{
		bool known = false;
		Check(ModeFromIni(0, &known) == Mode::kOff && known, "0 is off");
		Check(ModeFromIni(1, &known) == Mode::kVerify && known, "1 verifies");
		Check(ModeFromIni(2, &known) == Mode::kInstanced && known, "2 instances");
		Check(ModeFromIni(3, &known) == Mode::kOff && !known, "3 is unknown and off");
		Check(ModeFromIni(-1, &known) == Mode::kOff && !known, "-1 is unknown and off");
	}

	Section("Decide: a record waits only when every condition holds");
	{
		Check(Decide(Waiting()) == Why::kWaits, "the waiting record waits");
		Facts f = Waiting();
		f.inList = false;
		Check(Decide(f) == Why::kNotInList, "outside a pass list: drawn at once");
		f = Waiting();
		f.inWalk = false;
		Check(Decide(f) == Why::kNotInWalk, "not the walk's next item: drawn at once");
		f = Waiting();
		f.first = true;
		Check(Decide(f) == Why::kFirstOfList, "the list's first record (the shader state's bracket): drawn at once");
		f = Waiting();
		f.readable = false;
		Check(Decide(f) == Why::kUnreadable, "a read faulted: drawn at once");
		f = Waiting();
		f.geometry = false;
		Check(Decide(f) == Why::kNoGeometry, "no geometry: drawn at once");
		f = Waiting();
		f.skinned = true;
		Check(Decide(f) == Why::kSkinned, "skinned: drawn at once");
		f = Waiting();
		f.shaderAlpha = 0.5f;
		Check(Decide(f) == Why::kTranslucent, "the shader property's alpha below one: drawn at once");
		f = Waiting();
		f.materialAlpha = 0.99f;
		Check(Decide(f) == Why::kTranslucent, "the material's alpha below one: drawn at once");
		f = Waiting();
		f.hasAlpha = true;
		f.alphaFlags = 0x0001;
		Check(Decide(f) == Why::kBlended, "the alpha property blends: drawn at once");
		f.alphaFlags = 0x0200;
		Check(Decide(f) == Why::kWaits, "an alpha test alone does not depend on the order: waits");
		f = Waiting();
		f.hasZ = true;
		f.zFlags = 0x0001;
		Check(Decide(f) == Why::kDepthOff, "the depth tested but not written: drawn at once");
		f.zFlags = 0x000F;
		Check(Decide(f) == Why::kWaits, "the depth tested and written: waits");
		f = Waiting();
		f.multiPass = true;
		Check(Decide(f) == Why::kMultiPass, "a geometry with more render passes (they test against this pass's depth): drawn at once");
		f = Waiting();
		f.predicted = false;
		Check(Decide(f) == Why::kNotPredicted, "the pass not known to draw with SLS2003: drawn at once");
		Check(std::string(WhyName(Why::kWaits)) == "waits" && std::string(WhyName(Why::kNotPredicted)) == "pass not predicted", "the names");
	}

	Section("OrderFree: what may be drawn before records that wait");
	{
		Facts f = Waiting();
		Check(OrderFree(f), "opaque and the depth written");
		f.hasAlpha = true;
		f.alphaFlags = 1;
		Check(!OrderFree(f), "blended: the waiting records must be drawn first");
		f = Waiting();
		f.hasZ = true;
		f.zFlags = 1;
		Check(!OrderFree(f), "the depth not written: the waiting records must be drawn first");
		f = Waiting();
		f.readable = false;
		Check(!OrderFree(f), "unreadable: taken as dependent on the order");
	}

	Section("Pending: records by key in first-seen order, the records of a group in theirs");
	{
		static Pending p;
		Check(p.Empty() && p.GroupCount() == 0, "empty at first");
		Check(p.Add(KeyOf(10, 1), 0xA1, 0xB1) == Pending::Added::kNew, "a new key");
		Check(p.Add(KeyOf(20, 1), 0xA2, 0xB2) == Pending::Added::kNew, "another key");
		Check(p.Add(KeyOf(10, 1), 0xA3, 0xB3) == Pending::Added::kJoined, "the first key again joins its group");
		Check(p.Add(KeyOf(10, 2), 0xA4, 0xB4) == Pending::Added::kNew, "the same mesh in another pass is another group");
		Check(p.GroupCount() == 3 && p.RecordCount() == 4, "three groups, four records");
		const Pending::Group& g0 = p.GroupAt(0);
		Check(g0.count == 2 && p.RecordAt(g0.first).record == 0xA1 && p.RecordAt(p.RecordAt(g0.first).next).record == 0xA3 &&
		          p.RecordAt(p.RecordAt(g0.first).next).next == Pending::kNone,
		      "the first group: its two records in the order they came");
		Check(p.RecordAt(g0.first).geometry == 0xB1, "a record keeps its geometry");
		Check(p.GroupAt(1).count == 1 && p.RecordAt(p.GroupAt(1).first).record == 0xA2, "the second group: one record");
		Check(p.GroupAt(2).key.w[kWordPass] == 2, "the third group keeps its key");
		p.Clear();
		Check(p.Empty() && p.GroupCount() == 0 && p.RecordCount() == 0, "cleared");
		Check(p.Add(KeyOf(10, 1), 0xA5, 0xB5) == Pending::Added::kNew, "after the clear the old key is new again");
		p.Clear();
	}

	Section("Pending: full at the record or the group limit");
	{
		static Pending p;
		std::uint32_t added = 0;
		while (p.Add(KeyOf(added, 1), added + 1, added + 1) == Pending::Added::kNew) {
			++added;
		}
		Check(added == Pending::kMaxGroups, "new keys stop at the group limit");
		Check(p.Add(KeyOf(0, 1), 7, 7) == Pending::Added::kJoined, "an existing key still joins");
		p.Clear();
		for (std::uint32_t i = 0; i < Pending::kMaxRecords; ++i) {
			p.Add(KeyOf(5, 1), i + 1, i + 1);
		}
		Check(p.RecordCount() == Pending::kMaxRecords && p.GroupAt(0).count == Pending::kMaxRecords, "one group holds the record limit");
		Check(p.Add(KeyOf(5, 1), 1, 1) == Pending::Added::kFull, "one more record: full");
		Check(p.Add(KeyOf(6, 1), 1, 1) == Pending::Added::kFull, "a new key with the records full: full");
		p.Clear();
	}

	Section("Keys: equal words equal keys, one word apart another key");
	{
		Key a = KeyOf(10, 1);
		Key b = KeyOf(10, 1);
		Check(a == b && KeyHash(a) == KeyHash(b), "equal");
		b.w[kWordMaterial] = 5;
		Check(!(a == b), "a material apart");
		Check(HashWords(nullptr, 0) == 2166136261u, "no words: the FNV basis");
	}

	Section("IgnoredWords and LeaveOut: words named, the structural three never");
	{
		int unknown = 0;
		const std::uint32_t mask = IgnoredWords("lights, material,effects", &unknown);
		Check(unknown == 0 && mask == ((1u << kWordLights) | (1u << kWordMaterial) | (1u << kWordEffects)), "three names, three bits");
		Check(IgnoredWords("", &unknown) == 0 && unknown == 0, "nothing named: nothing left out");
		Check(IgnoredWords("pass,data,shader,bogus", &unknown) == 0 && unknown == 4, "the pass, mesh and shader are not names here; unknown counted");
		Key k = KeyOf(10, 1);
		k.w[kWordShader] = 3;
		k.w[kWordLights] = 7;
		k.w[kWordMaterial] = 8;
		k.w[kWordTexturing] = 9;
		LeaveOut(&k, mask | 0x7u);
		Check(k.w[kWordLights] == 0 && k.w[kWordMaterial] == 0 && k.w[kWordTexturing] == 9, "the named words zeroed, the others kept");
		Check(k.w[kWordPass] == 1 && k.w[kWordData] == 10 && k.w[kWordShader] == 3, "the low three bits ignored: pass, mesh, shader stay");
		Check(std::string(KeyWordName(kWordLights)) == "lights" && std::string(KeyWordName(99)) == "?", "names, and out of range");
	}

	Section("LightsWord: all lights, the first only, or none");
	{
		const std::uint32_t a[3] = {0x10, 0x20, 0x30};
		const std::uint32_t b[3] = {0x10, 0x21, 0x30};
		const std::uint32_t c[3] = {0x11, 0x20, 0x30};
		Check(LightsWord(a, 3, 2) != LightsWord(b, 3, 2), "all: a second light apart, another word");
		Check(LightsWord(a, 3, 2) != LightsWord(a, 2, 2), "all: another count, another word");
		Check(LightsWord(a, 3, 1) == LightsWord(b, 3, 1) && LightsWord(a, 3, 1) != LightsWord(c, 3, 1), "the first only: the rest does not count, the first does");
		Check(LightsWord(a, 3, 1) == LightsWord(a, 1, 1), "the first only: the count does not count");
		Check(LightsWord(a, 0, 1) != LightsWord(a, 3, 1), "the first only: none at all is apart from one");
		Check(LightsWord(a, 3, 0) == 0 && LightsWord(c, 1, 0) == 0, "none: always 0");
		const std::uint32_t many[20] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};
		std::uint32_t other[20];
		std::memcpy(other, many, sizeof(many));
		other[17] = 99;
		Check(LightsWord(many, 20, 2) == LightsWord(other, 20, 2), "all: lights beyond sixteen do not count");
	}

	Section("Splits: groups by pass and mesh, and the words that split them");
	{
		static Pending p;
		Key a = KeyOf(10, 1);
		Key b = KeyOf(10, 1);
		b.w[kWordLights] = 5;
		Key c = KeyOf(10, 1);
		c.w[kWordMaterial] = 6;
		c.w[kWordLights] = 5;
		Key d = KeyOf(20, 1);
		p.Add(a, 1, 1);
		p.Add(a, 2, 2);
		p.Add(b, 3, 3);
		p.Add(b, 4, 4);
		p.Add(b, 5, 5);
		p.Add(c, 6, 6);
		p.Add(d, 7, 7);
		Splits s;
		s.Add(p);
		Check(s.records == 7 && s.groups == 4 && s.meshGroups == 2, "seven records, four groups, two meshes");
		Check(s.differs[kWordLights] == 4 && s.differs[kWordMaterial] == 1 && s.differs[kWordData] == 0, "lights split four records off, the material one");
		s.Add(p);
		Check(s.records == 14 && s.meshGroups == 4 && s.differs[kWordLights] == 8, "a second flush adds up (the meshes found anew)");
		char line[800];
		FormatSplits(s, line, sizeof(line));
		Check(std::string(line).find("lights 8") != std::string::npos && std::string(line).find("material 2") != std::string::npos, "the line names the words");
		p.Clear();
	}

	Section("Predictions: learned, kept, and dropped for good when a key draws two ways");
	{
		static Predictions p;
		const PredictKey k{0x100, 0x20, 0x8};
		Check(p.Find(k) == nullptr, "nothing known at first");
		p.Note(k, 0x500, true);
		const Prediction* found = p.Find(k);
		Check(found != nullptr && found->passObject == 0x500 && found->sls && found->seen == 1, "noted: the pass object, SLS2003, seen once");
		Check(p.Predicts(k), "it predicts");
		p.Note(k, 0x500, true);
		Check(p.Find(k)->seen == 2 && p.Predicts(k), "seen again the same way");
		p.Note(k, 0x600, true);
		Check(!p.Predicts(k) && p.Find(k)->unstable, "another pass object for the same key: unstable, never predicted again");
		p.Note(k, 0x500, true);
		Check(!p.Predicts(k), "still not, after the first way again");
		const PredictKey other{0x100, 0x21, 0x8};
		p.Note(other, 0x700, false);
		Check(!p.Predicts(other), "a pass without SLS2003 is known but not predicted");
		p.Forget(other);
		Check(p.Find(other)->unstable, "forgotten: marked unstable");
		const PredictKey third{0x101, 0x21, 0x8};
		p.Note(third, 0x800, true);
		p.Note(third, 0x800, false);
		Check(!p.Predicts(third), "the same pass object with other programs: unstable");
	}

	Section("Predictions: a full table refuses new keys and keeps the old");
	{
		static Predictions p;
		std::uint32_t noted = 0;
		for (std::uint32_t i = 0; i < Predictions::kSlots + 10; ++i) {
			p.Note(PredictKey{i, 1, 0}, 0x10 + i, true);
			noted += p.Find(PredictKey{i, 1, 0}) != nullptr ? 1u : 0u;
		}
		Check(noted == Predictions::kSlots, "as many keys as slots");
		Check(p.Predicts(PredictKey{0, 1, 0}), "the first is still there");
	}

	Section("PackWorld: rotation times scale and translation minus the origin, each once rounded to float (0x00761AE0)");
	{
		float r[9];
		RotationZ(0.7, r);
		const float t[3] = {12345.67f, -9876.5f, 432.1f};
		const float o[3] = {12000.0f, -9000.0f, 400.0f};
		const InstanceRows w = PackWorld(r, t, 1.5f, o);
		bool same = true;
		for (int row = 0; row < 3; ++row) {
			for (int col = 0; col < 3; ++col) {
				const float expected = r[3 * row + col] * 1.5f;
				same = same && w.r[row][col] == expected;
			}
			const float expected = t[row] - o[row];
			same = same && w.r[row][3] == expected;
		}
		Check(same, "each element as the engine's float operation gives it");
	}

	Section("PackInstance and WorldRows: the stream's fourth row and the shadow rows' second form");
	{
		float r[9];
		RotationZ(0.3, r);
		const float t[3] = {-193234.0f, -32705.0f, 245.0f};
		const float o[3] = {-193200.0f, -32700.0f, 240.0f};
		const InstanceData d = PackInstance(r, t, 1.25f, o);
		Check(d.extra[0] == t[0] && d.extra[1] == t[1] && d.extra[2] == t[2] && d.extra[3] == 1.25f, "the absolute translation and the scale");
		const float expected = t[0] - o[0];
		Check(d.world.r[0][3] == expected, "the world rows as PackWorld");
		double rows[4][4];
		WorldRows(r, t, 1.25f, rows);
		Check(rows[0][0] == r[0] && rows[1][2] == r[5] && rows[2][3] == t[2] && rows[3][3] == 1.25 && rows[3][0] == 0.0, "rotation without the scale, the translation, (0, 0, 0, scale)");
		float vs[kVertexRegisters][4] = {};
		for (const int row : kShadowRowsRead) {
			for (int c = 0; c < 4; ++c) {
				vs[kRegisterShadow + row][c] = static_cast<float>(rows[row][c]);
			}
		}
		vs[kRegisterShadow + 2][0] = 1e6f;  // row 2 is not read
		Check(ShadowRowsError(vs, rows) < 1e-7, "registers holding the form: no error (row 2 ignored)");
		vs[kRegisterShadow + 3][3] = 1.0f;
		Check(ShadowRowsError(vs, rows) > 0.1, "another scale in the last row: apart");
	}

	Section("InvertAffine: the inverse of rotation, scale and translation; a singular one refused");
	{
		float r[9];
		RotationZ(1.1, r);
		const float t[3] = {10.0f, -20.0f, 30.0f};
		const float o[3] = {0, 0, 0};
		const InstanceRows w = PackWorld(r, t, 2.0f, o);
		Mat4 inverse;
		Check(InvertAffine(w, &inverse), "invertible");
		const Mat4 product = Product(ToMat4(w), inverse);
		double worst = 0;
		for (int i = 0; i < 4; ++i) {
			for (int j = 0; j < 4; ++j) {
				worst = std::fmax(worst, std::fabs(product.m[i][j] - (i == j ? 1.0 : 0.0)));
			}
		}
		Check(worst < 1e-12, "W * W^-1 is the identity");
		InstanceRows zero{};
		Check(!InvertAffine(zero, &inverse), "a zero scale: refused");
	}

	Section("Derive and ExpectFor: an instance's uploads rebuilt from the leader's");
	{
		const Mat4 vp = SomeViewProj();
		const Mat4 shadow = SomeShadow();
		const double light[3] = {0.3, -0.5, 0.81};
		const float o[3] = {1000.0f, 2000.0f, 50.0f};
		float r1[9];
		float r2[9];
		RotationZ(0.4, r1);
		RotationZ(-2.2, r2);
		const float t1[3] = {1003.5f, 2010.25f, 51.0f};
		const float t2[3] = {990.0f, 1985.5f, 49.0f};
		const InstanceRows leader = PackWorld(r1, t1, 1.0f, o);
		const InstanceRows member = PackWorld(r2, t2, 1.3f, o);
		float regs[kVertexRegisters][4];
		Registers(vp, shadow, light, leader, regs);
		Derived d;
		Check(Derive(regs, leader, &d), "derived from the leader's registers");
		float memberRegs[kVertexRegisters][4];
		Registers(vp, shadow, light, member, memberRegs);
		const Expected e = ExpectFor(d, member);
		double mvp = 0;
		double sh = 0;
		for (int row = 0; row < 4; ++row) {
			mvp = std::fmax(mvp, RowError(memberRegs[kRegisterViewProj + row], e.mvp[row]));
			sh = std::fmax(sh, RowError(memberRegs[kRegisterShadow + row], e.shadow[row]));
		}
		Check(mvp < 1e-6, "the member's ModelViewProj from the leader's within 1e-6 a row");
		Check(sh < 1e-6, "the member's shadow rows likewise");
		Check(DirectionError(memberRegs[kRegisterLight], e.light) < 1e-9, "the member's light direction in its model space, up to its length");
		// The view-projection the instanced draw uploads acts on the instance space.
		const Mat4 world = ToMat4(member);
		const Mat4 again = Product(ToMat4FromRows(d.viewProj), world);
		double worst = 0;
		for (int row = 0; row < 4; ++row) {
			worst = std::fmax(worst, RowError(memberRegs[kRegisterViewProj + row], again.m[row]));
		}
		Check(worst < 1e-6, "ViewProj uploaded times the instance rows is the member's ModelViewProj");
		InstanceRows singular{};
		Check(!Derive(regs, singular, &d), "a singular leader: refused");
	}

	Section("RowError and DirectionError: relative to the row's size, the direction's length free");
	{
		const float actual[4] = {1.0f, 2.0f, 3.0f, 4.0f};
		const double same[4] = {1, 2, 3, 4};
		const double off[4] = {1, 2, 3, 4.004};
		Check(RowError(actual, same) == 0.0, "equal rows: 0");
		Check(std::fabs(RowError(actual, off) - 0.001) < 1e-6, "0.004 off on a row of size 4.004: 1e-3");
		const double zero[4] = {0, 0, 0, 0};
		const float zeros[4] = {0, 0, 0, 0};
		Check(RowError(zeros, zero) == 0.0, "two zero rows: 0");
		const float dir[3] = {2.0f, 0.0f, 0.0f};
		const double longer[3] = {5.0, 0.0, 0.0};
		const double across[3] = {0.0, 1.0, 0.0};
		Check(DirectionError(dir, longer) < 1e-12, "the same direction at another length: 0");
		Check(std::fabs(DirectionError(dir, across) - 1.0) < 1e-12, "a right angle: 1");
		const float none[3] = {0, 0, 0};
		Check(DirectionError(none, longer) == 0.0, "a zero direction against one: 0 (the original's nrm of zero is not defined; not counted)");
	}

	Section("Verify counts: compared, over the tolerance, the largest error");
	{
		VerifyCounts v;
		v.Note(Compared::kMvp, 1e-7);
		v.Note(Compared::kMvp, 5e-4);
		v.Note(Compared::kLight, 0.0);
		Check(v.checked[static_cast<int>(Compared::kMvp)] == 2 && v.over[static_cast<int>(Compared::kMvp)] == 1, "two checked, one over 1e-4");
		Check(v.largest[static_cast<int>(Compared::kMvp)] == 5e-4, "the largest kept");
		Check(v.over[static_cast<int>(Compared::kLight)] == 0, "an equal light not over");
		v.NoteEqual(Compared::kTextures, false);
		v.NoteEqual(Compared::kTextures, true);
		Check(v.checked[static_cast<int>(Compared::kTextures)] == 2 && v.over[static_cast<int>(Compared::kTextures)] == 1, "equality checks: one of two apart");
	}

	Section("Format: the summary line names the waits, the flushes, the instanced draws and the checks");
	{
		Counts c;
		c.frames = 10;
		c.records = 1000;
		c.why[static_cast<int>(Why::kWaits)] = 600;
		c.why[static_cast<int>(Why::kFirstOfList)] = 100;
		c.flushes[static_cast<int>(Flush::kListEnd)] = 50;
		c.groups = 300;
		c.groupsMulti = 100;
		c.instances = 400;
		c.drawnInstanced = 90;
		c.fellBack[static_cast<int>(Fallback::kProgram)] = 10;
		char line[1600];
		Format(c, Mode::kInstanced, true, 2.0, line, sizeof(line));
		const std::string s(line);
		Check(s.find("instanced") != std::string::npos, "the mode named");
		Check(s.find("100.0 records a frame") != std::string::npos && s.find("60.0 waited") != std::string::npos, "records and waits a frame");
		Check(s.find("first of list 10.0") != std::string::npos, "a reason with its rate");
		Check(s.find("90 of 100 groups of two or more drawn instanced") != std::string::npos, "the instanced groups");
		Check(s.find("programs 10") != std::string::npos, "a fallback named");
		Format(c, Mode::kInstanced, false, 2.0, line, sizeof(line));
		Check(std::string(line).find("off in this window") != std::string::npos, "an alternate window off said so");
		VerifyCounts v;
		v.Note(Compared::kMvp, 2e-7);
		FormatVerify(v, line, sizeof(line));
		Check(std::string(line).find("ModelViewProj 1 checked, 0 over") != std::string::npos, "the verify line");
	}

	return Failures();
}
