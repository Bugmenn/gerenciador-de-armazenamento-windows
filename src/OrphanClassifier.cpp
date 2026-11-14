#include "storagecleaner/OrphanClassifier.h"

#include "storagecleaner/PathExclusion.h"

namespace storagecleaner {

namespace {

std::string IdleText(int days) {
    if (days < 60) return std::to_string(days) + (days == 1 ? " dia" : " dias");
    if (days < 730) return std::to_string(days / 30) + " meses";
    return std::to_string(days / 365) + " anos";
}

} // namespace

OrphanVerdict ClassifyOrphan(const OrphanEvidence& e) {
    OrphanVerdict verdict;
    if (e.runningProcess) verdict.reasons.push_back("programa em execução a partir da pasta");
    if (e.startupEntry) verdict.reasons.push_back("inicia junto com o Windows");
    if (e.shortcutTarget) verdict.reasons.push_back("atalho no Menu Iniciar/Área de Trabalho");
    if (e.uninstallPath) verdict.reasons.push_back("app instalado aponta para a pasta");
    if (e.publisherMatch) verdict.reasons.push_back("fabricante de um app instalado");
    bool strong = !verdict.reasons.empty();

    if (e.hasExecutables) verdict.reasons.push_back("contém programas (.exe)");
    if (e.softwareRegistryKey) verdict.reasons.push_back("configuração no registro");
    bool recent = e.daysSinceLastWrite < kRecentUseDays;
    if (e.otherProfile) verdict.reasons.push_back("perfil de outro usuário (sinais incompletos)");
    bool weak = e.hasExecutables || e.softwareRegistryKey || recent || e.otherProfile;

    if (strong) {
        verdict.chance = ExistenceChance::High;
    } else if (weak) {
        verdict.chance = ExistenceChance::Medium;
    } else {
        verdict.chance = ExistenceChance::Low;
        verdict.reasons.push_back("nenhum sinal do app");
    }
    verdict.reasons.push_back("sem uso há " + IdleText(e.daysSinceLastWrite));
    return verdict;
}

std::string JoinReasons(const std::vector<std::string>& reasons) {
    std::string out;
    for (const auto& r : reasons) {
        if (!out.empty()) out += "; ";
        out += r;
    }
    return out;
}

bool MentionsFolder(const std::wstring& text, const std::wstring& folder) {
    return MentionsNormalizedFolder(NormalizePathForCompare(text), NormalizePathForCompare(folder));
}

bool MentionsNormalizedFolder(const std::wstring& hay, const std::wstring& needle) {
    if (needle.empty()) return false;
    for (std::size_t pos = hay.find(needle); pos != std::wstring::npos; pos = hay.find(needle, pos + 1)) {
        std::size_t end = pos + needle.size();
        // Só conta se o nome da pasta termina ali: fim do texto, separador ou
        // delimitador de linha de comando (aspas, espaço, vírgula do
        // DisplayIcon "app.exe,0").
        if (end == hay.size()) return true;
        wchar_t next = hay[end];
        if (next == L'\\' || next == L'"' || next == L' ' || next == L',') return true;
    }
    return false;
}

} // namespace storagecleaner
