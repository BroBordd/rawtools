/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "bootanim_zip.h"

#include "qmgc/image.h"

#include <zip.h>
#include <png.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <unistd.h>
#include <vector>

namespace bootanim {

namespace {

// RAII temp directory under $TMPDIR (falls back to /tmp), removed
// recursively on destruction. Not hardcoded to /tmp because that path
// doesn't exist on some platforms (e.g. Termux on Android).
struct TempDir {
    std::string path;
    TempDir() {
        const char* base = std::getenv("TMPDIR");
        if (!base || !*base) base = "/tmp";
        std::string tmpl = std::string(base) + "/bootanim_qmg_XXXXXX";
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        char* d = mkdtemp(buf.data());
        if (!d) throw std::runtime_error("mkdtemp failed under " + std::string(base));
        path = d;
    }
    ~TempDir() {
        std::string cmd = "rm -rf '" + path + "'";
        if (std::system(cmd.c_str()) != 0) {
            std::fprintf(stderr, "warning: failed to clean up temp dir %s\n", path.c_str());
        }
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

void mkdir_p(const std::string& path) {
    std::string cmd = "mkdir -p '" + path + "'";
    if (std::system(cmd.c_str()) != 0) {
        throw std::runtime_error("failed to create directory: " + path);
    }
}

std::string dirname_of(const std::string& path) {
    auto pos = path.find_last_of('/');
    return pos == std::string::npos ? std::string(".") : path.substr(0, pos);
}

// Extracts every entry of `zip_path` into `dest_dir`, preserving relative
// paths. Throws on error.
void extract_zip(const std::string& zip_path, const std::string& dest_dir) {
    int err = 0;
    zip_t* archive = zip_open(zip_path.c_str(), ZIP_RDONLY, &err);
    if (!archive) {
        zip_error_t error;
        zip_error_init_with_code(&error, err);
        std::string msg = zip_error_strerror(&error);
        zip_error_fini(&error);
        throw std::runtime_error("failed to open zip " + zip_path + ": " + msg);
    }

    zip_int64_t n = zip_get_num_entries(archive, 0);
    for (zip_int64_t i = 0; i < n; ++i) {
        const char* name = zip_get_name(archive, i, 0);
        if (!name) continue;
        std::string entry_name = name;
        if (!entry_name.empty() && entry_name.back() == '/') {
            // Directory entry.
            mkdir_p(dest_dir + "/" + entry_name);
            continue;
        }

        std::string out_path = dest_dir + "/" + entry_name;
        mkdir_p(dirname_of(out_path));

        zip_file_t* zf = zip_fopen_index(archive, i, 0);
        if (!zf) {
            zip_close(archive);
            throw std::runtime_error("failed to open zip entry: " + entry_name);
        }

        FILE* out = std::fopen(out_path.c_str(), "wb");
        if (!out) {
            zip_fclose(zf);
            zip_close(archive);
            throw std::runtime_error("failed to create file: " + out_path);
        }

        char buf[65536];
        zip_int64_t got;
        while ((got = zip_fread(zf, buf, sizeof(buf))) > 0) {
            std::fwrite(buf, 1, static_cast<size_t>(got), out);
        }
        std::fclose(out);
        zip_fclose(zf);
        if (got < 0) {
            zip_close(archive);
            throw std::runtime_error("failed reading zip entry: " + entry_name);
        }
    }

    zip_close(archive);
}

std::string read_file_text(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) throw std::runtime_error("cannot open " + path);
    std::string text;
    char buf[4096];
    size_t got;
    while ((got = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        text.append(buf, got);
    }
    std::fclose(f);
    return text;
}

// Lists PNG (or JPEG) files directly inside `dir`, sorted lexically
// (which matches numeric order for the zero-padded names the spec
// requires, e.g. 0000.png, 0001.png, ...).
std::vector<std::string> list_frame_files(const std::string& dir) {
    std::vector<std::string> files;
    std::string cmd = "ls -1 '" + dir + "' 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) throw std::runtime_error("failed to list directory: " + dir);
    char line[4096];
    while (std::fgets(line, sizeof(line), pipe)) {
        std::string name = line;
        while (!name.empty() && (name.back() == '\n' || name.back() == '\r')) name.pop_back();
        if (name.empty()) continue;
        auto lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.size() >= 4 &&
            (lower.compare(lower.size() - 4, 4, ".png") == 0 ||
             lower.compare(lower.size() - 4, 4, ".jpg") == 0)) {
            files.push_back(name);
        } else if (lower.size() >= 5 && lower.compare(lower.size() - 5, 5, ".jpeg") == 0) {
            files.push_back(name);
        }
    }
    pclose(pipe);
    std::sort(files.begin(), files.end());
    return files;
}

// Converts RGB565 (as decoded by qmgd) to a plain RGB8 buffer.
std::vector<uint8_t> rgb565_to_rgb8(const uint8_t* data, int width, int height) {
    std::vector<uint8_t> out(static_cast<size_t>(width) * height * 3);
    for (int i = 0; i < width * height; ++i) {
        uint16_t v = static_cast<uint16_t>(data[i * 2]) | (static_cast<uint16_t>(data[i * 2 + 1]) << 8);
        int r = (v >> 11) & 0x1f;
        int g = (v >> 5) & 0x3f;
        int b = v & 0x1f;
        out[i * 3 + 0] = static_cast<uint8_t>((r * 255 + 15) / 31);
        out[i * 3 + 1] = static_cast<uint8_t>((g * 255 + 31) / 63);
        out[i * 3 + 2] = static_cast<uint8_t>((b * 255 + 15) / 31);
    }
    return out;
}

void write_png(const std::string& path, const uint8_t* rgb8, int width, int height) {
    FILE* fp = std::fopen(path.c_str(), "wb");
    if (!fp) throw std::runtime_error("cannot create " + path);

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        std::fclose(fp);
        throw std::runtime_error("png_create_write_struct failed");
    }
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, nullptr);
        std::fclose(fp);
        throw std::runtime_error("png_create_info_struct failed");
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        std::fclose(fp);
        throw std::runtime_error("libpng error writing " + path);
    }

    png_init_io(png, fp);
    png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    std::vector<png_bytep> rows(height);
    for (int y = 0; y < height; ++y) {
        rows[y] = const_cast<png_bytep>(rgb8 + static_cast<size_t>(y) * width * 3);
    }
    png_write_image(png, rows.data());
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    std::fclose(fp);
}

// Adds a file's contents to a zip archive with STORE (no compression),
// as required by the bootanimation spec.
void add_stored_file(zip_t* archive, const std::string& entry_name, const std::string& src_path) {
    zip_source_t* src = zip_source_file(archive, src_path.c_str(), 0, 0);
    if (!src) throw std::runtime_error("zip_source_file failed for " + src_path);
    zip_int64_t idx = zip_file_add(archive, entry_name.c_str(), src, ZIP_FL_ENC_UTF_8);
    if (idx < 0) {
        zip_source_free(src);
        throw std::runtime_error("zip_file_add failed for " + entry_name);
    }
    zip_set_file_compression(archive, idx, ZIP_CM_STORE, 0);
}

} // namespace

LoadedBootanim load_bootanim_zip(const std::string& zip_path) {
    TempDir tmp;
    extract_zip(zip_path, tmp.path);

    std::string desc_path = tmp.path + "/desc.txt";
    Desc desc = parse_desc(read_file_text(desc_path));

    LoadedBootanim result;
    result.desc = desc;
    result.had_multiple_parts = desc.parts.size() > 1;

    for (const auto& part : desc.parts) {
        std::string part_dir = tmp.path + "/" + part.path;
        std::vector<std::string> files = list_frame_files(part_dir);
        if (files.empty()) {
            throw std::runtime_error("part '" + part.path + "' has no PNG/JPEG frames");
        }

        std::vector<qmgc::Image> part_frames;
        part_frames.reserve(files.size());
        for (const auto& fname : files) {
            part_frames.push_back(qmgc::load_image(part_dir + "/" + fname));
        }

        int repeats = part.loop_count;
        if (repeats == 0) {
            result.had_infinite_loop = true;
            repeats = 1; // lossy: flatten "infinite" to a single pass
        }
        for (int r = 0; r < repeats; ++r) {
            for (const auto& img : part_frames) {
                result.frames.push_back(img);
            }
        }
    }

    return result;
}

void write_bootanim_zip(const std::string& zip_path, int width, int height,
                         int fps, bool infinite_loop,
                         const std::vector<Rgb565Frame>& frames) {
    TempDir tmp;
    std::string part_dir = tmp.path + "/part0";
    mkdir_p(part_dir);

    for (size_t i = 0; i < frames.size(); ++i) {
        const Rgb565Frame& f = frames[i];
        std::vector<uint8_t> rgb8 = rgb565_to_rgb8(f.rgb565.data(), f.width, f.height);
        char name[64];
        std::snprintf(name, sizeof(name), "%04zu.png", i);
        write_png(part_dir + "/" + name, rgb8.data(), f.width, f.height);
    }

    Desc desc;
    desc.width = width;
    desc.height = height;
    desc.fps = fps;
    Part part;
    part.type = 'p';
    part.loop_count = infinite_loop ? 0 : 1;
    part.pause_frames = 0;
    part.path = "part0";
    desc.parts.push_back(part);

    std::string desc_path = tmp.path + "/desc.txt";
    FILE* df = std::fopen(desc_path.c_str(), "wb");
    if (!df) throw std::runtime_error("cannot write " + desc_path);
    std::string desc_text = write_desc(desc);
    std::fwrite(desc_text.data(), 1, desc_text.size(), df);
    std::fclose(df);

    // Remove any existing output so zip_open's CREATE doesn't merge with
    // stale contents from a previous run.
    std::remove(zip_path.c_str());

    int err = 0;
    zip_t* archive = zip_open(zip_path.c_str(), ZIP_CREATE | ZIP_EXCL, &err);
    if (!archive) {
        zip_error_t error;
        zip_error_init_with_code(&error, err);
        std::string msg = zip_error_strerror(&error);
        zip_error_fini(&error);
        throw std::runtime_error("failed to create zip " + zip_path + ": " + msg);
    }

    add_stored_file(archive, "desc.txt", desc_path);
    for (size_t i = 0; i < frames.size(); ++i) {
        char name[64];
        std::snprintf(name, sizeof(name), "%04zu.png", i);
        add_stored_file(archive, std::string("part0/") + name, part_dir + "/" + name);
    }

    if (zip_close(archive) < 0) {
        throw std::runtime_error("failed to finalize zip " + zip_path);
    }
}

} // namespace bootanim
