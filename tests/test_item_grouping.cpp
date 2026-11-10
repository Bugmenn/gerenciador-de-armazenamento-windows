#include "storagecleaner/ItemGrouping.h"

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

ScanItem Item(const wchar_t* path, std::uint64_t size) {
    return ScanItem{path, size, Category::TempFiles, L"", false};
}

const std::vector<ScanItem> kItems = {
    Item(L"C:\\Temp\\a.tmp", 10),                         // 0
    Item(L"C:\\Temp\\B.tmp", 300),                        // 1
    Item(L"C:\\Logs\\rel\u00C1torio.log", 50),           // 2
    Item(L"C:\\Temp\\c.tmp", 20),                         // 3
    Item(L"D:/Cache/x.bin", 200),                         // 4
    Item(L"(Lixeira)", 1000),                             // 5
};

void TestFolderAndName() {
    Check(ParentFolder(L"C:\\Temp\\a.tmp") == L"C:\\Temp", "pasta de um arquivo");
    Check(FileNameOf(L"C:\\Temp\\a.tmp") == L"a.tmp", "nome de um arquivo");
    Check(ParentFolder(L"D:/Cache/x.bin") == L"D:/Cache", "aceita '/'");
    Check(ParentFolder(L"C:\\a.txt") == L"C:\\", "arquivo na raiz mantém 'C:\\'");
    Check(ParentFolder(L"(Lixeira)").empty(), "sem separador não tem pasta");
    Check(FileNameOf(L"(Lixeira)") == L"(Lixeira)", "sem separador: nome é o caminho todo");
}

void TestGroupingBySizeDescending() {
    auto groups = GroupItemsByFolder(kItems, {}, ItemSortKey::Size, true);
    Check(groups.size() == 4, "4 pastas distintas (incluindo a vazia da Lixeira)");
    if (groups.size() != 4) return;
    Check(groups[0].folder.empty() && groups[0].totalBytes == 1000, "maior grupo primeiro");
    Check(groups[1].folder == L"C:\\Temp" && groups[1].totalBytes == 330, "soma do grupo");
    Check(groups[1].itemIndices == std::vector<std::size_t>({1, 3, 0}),
          "itens do grupo do maior para o menor");
}

void TestGroupingByNameAscending() {
    auto groups = GroupItemsByFolder(kItems, {}, ItemSortKey::Name, false);
    if (groups.size() != 4) {
        Check(false, "4 grupos por nome");
        return;
    }
    Check(groups[0].folder.empty(), "pasta vazia vem primeiro em ordem alfabética");
    Check(groups[1].folder == L"C:\\Logs" && groups[2].folder == L"C:\\Temp",
          "pastas em ordem alfabética");
    Check(groups[2].itemIndices == std::vector<std::size_t>({0, 1, 3}),
          "nome ignora maiúsculas (a, B, c)");

    auto desc = GroupItemsByFolder(kItems, {}, ItemSortKey::Name, true);
    Check(!desc.empty() && desc[0].folder == L"D:/Cache", "nome decrescente inverte as pastas");
}

void TestFilter() {
    auto byText = GroupItemsByFolder(kItems, ItemFilter{L"RELátorio", 0}, ItemSortKey::Size, true);
    Check(byText.size() == 1 && byText[0].itemIndices == std::vector<std::size_t>({2}),
          "filtro ignora maiúsculas, inclusive acentuadas");

    auto byFolder = GroupItemsByFolder(kItems, ItemFilter{L"d:\\cache", 0}, ItemSortKey::Size, true);
    Check(byFolder.size() == 1 && byFolder[0].itemIndices == std::vector<std::size_t>({4}),
          "filtro casa com a pasta e trata '/' como '\\'");

    auto bySize = GroupItemsByFolder(kItems, ItemFilter{L"", 200}, ItemSortKey::Size, true);
    std::size_t count = 0;
    for (const auto& g : bySize) count += g.itemIndices.size();
    Check(count == 3, "tamanho mínimo é inclusivo (200, 300 e 1000)");

    Check(GroupItemsByFolder(kItems, ItemFilter{L"nada-casa", 0}, ItemSortKey::Size, true).empty(),
          "filtro sem correspondência não gera grupos");
    Check(GroupItemsByFolder({}, {}, ItemSortKey::Size, true).empty(), "lista vazia");
}

void TestCleanableByDrive() {
    std::vector<CategoryBucket> buckets(2);
    buckets[0].items = kItems;
    buckets[1].items = {Item(L"c:\\x\\y.dat", 5), Item(L"{1234-abcd}", 999), Item(L"C:", 7)};
    auto byDrive = CleanableBytesByDrive(buckets);
    Check(byDrive.size() == 2, "só C e D");
    Check(byDrive[L'C'] == 10 + 300 + 50 + 20 + 5, "letra minúscula soma em C");
    Check(byDrive[L'D'] == 200, "D com '/'");
}

} // namespace

int main() {
    TestFolderAndName();
    TestGroupingBySizeDescending();
    TestGroupingByNameAscending();
    TestFilter();
    TestCleanableByDrive();

    if (g_failures == 0) {
        std::printf("Todos os testes de agrupamento de itens passaram.\n");
        return 0;
    }
    std::fprintf(stderr, "%d teste(s) falharam.\n", g_failures);
    return 1;
}
