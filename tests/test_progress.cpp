#include "storagecleaner/ProgressState.h"
#include "storagecleaner/Utils.h"

#include <cmath>
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

bool Near(float a, float b) { return std::fabs(a - b) < 1e-5f; }

ProgressSnapshot Snap(int index, int count, float fraction) {
    ProgressSnapshot s;
    s.stageIndex = index;
    s.stageCount = count;
    s.fractionComplete = fraction;
    return s;
}

void TestWithoutStages() {
    Check(Near(OverallFraction(Snap(0, 0, 0.4f)), 0.4f), "sem etapas, vale a propria fracao");
}

void TestStages() {
    Check(Near(OverallFraction(Snap(0, 4, 0.0f)), 0.0f), "inicio da 1a etapa = 0");
    Check(Near(OverallFraction(Snap(2, 4, 0.5f)), 0.625f), "etapa 3 de 4 pela metade = 62,5%");
    Check(Near(OverallFraction(Snap(3, 4, 1.0f)), 1.0f), "ultima etapa completa = 100%");
}

// O anel nunca pode passar de 100% nem andar para tras por valor fora da faixa.
void TestClamps() {
    Check(Near(OverallFraction(Snap(1, 2, 1.7f)), 1.0f), "fracao > 1 e limitada");
    Check(Near(OverallFraction(Snap(1, 2, -0.3f)), 0.5f), "fracao < 0 e limitada");
    Check(Near(OverallFraction(Snap(9, 2, 0.0f)), 0.5f), "indice alem do total vira a ultima etapa");
}

// Os scanners publicam snapshots novos sem saber a etapa; o canal mantem a que
// o orquestrador gravou.
void TestChannelPreservesStage() {
    ProgressChannel channel;
    channel.SetStage(2, 5);
    ProgressSnapshot fromScanner;
    fromScanner.fractionComplete = 0.5f;
    channel.Update(fromScanner);
    ProgressSnapshot read = channel.Read();
    Check(read.stageIndex == 2 && read.stageCount == 5, "Update preserva a etapa gravada por SetStage");
    Check(Near(OverallFraction(read), 0.5f), "etapa 3 de 5 pela metade = 50%");

    channel.SetStage(3, 5);
    Check(Near(channel.Read().fractionComplete, 0.0f), "SetStage zera a fracao da etapa anterior");
}

void TestCombineParallelProgress() {
    std::vector<ProgressSnapshot> parts(3);
    parts[0].phase = Phase::ScanningTemp;
    parts[0].fractionComplete = 0.3f;
    parts[0].itemsProcessed = 5;
    parts[1].phase = Phase::ScanningDuplicates;
    parts[1].fractionComplete = 0.5f;
    parts[1].itemsProcessed = 7;
    parts[1].currentItem = L"C:\\x";
    parts[2].phase = Phase::ScanningLogs;
    parts[2].bytesFoundSoFar = 100;
    ProgressSnapshot c = CombineParallelProgress(parts, {true, false, false});
    Check(Near(OverallFraction(c), (1.0f + 0.5f + 0.0f) / 3.0f), "concluída conta 1, as outras pela fração");
    Check(c.stagesDone == 1 && c.stageCount == 3, "conta categorias concluídas");
    Check(c.itemsProcessed == 12 && c.bytesFoundSoFar == 100, "contadores somados");
    Check(c.phase == Phase::ScanningDuplicates && c.currentItem == L"C:\\x",
          "fase da primeira categoria ainda em andamento");

    ProgressSnapshot empty = CombineParallelProgress({}, {});
    Check(Near(OverallFraction(empty), 1.0f), "sem categorias = completo");
}

void TestOverallFractionOverride() {
    ProgressSnapshot s;
    s.stageIndex = 0;
    s.stageCount = 4;
    s.fractionComplete = 0.0f;
    s.overallFraction = 0.75f;
    Check(Near(OverallFraction(s), 0.75f), "overallFraction tem prioridade sobre as etapas");
    s.overallFraction = 3.0f;
    Check(Near(OverallFraction(s), 1.0f), "overallFraction limitado a 1");
}

void TestAgeHours() {
    const std::int64_t now = 1000 * storagecleaner::util::kTicksPerHour;
    Check(storagecleaner::util::AgeHoursAt(now - 5 * storagecleaner::util::kTicksPerHour, now) == 5, "5 horas");
    Check(storagecleaner::util::AgeHoursAt(now - storagecleaner::util::kTicksPerHour + 1, now) == 0,
          "menos de 1 hora arredonda para 0");
    Check(storagecleaner::util::AgeHoursAt(now + 10, now) == 0, "data no futuro = 0");
}

} // namespace

int main() {
    TestCombineParallelProgress();
    TestOverallFractionOverride();
    TestAgeHours();
    TestWithoutStages();
    TestStages();
    TestClamps();
    TestChannelPreservesStage();

    if (g_failures == 0) {
        std::printf("Todos os testes de progresso passaram.\n");
        return 0;
    }
    std::fprintf(stderr, "%d teste(s) falharam.\n", g_failures);
    return 1;
}
