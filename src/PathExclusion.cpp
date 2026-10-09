#include "storagecleaner/PathExclusion.h"

#include <cwctype>

namespace storagecleaner {

namespace {

// `child` está estritamente dentro de `parent` (ambos já normalizados). Exige
// o separador logo após o prefixo para "C:\dados" não casar com "C:\dados2".
bool IsStrictlyUnder(const std::wstring& child, const std::wstring& parent) {
    if (parent.empty() || child.size() <= parent.size()) return false;
    if (child.compare(0, parent.size(), parent) != 0) return false;
    // Raiz de unidade normalizada ("c:") ou já terminada em separador.
    return child[parent.size()] == L'\\';
}

// towlower no locale "C" só converte ASCII; acentos maiúsculos do Latin-1
// ("Á", "É", "Ç"...) ficam em U+00C0-U+00DE, exceto U+00D7 ("x" de
// multiplicação), e a minúscula correspondente está sempre 0x20 acima.
wchar_t FoldCase(wchar_t ch) {
    if (ch >= 0xC0 && ch <= 0xDE && ch != 0xD7) return static_cast<wchar_t>(ch + 0x20);
    return static_cast<wchar_t>(std::towlower(ch));
}

} // namespace

std::wstring NormalizePathForCompare(const std::wstring& path) {
    std::wstring out;
    out.reserve(path.size());
    for (wchar_t ch : path) out.push_back(ch == L'/' ? L'\\' : FoldCase(ch));
    while (!out.empty() && out.back() == L'\\') out.pop_back();
    return out;
}

bool IsExcludedPath(const std::wstring& itemPath, bool itemIsDirectory,
                    const std::vector<std::wstring>& excludedPaths) {
    std::wstring item = NormalizePathForCompare(itemPath);
    if (item.empty()) return false;
    for (const auto& raw : excludedPaths) {
        std::wstring excluded = NormalizePathForCompare(raw);
        if (excluded.empty()) continue;
        if (item == excluded || IsStrictlyUnder(item, excluded)) return true;
        if (itemIsDirectory && IsStrictlyUnder(excluded, item)) return true;
    }
    return false;
}

} // namespace storagecleaner
