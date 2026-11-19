// SPDX-License-Identifier: LGPL-2.1-or-later
#include "detail.hpp"
#include "dwg_paths.hpp"
namespace pd::io {
DwgTools configuredDwgTools() { return {fs::path(PD_DWG_READER), fs::path(PD_DWG_WRITER)}; }
namespace {
class DwgHandler final : public IFormatHandler {
    DwgTools tools_;

  public:
    explicit DwgHandler(DwgTools t) : tools_(std::move(t)) {}
    IoResult read(const fs::path& p, Document& d) const override {
#ifdef PD_ENABLE_LIBREDWG
        return checked([&](IoResult& r) {
            TempDir dir(fs::temp_directory_path());
            auto tmp = dir.path / "exchange.dxf";
            auto msg = runWorker(tools_.dwg2dxf, p, tmp, dir.path);
            r = DxfHandler{}.read(tmp, d);
            if (!msg.empty())
                r.warn(0, "dwg2dxf", msg);
        });
#else
        (void)p;
        (void)d;
        return failed(IoStatus::BackendUnavailable, "DWG bridge disabled (PD_ENABLE_LIBREDWG=OFF)");
#endif
    }
    IoResult write(const fs::path& p, const Document& d) const override {
#ifdef PD_ENABLE_LIBREDWG
        return checked([&](IoResult& r) {
            if (!tools_.dxf2dwg.is_absolute() || !fs::is_regular_file(tools_.dxf2dwg))
                throw Failure(IoStatus::BackendUnavailable, "dxf2dwg not configured");
            TempDir dir(fs::temp_directory_path());
            auto tmp = dir.path / "exchange.dxf";
            r = DxfHandler{}.write(tmp, d);
            if (r.ok()) {
                auto msg = runWorker(tools_.dxf2dwg, tmp, p, dir.path);
                if (msg.find("ERROR:") != msg.npos)
                    throw Failure(IoStatus::BackendError, "dxf2dwg: " + msg);
                if (!msg.empty())
                    r.warn(0, "dxf2dwg", msg);
            }
        });
#else
        (void)p;
        (void)d;
        return failed(IoStatus::BackendUnavailable, "DWG bridge disabled (PD_ENABLE_LIBREDWG=OFF)");
#endif
    }
};
} // namespace
std::unique_ptr<IFormatHandler> dwgHandler(DwgTools t) { return std::make_unique<DwgHandler>(std::move(t)); }
} // namespace pd::io
