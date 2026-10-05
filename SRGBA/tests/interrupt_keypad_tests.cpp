#include "srgba/core/gba_bus.hpp"
#include "srgba/core/interrupts.hpp"

#include <catch2/catch_test_macros.hpp>

using srgba::core::GbaBus;
using srgba::core::Interrupt;
using srgba::core::Key;

TEST_CASE("IF is acknowledged by writing ones and IE masks reserved bits", "[bus][irq]") {
    GbaBus bus;
    bus.request_interrupt(Interrupt::VBlank);
    bus.request_interrupt(Interrupt::Keypad);
    REQUIRE(bus.read16(0x04000202U).value == 0x1001U);
    REQUIRE_FALSE(bus.interrupt_pending());

    static_cast<void>(bus.write16(0x04000200U, 0xFFFFU));
    REQUIRE(bus.interrupt_enable() == 0x3FFFU);
    REQUIRE(bus.interrupt_pending());

    static_cast<void>(bus.write16(0x04000202U, 0x0001U));
    REQUIRE(bus.interrupt_flags() == 0x1000U);
    static_cast<void>(bus.write16(0x04000202U, 0x0000U));
    REQUIRE(bus.interrupt_flags() == 0x1000U);

    static_cast<void>(bus.write32(0x04000208U, 0xFFFFFFFFU));
    REQUIRE(bus.interrupt_master_enable());
    REQUIRE(bus.read16(0x04000208U).value == 0x0001U);
}

TEST_CASE("HALTCNT writes request a CPU halt once", "[bus][irq]") {
    GbaBus bus;
    REQUIRE_FALSE(bus.take_halt_request());
    static_cast<void>(bus.write8(0x04000301U, 0x00U));
    REQUIRE(bus.take_halt_request());
    REQUIRE_FALSE(bus.take_halt_request());
}

TEST_CASE("KEYINPUT is active-low and read-only", "[bus][keypad]") {
    GbaBus bus;
    REQUIRE(bus.read16(0x04000130U).value == 0x03FFU);

    bus.set_pressed_keys(Key::A | Key::Up);
    REQUIRE(bus.read16(0x04000130U).value == (0x03FFU & ~0x0041U));

    static_cast<void>(bus.write16(0x04000130U, 0x0000U));
    REQUIRE(bus.key_input() == (0x03FFU & ~0x0041U));
}

TEST_CASE("KEYCNT raises keypad interrupts in OR and AND modes", "[bus][keypad][irq]") {
    GbaBus bus;
    // OR mode: any of A or B.
    static_cast<void>(bus.write16(0x04000132U, 0x4003U));
    REQUIRE(bus.interrupt_flags() == 0U);
    bus.set_pressed_keys(static_cast<std::uint16_t>(Key::B));
    REQUIRE((bus.interrupt_flags() & 0x1000U) != 0U);

    static_cast<void>(bus.write16(0x04000202U, 0x1000U));
    bus.set_pressed_keys(0);
    // AND mode: both Start and Select.
    static_cast<void>(bus.write16(0x04000132U, 0xC00CU));
    bus.set_pressed_keys(static_cast<std::uint16_t>(Key::Start));
    REQUIRE(bus.interrupt_flags() == 0U);
    bus.set_pressed_keys(Key::Start | Key::Select);
    REQUIRE((bus.interrupt_flags() & 0x1000U) != 0U);
}

TEST_CASE("DISPSTAT status bits and VCOUNT are owned by the PPU", "[bus][video]") {
    GbaBus bus;
    bus.set_display_status_flags(0x05U);
    bus.set_vcount(42);
    static_cast<void>(bus.write16(0x04000004U, 0xFFFFU));
    REQUIRE(bus.read16(0x04000004U).value == 0xFF3DU);
    static_cast<void>(bus.write16(0x04000006U, 0x0000U));
    REQUIRE(bus.read16(0x04000006U).value == 42U);
}

TEST_CASE("The built-in system ROM is readable when no BIOS is loaded", "[bus][bios]") {
    GbaBus bus;
    REQUIRE(bus.using_builtin_bios());
    const srgba::core::BusAccess fetch{srgba::core::AccessSequence::NonSequential,
                                       srgba::core::AccessKind::Instruction, 0x18U};
    REQUIRE(bus.read32(0x00000018U, fetch).value == 0xE92D500FU);
    // Once execution leaves the BIOS, data reads return the last fetched BIOS opcode.
    const srgba::core::BusAccess outside{srgba::core::AccessSequence::NonSequential,
                                         srgba::core::AccessKind::Data, 0x08000000U};
    REQUIRE(bus.read32(0x00000100U, outside).value == 0xE92D500FU);
}
