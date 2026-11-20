// SPDX-License-Identifier: LGPL-2.1-or-later
#include <iostream>
#include <planedraft/io.hpp>
namespace {
int convert(std::filesystem::path in, std::filesystem::path out, std::filesystem::path layoutName = {}) {
    pd::io::IoRegistry io;
    pd::Document doc;
    auto report = [](const pd::io::IoResult& r) {
        for (auto& d : r.diagnostics) {
            std::cerr << (d.level == pd::io::IoDiagnostic::Level::Error ? "error: " : "warning: ");
            if (!d.subject.empty())
                std::cerr << d.subject << ": ";
            if (d.line)
                std::cerr << "line " << d.line << ": ";
            std::cerr << d.message;
            if (d.count > 1)
                std::cerr << " (" << d.count << " occurrences)";
            std::cerr << '\n';
        }
        return r.ok();
    };
    auto loaded = io.read(in, doc);
    if (!report(loaded))
        return 1;
    std::error_code ec;
    if (std::filesystem::equivalent(in, out, ec)) {
        std::cerr << "Use a separate output file.\n";
        return 1;
    }
    const pd::Layout* sheet = nullptr;
    if (!layoutName.empty()) {
        auto u = layoutName.u8string();
        std::string name(u.begin(), u.end());
        for (auto id : doc.layoutIds())
            if (doc.layout(id)->name == name)
                sheet = doc.layout(id);
        if (!sheet) {
            std::cerr << "Unknown layout: " << name << '\n';
            return 1;
        }
    }
    if (!report(io.write(out, doc, sheet)))
        return 1;
    std::cout << loaded.entities << " entities, " << loaded.layers << " layers, " << loaded.skipped
              << " skipped\n";
    return 0;
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    if (argc == 5 && std::filesystem::path(argv[1]) == "--layout")
        return convert(argv[3], argv[4], argv[2]);
    if (argc != 3) {
        std::cerr << "Usage: pdconvert [--layout NAME] INPUT.{dxf,dwg} OUTPUT.{dxf,dwg,svg,pdf}\n";
        return 2;
    }
    return convert(argv[1], argv[2]);
}
