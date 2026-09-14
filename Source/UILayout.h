#pragma once

#include <array>

namespace dd::ui
{
struct Bounds
{
    int x;
    int y;
    int width;
    int height;
};

inline constexpr int designWidth = 648;
inline constexpr int compactHeight = 286;
inline constexpr int expandedHeight = 450;
inline constexpr int frame = 4;

inline constexpr Bounds header { 4, 4, 640, 60 };
inline constexpr Bounds brand { 4, 4, 200, 60 };
inline constexpr Bounds algorithmPrevious { 204, 4, 22, 60 };
inline constexpr Bounds algorithm { 226, 4, 157, 60 };
inline constexpr Bounds algorithmNext { 383, 4, 22, 60 };
inline constexpr Bounds oversampling { 405, 4, 59, 60 };
inline constexpr Bounds autoGain { 464, 4, 110, 60 };
inline constexpr Bounds power { 574, 4, 70, 60 };

inline constexpr Bounds main { 4, 68, 640, 182 };
inline constexpr std::array<Bounds, 12> controls {{
    { 4, 68, 100, 60 }, { 104, 68, 100, 60 },
    { 204, 68, 100, 60 }, { 304, 68, 100, 60 },
    { 4, 129, 100, 60 }, { 104, 129, 100, 60 },
    { 204, 129, 100, 60 }, { 304, 129, 100, 60 },
    { 4, 190, 100, 60 }, { 104, 190, 100, 60 },
    { 204, 190, 100, 60 }, { 304, 190, 100, 60 }
}};
inline constexpr Bounds stereoToggle { 354, 76, 18, 44 };
inline constexpr Bounds meters { 404, 68, 60, 182 };
inline constexpr Bounds response { 464, 68, 180, 182 };

inline constexpr Bounds utility { 4, 254, 640, 28 };
inline constexpr std::array<Bounds, 5> utilityCells {{
    { 4, 254, 200, 28 }, { 204, 254, 100, 28 },
    { 304, 254, 100, 28 }, { 404, 254, 60, 28 },
    { 464, 254, 180, 28 }
}};
inline constexpr Bounds multibandPanel { 0, 282, 648, 168 };
inline constexpr Bounds rta { 4, 286, 640, 160 };

static_assert (main.x + 400 == meters.x);
static_assert (meters.x + meters.width == response.x);
static_assert (response.x + response.width == main.x + main.width);
static_assert (utilityCells[2].x + utilityCells[2].width == meters.x);
static_assert (utilityCells[3].x + utilityCells[3].width == response.x);
static_assert (rta.x == frame && rta.width == designWidth - 2 * frame);
static_assert (rta.y + rta.height == expandedHeight - frame);
} // namespace dd::ui
