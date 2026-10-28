/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "bootanim_zip.h"
#include "desc.h"

#include "qmgc/encode.h"
#include "qmgc/image.h"
#include "qmgc/types.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>

namespace {

void print_usage() {
    std::fprintf(stderr,
        "usage: zip2qmg bootanimation.zip out.qmg [--delay N] [--fps N] "
        "[--resolution WxH] [--fit stretch|pad] [--pad-color RRGGBB]\n");
}

// Parses "WxH" (e.g. "1080x2400") into (w, h). Returns false on malformed input.
bool parse_resolution(const std::string& s, int& w, int& h) {
    auto x_pos = s.find('x');
    if (x_pos == std::string::npos) x_pos = s.find('X');
    if (x_pos == std::string::npos) return false;
    std::string ws = s.substr(0, x_pos);
    std::string hs = s.substr(x_pos + 1);
    if (ws.empty() || hs.empty()) return false;
    w = std::atoi(ws.c_str());
    h = std::atoi(hs.c_str());
    return w > 0 && h > 0;
}

// Parses "RRGGBB" hex into a 0-255 rgb triple. Returns false on malformed input.
bool parse_hex_color(const std::string& s, uint8_t& r, uint8_t& g, uint8_t& b) {
    if (s.size() != 6) return false;
    for (char c : s) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    }
    r = static_cast<uint8_t>(std::stoi(s.substr(0, 2), nullptr, 16));
    g = static_cast<uint8_t>(std::stoi(s.substr(2, 2), nullptr, 16));
    b = static_cast<uint8_t>(std::stoi(s.substr(4, 2), nullptr, 16));
    return true;
}

// Scales `src` to fit within tw x th preserving aspect ratio (no cropping),
// then centers it on a tw x th canvas filled with (pr, pg, pb), padding
// out whatever margin is left over (top/bottom or left/right).
qmgc::Image scale_image_pad(const qmgc::Image& src, int tw, int th,
                             uint8_t pr, uint8_t pg, uint8_t pb) {
    if (tw < 1 || th < 1) {
        throw qmgc::QmgError("invalid target dimensions " + std::to_string(tw) + "x" + std::to_string(th));
    }

    // Fit `src` inside tw x th without stretching past either bound.
    double scale = std::min(static_cast<double>(tw) / src.width, static_cast<double>(th) / src.height);
    int fit_w = std::max(1, static_cast<int>(src.width * scale));
    int fit_h = std::max(1, static_cast<int>(src.height * scale));
    fit_w = std::min(fit_w, tw);
    fit_h = std::min(fit_h, th);

    qmgc::Image fitted = qmgc::scale_image(src, fit_w, fit_h);

    if (fit_w == tw && fit_h == th) return fitted;

    int off_x = (tw - fit_w) / 2;
    int off_y = (th - fit_h) / 2;

    qmgc::Image out;
    out.width = tw;
    out.height = th;
    out.rgb.resize(static_cast<size_t>(tw) * th * 3);
    for (int y = 0; y < th; ++y) {
        uint8_t* drow = out.rgb.data() + static_cast<size_t>(y) * tw * 3;
        for (int x = 0; x < tw; ++x) {
            int sx = x - off_x, sy = y - off_y;
            if (sx >= 0 && sx < fit_w && sy >= 0 && sy < fit_h) {
                const uint8_t* srow = fitted.rgb.data() + static_cast<size_t>(sy) * fit_w * 3;
                drow[x * 3 + 0] = srow[sx * 3 + 0];
                drow[x * 3 + 1] = srow[sx * 3 + 1];
                drow[x * 3 + 2] = srow[sx * 3 + 2];
            } else {
                drow[x * 3 + 0] = pr;
                drow[x * 3 + 1] = pg;
                drow[x * 3 + 2] = pb;
            }
        }
    }
    return out;
}

int run_encode(const std::string& zip_path, const std::string& qmg_path,
               int delay_override, int fps_override, int target_w, int target_h,
               bool pad_fit, uint8_t pad_r, uint8_t pad_g, uint8_t pad_b) {
    // Frames are NOT decoded up front: we only collect their paths and then
    // decode/scale/encode/write them a few at a time, so peak memory stays
    // roughly constant regardless of animation length.
    bootanim::BootanimSource src = bootanim::open_bootanim_zip(zip_path);

    if (src.frame_count() == 0) {
        std::fprintf(stderr, "error: bootanimation contains no frames\n");
        return 1;
    }
    if (src.had_multiple_parts) {
        std::fprintf(stderr,
            "warning: desc.txt has multiple parts; flattening into one "
            "linear qmg animation (per-part loop/pause/color settings "
            "are dropped -- see tools/zip2qmg/README.md)\n");
    }
    if (src.had_infinite_loop) {
        std::fprintf(stderr,
            "warning: a part used loop_count 0 (infinite); qmg animations "
            "have no direct 'infinite within a part' concept for a "
            "flattened multi-part sequence, so it was played once\n");
    }

    const bool resize = target_w > 0 && target_h > 0;
    if (resize) {
        std::fprintf(stderr, "%s frames to %dx%d for qmg output\n",
                     pad_fit ? "fitting (padded)" : "stretching", target_w, target_h);
    }

    // Decode (and optionally resize) a single frame on demand.
    auto get_frame = [&](int i) -> qmgc::Image {
        qmgc::Image img = src.load_frame(static_cast<size_t>(i));
        if (resize && (img.width != target_w || img.height != target_h)) {
            img = pad_fit ? scale_image_pad(img, target_w, target_h, pad_r, pad_g, pad_b)
                          : qmgc::scale_image(img, target_w, target_h);
        }
        return img;
    };

    int delay_cs;
    if (delay_override > 0) {
        delay_cs = delay_override;
    } else if (fps_override > 0) {
        delay_cs = 100 / fps_override;
    } else if (src.desc.fps > 0) {
        delay_cs = 100 / src.desc.fps;
    } else {
        delay_cs = 10;
    }

    FILE* out = std::fopen(qmg_path.c_str(), "wb");
    if (!out) {
        std::fprintf(stderr, "error: cannot write %s\n", qmg_path.c_str());
        return 1;
    }
    size_t bytes_written = 0;
    bool io_error = false;

    qmgc::StreamSink sink;
    sink.write = [&](const uint8_t* data, size_t n) {
        if (std::fwrite(data, 1, n, out) != n) {
            io_error = true;
            throw std::runtime_error("write to " + qmg_path + " failed (disk full?)");
        }
        bytes_written += n;
    };
    // Used by the automatic 0x0f -> 0x0b fallback: throw away what we wrote
    // and start the file over.
    sink.reset = [&]() {
        std::fflush(out);
        std::rewind(out);
        if (ftruncate(fileno(out), 0) != 0) {
            io_error = true;
            throw std::runtime_error("cannot truncate " + qmg_path);
        }
        bytes_written = 0;
    };

    bool ok = false;
    try {
        if (src.frame_count() == 1) {
            std::vector<uint8_t> still = qmgc::encode_still(get_frame(0));
            sink.write(still.data(), still.size());
        } else {
            qmgc::encode_animation_stream(static_cast<int>(src.frame_count()), get_frame,
                                          delay_cs, sink);
        }
        ok = true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s: %s\n", io_error ? "error" : "encode error", e.what());
    }

    if (std::fclose(out) != 0 && ok) {
        std::fprintf(stderr, "error: failed to finalize %s\n", qmg_path.c_str());
        ok = false;
    }
    if (!ok) {
        std::remove(qmg_path.c_str()); // don't leave a truncated .qmg behind
        return 1;
    }

    std::fprintf(stderr, "encoded %zu frame(s) -> %s (%zu bytes)\n",
                 src.frame_count(), qmg_path.c_str(), bytes_written);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        print_usage();
        return 1;
    }

    std::string zip_path = argv[1];
    std::string qmg_path = argv[2];

    int delay = 0;
    int fps = 0;
    int res_w = 0, res_h = 0;
    bool pad_fit = false;
    bool fit_given = false;
    uint8_t pad_r = 0, pad_g = 0, pad_b = 0; // default: black
    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "--delay") == 0 && i + 1 < argc) {
            delay = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--fps") == 0 && i + 1 < argc) {
            fps = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--resolution") == 0 && i + 1 < argc) {
            std::string res_arg = argv[++i];
            if (!parse_resolution(res_arg, res_w, res_h)) {
                std::fprintf(stderr, "error: --resolution expects WxH, e.g. 1080x2400\n");
                return 1;
            }
        } else if (std::strcmp(argv[i], "--fit") == 0 && i + 1 < argc) {
            std::string mode = argv[++i];
            fit_given = true;
            if (mode == "stretch") {
                pad_fit = false;
            } else if (mode == "pad") {
                pad_fit = true;
            } else {
                std::fprintf(stderr, "error: --fit expects 'stretch' or 'pad'\n");
                return 1;
            }
        } else if (std::strcmp(argv[i], "--pad-color") == 0 && i + 1 < argc) {
            std::string color_arg = argv[++i];
            if (!parse_hex_color(color_arg, pad_r, pad_g, pad_b)) {
                std::fprintf(stderr, "error: --pad-color expects RRGGBB hex, e.g. 000000\n");
                return 1;
            }
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", argv[i]);
            print_usage();
            return 1;
        }
    }

    if (delay > 0 && fps > 0) {
        std::fprintf(stderr, "error: pass only one of --delay or --fps, not both\n");
        return 1;
    }
    if (fps < 0 || delay < 0) {
        std::fprintf(stderr, "error: --delay/--fps must be positive\n");
        return 1;
    }
    if (fit_given && (res_w <= 0 || res_h <= 0)) {
        std::fprintf(stderr, "error: --fit requires --resolution WxH\n");
        return 1;
    }

    try {
        return run_encode(zip_path, qmg_path, delay, fps, res_w, res_h, pad_fit, pad_r, pad_g, pad_b);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
