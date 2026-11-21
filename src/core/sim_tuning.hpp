#pragma once

#include <string>
#include <string_view>

#include "core/sim_params.hpp"

namespace pixeltd {

// The arena's live-tuning file (#11 phase 2; design/test-arena.md): every SimParams value as
// `key = value` text, one per line, with `#` comments. The game reloads it when it changes on disk
// and the tuning panel can save over it. Pure string handling; the front end reads and writes it.
//
//   walker.speed_px_s = 32
//   tower.light.range_px = 72
//   routes.delay_ticks = 20

// Every value, in a fixed order, with a comment line per section.
[[nodiscard]] std::string to_tuning(const SimParams& p);

// Reads a tuning text over `p`: keys it doesn't mention keep their current values. Each unknown
// key, bad value or clamped value adds a line to `errors`. The result is always validated.
// Returns true if there were no errors.
bool from_tuning(std::string_view text, SimParams& p, std::string& errors);

} // namespace pixeltd
