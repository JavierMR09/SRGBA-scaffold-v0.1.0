#pragma once

#include <cstdint>

namespace srgba::core {

class Arm7Tdmi;
class GbaBus;

// High-level emulation of the GBA BIOS software-interrupt services. Active only when the built-in
// system ROM is in use (no user BIOS loaded). Behavior follows the public GBATEK description of
// each call; no BIOS code is reproduced.
class HleBios {
  public:
    // Executes the SWI that brought the CPU to the SWI vector. Returns the cycles consumed.
    // Most calls return to the caller immediately; IntrWait and VBlankIntrWait continue in the
    // built-in wait routine so that interrupts are serviced exactly as on hardware.
    [[nodiscard]] static std::uint32_t handle_swi(Arm7Tdmi& cpu, GbaBus& bus) noexcept;

    // Reads the SWI comment number for the exception currently being serviced.
    [[nodiscard]] static std::uint8_t pending_swi_number(const Arm7Tdmi& cpu, GbaBus& bus) noexcept;
};

} // namespace srgba::core
