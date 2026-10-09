#include "storagecleaner/ScanItem.h"

#include <cstdio>
#include <cstring>
#include <iterator>
#include <set>
#include <string>

using namespace storagecleaner;

namespace {

int g_failures = 0;

void Check(bool condition, const char* description) {
    if (!condition) {
        std::fprintf(stderr, "FALHOU: %s\n", description);
        ++g_failures;
    }
}

// As chaves de config.json/history.json gravados por versoes anteriores (que
// usavam o nome exibido, sem acento, como chave). Se este teste quebrar, a
// mudanca faz o app ignorar config/historico ja existentes do usuario.
void TestKeysMatchPersistedFormat() {
    Check(std::strcmp(CategoryKey(Category::TempFiles), "Arquivos temporarios") == 0,
          "chave de TempFiles inalterada");
    Check(std::strcmp(CategoryKey(Category::BrowserCache), "Cache de navegador") == 0,
          "chave de BrowserCache inalterada");
    Check(std::strcmp(CategoryKey(Category::Duplicate), "Arquivos duplicados") == 0,
          "chave de Duplicate inalterada");
    Check(std::strcmp(CategoryKey(Category::OldLogs), "Logs antigos") == 0,
          "chave de OldLogs inalterada");
    Check(std::strcmp(CategoryKey(Category::RecycleBin), "Lixeira") == 0,
          "chave de RecycleBin inalterada");
    Check(std::strcmp(CategoryKey(Category::RestorePoints), "Pontos de restauracao") == 0,
          "chave de RestorePoints inalterada");
    Check(std::strcmp(CategoryKey(Category::OrphanedApps), "Dados orfaos (apps desinstalados)") == 0,
          "chave de OrphanedApps inalterada");
    Check(std::strcmp(CategoryKey(Category::WindowsUpdateCache), "windows_update_cache") == 0,
          "chave de WindowsUpdateCache inalterada");
    Check(std::strcmp(CategoryKey(Category::CrashDumps), "crash_dumps") == 0,
          "chave de CrashDumps inalterada");
    Check(std::strcmp(CategoryKey(Category::ThumbnailCache), "thumbnail_cache") == 0,
          "chave de ThumbnailCache inalterada");
}

void TestRoundTripAllCategories() {
    for (Category c : kAllCategories) {
        Category parsed{};
        bool ok = CategoryFromKey(CategoryKey(c), parsed);
        Check(ok && parsed == c, "CategoryFromKey(CategoryKey(c)) devolve c");
    }
}

void TestKeysAreUnique() {
    std::set<std::string> keys;
    for (Category c : kAllCategories) keys.insert(CategoryKey(c));
    Check(keys.size() == std::size(kAllCategories), "chaves distintas por categoria");
}

void TestUnknownKeyIsRejected() {
    Category parsed = Category::Duplicate;
    Check(!CategoryFromKey("chave inexistente", parsed), "chave desconhecida e rejeitada");
    Check(parsed == Category::Duplicate, "chave desconhecida nao altera a saida");
}

// O nome exibido pode ter acento; a chave nao pode depender dele.
void TestDisplayNameIsDecoupledFromKey() {
    Category parsed{};
    Check(!CategoryFromKey(CategoryName(Category::RestorePoints), parsed),
          "nome exibido acentuado nao e aceito como chave");
}

} // namespace

int main() {
    TestKeysMatchPersistedFormat();
    TestRoundTripAllCategories();
    TestKeysAreUnique();
    TestUnknownKeyIsRejected();
    TestDisplayNameIsDecoupledFromKey();

    if (g_failures == 0) {
        std::printf("Todos os testes de chave de categoria passaram.\n");
        return 0;
    }
    std::fprintf(stderr, "%d teste(s) falharam.\n", g_failures);
    return 1;
}
