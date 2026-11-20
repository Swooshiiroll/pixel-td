#pragma once

#include <string>
#include <string_view>

#include "core/gen_params.hpp"

namespace pixeltd {

// Generator presets (issue #21): every GenParams value as `key = value` text, one per line, with
// `#` comments. Settings only: a preset never holds a seed, so it's a style to try across seeds.
// Pure string handling; reading and writing files is the front end's job.
//
//   height.step = 64
//   height.curve = smoothstep        (linear | smoothstep | quintic)
//   height.shape = normal            (normal | ridged | billowed)
//   mesa_feather_cells = 8
//   core_area.x = 106
//   spawn_waves = 1, 10, 20

// Every value, in a fixed order, with a comment line per section.
[[nodiscard]] std::string to_preset(const GenParams& p);

// Reads a preset over `p`: keys it doesn't mention keep their current values. Each unknown key,
// bad value or clamped value adds a line to `errors`. The result is always validated. Returns
// true if there were no errors.
bool from_preset(std::string_view text, GenParams& p, std::string& errors);

// The names used for the enums in presets (and the tuning panel).
[[nodiscard]] const char* curve_name(Curve c) noexcept;
[[nodiscard]] const char* shape_name(Shape s) noexcept;
[[nodiscard]] const char* basis_name(Basis b) noexcept;

} // namespace pixeltd
