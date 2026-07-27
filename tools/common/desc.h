/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOTANIM_QMG_DESC_H
#define BOOTANIM_QMG_DESC_H

#include <string>
#include <vector>

namespace bootanim {

// One "p"/"c" line from desc.txt.
struct Part {
    char type = 'p';        // 'p' (play) or 'c' (continue-till-boot-done)
    int loop_count = 1;      // 0 = infinite
    int pause_frames = 0;
    std::string path;        // e.g. "part0"
    std::string bg_color_hex; // optional, e.g. "FFFFFF"; empty if unset
};

struct Desc {
    int width = 0;
    int height = 0;
    int fps = 0;
    std::vector<Part> parts;
};

// Parses a desc.txt file's contents. Throws std::runtime_error on
// malformed input.
Desc parse_desc(const std::string& text);

// Serializes back to desc.txt format.
std::string write_desc(const Desc& desc);

} // namespace bootanim

#endif
