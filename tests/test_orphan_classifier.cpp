#include "storagecleaner/OrphanClassifier.h"

#include <cstdio>

using namespace storagecleaner;

namespace {

int g_failures = 0;

void Check(bool condition, const char* description) {
    if (!condition) {
        std::fprintf(stderr, "FALHOU: %s\n", description);
        ++g_failures;
    }
}

OrphanEvidence Idle(int days) {
    OrphanEvidence e;
    e.daysSinceLastWrite = days;
    return e;
}

void TestStrongSignalsAreHigh() {
    bool OrphanEvidence::*strong[] = {&OrphanEvidence::runningProcess, &OrphanEvidence::startupEntry,
                                      &OrphanEvidence::shortcutTarget, &OrphanEvidence::uninstallPath,
                                      &OrphanEvidence::publisherMatch};
    for (auto field : strong) {
        OrphanEvidence e = Idle(2000);
        e.*field = true;
        Check(ClassifyOrphan(e).chance == ExistenceChance::High, "cada sinal forte sozinho dá Alta");
    }
}

void TestWeakSignalsAreMedium() {
    OrphanEvidence exe = Idle(2000);
    exe.hasExecutables = true;
    Check(ClassifyOrphan(exe).chance == ExistenceChance::Medium, ".exe sozinho dá Média");

    OrphanEvidence reg = Idle(2000);
    reg.softwareRegistryKey = true;
    Check(ClassifyOrphan(reg).chance == ExistenceChance::Medium, "chave no registro sozinha dá Média");

    Check(ClassifyOrphan(Idle(kRecentUseDays - 1)).chance == ExistenceChance::Medium,
          "escrita há menos de 365 dias dá Média");
}

void TestNoSignalIsLow() {
    OrphanVerdict v = ClassifyOrphan(Idle(kRecentUseDays));
    Check(v.chance == ExistenceChance::Low, "sem sinal e parado há 365 dias dá Baixa (limite inclusivo)");
    Check(JoinReasons(v.reasons) == "nenhum sinal do app; sem uso há 12 meses", "motivo da Baixa");
}

void TestOtherProfileNeverLow() {
    OrphanEvidence e = Idle(2000);
    e.otherProfile = true;
    OrphanVerdict v = ClassifyOrphan(e);
    Check(v.chance == ExistenceChance::Medium, "outro perfil sem sinal fica em Média, não Baixa");
    ScanItem item{L"C:\\Users\\Outro\\AppData\\Roaming\\X", 1, Category::OrphanedApps, L"", true};
    item.existenceChance = v.chance;
    Check(!IsAutoCleanEligible(item), "pasta de outro perfil nunca vai para a limpeza automática");
    e.runningProcess = true;
    Check(ClassifyOrphan(e).chance == ExistenceChance::High, "sinal forte continua dando Alta");
}

void TestStrongWinsOverWeak() {
    OrphanEvidence e = Idle(10);
    e.hasExecutables = true;
    e.runningProcess = true;
    OrphanVerdict v = ClassifyOrphan(e);
    Check(v.chance == ExistenceChance::High, "forte vence fraco");
    Check(!v.reasons.empty() && v.reasons.front() == "programa em execução a partir da pasta",
          "motivo forte vem primeiro");
}

void TestIdleText() {
    Check(ClassifyOrphan(Idle(1)).reasons.back() == "sem uso há 1 dia", "singular de dia");
    Check(ClassifyOrphan(Idle(900)).reasons.back() == "sem uso há 2 anos", "anos acima de 730 dias");
}

void TestMentionsFolder() {
    const std::wstring folder = L"C:\\Users\\Ana\\AppData\\Local\\App";
    Check(MentionsFolder(L"\"C:\\Users\\Ana\\AppData\\Local\\App\\app.exe\" --minimized", folder),
          "comando do Run com aspas e argumentos");
    Check(MentionsFolder(L"c:/users/ana/appdata/local/app", folder), "pasta exata, '/' e minúsculas");
    Check(MentionsFolder(L"C:\\Users\\Ana\\AppData\\Local\\App\\icon.ico,0", folder), "DisplayIcon com ',0'");
    Check(!MentionsFolder(L"C:\\Users\\Ana\\AppData\\Local\\App2\\app.exe", folder), "pasta irmã com prefixo");
    Check(!MentionsFolder(L"C:\\Users\\Ana\\AppData\\Local\\App.exe", folder), "arquivo com o mesmo nome");
    Check(MentionsFolder(L"C:\\Users\\\u00C1na\\x.exe", L"c:\\users\\\u00E1na"), "acento maiúsculo x minúsculo");
    Check(!MentionsFolder(L"qualquer coisa", L""), "pasta vazia nunca casa");
}

void TestAutoCleanEligibility() {
    ScanItem orphan{L"C:\\x", 1, Category::OrphanedApps, L"", true};
    orphan.existenceChance = ExistenceChance::Low;
    Check(IsAutoCleanEligible(orphan), "órfão Baixa entra na limpeza automática");
    orphan.existenceChance = ExistenceChance::Medium;
    Check(!IsAutoCleanEligible(orphan), "órfão Média não entra");
    orphan.existenceChance = ExistenceChance::High;
    Check(!IsAutoCleanEligible(orphan), "órfão Alta não entra");
    orphan.existenceChance = ExistenceChance::NotApplicable;
    Check(!IsAutoCleanEligible(orphan), "órfão sem nível não entra");
    Check(IsAutoCleanEligible(ScanItem{L"C:\\t.tmp", 1, Category::TempFiles, L"", false}),
          "categoria não heurística entra");
}

} // namespace

int main() {
    TestStrongSignalsAreHigh();
    TestWeakSignalsAreMedium();
    TestNoSignalIsLow();
    TestOtherProfileNeverLow();
    TestStrongWinsOverWeak();
    TestIdleText();
    TestMentionsFolder();
    TestAutoCleanEligibility();

    if (g_failures == 0) {
        std::printf("Todos os testes de classificação de órfãos passaram.\n");
        return 0;
    }
    std::fprintf(stderr, "%d teste(s) falharam.\n", g_failures);
    return 1;
}
