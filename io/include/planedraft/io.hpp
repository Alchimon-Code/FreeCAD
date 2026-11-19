// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <planedraft/document.hpp>
#include <string>
#include <vector>

namespace pd::io {
enum class IoStatus { Ok, InvalidData, Unsupported, BackendUnavailable, BackendError, FileError };
struct IoDiagnostic {
    enum class Level { Warning, Error } level;
    std::size_t line; // first offending group, 1-based; 0 for file-wide diagnostics
    std::string subject, message;
    std::size_t count = 1;
};
struct IoResult {
    IoStatus status = IoStatus::Ok;
    std::vector<IoDiagnostic> diagnostics;
    std::size_t entities = 0, layers = 0, skipped = 0;
    [[nodiscard]] bool ok() const { return status == IoStatus::Ok; }
    void warn(std::size_t line, std::string subject, std::string message);
};
class IFormatHandler {
  public:
    virtual ~IFormatHandler() = default;
    // Called with an empty private document / a new private output path, never the live drawing/file.
    virtual IoResult read(const std::filesystem::path&, Document& staging) const;
    virtual IoResult write(const std::filesystem::path&, const Document&) const = 0;
    virtual IoResult writeLayout(const std::filesystem::path&, const Document&, const Layout&) const;
};
struct DwgTools {
    std::filesystem::path dwg2dxf, dxf2dwg;
};
DwgTools configuredDwgTools();
enum class DxfVersion { R2000, R2004 }; // 420 TrueColor is standard only from R2004
class IoRegistry {
  public:
    explicit IoRegistry(DwgTools tools = configuredDwgTools(), DxfVersion version = DxfVersion::R2000);
    void add(std::string extension, std::unique_ptr<IFormatHandler> handler);
    [[nodiscard]] IoResult read(const std::filesystem::path&, Document&) const;
    [[nodiscard]] IoResult write(const std::filesystem::path&, const Document&,
                                 const Layout* sheet = nullptr) const;

  private:
    std::map<std::string, std::unique_ptr<IFormatHandler>> handlers_;
};
} // namespace pd::io
