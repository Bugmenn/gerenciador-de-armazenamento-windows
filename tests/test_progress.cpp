#include "storagecleaner/ProgressState.h"

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

} // namespace

int main() {
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
