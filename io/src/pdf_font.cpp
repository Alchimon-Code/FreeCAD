// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pdf_font.hpp"
#include <string_view>
namespace pd::io {
namespace {
// Points A..Y are a 5x5 grid, from top left to bottom right. Space lifts the pen.
const char* strokes(char32_t c) {
    switch (c) {
    case U' ':
        return "";
    case U'A':
    case U'А':
        return "UCY LN";
    case U'B':
    case U'В':
        return "UADJNK NTXU";
    case U'C':
    case U'С':
        return "EBFPUXY";
    case U'D':
        return "UACJTXU";
    case U'E':
    case U'Е':
        return "EAUY KN";
    case U'F':
        return "UAE KN";
    case U'G':
        return "DBFPUXYOM";
    case U'H':
    case U'Н':
        return "AU EY KO";
    case U'I':
        return "AE CW UY";
    case U'J':
        return "BETXVP";
    case U'K':
    case U'К':
        return "AU EKY";
    case U'L':
        return "AUY";
    case U'M':
    case U'М':
        return "UAMEY";
    case U'N':
        return "UAYE";
    case U'O':
    case U'О':
    case U'0':
        return "BDJTXVPFB";
    case U'P':
    case U'Р':
        return "UADJNK";
    case U'Q':
        return "BDJTXVPFB MY";
    case U'R':
        return "UADJNK MY";
    case U'S':
    case U'Ѕ':
        return "JDBFKNTXVU";
    case U'T':
    case U'Т':
        return "AE CW";
    case U'U':
        return "APVXTE";
    case U'V':
        return "AWE";
    case U'W':
        return "AUMYE";
    case U'X':
    case U'Х':
        return "AY EU";
    case U'Y':
        return "AMW EM";
    case U'Z':
        return "AEUY";
    case U'Б':
        return "EAUKNTXU";
    case U'Г':
        return "UAE";
    case U'Д':
        return "UQBEY UT PTY";
    case U'Ё':
        return "JFUY KN AB DE";
    case U'Ж':
        return "AMU CW EMY";
    case U'З':
    case U'3':
        return "ABDJMN TXVU NT";
    case U'И':
        return "AUEY";
    case U'Й':
        return "FUJY BHD";
    case U'Л':
        return "UQBEY";
    case U'П':
        return "UAEY";
    case U'У':
        return "AME MWU";
    case U'Ф':
        return "CW GIKONLG GIJO";
    case U'Ц':
        return "AUXD XY";
    case U'Ч':
        return "AFKO EY";
    case U'Ш':
        return "AUYE CW";
    case U'Щ':
        return "APTE CR TY";
    case U'Ъ':
        return "ABVXNHL";
    case U'Ы':
        return "AUVSMK EY";
    case U'Ь':
        return "AUXTNK";
    case U'Э':
        return "ABDJTXVU MO";
    case U'Ю':
        return "AU KL HC DJTXWRLH CD";
    case U'Я':
        return "YE BFLN UO EB";
    case U'1':
        return "FCW VX";
    case U'2':
        return "FBDJOUY";
    case U'4':
        return "DPO DX";
    case U'5':
        return "EAKNTXVP";
    case U'6':
        return "DBFKPVXTNLK";
    case U'7':
        return "AEV";
    case U'8':
        return "BDJNLFB LNTXVPL";
    case U'9':
        return "BDJOTXV OLFB";
    case U'.':
        return "WX";
    case U',':
        return "RV";
    case U':':
        return "GH RS";
    case U';':
        return "GH RV";
    case U'-':
    case U'−':
        return "KO";
    case U'_':
        return "UY";
    case U'+':
        return "KO HR";
    case U'=':
        return "FJ PT";
    case U'/':
        return "UE";
    case U'\\':
        return "AY";
    case U'!':
        return "CR WX";
    case U'?':
        return "FBDJMR WX";
    case U'(':
        return "DFPX";
    case U')':
        return "BJTV";
    case U'[':
        return "DAUX";
    case U']':
        return "BEYV";
    case U'\"':
        return "BG DI";
    case U'\'':
        return "CH";
    case U'Ø':
        return "BDJTXVPFB UE";
    case U'×':
        return "AY EU";
    case U'№':
        return "UAWE DIHGD PT";
    case U'°':
        return "BCHGB";
    case U'<':
        return "EKY";
    case U'>':
        return "AOU";
    case U'&':
        return "YFBCILPVXN";
    case U'*':
        return "FT JP CW";
    case U'#':
        return "BV DX FJ PT";
    case U'%':
        return "EU BFGB INJI";
    case U'|':
        return "CW";
    default:
        return nullptr;
    }
}
} // namespace
bool pdfGlyph(char32_t scalar, std::string& proc) {
    auto c = scalar;
    bool small = (c >= U'a' && c <= U'z') || (c >= U'а' && c <= U'я') || c == U'ё';
    if (small)
        c = c == U'ё' ? U'Ё' : c - 32;
    auto def = strokes(c);
    if (!def)
        return false;
    proc = "600 0 -25 -25 550 1025 d1\n38 w 1 J 1 j\n";
    bool move = true;
    for (char p : std::string_view(def)) {
        if (p == ' ') {
            move = true;
            continue;
        }
        int i = p - 'A';
        proc += std::to_string(25 + 125 * (i % 5)) + " " +
                std::to_string(20 + (4 - i / 5) * (small ? 185 : 240)) + (move ? " m\n" : " l\n");
        move = false;
    }
    proc += "S\n";
    return true;
}
} // namespace pd::io
