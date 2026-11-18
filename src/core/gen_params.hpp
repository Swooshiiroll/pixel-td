#pragma once

namespace pixeltd {

// Every number the map generator uses. All are (start) values (design/world.md): starting points
// tuned from map shots now, and by the live-tuning file once the test arena exists.
struct GenParams {
    // --- High ground (plateaus): two octaves of value noise, the top share becomes high ground.
    int high_noise_step = 16;    // cells between lattice points of the coarse octave
    int high_share_min_pct = 12; // target share of the map, picked per attempt
    int high_share_max_pct = 22;
    int high_smooth_steps = 2;   // majority smoothing passes
    int min_plateau_cells = 40;  // smaller plateaus are flattened
    int min_hole_cells = 12;     // smaller low pockets inside plateaus are filled
    int high_final_min_pct = 10; // accepted share after clean-up, or the attempt is rejected
    int high_final_max_pct = 30;

    // --- Cliffs and mountains (tall blockers): the same method on low ground, at a smaller scale.
    int cliff_noise_step = 10;
    int cliff_share_min_pct = 3;
    int cliff_share_max_pct = 6;
    int min_cliff_cells = 10;
    int cliff_final_max_pct = 9;

    // --- Rocks (low blockers): rock fields (pieces scattered around a centre) plus loose rocks, on
    // low ground. Fields are what make line of sight matter.
    int rock_fields_min = 7;
    int rock_fields_max = 12;
    int rock_field_pieces_min = 9;
    int rock_field_pieces_max = 16;
    int rock_field_radius = 4;
    int rock_loose_min = 15;
    int rock_loose_max = 30;

    // --- Core zone and core.
    int zone_w = 26;
    int zone_h = 15;
    int zone_tries = 32;      // zone positions tried per terrain attempt
    int min_legal_spots = 40; // legal 6x6 core spots the zone must offer
    int min_spawn_dist = 60;  // path cells from the core to the farthest edge entry, at least
    int ramp_max = 6;         // a high-ground core's ramp stays within this many cells of it

    // --- Retries.
    int stage1_attempts = 64; // then the known-safe fallback layout
};

} // namespace pixeltd
