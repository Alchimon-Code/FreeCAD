// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <fstream>
#include <planedraft/entities/primitives.hpp>
#include <planedraft/io.hpp>
#include <stdexcept>

namespace pd::io {
namespace fs = std::filesystem;
struct Failure : std::runtime_error {
    IoStatus status;
    std::size_t line;
    std::string subject;
    Failure(IoStatus s, std::string msg, std::size_t l = 0, std::string type = {})
        : std::runtime_error(std::move(msg)), status(s), line(l), subject(std::move(type)) {}
};
IoResult failed(IoStatus, std::string);
template <class F> IoResult checked(F&& f) {
    IoResult r;
    try {
        f(r);
    } catch (const Failure& e) {
        r.status = e.status;
        r.diagnostics.push_back({IoDiagnostic::Level::Error, e.line, e.subject, e.what()});
    } catch (const fs::filesystem_error& e) {
        return failed(IoStatus::FileError, e.what());
    } catch (const std::ios_base::failure& e) {
        return failed(IoStatus::FileError, e.what());
    } catch (const std::exception& e) {
        return failed(IoStatus::InvalidData, e.what());
    }
    return r;
}
class TempDir {
  public:
    explicit TempDir(const fs::path& parent);
    ~TempDir();
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    fs::path path;
};
std::ofstream output(const fs::path&);
std::string num(double); // independent of the process locale
std::string hex(unsigned long long, unsigned width = 0);
std::u32string unicode(std::string_view); // strict UTF-8, no NUL
std::string utf8(char32_t);
std::string utf16hex(std::string_view, bool bom = true);
constexpr std::uintmax_t maxFileBytes = 128u * 1024 * 1024;
class DxfHandler final : public IFormatHandler {
    DxfVersion version_;

  public:
    explicit DxfHandler(DxfVersion v = DxfVersion::R2000) : version_(v) {}
    IoResult read(const fs::path&, Document&) const override;
    IoResult write(const fs::path&, const Document&) const override;
};
class SvgExporter final : public IFormatHandler {
  public:
    IoResult write(const fs::path&, const Document&) const override;
    IoResult writeLayout(const fs::path&, const Document&, const Layout&) const override;
};
class PdfExporter final : public IFormatHandler {
  public:
    IoResult write(const fs::path&, const Document&) const override;
    IoResult writeLayout(const fs::path&, const Document&, const Layout&) const override;
};
std::unique_ptr<IFormatHandler> dwgHandler(DwgTools);
#ifdef PD_ENABLE_LIBREDWG
std::string runWorker(const fs::path& tool, const fs::path& input, const fs::path& output,
                      const fs::path& work);
#endif
} // namespace pd::io
