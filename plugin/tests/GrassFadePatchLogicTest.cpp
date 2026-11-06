// The grass fade fix's decisions (plugin/src/GrassFadePatchLogic.h): which
// byte code is a grass vertex shader, the twin's byte code and what it
// computes, the refusals, and the INI's reach. The shaders here are written
// for the test, token by token, with the structure the game's grass shaders
// have; the game's own are checked by GrassFadePatchPackageTest where the
// shader package is at hand.

#include "Check.h"
#include "GrassFadePatchLogic.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace gf = obng::plugin::grass_fade;
using Code = std::vector<std::uint32_t>;

namespace {

constexpr std::uint32_t kVs20 = 0xFFFE0200u;
constexpr std::uint32_t kVs11 = 0xFFFE0101u;
constexpr std::uint32_t kPs20 = 0xFFFF0200u;
constexpr std::uint32_t kInput = 1;

std::uint32_t Op(std::uint32_t opcode, std::uint32_t parameters) {
	return opcode | (parameters << 24);
}
std::uint32_t Dest(std::uint32_t type, std::uint32_t reg, std::uint32_t mask = 0xF) {
	return 0x80000000u | ((type & 7u) << 28) | ((type & 0x18u) << 8) | (mask << 16) | reg;
}
std::uint32_t Src(std::uint32_t type, std::uint32_t reg, std::uint32_t swizzle = 0xE4) {
	return 0x80000000u | ((type & 7u) << 28) | ((type & 0x18u) << 8) | (swizzle << 16) | reg;
}

// One constant of a constant table.
struct Constant {
	const char* name;
	std::uint16_t set;  // 2 = float
	std::uint16_t index;
	std::uint16_t count;
};
using Table = std::vector<Constant>;

// A comment holding a constant table with the given constants, laid out as
// the D3DX compiler writes one ('CTAB', the header, the records, the names).
void Comment(Code* code, const Table& constants, const char* tag = "CTAB") {
	std::vector<unsigned char> bytes(4 + 28 + constants.size() * gf::kConstantInfoBytes);
	std::memcpy(bytes.data(), tag, 4);
	const std::uint32_t head[7] = {28, 0, 0xFFFE0200u, static_cast<std::uint32_t>(constants.size()), 28, 0, 0};
	std::memcpy(bytes.data() + 4, head, sizeof(head));
	for (std::size_t i = 0; i < constants.size(); ++i) {
		const std::uint32_t nameOffset = static_cast<std::uint32_t>(bytes.size() - 4);
		const std::uint16_t fields[4] = {constants[i].set, constants[i].index, constants[i].count, 0};
		unsigned char* record = bytes.data() + 4 + 28 + i * gf::kConstantInfoBytes;
		std::memcpy(record, &nameOffset, 4);
		std::memcpy(record + 4, fields, sizeof(fields));
		const std::size_t n = std::strlen(constants[i].name) + 1;
		bytes.insert(bytes.end(), constants[i].name, constants[i].name + n);
	}
	while (bytes.size() % 4 != 0) {
		bytes.push_back(0);
	}
	code->push_back(gf::kOpComment | (static_cast<std::uint32_t>(bytes.size() / 4) << 16));
	const std::size_t at = code->size();
	code->resize(at + bytes.size() / 4);
	std::memcpy(code->data() + at, bytes.data(), bytes.size());
}

// The game's grass table as far as it matters: the four names, ModelViewProj at c9.
const Table kGrassTable = {{"AlphaParam", 2, 5, 1}, {"DiffuseDir", 2, 0, 1}, {"InstanceData", 2, 20, 2},
                           {"ModelViewProj", 2, 9, 4}, {"WindData", 2, 4, 1}, {"FogParam", 2, 15, 1}};

// The grass shader's skeleton: the clip position in r0 from the table's
// ModelViewProj rows, its length into r2.w, r0 to oPos. Returns the index of
// the dp4's opcode token.
std::size_t Grass(Code* code, const Table& table = kGrassTable, std::uint32_t version = kVs20, std::uint32_t extraTemp = 5,
                  std::uint32_t rowRegister = 9) {
	code->clear();
	code->push_back(version);
	Comment(code, table);
	code->push_back(Op(gf::kOpDef, 5));
	code->push_back(Dest(gf::kTypeConst, 3));
	const float literals[4] = {-0.5f, 0.01f, 1.0f, -2.0f};  // -2.0f has bit 31 set, as a register token has
	const std::size_t at = code->size();
	code->resize(at + 4);
	std::memcpy(code->data() + at, literals, sizeof(literals));
	code->push_back(Op(gf::kOpDcl, 2));
	code->push_back(0x80000000u);  // usage: position 0
	code->push_back(Dest(kInput, 0));
	for (std::uint32_t row = 0; row < 4; ++row) {
		code->push_back(Op(gf::kOpDp4, 3));
		code->push_back(Dest(gf::kTypeTemp, 0, 1u << row));
		code->push_back(Src(gf::kTypeConst, rowRegister + row));
		code->push_back(Src(kInput, 0));
	}
	code->push_back(Op(gf::kOpMov, 2));
	code->push_back(Dest(gf::kTypeTemp, extraTemp));
	code->push_back(Src(gf::kTypeConst, 5));
	const std::size_t length = code->size();
	code->push_back(Op(gf::kOpDp4, 3));
	code->push_back(Dest(gf::kTypeTemp, 2, 8));
	code->push_back(Src(gf::kTypeTemp, 0));
	code->push_back(Src(gf::kTypeTemp, 0));
	code->push_back(Op(gf::kOpMov, 2));
	code->push_back(Dest(gf::kTypeRastOut, 0));
	code->push_back(Src(gf::kTypeTemp, 0));
	code->push_back(gf::kEndToken);
	return length;
}

// A machine for the handful of instructions the skeleton and the patch use,
// so a twin can be run: what its length register holds is the test.
struct Machine {
	float r[12][4] = {};
	float c[32][4] = {};
	float v[4][4] = {};
	float position[4] = {};
	bool unknown = false;

	const float* Register(std::uint32_t token) {
		const std::uint32_t n = gf::RegisterNumber(token);
		switch (gf::RegisterType(token)) {
		case gf::kTypeTemp: return r[n];
		case kInput: return v[n];
		case gf::kTypeConst: return c[n];
		default: unknown = true; return r[0];
		}
	}
	void Read(std::uint32_t token, float out[4]) {
		const float* reg = Register(token);
		for (int i = 0; i < 4; ++i) {
			out[i] = reg[(token >> (16 + 2 * i)) & 3u];
		}
		unknown = unknown || ((token >> 24) & 0xFu) != 0;  // no source modifiers here
	}
	void Write(std::uint32_t token, const float value[4]) {
		float* reg = gf::RegisterType(token) == gf::kTypeRastOut ? position : (gf::RegisterType(token) == gf::kTypeTemp ? r[gf::RegisterNumber(token)] : nullptr);
		if (reg == nullptr) {
			unknown = true;
			return;
		}
		for (int i = 0; i < 4; ++i) {
			if ((token >> (16 + i) & 1u) != 0) {
				reg[i] = value[i];
			}
		}
	}
	void Run(const Code& code) {
		gf::ForInstructions(code.data(), code.size(), [&](const gf::Instruction& in) {
			const std::uint32_t* t = code.data() + in.at;
			float a[4];
			float b[4];
			float out[4];
			switch (in.opcode) {
			case gf::kOpComment:
			case gf::kOpDcl:
				break;
			case gf::kOpDef:
				std::memcpy(c[gf::RegisterNumber(t[1])], t + 2, 4 * sizeof(float));
				break;
			case gf::kOpMov:
				Read(t[2], a);
				Write(t[1], a);
				break;
			case gf::kOpAdd:
			case gf::kOpMul:
				Read(t[2], a);
				Read(t[3], b);
				for (int i = 0; i < 4; ++i) {
					out[i] = in.opcode == gf::kOpAdd ? a[i] + b[i] : a[i] * b[i];
				}
				Write(t[1], out);
				break;
			case gf::kOpDp3:
			case gf::kOpDp4: {
				Read(t[2], a);
				Read(t[3], b);
				float sum = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
				if (in.opcode == gf::kOpDp4) {
					sum += a[3] * b[3];
				}
				out[0] = out[1] = out[2] = out[3] = sum;
				Write(t[1], out);
				break;
			}
			case gf::kOpRsq:
				Read(t[2], a);
				out[0] = out[1] = out[2] = out[3] = 1.0f / std::sqrt(a[0]);
				Write(t[1], out);
				break;
			default:
				unknown = true;
			}
		});
	}
};

bool Near(float a, float b, float relative = 1e-5f) {
	return std::fabs(a - b) <= relative * (std::fabs(b) > 1.0f ? std::fabs(b) : 1.0f);
}

void TestTokens() {
	Section("The tokens: register number and type, the versions, the patch's own");
	Check(gf::RegisterNumber(Src(gf::kTypeConst, 245)) == 245 && gf::RegisterType(Src(gf::kTypeConst, 245)) == gf::kTypeConst,
	      "a constant register");
	Check(gf::RegisterType(Dest(gf::kTypeRastOut, 0)) == gf::kTypeRastOut && gf::RegisterType(Dest(gf::kTypeTemp, 11)) == gf::kTypeTemp,
	      "the position output and a temp register");
	Check(gf::RegisterType(Src(10, 3)) == 10, "a type with the high bits (bits 11 and 12)");
	Check(gf::IsVertexShader2OrLater(kVs20) && gf::IsVertexShader2OrLater(0xFFFE0300u) && !gf::IsVertexShader2OrLater(kVs11) &&
	          !gf::IsVertexShader2OrLater(kPs20) && !gf::IsVertexShader2OrLater(0),
	      "vertex shaders 2.0 and 3.0 yes; 1.1, a pixel shader and nothing no");
	Check(gf::OpToken(gf::kOpMul, 3) == Op(gf::kOpMul, 3) && gf::TempDest(6, 3) == Dest(gf::kTypeTemp, 6, 3) &&
	          gf::TempSource(6) == Src(gf::kTypeTemp, 6) && gf::ConstSource(9) == Src(gf::kTypeConst, 9),
	      "the patch's tokens are the test's");
	Check(gf::Replicate(0) == 0x00 && gf::Replicate(1) == 0x55 && gf::Replicate(2) == 0xAA && gf::Replicate(3) == 0xFF, "the replicate swizzles");
}

void TestPatched() {
	Section("A grass vertex shader: the twin's byte code");
	Code code;
	const std::size_t at = Grass(&code);
	Code twin;
	const gf::Patch patch = gf::PatchGrassFade(code.data(), code.size(), gf::Reach::kGame, &twin);
	Check(patch.verdict == gf::Verdict::kPatched && patch.clipRegister == 0 && patch.tempRegister == 6 && patch.rowRegister == 9 && patch.at == at,
	      "patched: the clip register r0, the temps r6 and r7 (r5 is the highest named), the rows from c9, at the dp4");
	Check(patch.added == 31 && twin.size() == code.size() + 31, "31 tokens more for the game's reach");
	Check(std::equal(code.begin(), code.begin() + static_cast<std::ptrdiff_t>(at), twin.begin()), "everything before the dp4 as it was");
	const std::uint32_t expected[] = {
	    Op(gf::kOpMov, 2), Dest(gf::kTypeTemp, 6, 7), Src(gf::kTypeConst, 9),
	    Op(gf::kOpDp3, 3), Dest(gf::kTypeTemp, 6, 1), Src(gf::kTypeTemp, 6), Src(gf::kTypeTemp, 6),
	    Op(gf::kOpMov, 2), Dest(gf::kTypeTemp, 7, 7), Src(gf::kTypeConst, 10),
	    Op(gf::kOpDp3, 3), Dest(gf::kTypeTemp, 6, 2), Src(gf::kTypeTemp, 7), Src(gf::kTypeTemp, 7),
	    Op(gf::kOpRsq, 2), Dest(gf::kTypeTemp, 6, 1), Src(gf::kTypeTemp, 6, 0x00),
	    Op(gf::kOpRsq, 2), Dest(gf::kTypeTemp, 6, 2), Src(gf::kTypeTemp, 6, 0x55),
	    Op(gf::kOpMul, 3), Dest(gf::kTypeTemp, 6, 3), Src(gf::kTypeTemp, 0), Src(gf::kTypeTemp, 6),
	    Op(gf::kOpMov, 2), Dest(gf::kTypeTemp, 6, 4), Src(gf::kTypeTemp, 0, 0xFF),
	    Op(gf::kOpDp3, 3), Dest(gf::kTypeTemp, 2, 8), Src(gf::kTypeTemp, 6), Src(gf::kTypeTemp, 6),
	    Op(gf::kOpAdd, 3), Dest(gf::kTypeTemp, 2, 8), Src(gf::kTypeTemp, 2, 0xFF), Src(gf::kTypeTemp, 2, 0xFF),
	};
	Check(std::equal(std::begin(expected), std::end(expected), twin.begin() + static_cast<std::ptrdiff_t>(at)),
	      "in the dp4's place: the two rows' lengths, their inverses, the view-space position, its squared length, doubled");
	Check(std::equal(code.begin() + static_cast<std::ptrdiff_t>(at) + 4, code.end(), twin.begin() + static_cast<std::ptrdiff_t>(at) + 35) &&
	          twin.back() == gf::kEndToken,
	      "everything after it as it was, the end token last");

	Section("The INI's reach: the same without the doubling");
	Code ini;
	const gf::Patch iniPatch = gf::PatchGrassFade(code.data(), code.size(), gf::Reach::kIni, &ini);
	Check(iniPatch.verdict == gf::Verdict::kPatched && iniPatch.added == 27 && ini.size() == code.size() + 27, "27 tokens more");
	Check(std::equal(twin.begin(), twin.begin() + static_cast<std::ptrdiff_t>(at) + 31, ini.begin()) &&
	          std::equal(twin.begin() + static_cast<std::ptrdiff_t>(at) + 35, twin.end(), ini.begin() + static_cast<std::ptrdiff_t>(at) + 31),
	      "the game's twin without its add");

	Section("A twin is not patched a second time; the verdict without an output; tokens after the end are not copied");
	Code again;
	Check(gf::PatchGrassFade(twin.data(), twin.size(), gf::Reach::kGame, &again).verdict == gf::Verdict::kNoClipLength,
	      "a twin has no dp4 of a register with itself any more");
	Check(gf::PatchGrassFade(code.data(), code.size(), gf::Reach::kGame, nullptr).verdict == gf::Verdict::kPatched, "no output asked for");
	Code padded = code;
	padded.push_back(0x12345678u);
	Check(gf::PatchGrassFade(padded.data(), padded.size(), gf::Reach::kGame, &again).verdict == gf::Verdict::kPatched &&
	          again.size() == code.size() + 31 && again.back() == gf::kEndToken,
	      "the twin ends at the end token");

	Section("Other registers: the rows where the table has ModelViewProj, the temps above the highest named, the length's own component");
	Table table = kGrassTable;
	for (Constant& c : table) {
		if (std::strcmp(c.name, "ModelViewProj") == 0) {
			c.index = 0;
		}
	}
	Grass(&code, table, kVs20, 9, 0);
	gf::Patch other = gf::PatchGrassFade(code.data(), code.size(), gf::Reach::kGame, &twin);
	Check(other.verdict == gf::Verdict::kPatched && other.rowRegister == 0 && other.tempRegister == 10, "rows from c0, temps r10 and r11");
	std::size_t lengthAt = Grass(&code);
	code[lengthAt + 1] = Dest(gf::kTypeTemp, 3, 1);  // the length into r3.x
	other = gf::PatchGrassFade(code.data(), code.size(), gf::Reach::kGame, &twin);
	Check(other.verdict == gf::Verdict::kPatched && twin[lengthAt + 28] == Dest(gf::kTypeTemp, 3, 1) && twin[lengthAt + 32] == Dest(gf::kTypeTemp, 3, 1) &&
	          twin[lengthAt + 33] == Src(gf::kTypeTemp, 3, 0x00) && twin[lengthAt + 34] == Src(gf::kTypeTemp, 3, 0x00),
	      "the dp3 and the add write r3.x, the add reads r3.x");
}

// The twin run: its length register against the distance.
void TestComputes() {
	Section("What the twin computes: twice the squared distance to the camera, wherever in the picture");
	// Model = view; a projection with half-width tangent 1.4 and half-height 1.1, near 4, the far plane far away.
	const float p11 = 1.0f / 1.4f;
	const float p22 = 1.0f / 1.1f;
	const float rows[4][4] = {{p11, 0, 0, 0}, {0, p22, 0, 0}, {0, 0, 1, -4}, {0, 0, 1, 0}};
	Code code;
	Grass(&code);
	Code game;
	Code ini;
	gf::PatchGrassFade(code.data(), code.size(), gf::Reach::kGame, &game);
	gf::PatchGrassFade(code.data(), code.size(), gf::Reach::kIni, &ini);
	const auto run = [&rows](const Code& shader, float x, float y, float z, bool* unknown) {
		Machine m;
		std::memcpy(m.c[9], rows, sizeof(rows));
		m.v[0][0] = x;
		m.v[0][1] = y;
		m.v[0][2] = z;
		m.v[0][3] = 1.0f;
		m.Run(shader);
		*unknown = m.unknown;
		return m.r[2][3];
	};
	bool unknown = false;
	const float z = 3000.0f;
	const float middleOriginal = run(code, 0, 0, z, &unknown);
	Check(!unknown && Near(middleOriginal, (z - 4) * (z - 4) + z * z), "the game's shader in the middle: clip z squared plus clip w squared");
	const float middleGame = run(game, 0, 0, z, &unknown);
	Check(!unknown && Near(middleGame, 2 * z * z), "the twin in the middle: twice the squared distance");
	Check(Near(middleGame, middleOriginal, 3e-3f), "which is the game's value there but for the near plane");
	const float edgeOriginal = run(code, 1.4f * z, 0, z, &unknown);
	const float edgeDistance2 = (1.0f + 1.4f * 1.4f) * z * z;
	Check(!unknown && Near(edgeOriginal, 3 * z * z, 3e-3f) && edgeOriginal < 1.02f * edgeDistance2,
	      "the game's shader at the picture's edge: three depths squared, about the squared distance only");
	const float edgeGame = run(game, 1.4f * z, 0, z, &unknown);
	Check(!unknown && Near(edgeGame, 2 * edgeDistance2), "the twin at the edge: twice the squared distance, as in the middle");
	const float cornerGame = run(game, -0.9f * z, 0.7f * z, z, &unknown);
	Check(!unknown && Near(cornerGame, 2 * (0.81f + 0.49f + 1.0f) * z * z), "the twin anywhere: twice the squared distance");
	Check(Near(run(ini, 1.4f * z, 0, z, &unknown), edgeDistance2) && !unknown, "the INI's reach: the squared distance itself");

	Section("The twin leaves the position and every other register of the shader as the game's shader has them");
	Machine a;
	Machine b;
	std::memcpy(a.c[9], rows, sizeof(rows));
	std::memcpy(b.c[9], rows, sizeof(rows));
	a.v[0][0] = b.v[0][0] = 500.0f;
	a.v[0][1] = b.v[0][1] = -200.0f;
	a.v[0][2] = b.v[0][2] = 2500.0f;
	a.v[0][3] = b.v[0][3] = 1.0f;
	a.Run(code);
	b.Run(game);
	bool same = std::memcmp(a.position, b.position, sizeof(a.position)) == 0;
	for (int reg = 0; reg < 6; ++reg) {
		for (int i = 0; i < 4; ++i) {
			same = same && (reg == 2 && i == 3 ? true : a.r[reg][i] == b.r[reg][i]);
		}
	}
	Check(same && !a.unknown && !b.unknown, "oPos and r0..r5 equal but for the length itself");
}

void TestRefusals() {
	Section("The refusals, each with its reason");
	Code code;
	Code twin;
	const auto verdict = [&code, &twin]() { return gf::PatchGrassFade(code.data(), code.size(), gf::Reach::kGame, &twin).verdict; };
	Check(gf::PatchGrassFade(nullptr, 0, gf::Reach::kGame, &twin).verdict == gf::Verdict::kNotVertexShader2, "no byte code");
	Grass(&code, kGrassTable, kVs11);
	Check(verdict() == gf::Verdict::kNotVertexShader2, "a 1.1 shader");
	Grass(&code, kGrassTable, kPs20);
	Check(verdict() == gf::Verdict::kNotVertexShader2, "a pixel shader");

	Grass(&code, Table{{"ModelViewProj", 2, 0, 4}, {"LightDirection", 2, 4, 1}});
	Check(verdict() == gf::Verdict::kNotGrass, "another shader's table");
	Grass(&code, Table{{"AlphaParam", 2, 5, 1}, {"ModelViewProj", 2, 9, 4}, {"WindData", 2, 4, 1}});
	Check(verdict() == gf::Verdict::kNotGrass, "one of the four names missing");
	Grass(&code, Table{{"AlphaParam", 2, 5, 1}, {"EyePos", 2, 13, 1}, {"InstanceData", 2, 20, 2}, {"ModelViewProj", 2, 9, 4}});
	Check(verdict() == gf::Verdict::kNotGrass, "the distant trees' table (no wind) is not the grass's");
	Grass(&code, Table{{"AlphaParams", 2, 5, 1}, {"InstanceData", 2, 20, 2}, {"ModelViewProj", 2, 9, 4}, {"WindData", 2, 4, 1}});
	Check(verdict() == gf::Verdict::kNotGrass, "a longer name is another name");
	Grass(&code, Table{{"AlphaParam", 2, 5, 1}, {"InstanceData", 2, 20, 2}, {"ModelViewProj", 2, 9, 3}, {"WindData", 2, 4, 1}});
	Check(verdict() == gf::Verdict::kNotGrass, "a ModelViewProj of three registers has no row for the depth: not the grass's");
	Grass(&code, Table{{"AlphaParam", 2, 5, 1}, {"InstanceData", 2, 20, 2}, {"ModelViewProj", 3, 9, 4}, {"WindData", 2, 4, 1}});
	Check(verdict() == gf::Verdict::kNotGrass, "a ModelViewProj that is no float constant");
	{
		Code rest;
		Grass(&rest);
		code.clear();
		code.push_back(kVs20);
		Comment(&code, kGrassTable, "XTAB");  // the names in a comment that is no constant table
		const std::size_t skip = 1 + 1 + ((rest[1] >> 16) & 0x7FFFu);
		code.insert(code.end(), rest.begin() + static_cast<std::ptrdiff_t>(skip), rest.end());
		Check(verdict() == gf::Verdict::kNotGrass, "the names in a comment that is no constant table");
	}

	std::size_t at = Grass(&code);
	Code cut(code.begin(), code.end() - 1);
	Check(gf::PatchGrassFade(cut.data(), cut.size(), gf::Reach::kGame, &twin).verdict == gf::Verdict::kMalformed, "no end token");
	cut.assign(code.begin(), code.begin() + static_cast<std::ptrdiff_t>(at) + 2);
	Check(gf::PatchGrassFade(cut.data(), cut.size(), gf::Reach::kGame, &twin).verdict == gf::Verdict::kMalformed, "cut inside an instruction");

	at = Grass(&code);
	code[at + 3] = Src(gf::kTypeTemp, 1);  // dp4 r2.w, r0, r1: not a length
	Check(verdict() == gf::Verdict::kNoClipLength, "no dp4 of a register with itself");
	at = Grass(&code);
	code[at + 2] = Src(gf::kTypeTemp, 0, 0x00);  // .xxxx
	code[at + 3] = Src(gf::kTypeTemp, 0, 0x00);
	Check(verdict() == gf::Verdict::kNoClipLength, "a swizzled dp4 is not the length");
	at = Grass(&code);
	code[at + 2] = Src(gf::kTypeTemp, 0) | (1u << 24);  // a negated source
	code[at + 3] = code[at + 2];
	Check(verdict() == gf::Verdict::kNoClipLength, "a modified source is not the length");
	at = Grass(&code);
	code[at + 2] = Src(gf::kTypeTemp, 5);  // the length of r5, which does not go to oPos
	code[at + 3] = Src(gf::kTypeTemp, 5);
	Check(verdict() == gf::Verdict::kNoClipLength, "the length of a register that is not the position");
	at = Grass(&code);
	code[at + 1] = Dest(gf::kTypeRastOut, 1);  // the result to an output register
	Check(verdict() == gf::Verdict::kNoClipLength, "the length written to an output");
	at = Grass(&code);
	code[at + 1] = Dest(gf::kTypeTemp, 2, 0xC);  // the result into two components
	Check(verdict() == gf::Verdict::kNoClipLength, "a length written into more than one component is not taken");

	at = Grass(&code);
	code.insert(code.begin() + static_cast<std::ptrdiff_t>(at), {Op(gf::kOpDp4, 3), Dest(gf::kTypeTemp, 3, 1), Src(gf::kTypeTemp, 0), Src(gf::kTypeTemp, 0)});
	Check(verdict() == gf::Verdict::kAmbiguous, "two lengths");

	Grass(&code, kGrassTable, kVs20, 10);
	Check(verdict() == gf::Verdict::kNoFreeTemp, "r10 the highest named: one temp left, two are needed");
	Grass(&code, kGrassTable, kVs20, 11);
	Check(verdict() == gf::Verdict::kNoFreeTemp, "r11 the highest named: none left");
	Grass(&code, kGrassTable, kVs20, 9);
	Check(gf::PatchGrassFade(code.data(), code.size(), gf::Reach::kGame, &twin).tempRegister == 10, "r9 the highest: r10 and r11 are taken");

	Section("A float of a definition is not taken for a register");
	Grass(&code);
	// The definition's literal -2.0f (0xC0000000) reads as a register token; it must not count as a temp.
	Check(gf::PatchGrassFade(code.data(), code.size(), gf::Reach::kGame, &twin).tempRegister == 6, "the highest temp is the code's, not a literal's");

	Section("Every verdict has its words");
	Check(std::string(gf::VerdictName(gf::Verdict::kPatched)) == "patched" &&
	          std::string(gf::VerdictName(gf::Verdict::kNotVertexShader2)) == "not a vertex shader of version 2.0 or later" &&
	          std::string(gf::VerdictName(gf::Verdict::kMalformed)) == "byte code that ends inside an instruction" &&
	          std::string(gf::VerdictName(gf::Verdict::kNotGrass)) == "no grass constant table" &&
	          std::string(gf::VerdictName(gf::Verdict::kNoClipLength)) == "no length of the clip position found" &&
	          std::string(gf::VerdictName(gf::Verdict::kAmbiguous)) == "more than one length of a temp register" &&
	          std::string(gf::VerdictName(gf::Verdict::kNoFreeTemp)) == "fewer than two free temp registers",
	      "the seven names");
}

void TestReach() {
	Section("The INI's reach");
	bool known = false;
	Check(gf::ReachFromIni("game", &known) == gf::Reach::kGame && known, "game");
	Check(gf::ReachFromIni("ini", &known) == gf::Reach::kIni && known, "ini");
	Check(gf::ReachFromIni("far", &known) == gf::Reach::kGame && !known, "anything else: game, and said unknown");
	Check(gf::ReachFromIni("", &known) == gf::Reach::kGame && !known, "nothing: game, unknown");
	Check(std::string(gf::ReachName(gf::Reach::kGame)) == "game" && std::string(gf::ReachName(gf::Reach::kIni)) == "ini", "the two names");
}

void TestWalkAndTable() {
	Section("ForInstructions: comments by their own length, the count of instructions");
	Code code;
	Grass(&code);
	int instructions = 0;
	int comments = 0;
	Check(gf::ForInstructions(code.data(), code.size(), [&](const gf::Instruction& in) {
		      ++instructions;
		      comments += in.opcode == gf::kOpComment ? 1 : 0;
	      }),
	      "walks to the end token");
	Check(instructions == 10 && comments == 1, "a comment, a definition, a declaration, four rows, a move, the length, the position");

	Section("ReadConstantTable: the names, ModelViewProj's register; nothing out of bounds");
	Code comment;
	Comment(&comment, kGrassTable);
	const gf::ConstantTable table = gf::ReadConstantTable(comment.data() + 1, comment.size() - 1);
	Check(table.found && table.IsGrass() && table.modelViewProjRegister == 9, "the grass table");
	Check(!gf::ReadConstantTable(comment.data() + 1, 3).found, "a comment too short for a header");
	Code broken = comment;
	broken[1 + 1 + 3] = 5000;  // the count of constants, beyond the comment
	Check(!gf::ReadConstantTable(broken.data() + 1, broken.size() - 1).found, "more constants than the comment holds");
	broken = comment;
	broken[1 + 1 + 4] = 100000;  // the records' offset, beyond the comment
	Check(!gf::ReadConstantTable(broken.data() + 1, broken.size() - 1).found, "the records beyond the comment");
	broken = comment;
	broken[1 + 1 + 7] = 100000;  // the first record's name offset, beyond the comment
	Check(!gf::ReadConstantTable(broken.data() + 1, broken.size() - 1).found, "a name beyond the comment");
	Code empty;
	Comment(&empty, Table{});
	Check(gf::ReadConstantTable(empty.data() + 1, empty.size() - 1).found && !gf::ReadConstantTable(empty.data() + 1, empty.size() - 1).IsGrass(),
	      "an empty table is a table, and not the grass's");
}

void TestBindCache() {
	Section("BindCache: what was put is found for its generation, and for no other");
	static gf::BindCache cache;
	std::uintptr_t twin = 99;
	Check(!cache.Find(0x1000, 1, &twin) && twin == 99, "an empty table knows nothing and leaves the output");
	cache.Put(0x1000, 0x2000, 1);
	cache.Put(0x1010, 0, 1);
	Check(cache.Find(0x1000, 1, &twin) && twin == 0x2000, "a shader with a twin");
	Check(cache.Find(0x1010, 1, &twin) && twin == 0, "a shader known to have none");
	Check(!cache.Find(0x1000, 2, &twin), "another generation: forgotten");
	Check(!cache.Find(0x1020, 1, &twin), "a shader never put");

	Section("BindCache: two shaders in one slot - the later one stays, the earlier is asked for again");
	const std::uintptr_t a = 0x40000;
	std::uintptr_t b = a + 16;
	while (gf::BindCache::Slot(b) != gf::BindCache::Slot(a)) {
		b += 16;
	}
	cache.Put(a, 1, 3);
	cache.Put(b, 2, 3);
	Check(!cache.Find(a, 3, &twin) && cache.Find(b, 3, &twin) && twin == 2, "the slot holds the later shader");
	Check(gf::BindCache::Slot(0x1000) != gf::BindCache::Slot(0x1010) && gf::BindCache::Slot(~std::uintptr_t{0}) < gf::BindCache::kEntries,
	      "neighbouring blocks take different slots; every slot is in the table");
}

}  // namespace

int main() {
	TestBindCache();
	TestTokens();
	TestPatched();
	TestComputes();
	TestRefusals();
	TestReach();
	TestWalkAndTable();
	return Failures();
}
