#include "StereoDrawTimer.h"

#include <windows.h>

namespace obng::plugin::draw_timer {

namespace {

constexpr std::uint32_t kPropertyStateSlots = 0x08;     // NiPropertyState: ten property pointers after the reference count (xOBSE NiNodes.h, size 0x30)
constexpr std::uint32_t kMaterialValues = 0x1C;         // NiMaterialProperty: ambient .. alpha, 14 floats to +0x53 (xOBSE NiProperties.h)
constexpr std::uint32_t kMaterialWords = 14;
constexpr std::uint32_t kShaderFlags = 0x1C;            // BSShaderProperty passInfo / flags (xOBSE; knowledge/shadow-light-list-remove.md)
constexpr std::uint32_t kShaderAlpha = 0x20;            // BSShaderProperty alpha (xOBSE)
constexpr std::uint32_t kLightsHead = 0x70;             // BSShaderLightingProperty lights: NiTPointerList at +0x6C, head +0x70 (xOBSE)
constexpr std::uint32_t kLightsCount = 0x78;            // its count, 16 bits read (knowledge/shadow-light-list-remove.md, FUN_007ED5C0)
constexpr std::uint32_t kNodeNext = 0x00;               // a list node: next, previous, element (Gamebryo's NiTListItem; unverified here)
constexpr std::uint32_t kNodeElement = 0x08;
constexpr std::uint32_t kMaxLights = 16;
constexpr std::uint32_t kPassProgramA = 0x44;           // the pass's two programs (re/functions/0x0077A1F0.md step 3)
constexpr std::uint32_t kPassProgramB = 0x58;
constexpr std::uint32_t kLightingPropertyVtables[] = {0x00A92524, 0x00A91384};  // BSShaderLightingProperty, BSShaderPPLightingProperty (knowledge/shadow-light-list-remove.md)

std::uint32_t At(std::uint32_t address) { return *reinterpret_cast<const volatile std::uint32_t*>(static_cast<std::uintptr_t>(address)); }

bool IsLightingProperty(std::uint32_t vtable) {
	for (std::uint32_t v : kLightingPropertyVtables) {
		if (v == vtable) {
			return true;
		}
	}
	return false;
}

}  // namespace

// Under SEH: a layout read wrongly must not end the game - the count is a
// measurement. No object with a destructor lives in this function.
bool ReadInstanceWords(std::uint32_t propertyState, std::uint32_t pass, std::uint32_t* words) {
	__try {
		for (std::uint32_t i = 0; i < 10; ++i) {
			words[i] = propertyState != 0 ? At(propertyState + kPropertyStateSlots + 4 * i) : 0;
		}
		const std::uint32_t shaderProperty = words[4];
		words[10] = shaderProperty != 0 ? At(shaderProperty) : 0;
		const std::uint32_t material = words[2];
		if (material != 0) {
			std::uint32_t values[kMaterialWords];
			for (std::uint32_t i = 0; i < kMaterialWords; ++i) {
				values[i] = At(material + kMaterialValues + 4 * i);
			}
			words[instance_groups::kWordMaterial] = instance_groups::HashWords(values, kMaterialWords);
		}
		if (shaderProperty != 0) {
			words[instance_groups::kWordShaderFlags] = At(shaderProperty + kShaderFlags);
			words[instance_groups::kWordShaderAlpha] = At(shaderProperty + kShaderAlpha);
			if (IsLightingProperty(words[10])) {
				const std::uint32_t count = At(shaderProperty + kLightsCount) & 0xFFFFu;
				std::uint32_t sum = 0;
				std::uint32_t mix = 0;
				std::uint32_t node = At(shaderProperty + kLightsHead);
				for (std::uint32_t i = 0; i < count && i < kMaxLights && node != 0; ++i) {
					const std::uint32_t light = At(node + kNodeElement);
					sum += light;
					mix ^= light * 2654435761u;
					node = At(node + kNodeNext);
				}
				words[instance_groups::kWordLights] = sum ^ (mix << 1);
				words[instance_groups::kWordLightCount] = count;
			}
		}
		words[instance_groups::kWordPrograms] = pass != 0 ? At(pass + kPassProgramA) ^ (At(pass + kPassProgramB) * 31u) : 0;
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

}  // namespace obng::plugin::draw_timer
