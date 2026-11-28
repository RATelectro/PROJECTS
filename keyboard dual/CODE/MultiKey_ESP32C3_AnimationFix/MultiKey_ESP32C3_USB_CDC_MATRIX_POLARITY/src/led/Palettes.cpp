#include "Palettes.h"

namespace {

struct BuiltinPalette {
    const char *name;
    uint8_t count;
    uint8_t color[PALETTE_MAX_STOPS][3];
};

#define C(hex) { static_cast<uint8_t>(((hex) >> 16) & 0xFF), \
                 static_cast<uint8_t>(((hex) >> 8) & 0xFF), \
                 static_cast<uint8_t>((hex) & 0xFF) }

constexpr BuiltinPalette BUILTINS[NUM_PALETTES] = {
    { "RAINBOW", 8, { C(0xFF0000), C(0xFF8000), C(0xFFFF00), C(0x00FF00),
                      C(0x00FFFF), C(0x0000FF), C(0x8000FF), C(0xFF0080) } },
    { "HEAT", 6, { C(0x200000), C(0x800000), C(0xE02000), C(0xFF6000),
                   C(0xFFB000), C(0xFFE080) } },
    { "OCEAN", 6, { C(0x000020), C(0x002060), C(0x0040A0), C(0x0080C0),
                    C(0x00C0C0), C(0x80FFE0) } },
    { "FOREST", 6, { C(0x001800), C(0x084808), C(0x108010), C(0x30B030),
                     C(0x70D040), C(0xC0F080) } },
    { "SUNSET", 6, { C(0x301060), C(0x7010A0), C(0xC02080), C(0xF04060),
                     C(0xFF8040), C(0xFFC060) } },
    { "CANDY", 6, { C(0xFF4080), C(0xFF70A0), C(0xFFA0C0), C(0xC080FF),
                    C(0x8060FF), C(0x5090FF) } },
    { "GRAPE", 5, { C(0x300050), C(0x6000A0), C(0x9020D0), C(0xC050E0),
                    C(0xE080FF) } },
    { "MINT", 5, { C(0x003020), C(0x008060), C(0x00B090), C(0x40E0B0),
                   C(0xA0FFD0) } },
    { "GOLD", 5, { C(0x201000), C(0x604010), C(0xA07020), C(0xD0A040),
                   C(0xFFE090) } },
    { "ICE", 6, { C(0x102040), C(0x2050A0), C(0x3080D0), C(0x60B0F0),
                  C(0xA0D8FF), C(0xE0F4FF) } },
    { "CORAL", 5, { C(0x801020), C(0xC03040), C(0xF06060), C(0xFF9070),
                    C(0xFFC0A0) } },
    { "LAGOON", 6, { C(0x003030), C(0x008080), C(0x00B0A0), C(0x40D080),
                     C(0x90E060), C(0xE0F0A0) } },
    { "MAGMA", 6, { C(0x200000), C(0x600000), C(0xA01000), C(0xE04000),
                    C(0xFF9000), C(0xFFE040) } },
    { "VIOLET", 5, { C(0x200040), C(0x500090), C(0x8020C0), C(0xB060E0),
                     C(0xE0A0FF) } },
    { "NEON", 6, { C(0x00FF40), C(0x80FF00), C(0xFFFF00), C(0xFF8000),
                   C(0xFF2080), C(0xB000FF) } },
    { "RUST", 5, { C(0x301000), C(0x702008), C(0xB04018), C(0xD07030),
                   C(0xF0A860) } },
};

#undef C

} // namespace

void initDefaultPalettes(Palette *palettes) {
    for (uint8_t i = 0; i < NUM_PALETTES; ++i) {
        palettes[i].count = BUILTINS[i].count;
        memcpy(palettes[i].color, BUILTINS[i].color, sizeof(palettes[i].color));
    }
}

const char *paletteName(uint8_t index) {
    if (index >= NUM_PALETTES) return "?";
    return BUILTINS[index].name;
}

CRGB paletteColor(const Palette &palette, uint8_t position) {
    if (palette.count == 0) return CHSV(position, 255, 255);
    if (palette.count == 1) {
        return CRGB(palette.color[0][0], palette.color[0][1], palette.color[0][2]);
    }
    // Crossfade between adjacent stops across the 0..255 range.
    const uint16_t span = static_cast<uint16_t>(palette.count - 1) * 255UL;
    const uint32_t scaled = static_cast<uint32_t>(position) * span / 255;
    const uint8_t stop = scaled / 255;
    const uint8_t frac = scaled % 255;
    const uint8_t i = min(static_cast<uint8_t>(stop), static_cast<uint8_t>(palette.count - 2));
    const CRGB a(palette.color[i][0], palette.color[i][1], palette.color[i][2]);
    const CRGB b(palette.color[i + 1][0], palette.color[i + 1][1], palette.color[i + 1][2]);
    return CRGB( a.r + (static_cast<int16_t>(b.r) - a.r) * frac / 255,
                 a.g + (static_cast<int16_t>(b.g) - a.g) * frac / 255,
                 a.b + (static_cast<int16_t>(b.b) - a.b) * frac / 255 );
}
