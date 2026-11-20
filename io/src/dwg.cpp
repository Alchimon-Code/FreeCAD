// SPDX-License-Identifier: LGPL-2.1-or-later
#include "detail.hpp"
#include "dwg_paths.hpp"

#include <array>
#include <cstdlib>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace pd::io {
namespace {
namespace fs = std::filesystem;

// The converters are separate GPLv3 programs. Nothing from libredwg is linked, loaded or
// re-exported here: PlaneDraft only starts them and exchanges DXF files in a private directory.
fs::path executablePath() {
#ifdef _WIN32
    std::vector<wchar_t> buf(32768);
    auto n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (n && n < buf.size())
        return fs::path(std::wstring(buf.data(), n));
#else
    std::array<char, 4096> buf{};
    auto n = ::readlink("/proc/self/exe", buf.data(), buf.size() - 1);
    if (n > 0)
        return fs::path(std::string(buf.data(), static_cast<std::size_t>(n)));
#endif
    return {};
}

bool usable(const fs::path& p) {
    std::error_code ec;
    return !p.empty() && p.is_absolute() && fs::is_regular_file(p, ec);
}

// The pinned release is compiled in; an externally provided converter cannot be identified by
// the tools themselves (their --version is empty unless built from a git checkout).
std::string libreDwgName() {
#ifdef PD_LIBREDWG_VERSION
    std::string v = PD_LIBREDWG_VERSION;
    return v == "unknown" ? "external build" : v;
#else
    return "external build";
#endif
}
std::string toolName(const char* name) {
#ifdef _WIN32
    return std::string(name) + ".exe";
#else
    return name;
#endif
}

// Search beside the executable exactly like the installed layout (bin/ + tools/libredwg/),
// one level up for a development build, then PATH. No environment variable is required.
fs::path findTool(const char* name) {
    auto file = toolName(name);
    auto exe = executablePath();
    std::vector<fs::path> candidates;
    if (!exe.empty()) {
        auto dir = exe.parent_path();
        candidates = {dir / "tools" / "libredwg" / file, dir / ".." / "tools" / "libredwg" / file};
    }
    if (auto* path = std::getenv("PATH")) {
        std::stringstream stream(path);
        std::string entry;
#ifdef _WIN32
        constexpr char separator = ';';
#else
        constexpr char separator = ':';
#endif
        while (std::getline(stream, entry, separator))
            if (!entry.empty())
                candidates.push_back(fs::path(entry) / file);
    }
    for (const auto& candidate : candidates)
        if (usable(candidate))
            return fs::weakly_canonical(candidate);
    return {};
}

std::optional<std::pair<std::string, std::string>> dwgFormat(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::array<char, 6> head{};
    f.read(head.data(), head.size());
    if (f.gcount() != static_cast<std::streamsize>(head.size()) || std::string_view(head.data(), 4) != "AC10")
        return {}; // let the external converter produce the authoritative error
    static const std::map<std::string_view, std::string_view> versions{
        {"AC1003", "R2.10"}, {"AC1004", "R9"},   {"AC1006", "R10"},  {"AC1009", "R11/R12"},
        {"AC1012", "R13"},   {"AC1014", "R14"},  {"AC1015", "R2000"}, {"AC1018", "R2004"},
        {"AC1021", "R2007"}, {"AC1024", "R2010"}, {"AC1027", "R2013"}, {"AC1032", "R2018+"}};
    std::string code(head.data(), head.size());
    auto it = versions.find(code);
    if (it != versions.end())
        return std::pair{std::string(it->second), code};
    return std::pair{std::string("unknown"), code};
}

// Keep the converter's diagnostics useful but bounded: LibreDWG emits one line per unstable
// object class, which would flood the command line. Report the first few errors verbatim.
void reportWorkerOutput(IoResult& r, const std::string& output) {
    std::stringstream stream(output);
    std::string line, firstError, other;
    std::size_t errors = 0, warnings = 0;
    while (std::getline(stream, line)) {
        if (line.empty())
            continue;
        if (line.find("ERROR") != std::string::npos) {
            ++errors;
            if (firstError.empty() && errors <= 2)
                r.warn(0, "LibreDWG", line.substr(0, 240));
        } else if (line.find("Warning") != std::string::npos) {
            ++warnings;
        } else if (other.empty()) {
            other = line.substr(0, 240);
        }
    }
    // An unrecognised message is still the converter's own explanation of what it did.
    if (!errors && !warnings && !other.empty())
        r.warn(0, "LibreDWG", other);
    if (warnings)
        r.warn(0, "LibreDWG", "converter reported " + std::to_string(warnings) +
                                  " warning(s) about non-imported DWG objects or classes");
    if (errors > 2)
        r.warn(0, "LibreDWG", "converter reported " + std::to_string(errors) + " errors");
}
} // namespace

DwgTools configuredDwgTools() {
    DwgTools t;
    // Configure-time hints first, but only if they still exist; otherwise discover at runtime.
    fs::path reader(PD_DWG_READER), writer(PD_DWG_WRITER);
    if (usable(reader))
        t.dwg2dxf = reader;
    if (usable(writer))
        t.dxf2dwg = writer;
    if (t.dwg2dxf.empty())
        t.dwg2dxf = findTool("dwg2dxf");
    if (t.dxf2dwg.empty())
        t.dxf2dwg = findTool("dxf2dwg");
    return t;
}

namespace {
class DwgHandler final : public IFormatHandler {
    DwgTools tools_;

    // Import through an ASCII-only DXF: LibreDWG's reader copies raw bytes rather than applying
    // $DWGCODEPAGE, so a codepage file would lose non-Latin text. \U+XXXX escapes are exact.
    static constexpr DxfVersion exchangeVersion = DxfVersion::R2000;

  public:
    explicit DwgHandler(DwgTools t) : tools_(std::move(t)) {}

    IoResult read(const fs::path& p, Document& d) const override {
#ifdef PD_ENABLE_LIBREDWG
        return checked([&](IoResult& r) {
            if (!usable(tools_.dwg2dxf))
                throw Failure(IoStatus::BackendUnavailable,
                              "dwg2dxf was not found in tools/libredwg or PATH");
            auto format = dwgFormat(p);
            TempDir dir(fs::temp_directory_path());
            auto tmp = dir.path / "exchange.dxf";
            auto msg = runWorker(tools_.dwg2dxf, p, tmp, dir.path); // native DXF version
            DxfHandler exchange(exchangeVersion, true, true);
            auto converted = exchange.read(tmp, d);
            for (auto& diag : converted.diagnostics) {
                if (diag.level == IoDiagnostic::Level::Error)
                    throw Failure(converted.status == IoStatus::Ok ? IoStatus::InvalidData
                                                                   : converted.status,
                                  diag.message, diag.line, diag.subject);
                r.warn(diag.line, diag.subject, diag.message);
            }
            r.entities = converted.entities;
            r.layers = converted.layers;
            r.skipped = converted.skipped;
            if (format)
                r.warn(0, "DWG", format->first + " (" + format->second + ") read through " +
                                    std::string("LibreDWG ") + libreDwgName() +
                                    " in a separate dwg2dxf process");
            else
                r.warn(0, "DWG", "unrecognized DWG version signature; attempting conversion anyway");
            if (!msg.empty())
                reportWorkerOutput(r, msg);
            if (r.skipped)
                r.warn(0, "DWG", std::to_string(r.skipped) +
                                    " entity record(s) skipped; see the per-type lines above");
        });
#else
        (void)p;
        (void)d;
        return failed(IoStatus::BackendUnavailable,
                      "DWG bridge disabled at build time (PD_ENABLE_LIBREDWG=OFF)");
#endif
    }

    IoResult write(const fs::path& p, const Document& d) const override {
#ifdef PD_ENABLE_LIBREDWG
        return checked([&](IoResult& r) {
            if (!usable(tools_.dxf2dwg))
                throw Failure(IoStatus::BackendUnavailable,
                              "dxf2dwg was not found in tools/libredwg or PATH");
            TempDir dir(fs::temp_directory_path());
            auto tmp = dir.path / "exchange.dxf";
            DxfHandler exchange(exchangeVersion, true);
            auto written = exchange.write(tmp, d);
            if (!written.ok()) {
                r.status = written.status;
                r.diagnostics = written.diagnostics;
                return;
            }
            auto msg = runWorker(tools_.dxf2dwg, tmp, p, dir.path, "r2000");
            if (msg.find("ERROR:") != msg.npos)
                throw Failure(IoStatus::BackendError, "dxf2dwg: " + msg.substr(0, 512));
            r.warn(0, "DWG", std::string("written as DWG R2000 through LibreDWG ") + libreDwgName() +
                                " in a separate dxf2dwg process");
            if (!msg.empty())
                reportWorkerOutput(r, msg);
        });
#else
        (void)p;
        (void)d;
        return failed(IoStatus::BackendUnavailable,
                      "DWG bridge disabled at build time (PD_ENABLE_LIBREDWG=OFF)");
#endif
    }
};
} // namespace

std::unique_ptr<IFormatHandler> dwgHandler(DwgTools t) { return std::make_unique<DwgHandler>(std::move(t)); }
} // namespace pd::io
