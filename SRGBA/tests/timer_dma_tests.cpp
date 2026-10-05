#include "test_helpers.hpp"

#include "srgba/core/dma.hpp"
#include "srgba/core/emulator.hpp"
#include "srgba/core/gba_bus.hpp"
#include "srgba/core/scheduler.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>

namespace {

using srgba::core::DmaTiming;
using srgba::core::EventType;
using srgba::core::GbaBus;
using srgba::core::Scheduler;

// Advances a standalone bus's clock, delivering timer overflows as the emulator would.
void advance(GbaBus& bus, const std::uint64_t cycles) {
    auto& scheduler = bus.scheduler();
    const auto target = scheduler.now() + cycles;
    while (scheduler.next_deadline() <= target) {
        static_cast<void>(scheduler.skip_to_next_event());
        while (const auto event = scheduler.pop_due()) {
            const auto index = static_cast<std::size_t>(event->type) -
                               static_cast<std::size_t>(EventType::Timer0Overflow);
            static_cast<void>(bus.on_timer_overflow(index, event->timestamp));
        }
    }
    scheduler.advance(target - scheduler.now());
}

} // namespace

TEST_CASE("Timers count at the selected prescaler rate", "[timers]") {
    GbaBus bus;
    static_cast<void>(bus.write16(0x04000100U, 0xFF00U)); // reload
    static_cast<void>(bus.write16(0x04000102U, 0x0081U)); // start, /64
    REQUIRE(bus.read16(0x04000100U).value == 0xFF00U);

    advance(bus, 64U * 10U + 63U);
    REQUIRE(bus.read16(0x04000100U).value == 0xFF0AU);
    REQUIRE(bus.read16(0x04000102U).value == 0x0081U);

    // Stopping freezes the counter; restarting reloads it.
    static_cast<void>(bus.write16(0x04000102U, 0x0001U));
    advance(bus, 6400U);
    REQUIRE(bus.read16(0x04000100U).value == 0xFF0AU);
    static_cast<void>(bus.write16(0x04000102U, 0x0081U));
    REQUIRE(bus.read16(0x04000100U).value == 0xFF00U);
}

TEST_CASE("Timer overflow reloads, raises its interrupt, and cascades", "[timers][irq]") {
    GbaBus bus;
    static_cast<void>(bus.write16(0x04000104U, 0xFFFDU));
    static_cast<void>(bus.write16(0x04000106U, 0x0084U)); // timer 1: count-up, start
    static_cast<void>(bus.write16(0x04000100U, 0xFF00U));
    static_cast<void>(bus.write16(0x04000102U, 0x00C0U)); // timer 0: IRQ, start, /1

    advance(bus, 256U);
    REQUIRE(bus.read16(0x04000100U).value == 0xFF00U);
    REQUIRE((bus.interrupt_flags() & 0x0008U) != 0U);
    REQUIRE(bus.read16(0x04000104U).value == 0xFFFEU);

    advance(bus, 256U * 2U + 10U);
    REQUIRE(bus.read16(0x04000100U).value == 0xFF0AU);
    // Timer 1 overflowed after three timer 0 overflows and reloaded.
    REQUIRE(bus.read16(0x04000104U).value == 0xFFFDU);
    REQUIRE((bus.interrupt_flags() & 0x0010U) == 0U); // timer 1 IRQ disabled
}

TEST_CASE("A timer interrupt wakes a halted CPU", "[timers][irq][emulator]") {
    const srgba::tests::TemporaryRom rom(srgba::tests::make_m2_cpu_test_rom());
    srgba::core::Emulator emulator;
    std::string error;
    REQUIRE(emulator.load_rom(rom.path(), error));
    auto& bus = emulator.bus();
    static_cast<void>(bus.write16(0x04000200U, 0x0008U)); // IE: timer 0
    static_cast<void>(bus.write16(0x04000100U, 0xF000U));
    static_cast<void>(bus.write16(0x04000102U, 0x00C0U));
    static_cast<void>(bus.write8(0x04000301U, 0x00U)); // HALT
    static_cast<void>(emulator.step_instruction());
    REQUIRE(emulator.is_halted());

    const auto start = emulator.cycle_counter();
    while (emulator.is_halted()) {
        REQUIRE(emulator.step_instruction().has_value());
    }
    // The timer overflows 0x1000 cycles after it starts.
    REQUIRE(emulator.cycle_counter() - start <= 0x1000U + 32U);
    REQUIRE((bus.interrupt_flags() & 0x0008U) != 0U);
}

TEST_CASE("Immediate DMA copies words and stalls the CPU", "[dma]") {
    GbaBus bus;
    for (std::uint32_t index = 0; index < 4U; ++index) {
        static_cast<void>(bus.write32(0x02000000U + index * 4U, 0xA0000000U + index));
    }
    static_cast<void>(bus.write32(0x040000D4U, 0x02000000U)); // DMA3SAD
    static_cast<void>(bus.write32(0x040000D8U, 0x03000000U)); // DMA3DAD
    static_cast<void>(bus.write32(0x040000DCU, 0xC4000004U)); // 4 words, IRQ, enable
    REQUIRE(bus.read32(0x0300000CU).value == 0xA0000003U);
    REQUIRE(bus.read16(0x040000DEU).value == 0x4400U); // enable bit cleared
    REQUIRE((bus.interrupt_flags() & 0x0800U) != 0U);  // DMA3 IRQ
    REQUIRE(bus.take_dma_cycles() > 4U * 2U);
    REQUIRE(bus.take_dma_cycles() == 0U);
}

TEST_CASE("DMA address control supports fixed sources and decrementing destinations", "[dma]") {
    GbaBus bus;
    static_cast<void>(bus.write16(0x02000000U, 0x1234U));
    static_cast<void>(bus.write32(0x040000B0U, 0x02000000U)); // DMA0SAD
    static_cast<void>(bus.write32(0x040000B4U, 0x03000006U)); // DMA0DAD
    static_cast<void>(bus.write16(0x040000B8U, 4U));
    // 16-bit, source fixed (2 << 7), destination decrement (1 << 5), enable.
    static_cast<void>(bus.write16(0x040000BAU, 0x8120U));
    REQUIRE(bus.read16(0x03000006U).value == 0x1234U);
    REQUIRE(bus.read16(0x03000000U).value == 0x1234U);
    REQUIRE(bus.read16(0x03000008U).value == 0x0000U);
}

TEST_CASE("Repeating HBlank DMA reloads its count and destination", "[dma]") {
    GbaBus bus;
    static_cast<void>(bus.write32(0x02000000U, 0x11112222U));
    static_cast<void>(bus.write32(0x02000004U, 0x33334444U));
    static_cast<void>(bus.write32(0x040000C8U, 0x02000000U)); // DMA2SAD
    static_cast<void>(bus.write32(0x040000CCU, 0x03000100U)); // DMA2DAD
    static_cast<void>(bus.write16(0x040000D0U, 1U));
    // 32-bit, repeat, HBlank, destination increment/reload, enable.
    static_cast<void>(bus.write16(0x040000D2U, 0xA660U));
    REQUIRE(bus.read32(0x03000100U).value == 0U); // waits for HBlank

    bus.trigger_dma(DmaTiming::HBlank);
    REQUIRE(bus.read32(0x03000100U).value == 0x11112222U);
    REQUIRE(bus.dma().enabled(2));
    bus.trigger_dma(DmaTiming::HBlank);
    // The source kept counting up; the destination reloaded.
    REQUIRE(bus.read32(0x03000100U).value == 0x33334444U);
    bus.trigger_dma(DmaTiming::VBlank);
    REQUIRE(bus.read32(0x03000104U).value == 0U);
}

TEST_CASE("VBlank DMA runs at the start of vertical blank", "[dma][emulator]") {
    const srgba::tests::TemporaryRom rom(srgba::tests::make_m2_cpu_test_rom());
    srgba::core::Emulator emulator;
    std::string error;
    REQUIRE(emulator.load_rom(rom.path(), error));
    auto& bus = emulator.bus();
    static_cast<void>(bus.write32(0x02000100U, 0xCAFEF00DU));
    static_cast<void>(bus.write32(0x040000D4U, 0x02000100U));
    static_cast<void>(bus.write32(0x040000D8U, 0x03000200U));
    static_cast<void>(bus.write32(0x040000DCU, 0x94000001U)); // 32-bit, VBlank, enable

    while (emulator.ppu().vcount() < 159U) {
        REQUIRE(emulator.step_instruction().has_value());
    }
    REQUIRE(bus.read32(0x03000200U).value == 0U);
    while (emulator.ppu().vcount() != 160U) {
        REQUIRE(emulator.step_instruction().has_value());
    }
    REQUIRE(bus.read32(0x03000200U).value == 0xCAFEF00DU);
    REQUIRE_FALSE(bus.dma().enabled(3));
}
