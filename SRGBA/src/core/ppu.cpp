#include "srgba/core/ppu.hpp"

#include "srgba/core/dma.hpp"
#include "srgba/core/gba_bus.hpp"
#include "srgba/core/interrupts.hpp"
#include "srgba/core/scheduler.hpp"

#include <algorithm>
#include <span>

namespace srgba::core {
namespace {

// IO register offsets.
constexpr std::uint32_t kDisplayControl = 0x000U;
constexpr std::uint32_t kDisplayStatus = 0x004U;
constexpr std::uint32_t kBackgroundControl = 0x008U;
constexpr std::uint32_t kBackgroundOffset = 0x010U;
constexpr std::uint32_t kAffineParameters = 0x020U; // BG2PA; BG3 parameters follow at +0x10
constexpr std::uint32_t kWindowHorizontal = 0x040U;
constexpr std::uint32_t kWindowVertical = 0x044U;
constexpr std::uint32_t kWindowInside = 0x048U;
constexpr std::uint32_t kWindowOutside = 0x04AU;
constexpr std::uint32_t kMosaic = 0x04CU;
constexpr std::uint32_t kBlendControl = 0x050U;
constexpr std::uint32_t kBlendAlpha = 0x052U;
constexpr std::uint32_t kBlendBrightness = 0x054U;

// DISPCNT bits.
constexpr std::uint16_t kFrameSelect = 1U << 4U;
constexpr std::uint16_t kObjectMapping1D = 1U << 6U;
constexpr std::uint16_t kForcedBlank = 1U << 7U;
constexpr std::uint16_t kObjectEnable = 1U << 12U;
constexpr std::uint16_t kWindow0Enable = 1U << 13U;
constexpr std::uint16_t kWindow1Enable = 1U << 14U;
constexpr std::uint16_t kObjectWindowEnable = 1U << 15U;

// DISPSTAT bits.
constexpr std::uint16_t kVBlankIrqEnable = 1U << 3U;
constexpr std::uint16_t kHBlankIrqEnable = 1U << 4U;
constexpr std::uint16_t kVCountIrqEnable = 1U << 5U;

// Layer identifiers used by windows and BLDCNT.
constexpr unsigned kObjectLayer = 4;
constexpr unsigned kBackdropLayer = 5;
constexpr std::uint8_t kEffectsBit = 1U << 5U;

constexpr std::size_t kObjectTileBase = 0x10000U;
constexpr std::size_t kBackgroundVramLimit = 0x10000U;
constexpr std::size_t kObjectPaletteBase = 0x200U;
constexpr std::size_t kBitmapFrameOffset = 0xA000U;

// Sprite dimensions indexed by [shape][size].
constexpr std::array<std::array<std::array<int, 2>, 4>, 3> kObjectSizes{{
    {{{8, 8}, {16, 16}, {32, 32}, {64, 64}}},
    {{{16, 8}, {32, 8}, {32, 16}, {64, 32}}},
    {{{8, 16}, {8, 32}, {16, 32}, {32, 64}}},
}};

[[nodiscard]] std::uint16_t read16(const std::span<const std::uint8_t> memory,
                                   const std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(memory[offset] | (memory[offset + 1U] << 8U));
}

[[nodiscard]] std::uint16_t palette_color(const std::span<const std::uint8_t> palette,
                                          const std::size_t index) noexcept {
    return static_cast<std::uint16_t>(read16(palette, (index * 2U) & 0x3FEU) & 0x7FFFU);
}

[[nodiscard]] std::int32_t sign_extend28(const std::uint32_t value) noexcept {
    return static_cast<std::int32_t>((value & 0x0FFFFFFFU) << 4U) >> 4;
}

// Returns true when `position` lies in the window range [start, end), wrapping around when
// start > end as the hardware does.
[[nodiscard]] bool in_window_range(const unsigned position, const unsigned start,
                                   const unsigned end) noexcept {
    if (start <= end) {
        return position >= start && position < end;
    }
    return position >= start || position < end;
}

[[nodiscard]] std::uint16_t blend_alpha(const std::uint16_t top, const std::uint16_t bottom,
                                        const unsigned eva, const unsigned evb) noexcept {
    std::uint16_t result = 0;
    for (unsigned shift = 0; shift < 15U; shift += 5U) {
        const auto a = (static_cast<unsigned>(top) >> shift) & 0x1FU;
        const auto b = (static_cast<unsigned>(bottom) >> shift) & 0x1FU;
        const auto channel = std::min(31U, (a * eva + b * evb) >> 4U);
        result = static_cast<std::uint16_t>(result | (channel << shift));
    }
    return result;
}

[[nodiscard]] std::uint16_t brighten(const std::uint16_t color, const unsigned evy) noexcept {
    std::uint16_t result = 0;
    for (unsigned shift = 0; shift < 15U; shift += 5U) {
        const auto channel = (static_cast<unsigned>(color) >> shift) & 0x1FU;
        result = static_cast<std::uint16_t>(result |
                                            ((channel + (((31U - channel) * evy) >> 4U)) << shift));
    }
    return result;
}

[[nodiscard]] std::uint16_t darken(const std::uint16_t color, const unsigned evy) noexcept {
    std::uint16_t result = 0;
    for (unsigned shift = 0; shift < 15U; shift += 5U) {
        const auto channel = (static_cast<unsigned>(color) >> shift) & 0x1FU;
        result =
            static_cast<std::uint16_t>(result | ((channel - ((channel * evy) >> 4U)) << shift));
    }
    return result;
}

} // namespace

void Ppu::reset(GbaBus& bus, Scheduler& scheduler) noexcept {
    vcount_ = 0;
    hblank_ = false;
    affine_ = {};
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
    // HBlank DMA only runs during the visible period.
    if (vcount_ < kVisibleLines) {
        bus.trigger_dma(DmaTiming::HBlank);
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
    if (vcount_ == kVisibleLines) {
        if ((status & kVBlankIrqEnable) != 0U) {
            bus.request_interrupt(Interrupt::VBlank);
        }
        bus.trigger_dma(DmaTiming::VBlank);
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

void Ppu::reload_affine(const GbaBus& bus, const std::size_t index) noexcept {
    const auto base = static_cast<std::uint32_t>(0x028U + index * 0x10U);
    const auto x = static_cast<std::uint32_t>(bus.io_register16(base)) |
                   (static_cast<std::uint32_t>(bus.io_register16(base + 2U)) << 16U);
    const auto y = static_cast<std::uint32_t>(bus.io_register16(base + 4U)) |
                   (static_cast<std::uint32_t>(bus.io_register16(base + 6U)) << 16U);
    affine_[index] = AffineReference{sign_extend28(x), sign_extend28(y)};
}

void Ppu::render_frame(GbaBus& bus, Framebuffer& framebuffer) noexcept {
    for (std::uint32_t line = 0; line < kVisibleLines; ++line) {
        render_scanline(bus, line, framebuffer);
    }
}

void Ppu::render_scanline(GbaBus& bus, const std::uint32_t line,
                          Framebuffer& framebuffer) noexcept {
    // The internal reference points restart every frame and whenever the CPU writes them.
    for (std::size_t index = 0; index < affine_.size(); ++index) {
        if (bus.take_affine_reload(index) || line == 0U) {
            reload_affine(bus, index);
        }
    }

    const auto control = bus.io_register16(kDisplayControl);
    const auto row = static_cast<std::size_t>(line) * kScreenWidth;
    if ((control & kForcedBlank) != 0U) {
        std::fill_n(framebuffer.begin() + static_cast<std::ptrdiff_t>(row), kScreenWidth,
                    Rgba8{255, 255, 255, 255});
    } else {
        const auto mode = control & 0x7U;
        background_active_.fill(false);
        for (std::size_t background = 0; background < 4U; ++background) {
            if ((control & (1U << (8U + background))) == 0U) {
                continue;
            }
            switch (mode) {
            case 0:
                render_text_background(bus, background, line);
                break;
            case 1:
                if (background < 2U) {
                    render_text_background(bus, background, line);
                } else if (background == 2U) {
                    render_affine_background(bus, background);
                }
                break;
            case 2:
                if (background >= 2U) {
                    render_affine_background(bus, background);
                }
                break;
            case 3:
            case 4:
            case 5:
                if (background == 2U) {
                    render_bitmap_background(bus, control);
                }
                break;
            default:
                break;
            }
        }

        object_line_.fill(ObjectPixel{});
        object_window_.fill(false);
        if ((control & kObjectEnable) != 0U) {
            render_objects(bus, control, line);
        }
        compute_windows(bus, control, line);
        compose(bus, control, line, framebuffer);
    }

    // Affine reference points advance by (PB, PD) after every visible line.
    for (std::size_t index = 0; index < affine_.size(); ++index) {
        const auto base = static_cast<std::uint32_t>(kAffineParameters + index * 0x10U);
        affine_[index].x += static_cast<std::int16_t>(bus.io_register16(base + 2U));
        affine_[index].y += static_cast<std::int16_t>(bus.io_register16(base + 6U));
    }
}

void Ppu::render_text_background(const GbaBus& bus, const std::size_t background,
                                 const std::uint32_t line) noexcept {
    auto& output = background_lines_[background];
    background_active_[background] = true;
    const auto vram = bus.video_ram();
    const auto palette = bus.palette_ram();
    const auto control =
        bus.io_register16(kBackgroundControl + static_cast<std::uint32_t>(background) * 2U);
    const auto horizontal_offset =
        bus.io_register16(kBackgroundOffset + static_cast<std::uint32_t>(background) * 4U) & 0x1FFU;
    const auto vertical_offset =
        bus.io_register16(kBackgroundOffset + static_cast<std::uint32_t>(background) * 4U + 2U) &
        0x1FFU;
    const std::size_t character_base = ((control >> 2U) & 0x3U) * 0x4000U;
    const std::size_t screen_base = ((control >> 8U) & 0x1FU) * 0x800U;
    const bool colors256 = (control & 0x80U) != 0U;
    const unsigned size = (control >> 14U) & 0x3U;
    const unsigned width = (size & 1U) != 0U ? 512U : 256U;
    const unsigned height = (size & 2U) != 0U ? 512U : 256U;

    unsigned mosaic_width = 1;
    auto source_line = line;
    if ((control & 0x40U) != 0U) {
        const auto mosaic = bus.io_register16(kMosaic);
        mosaic_width = (mosaic & 0xFU) + 1U;
        const auto mosaic_height = ((mosaic >> 4U) & 0xFU) + 1U;
        source_line -= line % mosaic_height;
    }

    const auto y = (source_line + vertical_offset) & (height - 1U);
    const auto tile_row = (y / 8U) % 32U;
    const auto fine_y = y % 8U;
    const auto block_row = (y / 256U) * (width / 256U);

    for (unsigned screen_x = 0; screen_x < kScreenWidth; ++screen_x) {
        const auto sample_x = screen_x - screen_x % mosaic_width;
        const auto x = (sample_x + horizontal_offset) & (width - 1U);
        const auto block = (x / 256U) + block_row;
        const auto entry_offset =
            screen_base + block * 0x800U + (tile_row * 32U + (x / 8U) % 32U) * 2U;
        if (entry_offset + 1U >= vram.size()) {
            output[screen_x] = kTransparent;
            continue;
        }
        const auto entry = read16(vram, entry_offset);
        const auto tile = static_cast<std::size_t>(entry & 0x3FFU);
        auto pixel_x = x % 8U;
        auto pixel_y = fine_y;
        if ((entry & 0x0400U) != 0U) {
            pixel_x = 7U - pixel_x;
        }
        if ((entry & 0x0800U) != 0U) {
            pixel_y = 7U - pixel_y;
        }

        std::uint16_t color = kTransparent;
        if (colors256) {
            const auto address = character_base + tile * 64U + pixel_y * 8U + pixel_x;
            if (address < kBackgroundVramLimit && vram[address] != 0U) {
                color = palette_color(palette, vram[address]);
            }
        } else {
            const auto address = character_base + tile * 32U + pixel_y * 4U + pixel_x / 2U;
            if (address < kBackgroundVramLimit) {
                const auto index =
                    (static_cast<unsigned>(vram[address]) >> ((pixel_x & 1U) * 4U)) & 0xFU;
                if (index != 0U) {
                    color = palette_color(palette, ((entry >> 12U) & 0xFU) * 16U + index);
                }
            }
        }
        output[screen_x] = color;
    }
}

void Ppu::render_affine_background(const GbaBus& bus, const std::size_t background) noexcept {
    auto& output = background_lines_[background];
    background_active_[background] = true;
    const auto vram = bus.video_ram();
    const auto palette = bus.palette_ram();
    const auto control =
        bus.io_register16(kBackgroundControl + static_cast<std::uint32_t>(background) * 2U);
    const std::size_t character_base = ((control >> 2U) & 0x3U) * 0x4000U;
    const std::size_t screen_base = ((control >> 8U) & 0x1FU) * 0x800U;
    const bool wrap = (control & 0x2000U) != 0U;
    const auto size = static_cast<std::int32_t>(128U << ((control >> 14U) & 0x3U));
    const auto tiles_per_row = static_cast<std::size_t>(size / 8);

    const auto index = background - 2U;
    const auto parameters = static_cast<std::uint32_t>(kAffineParameters + index * 0x10U);
    const auto pa =
        static_cast<std::int32_t>(static_cast<std::int16_t>(bus.io_register16(parameters)));
    const auto pc =
        static_cast<std::int32_t>(static_cast<std::int16_t>(bus.io_register16(parameters + 4U)));

    unsigned mosaic_width = 1;
    if ((control & 0x40U) != 0U) {
        mosaic_width = (bus.io_register16(kMosaic) & 0xFU) + 1U;
    }

    for (unsigned screen_x = 0; screen_x < kScreenWidth; ++screen_x) {
        const auto sample_x = static_cast<std::int32_t>(screen_x - screen_x % mosaic_width);
        auto x = (affine_[index].x + pa * sample_x) >> 8;
        auto y = (affine_[index].y + pc * sample_x) >> 8;
        if (wrap) {
            x &= size - 1;
            y &= size - 1;
        } else if (x < 0 || y < 0 || x >= size || y >= size) {
            output[screen_x] = kTransparent;
            continue;
        }
        const auto ux = static_cast<std::size_t>(x);
        const auto uy = static_cast<std::size_t>(y);
        const auto map_address = screen_base + (uy / 8U) * tiles_per_row + ux / 8U;
        const std::size_t tile = map_address < vram.size() ? vram[map_address] : 0U;
        const auto address = character_base + tile * 64U + (uy % 8U) * 8U + ux % 8U;
        output[screen_x] = address < kBackgroundVramLimit && vram[address] != 0U
                               ? palette_color(palette, vram[address])
                               : kTransparent;
    }
}

void Ppu::render_bitmap_background(const GbaBus& bus, const std::uint16_t control) noexcept {
    auto& output = background_lines_[2];
    background_active_[2] = true;
    const auto vram = bus.video_ram();
    const auto palette = bus.palette_ram();
    const auto mode = control & 0x7U;
    const std::size_t frame =
        (control & kFrameSelect) != 0U && mode != 3U ? kBitmapFrameOffset : 0U;
    const std::int32_t width = mode == 5U ? 160 : 240;
    const std::int32_t height = mode == 5U ? 128 : 160;

    const auto pa = static_cast<std::int32_t>(static_cast<std::int16_t>(bus.io_register16(0x020U)));
    const auto pc = static_cast<std::int32_t>(static_cast<std::int16_t>(bus.io_register16(0x024U)));
    const auto background_control = bus.io_register16(kBackgroundControl + 4U);
    unsigned mosaic_width = 1;
    if ((background_control & 0x40U) != 0U) {
        mosaic_width = (bus.io_register16(kMosaic) & 0xFU) + 1U;
    }

    for (unsigned screen_x = 0; screen_x < kScreenWidth; ++screen_x) {
        const auto sample_x = static_cast<std::int32_t>(screen_x - screen_x % mosaic_width);
        const auto x = (affine_[0].x + pa * sample_x) >> 8;
        const auto y = (affine_[0].y + pc * sample_x) >> 8;
        if (x < 0 || y < 0 || x >= width || y >= height) {
            output[screen_x] = kTransparent;
            continue;
        }
        const auto pixel = static_cast<std::size_t>(y * width + x);
        if (mode == 4U) {
            const auto index = vram[frame + pixel];
            output[screen_x] = index != 0U ? palette_color(palette, index) : kTransparent;
        } else {
            output[screen_x] =
                static_cast<std::uint16_t>(read16(vram, frame + pixel * 2U) & 0x7FFFU);
        }
    }
}

void Ppu::render_objects(const GbaBus& bus, const std::uint16_t control,
                         const std::uint32_t line) noexcept {
    const auto oam = bus.object_attribute_memory();
    const auto vram = bus.video_ram();
    const auto palette = bus.palette_ram();
    const bool mapping_1d = (control & kObjectMapping1D) != 0U;
    const bool bitmap_mode = (control & 0x7U) >= 3U;
    const auto mosaic = bus.io_register16(kMosaic);
    const auto mosaic_width = static_cast<int>(((mosaic >> 8U) & 0xFU) + 1U);
    const auto mosaic_height = static_cast<int>(((mosaic >> 12U) & 0xFU) + 1U);
    const auto screen_y = static_cast<int>(line);

    for (std::size_t object = 0; object < 128U; ++object) {
        const auto attribute0 = read16(oam, object * 8U);
        const auto attribute1 = read16(oam, object * 8U + 2U);
        const auto attribute2 = read16(oam, object * 8U + 4U);
        const bool affine = (attribute0 & 0x0100U) != 0U;
        const bool double_size = (attribute0 & 0x0200U) != 0U;
        if (!affine && double_size) {
            continue; // disabled
        }
        const auto mode = (attribute0 >> 10U) & 0x3U;
        const auto shape = (attribute0 >> 14U) & 0x3U;
        if (mode == 3U || shape == 3U) {
            continue;
        }
        const auto [width, height] = kObjectSizes[shape][(attribute1 >> 14U) & 0x3U];
        const int box_width = affine && double_size ? width * 2 : width;
        const int box_height = affine && double_size ? height * 2 : height;

        int y = attribute0 & 0xFF;
        if (y + box_height > 256) {
            y -= 256;
        }
        if (screen_y < y || screen_y >= y + box_height) {
            continue;
        }
        int x = attribute1 & 0x1FF;
        if (x >= 240) {
            x -= 512;
        }

        const auto tile = static_cast<std::size_t>(attribute2 & 0x3FFU);
        if (bitmap_mode && tile < 512U) {
            continue; // these tiles overlap the bitmap frame buffer
        }
        const auto priority = static_cast<std::uint8_t>((attribute2 >> 10U) & 0x3U);
        const auto palette_bank = static_cast<std::size_t>((attribute2 >> 12U) & 0xFU);
        const bool colors256 = (attribute0 & 0x2000U) != 0U;
        const bool mosaic_enabled = (attribute0 & 0x1000U) != 0U;

        int local_y = screen_y - y;
        if (mosaic_enabled) {
            local_y = std::max(0, (screen_y - screen_y % mosaic_height) - y);
        }

        std::int32_t pa = 0x100;
        std::int32_t pb = 0;
        std::int32_t pc = 0;
        std::int32_t pd = 0x100;
        if (affine) {
            const auto group = static_cast<std::size_t>((attribute1 >> 9U) & 0x1FU) * 32U;
            pa = static_cast<std::int16_t>(read16(oam, group + 6U));
            pb = static_cast<std::int16_t>(read16(oam, group + 14U));
            pc = static_cast<std::int16_t>(read16(oam, group + 22U));
            pd = static_cast<std::int16_t>(read16(oam, group + 30U));
        }
        const bool horizontal_flip = !affine && (attribute1 & 0x1000U) != 0U;
        const bool vertical_flip = !affine && (attribute1 & 0x2000U) != 0U;
        const std::size_t row_stride =
            mapping_1d ? static_cast<std::size_t>(width / 8) * (colors256 ? 2U : 1U) : 32U;

        for (int local_x = 0; local_x < box_width; ++local_x) {
            const int screen_x = x + local_x;
            if (screen_x < 0 || screen_x >= 240) {
                continue;
            }
            int sample_local_x = local_x;
            if (mosaic_enabled) {
                sample_local_x = std::max(0, (screen_x - screen_x % mosaic_width) - x);
            }

            int texture_x = 0;
            int texture_y = 0;
            if (affine) {
                const int dx = sample_local_x - box_width / 2;
                const int dy = local_y - box_height / 2;
                texture_x = ((pa * dx + pb * dy) >> 8) + width / 2;
                texture_y = ((pc * dx + pd * dy) >> 8) + height / 2;
                if (texture_x < 0 || texture_y < 0 || texture_x >= width || texture_y >= height) {
                    continue;
                }
            } else {
                texture_x = horizontal_flip ? width - 1 - sample_local_x : sample_local_x;
                texture_y = vertical_flip ? height - 1 - local_y : local_y;
            }

            const auto tile_x = static_cast<std::size_t>(texture_x / 8);
            const auto tile_y = static_cast<std::size_t>(texture_y / 8);
            const auto pixel_x = static_cast<std::size_t>(texture_x % 8);
            const auto pixel_y = static_cast<std::size_t>(texture_y % 8);
            std::uint16_t color = kTransparent;
            if (colors256) {
                const auto base_tile = mapping_1d ? tile : (tile & ~std::size_t{1});
                const auto tile_number = (base_tile + tile_y * row_stride + tile_x * 2U) & 0x3FFU;
                const auto address = kObjectTileBase + tile_number * 32U + pixel_y * 8U + pixel_x;
                const auto index = address < vram.size() ? vram[address] : 0U;
                if (index != 0U) {
                    color = palette_color(palette, kObjectPaletteBase / 2U + index);
                }
            } else {
                const auto tile_number = (tile + tile_y * row_stride + tile_x) & 0x3FFU;
                const auto address =
                    kObjectTileBase + tile_number * 32U + pixel_y * 4U + pixel_x / 2U;
                const auto packed = address < vram.size() ? vram[address] : 0U;
                const auto index = (packed >> ((pixel_x & 1U) * 4U)) & 0xFU;
                if (index != 0U) {
                    color = palette_color(palette,
                                          kObjectPaletteBase / 2U + palette_bank * 16U + index);
                }
            }
            if (color == kTransparent) {
                continue;
            }

            const auto column = static_cast<std::size_t>(screen_x);
            if (mode == 2U) {
                object_window_[column] = true;
                continue;
            }
            auto& pixel = object_line_[column];
            if (pixel.color == kTransparent || priority < pixel.priority) {
                pixel = ObjectPixel{color, priority, mode == 1U};
            }
        }
    }
}

void Ppu::compute_windows(const GbaBus& bus, const std::uint16_t control,
                          const std::uint32_t line) noexcept {
    const bool window0 = (control & kWindow0Enable) != 0U;
    const bool window1 = (control & kWindow1Enable) != 0U;
    const bool object_window =
        (control & kObjectWindowEnable) != 0U && (control & kObjectEnable) != 0U;
    if (!window0 && !window1 && !object_window) {
        window_mask_.fill(0x3FU);
        return;
    }

    const auto inside = bus.io_register16(kWindowInside);
    const auto outside = bus.io_register16(kWindowOutside);
    const auto inside_vertical = [&](const unsigned window) {
        const auto range = bus.io_register16(kWindowVertical + window * 2U);
        return in_window_range(line, range >> 8U, range & 0xFFU);
    };
    const bool window0_line = window0 && inside_vertical(0);
    const bool window1_line = window1 && inside_vertical(1);
    const auto window0_range = bus.io_register16(kWindowHorizontal);
    const auto window1_range = bus.io_register16(kWindowHorizontal + 2U);

    for (unsigned x = 0; x < kScreenWidth; ++x) {
        auto mask = static_cast<std::uint8_t>(outside & 0x3FU);
        if (object_window && object_window_[x]) {
            mask = static_cast<std::uint8_t>((outside >> 8U) & 0x3FU);
        }
        if (window1_line && in_window_range(x, window1_range >> 8U, window1_range & 0xFFU)) {
            mask = static_cast<std::uint8_t>((inside >> 8U) & 0x3FU);
        }
        if (window0_line && in_window_range(x, window0_range >> 8U, window0_range & 0xFFU)) {
            mask = static_cast<std::uint8_t>(inside & 0x3FU);
        }
        window_mask_[x] = mask;
    }
}

void Ppu::compose(const GbaBus& bus, const std::uint16_t control, const std::uint32_t line,
                  Framebuffer& framebuffer) noexcept {
    const auto palette = bus.palette_ram();
    const auto backdrop = palette_color(palette, 0);
    const auto blend_control = bus.io_register16(kBlendControl);
    const auto effect = (blend_control >> 6U) & 0x3U;
    const auto first_targets = blend_control & 0x3FU;
    const auto second_targets = (blend_control >> 8U) & 0x3FU;
    const auto alpha = bus.io_register16(kBlendAlpha);
    const auto eva = std::min(16U, alpha & 0x1FU);
    const auto evb = std::min(16U, (alpha >> 8U) & 0x1FU);
    const auto evy = std::min(16U, bus.io_register16(kBlendBrightness) & 0x1FU);
    const bool objects = (control & kObjectEnable) != 0U;

    // Active backgrounds sorted front to back: by priority, then by background number.
    std::array<std::size_t, 4> order{};
    std::array<std::uint8_t, 4> priorities{};
    std::size_t active = 0;
    for (unsigned priority = 0; priority < 4U; ++priority) {
        for (std::size_t background = 0; background < 4U; ++background) {
            const auto background_priority =
                bus.io_register16(kBackgroundControl +
                                  static_cast<std::uint32_t>(background) * 2U) &
                0x3U;
            if (background_active_[background] && background_priority == priority) {
                priorities[active] = static_cast<std::uint8_t>(priority);
                order[active++] = background;
            }
        }
    }

    const auto row = static_cast<std::size_t>(line) * kScreenWidth;
    for (std::size_t x = 0; x < kScreenWidth; ++x) {
        const auto mask = window_mask_[x];
        std::array<std::uint16_t, 2> colors{backdrop, backdrop};
        std::array<unsigned, 2> layers{kBackdropLayer, kBackdropLayer};
        std::size_t found = 0;
        bool top_semi_transparent = false;

        const auto& object = object_line_[x];
        // Sprites sit in front of backgrounds that share their priority.
        bool object_pending =
            objects && object.color != kTransparent && (mask & (1U << kObjectLayer)) != 0U;
        const auto push_object = [&]() {
            if (found == 0U) {
                top_semi_transparent = object.semi_transparent;
            }
            colors[found] = object.color;
            layers[found] = kObjectLayer;
            ++found;
            object_pending = false;
        };
        for (std::size_t slot = 0; slot < active && found < 2U; ++slot) {
            if (object_pending && object.priority <= priorities[slot]) {
                push_object();
                if (found == 2U) {
                    break;
                }
            }
            const auto background = order[slot];
            if ((mask & (1U << background)) == 0U) {
                continue;
            }
            const auto color = background_lines_[background][x];
            if (color != kTransparent) {
                colors[found] = color;
                layers[found] = static_cast<unsigned>(background);
                ++found;
            }
        }
        if (object_pending && found < 2U) {
            push_object();
        }

        auto color = colors[0];
        const bool below_is_target = (second_targets & (1U << layers[1])) != 0U;
        if (top_semi_transparent && below_is_target) {
            // Semi-transparent sprites always alpha-blend onto a second-target layer.
            color = blend_alpha(colors[0], colors[1], eva, evb);
        } else if ((mask & kEffectsBit) != 0U && (first_targets & (1U << layers[0])) != 0U) {
            switch (effect) {
            case 1:
                if (below_is_target) {
                    color = blend_alpha(colors[0], colors[1], eva, evb);
                }
                break;
            case 2:
                color = brighten(colors[0], evy);
                break;
            case 3:
                color = darken(colors[0], evy);
                break;
            default:
                break;
            }
        }
        framebuffer[row + x] = to_rgba(color);
    }
}

} // namespace srgba::core
