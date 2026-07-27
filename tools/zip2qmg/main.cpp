/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "bootanim_zip.h"
#include "desc.h"

#include "qmgc/encode.h"
#include "qmgc/types.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

void print_usage() {
    std::fprintf(stderr, "usage: zip2qmg bootanimation.zip out.qmg [--delay N]\n");
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
            "are dropped -- see tools/zip2qmg/README.md)\n");
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

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        print_usage();
        return 1;
    }

    std::string zip_path = argv[1];
    std::string qmg_path = argv[2];

    int delay = 0;
    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "--delay") == 0 && i + 1 < argc) {
            delay = std::atoi(argv[++i]);
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", argv[i]);
            print_usage();
            return 1;
        }
    }

    try {
        return run_encode(zip_path, qmg_path, delay);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
