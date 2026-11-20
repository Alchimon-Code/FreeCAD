// SPDX-License-Identifier: LGPL-2.1-or-later
// Tests ONLY the external-process protocol, not the DWG format. Read/write a DXF payload verbatim.
#include <filesystem>
#include <fstream>
#include <iostream>
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    namespace fs = std::filesystem;
    // dwg2dxf: -o OUT IN   (native DXF version)
    // dxf2dwg: -a r2000 -o OUT IN
    const bool versioned = argc == 6 && fs::path(argv[1]) == "-a" && fs::path(argv[2]) == "r2000";
    if (!versioned && argc != 4)
        return 22;
    if (versioned && argc != 6)
        return 22;
    const int inIndex = versioned ? 5 : 3, outIndex = versioned ? 4 : 2;
    if (fs::path(argv[outIndex - 1]) != "-o")
        return 22;
    std::ifstream f(fs::path(argv[inIndex]), std::ios::binary);
    if (f.peek() == 'f')
        return 23;
    std::cerr << "fixture stderr";
    std::ofstream out(fs::path(argv[outIndex]), std::ios::binary);
    out << f.rdbuf();
    return out.good() ? 0 : 24;
}
