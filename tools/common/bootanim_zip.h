/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOTANIM_QMG_ZIP_H
#define BOOTANIM_QMG_ZIP_H

#include "desc.h"
#include "qmgc/types.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bootanim {

// Reads a bootanimation.zip (path to the .zip itself), parses desc.txt,
// and returns every frame across every part in playback order. A part's
// loop_count is honored except 0 ("infinite"), which is treated as a
// single pass -- there is no lossless finite mapping, and this is
// reported to the caller via `looped_infinite` so the CLI can warn.
//
// Throws std::runtime_error on malformed zips/desc.txt/images.
struct LoadedBootanim {
    Desc desc;
    std::vector<qmgc::Image> frames; // flattened, in playback order
    bool had_infinite_loop = false;  // true if any part used loop_count == 0
    bool had_multiple_parts = false; // true if desc.parts.size() > 1
};
LoadedBootanim load_bootanim_zip(const std::string& zip_path);

// Memory-bounded variant of load_bootanim_zip. The archive is extracted to a
// temp dir (disk, not RAM) and only the *paths* of the frames are collected;
// no image is decoded until load_frame() is called for it. A part that loops
// N times simply lists its files N times, so repeats cost nothing in memory.
// The temp dir lives as long as this object (or any copy of it) does.
struct BootanimSource {
    Desc desc;
    std::vector<std::string> frame_paths; // playback order, repeats included
    bool had_infinite_loop = false;
    bool had_multiple_parts = false;
    std::shared_ptr<void> tmp_guard; // keeps the extracted files alive

    size_t frame_count() const { return frame_paths.size(); }
    // Decodes frame `index` on demand. Thread-safe (read-only on shared state).
    qmgc::Image load_frame(size_t index) const;
};
BootanimSource open_bootanim_zip(const std::string& zip_path);

// A single decoded qmg frame in RGB565, as produced by qmgd.
struct Rgb565Frame {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgb565; // width*height*2 bytes, tightly packed
    int delay_centiseconds = 0;
};

// Writes `frames` out as a single-part bootanimation.zip (part0/, PNGs
// numbered 0000.png, 0001.png, ...) plus a generated desc.txt, stored
// uncompressed as required by the Android bootanimation loader.
void write_bootanim_zip(const std::string& zip_path, int width, int height,
                         int fps, bool infinite_loop,
                         const std::vector<Rgb565Frame>& frames);

} // namespace bootanim

#endif
