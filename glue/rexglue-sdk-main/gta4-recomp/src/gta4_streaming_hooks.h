#pragma once
#include <cstdint>

namespace gta4::streaming {
// Called at the verified stream.ini parser boundary, before the original setters.
void Initialize(uint8_t* base);
void FlushTrace();
// Drains the bounded trace before window-close hard exit or orderly shutdown.
void FinishTrace();
}  // namespace gta4::streaming
