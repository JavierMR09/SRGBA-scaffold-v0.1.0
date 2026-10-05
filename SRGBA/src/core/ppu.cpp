#include "srgba/core/ppu.hpp"

#include "srgba/core/gba_bus.hpp"
#include "srgba/core/interrupts.hpp"
#include "srgba/core/scheduler.hpp"

#include <cstddef>

namespace srgba::core {
namespace {

constexpr std::uint32_t kDisplayControl = 0x000U;
constexpr std::uint32_t kDisplayStatus = 0x004U;

constexpr std::uint16_t kForcedBlank = 1U << 7U;
constexpr std::uint16_t kFrameSelect = 1U << 4U;
constexpr std::uint16_t kBg2Enable = 1U << 10U;

constexpr std::uint16_t kVBlankIrqEnable = 1U << 3U;
constexpr std::uint16_t kHBlankIrqEnable = 1U << 4U;
constexpr std::uint16_t kVCountIrqEnable = 1U << 5U;

constexpr std::size_t kBitmapFrameOffset = 0xA000U;
constexpr std::size_t kMode5Width = 160;
constexpr std::size_t kMode5Height = 128;

[[nodiscard]] std::uint16_t read16(const std::span<const std::uint8_t> memory,
                                   const std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(memory[offset] | (memory[offset + 1U] << 8U));
}

} // namespace

void Ppu::reset(GbaBus& bus, Scheduler& scheduler) noexcept {
    vcount_ = 0;
    hblank_ = false;
    bus.set_vcount(0);
    update_status(bus);
    scheduler.schedule(EventType::HBlankStart, kHDrawCycles);
    scheduler.schedule(EventType::ScanlineEnd, kCyclesPerLine);
}

std::uint32_t Ppu::vcount() const noexcept {
    return vcount_;
}

void Ppu::on_hblank_start(GbaBus& bus, Scheduler& scheduler, const std::uint64_t timestamp,
                          Framebuffer& framebuffer) noexcept {
    if (vcount_ < kVisibleLines) {
        render_scanline(bus, vcount_, framebuffer);
    }
    hblank_ = true;
    update_status(bus);
    // HBlank interrupts fire on every line, including those inside VBlank.
    if ((bus.io_register16(kDisplayStatus) & kHBlankIrqEnable) != 0U) {
        bus.request_interrupt(Interrupt::HBlank);
    }
    scheduler.schedule_at(EventType::HBlankStart, timestamp + kCyclesPerLine);
}

void Ppu::on_scanline_end(GbaBus& bus, Scheduler& scheduler,
                          const std::uint64_t timestamp) noexcept {
    hblank_ = false;
    vcount_ = (vcount_ + 1U) % kLinesPerFrame;
    bus.set_vcount(static_cast<std::uint8_t>(vcount_));
    update_status(bus);

    const auto status = bus.io_register16(kDisplayStatus);
    if (vcount_ == kVisibleLines && (status & kVBlankIrqEnable) != 0U) {
        bus.request_interrupt(Interrupt::VBlank);
    }
    if ((status & 0x0004U) != 0U && (status & kVCountIrqEnable) != 0U) {
        bus.request_interrupt(Interrupt::VCount);
    }
    scheduler.schedule_at(EventType::ScanlineEnd, timestamp + kCyclesPerLine);
}

void Ppu::update_status(GbaBus& bus) noexcept {
    const auto status = bus.io_register16(kDisplayStatus);
    const auto target_line = static_cast<std::uint32_t>(status >> 8U);
    std::uint8_t flags = 0;
    // The VBlank flag is set on lines 160-226; line 227 already reports VDraw.
    if (vcount_ >= kVisibleLines && vcount_ < kLinesPerFrame - 1U) {
        flags |= 0x01U;
    }
    if (hblank_) {
        flags |= 0x02U;
    }
    if (vcount_ == target_line) {
        flags |= 0x04U;
    }
    bus.set_display_status_flags(flags);
}

void Ppu::render_scanline(const GbaBus& bus, const std::uint32_t line,
                          Framebuffer& framebuffer) noexcept {
    const auto row = static_cast<std::size_t>(line) * kScreenWidth;
    const auto control = bus.io_register16(kDisplayControl);

    if ((control & kForcedBlank) != 0U) {
        for (std::size_t x = 0; x < kScreenWidth; ++x) {
            framebuffer[row + x] = Rgba8{255, 255, 255, 255};
        }
        return;
    }

    const auto palette = bus.palette_ram();
    const auto vram = bus.video_ram();
    const auto backdrop = to_rgba(read16(palette, 0));
    const auto mode = static_cast<std::uint16_t>(control & 0x7U);
    const bool bg2 = (control & kBg2Enable) != 0U;
    const std::size_t frame = (control & kFrameSelect) != 0U ? kBitmapFrameOffset : 0U;

    for (std::size_t x = 0; x < kScreenWidth; ++x) {
        auto color = backdrop;
        if (bg2) {
            switch (mode) {
            case 3:
                color = to_rgba(read16(vram, (row + x) * 2U));
                break;
            case 4: {
                const auto index = vram[frame + row + x];
                if (index != 0U) {
                    color = to_rgba(read16(palette, static_cast<std::size_t>(index) * 2U));
                }
                break;
            }
            case 5:
                if (x < kMode5Width && line < kMode5Height) {
                    color = to_rgba(read16(
                        vram, frame + (static_cast<std::size_t>(line) * kMode5Width + x) * 2U));
                }
                break;
            default:
                break;
            }
        }
        framebuffer[row + x] = color;
    }
}

} // namespace srgba::core
