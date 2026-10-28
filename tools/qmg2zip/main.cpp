/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "bootanim_zip.h"
#include "desc.h"

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
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

void print_usage() {
    std::fprintf(stderr, "usage: qmg2zip in.qmg bootanimation.zip [--fps N]\n");
}

// Read-only memory-mapped view of the input file. Pages are loaded on demand
// and are clean file-backed pages the kernel can drop under pressure, unlike
// a heap copy of the whole file.
class MappedFile {
public:
    explicit MappedFile(const std::string& path) {
        int fd = ::open(path.c_str(), O_RDONLY);
        if (fd < 0) throw std::runtime_error("cannot open " + path);
        struct stat st;
        if (fstat(fd, &st) != 0) {
            ::close(fd);
            throw std::runtime_error("cannot stat " + path);
        }
        size_ = static_cast<size_t>(st.st_size);
        if (size_ > 0) {
            void* p = mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd, 0);
            if (p == MAP_FAILED) {
                ::close(fd);
                throw std::runtime_error("cannot map " + path);
            }
            data_ = static_cast<const uint8_t*>(p);
        }
        ::close(fd);
    }
    ~MappedFile() {
        if (data_) munmap(const_cast<uint8_t*>(data_), size_);
    }
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;
    const uint8_t* data() const { return data_; }
    size_t size() const { return size_; }

private:
    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
};

int run_decode(const std::string& qmg_path, const std::string& zip_path, int fps_override) {
    std::unique_ptr<MappedFile> file;
    try {
        file.reset(new MappedFile(qmg_path));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    const uint8_t* data = file->data();
    const size_t data_size = file->size();

    // Memory is bounded to two frame buffers (current + reference): each
    // decoded frame is written straight to a PNG on disk and not kept.
    bootanim::BootanimWriter writer;
    std::vector<uint8_t> dst, ref;
    int ref_linesize = 0;
    size_t pos = 0;
    int width = 0, height = 0;
    bool no_repeat = false;
    int last_delay_cs = 10;

    try {
        while (pos < data_size) {
            QmageHeader h;
            if (qmage_parse_header(data + pos, data_size - pos, &h) < 0) {
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
                reclen = static_cast<uint32_t>(data_size - pos);
            }

            dst.assign(static_cast<size_t>(h.width) * h.height * 2, 0);
            int ret = qmage_decode_frame(data + pos, reclen, &h, dst.data(),
                                          ref.empty() ? nullptr : ref.data(), ref_linesize);
            if (ret < 0) {
                std::fprintf(stderr, "error: decode failed at frame %zu (encoder_mode=%d depth=%d)\n",
                             writer.frame_count(), h.encoder_mode, h.depth);
                return 1;
            }

            width = h.width;
            height = h.height;
            no_repeat = h.animation_no_repeat != 0;
            last_delay_cs = h.animation_delay_time > 0 ? h.animation_delay_time : last_delay_cs;

            writer.add_frame(dst.data(), h.width, h.height);

            ref.swap(dst); // current frame becomes the next reference
            ref_linesize = h.width * 2;
            pos += reclen;
        }

        if (writer.frame_count() == 0) {
            std::fprintf(stderr, "error: no frames decoded from %s\n", qmg_path.c_str());
            return 1;
        }

        int fps = fps_override > 0 ? fps_override
                                    : (last_delay_cs > 0 ? 100 / last_delay_cs : 10);
        writer.finish(zip_path, width, height, fps, !no_repeat);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }

    std::fprintf(stderr, "decoded %zu frame(s) -> %s\n", writer.frame_count(), zip_path.c_str());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        print_usage();
        return 1;
    }

    std::string qmg_path = argv[1];
    std::string zip_path = argv[2];

    int fps = 0;
    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "--fps") == 0 && i + 1 < argc) {
            fps = std::atoi(argv[++i]);
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", argv[i]);
            print_usage();
            return 1;
        }
    }

    try {
        return run_decode(qmg_path, zip_path, fps);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
