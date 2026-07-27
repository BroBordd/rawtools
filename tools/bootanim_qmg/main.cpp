/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "bootanim_zip.h"
#include "desc.h"

#include "qmgc/encode.h"
#include "qmgc/types.h"

// qmgd/include/qmgd/decode.h is missing extern "C" guards, so its C
// symbols get C++-mangled unless we wrap the include ourselves. Worth
// fixing upstream in qmgd so any C++ consumer doesn't have to do this.
extern "C" {
#include "qmgd/decode.h"
}

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

void print_usage() {
    std::fprintf(stderr,
        "usage: bootanim_qmg encode bootanimation.zip out.qmg [--delay N]\n"
        "       bootanim_qmg decode in.qmg bootanimation.zip [--fps N]\n");
}

std::vector<uint8_t> read_whole_file(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) throw std::runtime_error("cannot open " + path);
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    if (size > 0 && std::fread(buf.data(), 1, static_cast<size_t>(size), f) != static_cast<size_t>(size)) {
        std::fclose(f);
        throw std::runtime_error("short read on " + path);
    }
    std::fclose(f);
    return buf;
}

int run_encode(const std::string& zip_path, const std::string& qmg_path, int delay_override) {
    bootanim::LoadedBootanim loaded = bootanim::load_bootanim_zip(zip_path);

    if (loaded.frames.empty()) {
        std::fprintf(stderr, "error: bootanimation contains no frames\n");
        return 1;
    }
    if (loaded.had_multiple_parts) {
        std::fprintf(stderr,
            "warning: desc.txt has multiple parts; flattening into one "
            "linear qmg animation (per-part loop/pause/color settings "
            "are dropped -- see tools/bootanim_qmg/README.md)\n");
    }
    if (loaded.had_infinite_loop) {
        std::fprintf(stderr,
            "warning: a part used loop_count 0 (infinite); qmg animations "
            "have no direct 'infinite within a part' concept for a "
            "flattened multi-part sequence, so it was played once\n");
    }

    int delay_cs = delay_override > 0 ? delay_override
                                       : (loaded.desc.fps > 0 ? 100 / loaded.desc.fps : 10);

    std::vector<uint8_t> qmg_data;
    try {
        if (loaded.frames.size() == 1) {
            qmg_data = qmgc::encode_still(loaded.frames[0]);
        } else {
            qmg_data = qmgc::encode_animation(loaded.frames, delay_cs);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "encode error: %s\n", e.what());
        return 1;
    }

    FILE* out = std::fopen(qmg_path.c_str(), "wb");
    if (!out) {
        std::fprintf(stderr, "error: cannot write %s\n", qmg_path.c_str());
        return 1;
    }
    std::fwrite(qmg_data.data(), 1, qmg_data.size(), out);
    std::fclose(out);

    std::fprintf(stderr, "encoded %zu frame(s) -> %s (%zu bytes)\n",
                 loaded.frames.size(), qmg_path.c_str(), qmg_data.size());
    return 0;
}

int run_decode(const std::string& qmg_path, const std::string& zip_path, int fps_override) {
    std::vector<uint8_t> data;
    try {
        data = read_whole_file(qmg_path);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }

    std::vector<bootanim::Rgb565Frame> frames;
    std::vector<uint8_t> ref;
    int ref_linesize = 0;
    size_t pos = 0;
    int width = 0, height = 0;
    bool no_repeat = false;
    int last_delay_cs = 10;

    while (pos < data.size()) {
        QmageHeader h;
        if (qmage_parse_header(data.data() + pos, data.size() - pos, &h) < 0) {
            std::fprintf(stderr, "error: header parse failed at offset %zu\n", pos);
            return 1;
        }

        uint32_t reclen;
        if (h.mode) {
            if (h.transparency) {
                std::fprintf(stderr, "error: transparency frames not supported yet\n");
                return 1;
            }
            if (h.alpha_position <= h.header_size) {
                std::fprintf(stderr, "error: invalid alpha_position (%d <= header_size %d)\n",
                              h.alpha_position, h.header_size);
                return 1;
            }
            reclen = static_cast<uint32_t>(h.alpha_position);
        } else {
            reclen = static_cast<uint32_t>(data.size() - pos);
        }

        std::vector<uint8_t> dst(static_cast<size_t>(h.width) * h.height * 2);
        int ret = qmage_decode_frame(data.data() + pos, reclen, &h, dst.data(),
                                      ref.empty() ? nullptr : ref.data(), ref_linesize);
        if (ret < 0) {
            std::fprintf(stderr, "error: decode failed at frame %zu (encoder_mode=%d depth=%d)\n",
                         frames.size(), h.encoder_mode, h.depth);
            return 1;
        }

        width = h.width;
        height = h.height;
        no_repeat = h.animation_no_repeat != 0;
        last_delay_cs = h.animation_delay_time > 0 ? h.animation_delay_time : last_delay_cs;

        bootanim::Rgb565Frame frame;
        frame.width = h.width;
        frame.height = h.height;
        frame.rgb565 = dst;
        frame.delay_centiseconds = h.animation_delay_time;
        frames.push_back(std::move(frame));

        ref = dst;
        ref_linesize = h.width * 2;
        pos += reclen;
    }

    if (frames.empty()) {
        std::fprintf(stderr, "error: no frames decoded from %s\n", qmg_path.c_str());
        return 1;
    }

    int fps = fps_override > 0 ? fps_override
                                : (last_delay_cs > 0 ? 100 / last_delay_cs : 10);

    try {
        bootanim::write_bootanim_zip(zip_path, width, height, fps, !no_repeat, frames);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }

    std::fprintf(stderr, "decoded %zu frame(s) -> %s\n", frames.size(), zip_path.c_str());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        print_usage();
        return 1;
    }

    std::string mode = argv[1];
    std::string in_path = argv[2];
    std::string out_path = argv[3];

    int delay = 0;
    int fps = 0;
    for (int i = 4; i < argc; ++i) {
        if (std::strcmp(argv[i], "--delay") == 0 && i + 1 < argc) {
            delay = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--fps") == 0 && i + 1 < argc) {
            fps = std::atoi(argv[++i]);
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", argv[i]);
            print_usage();
            return 1;
        }
    }

    try {
        if (mode == "encode") {
            return run_encode(in_path, out_path, delay);
        } else if (mode == "decode") {
            return run_decode(in_path, out_path, fps);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }

    print_usage();
    return 1;
}
