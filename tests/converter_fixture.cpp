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
    if (argc != 6 || fs::path(argv[1]) != "-a" || fs::path(argv[2]) != "r2000" || fs::path(argv[3]) != "-o")
        return 22;
    std::ifstream f(fs::path(argv[5]), std::ios::binary);
    if (f.peek() == 'f')
        return 23;
    std::cerr << "fixture stderr";
    std::ofstream out(fs::path(argv[4]), std::ios::binary);
    out << f.rdbuf();
    return out.good() ? 0 : 24;
}
