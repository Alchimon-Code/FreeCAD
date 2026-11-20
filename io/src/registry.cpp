// SPDX-License-Identifier: LGPL-2.1-or-later
#include "detail.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <random>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace pd::io {
IoResult failed(IoStatus status, std::string why) {
    IoResult r;
    r.status = status;
    r.diagnostics.push_back({IoDiagnostic::Level::Error, 0, {}, std::move(why)});
    return r;
}
void IoResult::warn(std::size_t line, std::string subject, std::string message) {
    for (auto& d : diagnostics)
        if (d.subject == subject && d.message == message) {
            ++d.count;
            return;
        }
    if (diagnostics.size() >= 128) {
        if (diagnostics.back().subject == "...")
            ++diagnostics.back().count;
        else
            diagnostics.push_back({IoDiagnostic::Level::Warning, line, "...", "additional warnings"});
        return;
    }
    diagnostics.push_back({IoDiagnostic::Level::Warning, line, std::move(subject), std::move(message)});
}
std::ofstream output(const fs::path& p) {
    std::ofstream f;
    f.exceptions(std::ios::badbit | std::ios::failbit);
    f.open(p, std::ios::binary);
    f.imbue(std::locale::classic());
    return f;
}
TempDir::TempDir(const fs::path& parent) {
    std::random_device random;
    for (int i = 0; i < 32; ++i) {
        path = parent / (".planedraft-" + hex(random()) + hex(random()));
#ifdef _WIN32
        if (CreateDirectoryW(path.c_str(), nullptr))
            return;
        if (GetLastError() != ERROR_ALREADY_EXISTS)
            break;
#else
        if (::mkdir(path.c_str(), 0700) == 0)
            return;
        if (errno != EEXIST)
            break;
#endif
    }
    throw Failure(IoStatus::FileError, "cannot create private temporary directory");
}
TempDir::~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
}
std::string num(double v) {
    if (!std::isfinite(v))
        throw Failure(IoStatus::InvalidData, "non-finite output coordinate");
    char b[64];
    auto r = std::to_chars(b, b + sizeof b, v == 0 ? 0 : v);
    return {b, r.ptr};
}
std::string hex(unsigned long long n, unsigned width) {
    const char* digits = "0123456789ABCDEF";
    std::string s;
    do {
        s += digits[n % 16];
        n /= 16;
    } while (n || s.size() < width);
    std::reverse(s.begin(), s.end());
    return s;
}
IoResult IFormatHandler::writeLayout(const fs::path&, const Document&, const Layout&) const {
    return failed(IoStatus::Unsupported, "not a sheet export format");
}
IoResult IFormatHandler::read(const fs::path&, Document&) const {
    return failed(IoStatus::Unsupported, "export-only format");
}
IoRegistry::IoRegistry(DwgTools tools, DxfVersion version) {
    add(".dxf", std::make_unique<DxfHandler>(version));
    add(".dwg", dwgHandler(std::move(tools)));
    add(".svg", std::make_unique<SvgExporter>());
    add(".pdf", std::make_unique<PdfExporter>());
}
void IoRegistry::add(std::string ext, std::unique_ptr<IFormatHandler> h) {
    for (char& c : ext)
        if (c >= 'A' && c <= 'Z')
            c += 32;
    if (!h || ext.size() < 2 || ext[0] != '.' ||
        ext.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789", 1) != ext.npos ||
        !handlers_.emplace(std::move(ext), std::move(h)).second)
        throw std::invalid_argument("invalid or duplicate format");
}
static std::string extension(const fs::path& p) {
    auto u = p.extension().u8string();
    std::string s(u.begin(), u.end());
    for (char& c : s)
        if (c >= 'A' && c <= 'Z')
            c += 32;
    return s;
}
IoResult IoRegistry::read(const fs::path& path, Document& doc) const {
    try {
        auto it = handlers_.find(extension(path));
        if (it == handlers_.end())
            return failed(IoStatus::Unsupported, "unknown file extension");
        auto absolute = fs::absolute(path);
        auto staged = std::make_unique<Document>();
        auto r = it->second->read(absolute, *staged);
        if (r.ok()) {
            r.entities = staged->size();
            r.layers = staged->layerIds().size();
            doc.replaceContents(std::move(staged));
        }
        return r;
    } catch (const std::exception& e) {
        return failed(IoStatus::FileError, e.what());
    }
}
IoResult IoRegistry::write(const fs::path& path, const Document& doc, const Layout* sheet) const {
    try {
        auto ext = extension(path);
        auto it = handlers_.find(ext);
        if (it == handlers_.end())
            return failed(IoStatus::Unsupported, "unknown file extension");
        auto absolute = fs::absolute(path);
        TempDir temp(absolute.parent_path());
        auto file = temp.path / ("drawing" + ext);
        auto r = sheet ? it->second->writeLayout(file, doc, *sheet) : it->second->write(file, doc);
        if (!r.ok())
            return r;
        if (!fs::is_regular_file(file) || !fs::file_size(file))
            return failed(IoStatus::FileError, "writer produced no output");
        // No allocation or model query after the atomic publication.
        r.entities = doc.size();
        r.layers = doc.layerIds().size();
#ifdef _WIN32
        if (!MoveFileExW(file.c_str(), absolute.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::system_error(GetLastError(), std::system_category(), "replace drawing");
#else
        fs::rename(file, absolute);
#endif
        return r;
    } catch (const std::exception& e) {
        return failed(IoStatus::FileError, e.what());
    }
}
} // namespace pd::io
