// SPDX-License-Identifier: LGPL-2.1-or-later
#include "sheet_example.hpp"
#include <iostream>
#include <planedraft/io.hpp>
int main(int argc, char** argv) {
    if (argc != 2)
        return 2;
    try {
        std::filesystem::path dir = argv[1];
        pd::Document source, loaded;
        pd::io::IoRegistry io;
        auto check = [](const pd::io::IoResult& r) {
            for (auto& d : r.diagnostics)
                std::cerr << d.subject << ": " << d.message << '\n';
            if (!r.ok() || r.skipped)
                throw std::runtime_error("sheet exchange failed");
        };
        sheet_test::drawing(source);
        check(io.write(dir / "source.dxf", source));
        check(io.read(dir / "source.dxf", loaded));
        check(io.write(dir / "roundtrip.dxf", loaded));
        auto* page = loaded.layout(loaded.layoutIds().at(0));
        if (page->name != "Plate")
            return 1;
        check(io.write(dir / "sheet.pdf", loaded, page));
        check(io.write(dir / "sheet.svg", loaded, page));
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
