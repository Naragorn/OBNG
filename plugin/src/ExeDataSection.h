#pragma once
// The .data section of Oblivion.exe 1.2.0.416, read from its section header
// (2026-11-12, docs/subsystems/actor-batch.md): where the EXE's own statics
// lie. Probes copy it before and after a piece of the frame to see which
// statics that piece writes.

#include <cstdint>

namespace obng::plugin::exe_data {

constexpr std::uint32_t kStart = 0x00B02000;
constexpr std::uint32_t kDwords = 0xA9C1C / 4;

}  // namespace obng::plugin::exe_data
