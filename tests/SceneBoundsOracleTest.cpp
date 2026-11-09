// The bounds' arithmetic (SceneUpdateLogic.h, phase 2) against the original's
// own bytes: NiBound::Merge 0x0072A6B0, the bound transform 0x0072A820, the
// transform inverse 0x00718A80 and the skin bound 0x0072BB30 on fake skin
// records, each called directly on random inputs under the game's two
// control words, every result compared bit for bit.

#include "Check.h"
#include "ExeImage.h"
#include "SceneFakes.h"
#include "engine/scene/SceneUpdate.h"
#include "engine/scene/SceneUpdateLogic.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace obng::engine::scene::scene_update;
namespace eng = obng::engine::scene::scene_update::engine;
namespace image = obng::tests::exe_image;

namespace {

using MergeFn = void(__thiscall*)(Bound* self, const Bound* other);
using TransformBoundFn = void(__thiscall*)(Bound* out, const Bound* bound, const Transform* by);
using InverseFn = void(__thiscall*)(const Transform* self, Transform* out);
using SkinBoundFn = void(__thiscall*)(void* skinInstance, Bound* out);

void SetControlWord(std::uint16_t word) { asm volatile("fldcw %0" : : "m"(word)); }
std::uint16_t GetControlWord() {
	std::uint16_t word;
	asm volatile("fnstcw %0" : "=m"(word));
	return word;
}

using obng::tests::scene_fakes::FakeSkin;
using obng::tests::scene_fakes::RandomBound;
using obng::tests::scene_fakes::RandomTransform;
std::mt19937& g_random = obng::tests::scene_fakes::Random();

bool Same(const Bound& a, const Bound& b) { return std::memcmp(&a, &b, sizeof(Bound)) == 0; }
bool Same(const Transform& a, const Transform& b) { return std::memcmp(&a, &b, sizeof(Transform)) == 0; }

}  // namespace

int main() {
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	std::string where;
	if (!image::Map(&where)) {
		std::printf("SKIPPED: Oblivion.exe not available at %s (set OBNG_ORACLE_EXE)\n", where.c_str());
		return 77;
	}
	const unsigned char mergeBytes[] = {0x83, 0xEC, 0x20, 0x56, 0x8B, 0xF1};
	const unsigned char transformBytes[] = {0x83, 0xEC, 0x18, 0x53, 0x8B, 0x5C, 0x24, 0x20};
	const unsigned char inverseBytes[] = {0x83, 0xEC, 0x48, 0x53, 0x55, 0x56, 0x57};
	const unsigned char skinBytes[] = {0x81, 0xEC, 0xBC, 0x00, 0x00, 0x00, 0x53, 0x55, 0x56, 0x57};
	if (!image::BytesAre(0x0072A6B0, mergeBytes, sizeof(mergeBytes)) || !image::BytesAre(0x0072A820, transformBytes, sizeof(transformBytes)) ||
	    !image::BytesAre(0x00718A80, inverseBytes, sizeof(inverseBytes)) || !image::BytesAre(0x0072BB30, skinBytes, sizeof(skinBytes))) {
		std::printf("the bytes are not the functions' - not the reference build\n");
		return 1;
	}
	const auto merge = reinterpret_cast<MergeFn>(0x0072A6B0);
	const auto transformBound = reinterpret_cast<TransformBoundFn>(0x0072A820);
	const auto inverse = reinterpret_cast<InverseFn>(0x00718A80);
	const auto skinBound = reinterpret_cast<SkinBoundFn>(0x0072BB30);
	const std::uint16_t entryWord = GetControlWord();

	for (const std::uint16_t word : {std::uint16_t{0x007F}, std::uint16_t{0x0C7F}}) {
		char title[96];
		std::snprintf(title, sizeof(title), "Under the control word %04X: merge, transform, inverse, skin bound against the original's bytes", word);
		Section(title);
		unsigned mergeDiffers = 0;
		unsigned transformDiffers = 0;
		unsigned inverseDiffers = 0;
		unsigned skinDiffers = 0;
		const unsigned n = 20000;
		for (unsigned i = 0; i < n; ++i) {
			Bound a = RandomBound(true);
			Bound b = RandomBound(true);
			if (i % 7 == 0) {
				b = a;  // equal bounds: the "inside" branches with equality
			}
			if (i % 11 == 0) {
				b.center[0] = a.center[0];
				b.center[1] = a.center[1];
				b.center[2] = a.center[2];  // concentric: the dist <= 1e-6 path
			}
			Bound original = a;
			SetControlWord(word);
			merge(&original, &b);
			SetControlWord(entryWord);
			Bound ours = a;
			SetControlWord(word);
			{
				eng::RoundingFromX87 rounding;  // reads the word just set
				MergeBound(ours, b);
			}
			SetControlWord(entryWord);
			if (!Same(original, ours)) {
				if (mergeDiffers == 0) {
					std::printf("  merge differs first at %u: self (%g %g %g r %g) other (%g %g %g r %g): original r %.9g ours r %.9g c0 %.9g / %.9g\n", i,
					            a.center[0], a.center[1], a.center[2], a.radius, b.center[0], b.center[1], b.center[2], b.radius, original.radius,
					            ours.radius, original.center[0], ours.center[0]);
				}
				++mergeDiffers;
			}

			const Transform t = RandomTransform();
			Bound tOriginal;
			Bound tOurs;
			SetControlWord(word);
			transformBound(&tOriginal, &a, &t);
			SetControlWord(entryWord);
			SetControlWord(word);
			{
				eng::RoundingFromX87 rounding;
				TransformBound(a, t, tOurs);
			}
			SetControlWord(entryWord);
			if (!Same(tOriginal, tOurs)) {
				++transformDiffers;
			}

			Transform iOriginal;
			Transform iOurs;
			SetControlWord(word);
			inverse(&t, &iOriginal);
			SetControlWord(entryWord);
			SetControlWord(word);
			{
				eng::RoundingFromX87 rounding;
				InverseTransform(t, iOurs);
			}
			SetControlWord(entryWord);
			if (!Same(iOriginal, iOurs)) {
				++inverseDiffers;
			}

			if (i % 20 == 0) {
				std::uniform_int_distribution<int> bonesDie(1, 40);
				FakeSkin skin(static_cast<unsigned>(bonesDie(g_random)));
				Bound sOriginal;
				SetControlWord(word);
				skinBound(skin.instance.data(), &sOriginal);
				SetControlWord(entryWord);
				Bound sOurs;
				SetControlWord(word);
				{
					eng::RoundingFromX87 rounding;
					sOurs = SkinBound(skin);
				}
				SetControlWord(entryWord);
				if (!Same(sOriginal, sOurs)) {
					++skinDiffers;
				}
			}
		}
		std::printf("  %u merges, %u transforms, %u inverses, %u skin bounds; differing: %u / %u / %u / %u\n", n, n, n, n / 20, mergeDiffers,
		            transformDiffers, inverseDiffers, skinDiffers);
		Check(mergeDiffers == 0, "NiBound::Merge: every result equal, bit for bit");
		Check(transformDiffers == 0, "the bound transform: every result equal");
		Check(inverseDiffers == 0, "the transform inverse: every result equal");
		Check(skinDiffers == 0, "the skin bound: every result equal");
	}
	std::printf("  errno stub calls %u (the roots' CRT path)\n", image::g_errnoCalls);
	return Failures();
}
