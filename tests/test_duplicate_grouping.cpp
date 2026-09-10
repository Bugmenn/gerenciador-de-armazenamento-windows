#include "storagecleaner/DuplicateGrouping.h"

#include <cassert>
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

void TestNoFilesNoGroups() {
    auto result = GroupDuplicatesByHash({});
    Check(result.empty(), "sem arquivos de entrada, sem duplicados na saida");
}

void TestSingleFileIsNotDuplicate() {
    std::vector<HashedFile> input = {{L"C:\\a.txt", 100, "hash1", 10}};
    auto result = GroupDuplicatesByHash(input);
    Check(result.empty(), "um unico arquivo com um hash nao e duplicado de nada");
}

void TestTwoIdenticalFilesKeepsOldest() {
    std::vector<HashedFile> input = {
        {L"C:\\newer.txt", 100, "samehash", /*lastWriteTicks=*/200},
        {L"C:\\older.txt", 100, "samehash", /*lastWriteTicks=*/100},
    };
    auto result = GroupDuplicatesByHash(input);
    Check(result.size() == 1, "duas copias identicas geram exatamente 1 item a remover");
    if (result.size() == 1) {
        Check(result[0].path == L"C:\\newer.txt", "remove a copia mais nova");
        Check(result[0].keepPath == L"C:\\older.txt", "mantem a copia mais antiga");
        Check(result[0].category == Category::Duplicate, "categoria correta");
    }
}

void TestDifferentHashesAreNotGrouped() {
    std::vector<HashedFile> input = {
        {L"C:\\a.txt", 100, "hashA", 10},
        {L"C:\\b.txt", 100, "hashB", 20},
    };
    auto result = GroupDuplicatesByHash(input);
    Check(result.empty(), "hashes diferentes nunca sao tratados como duplicados");
}

void TestEmptyHashIsIgnored() {
    // Hash vazio simula falha de leitura do arquivo (ver util::HashFileSHA256)
    // — esses arquivos nao devem aparecer em nenhum grupo.
    std::vector<HashedFile> input = {
        {L"C:\\unreadable.txt", 100, "", 10},
        {L"C:\\another_unreadable.txt", 100, "", 20},
    };
    auto result = GroupDuplicatesByHash(input);
    Check(result.empty(), "arquivos sem hash calculado sao ignorados no agrupamento");
}

void TestThreeWayDuplicateKeepsOnlyOldest() {
    std::vector<HashedFile> input = {
        {L"C:\\c.txt", 50, "h", 300},
        {L"C:\\a.txt", 50, "h", 100},
        {L"C:\\b.txt", 50, "h", 200},
    };
    auto result = GroupDuplicatesByHash(input);
    Check(result.size() == 2, "3 copias identicas geram 2 itens a remover (mantem so 1)");
    for (const auto& item : result) Check(item.keepPath == L"C:\\a.txt", "mantem sempre o mais antigo");
}

} // namespace

int main() {
    TestNoFilesNoGroups();
    TestSingleFileIsNotDuplicate();
    TestTwoIdenticalFilesKeepsOldest();
    TestDifferentHashesAreNotGrouped();
    TestEmptyHashIsIgnored();
    TestThreeWayDuplicateKeepsOnlyOldest();

    if (g_failures > 0) {
        std::fprintf(stderr, "%d verificacao(oes) falharam\n", g_failures);
        return 1;
    }
    std::printf("Todas as verificacoes de agrupamento de duplicados passaram.\n");
    return 0;
}
