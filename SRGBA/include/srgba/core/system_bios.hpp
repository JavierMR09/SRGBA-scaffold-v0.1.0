#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace srgba::core {

// SRGBA ships a tiny, original replacement system ROM so games can run without a user-supplied
// BIOS dump. It contains only the exception vectors, the standard interrupt dispatcher, and a
// wait loop used by IntrWait/VBlankIntrWait. All other BIOS calls (SWIs) are implemented in C++
// by HleBios. None of this code is derived from Nintendo's BIOS.
namespace builtin_bios {

inline constexpr std::size_t kImageSize = 16U * 1024U;
inline constexpr std::uint32_t kSoftwareInterruptVector = 0x00000008U;
inline constexpr std::uint32_t kIrqVector = 0x00000018U;
inline constexpr std::uint32_t kIntrWaitRoutine = 0x00000038U;
// The opcode the real BIOS leaves in the protection latch after returning from an SWI.
inline constexpr std::uint32_t kSwiReturnLatch = 0xE3A02004U;

[[nodiscard]] std::span<const std::uint32_t> program_words() noexcept;

} // namespace builtin_bios

[[nodiscard]] const std::array<std::uint8_t, builtin_bios::kImageSize>&
builtin_bios_image() noexcept;

} // namespace srgba::core
