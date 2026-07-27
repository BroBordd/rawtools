/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "desc.h"

#include <sstream>
#include <stdexcept>
#include <vector>

namespace bootanim {

namespace {

std::vector<std::string> split_ws(const std::string& line) {
    std::vector<std::string> tokens;
    std::istringstream iss(line);
    std::string tok;
    while (iss >> tok) tokens.push_back(tok);
    return tokens;
}

std::string strip_comment(const std::string& line) {
    auto pos = line.find('#');
    if (pos == std::string::npos) return line;
    return line.substr(0, pos);
}

} // namespace

Desc parse_desc(const std::string& text) {
    std::istringstream stream(text);
    std::string line;
    Desc desc;
    bool have_header = false;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        std::string stripped = strip_comment(line);
        std::vector<std::string> tokens = split_ws(stripped);
        if (tokens.empty()) continue;

        if (!have_header) {
            if (tokens.size() < 3) {
                throw std::runtime_error(
                    "parse_desc: first line must be 'WIDTH HEIGHT FPS'");
            }
            desc.width = std::stoi(tokens[0]);
            desc.height = std::stoi(tokens[1]);
            desc.fps = std::stoi(tokens[2]);
            have_header = true;
            continue;
        }

        if (tokens[0] == "dynamic_colors") {
            // No qmg equivalent -- dropped. Not fatal: the animation
            // still has a well-defined non-dynamic-color rendering.
            continue;
        }

        if (tokens[0].size() == 1 && (tokens[0][0] == 'p' || tokens[0][0] == 'c')) {
            if (tokens.size() < 4) {
                throw std::runtime_error(
                    "parse_desc: part line needs at least TYPE COUNT PAUSE PATH: " + line);
            }
            Part part;
            part.type = tokens[0][0];
            part.loop_count = std::stoi(tokens[1]);
            part.pause_frames = std::stoi(tokens[2]);
            part.path = tokens[3];
            if (tokens.size() >= 5 && !tokens[4].empty() && tokens[4] != "CLOCK") {
                part.bg_color_hex = tokens[4];
            }
            desc.parts.push_back(std::move(part));
            continue;
        }

        // Unrecognized line (font/progress overlay declarations, etc) --
        // not needed for the qmg conversion, skip rather than fail.
    }

    if (!have_header) {
        throw std::runtime_error("parse_desc: empty or missing header line");
    }
    if (desc.parts.empty()) {
        throw std::runtime_error("parse_desc: no part ('p'/'c') lines found");
    }

    return desc;
}

std::string write_desc(const Desc& desc) {
    std::ostringstream out;
    out << desc.width << " " << desc.height << " " << desc.fps << "\n";
    for (const auto& part : desc.parts) {
        out << part.type << " " << part.loop_count << " " << part.pause_frames
            << " " << part.path;
        if (!part.bg_color_hex.empty()) out << " #" << part.bg_color_hex;
        out << "\n";
    }
    return out.str();
}

} // namespace bootanim
