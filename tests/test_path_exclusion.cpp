#include "storagecleaner/PathExclusion.h"

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

const std::vector<std::wstring> kExcluded = {L"C:\\Users\\Ana\\Projetos\\", L"d:/Backup"};

void TestExactAndDescendant() {
    Check(IsExcludedPath(L"C:\\Users\\Ana\\Projetos", true, kExcluded), "pasta excluída em si");
    Check(IsExcludedPath(L"C:\\Users\\Ana\\Projetos\\a\\b.txt", false, kExcluded),
          "arquivo dentro da pasta excluída");
    Check(IsExcludedPath(L"D:\\BACKUP\\x.zip", false, kExcluded),
          "comparação ignora maiúsculas e aceita '/'");
}

void TestSiblingPrefixIsNotExcluded() {
    Check(!IsExcludedPath(L"C:\\Users\\Ana\\Projetos2\\a.txt", false, kExcluded),
          "prefixo de nome sem separador não casa");
    Check(!IsExcludedPath(L"C:\\Users\\Ana\\Downloads\\a.txt", false, kExcluded),
          "caminho sem relação não é excluído");
}

// Apagar uma pasta-pai levaria junto o que o usuário pediu para proteger.
void TestDirectoryContainingExcludedPath() {
    Check(IsExcludedPath(L"C:\\Users\\Ana", true, kExcluded),
          "pasta que contém um caminho excluído é bloqueada");
    Check(!IsExcludedPath(L"C:\\Users\\Ana", false, kExcluded),
          "arquivo (não pasta) com mesmo prefixo não é bloqueado por conter");
}

void TestDriveRoot() {
    std::vector<std::wstring> root = {L"E:\\"};
    Check(IsExcludedPath(L"E:\\qualquer\\coisa.txt", false, root), "raiz de unidade exclui tudo nela");
    Check(!IsExcludedPath(L"F:\\coisa.txt", false, root), "outra unidade não é afetada");
}

// Caminho real deste projeto ("Área de Trabalho") — o NTFS trata "Á" e "á"
// como iguais, a comparação também precisa tratar.
void TestAccentedCaseInsensitive() {
    std::vector<std::wstring> excluded = {L"C:\\Users\\Ana\\\u00C1rea de Trabalho"};
    Check(IsExcludedPath(L"c:\\users\\ana\\\u00E1rea de trabalho\\x.txt", false, excluded),
          "maiúscula acentuada casa com minúscula acentuada");
}

void TestEmptyInputs() {
    Check(!IsExcludedPath(L"C:\\a.txt", false, {}), "lista vazia não exclui nada");
    Check(!IsExcludedPath(L"C:\\a.txt", false, {L"", L"\\"}), "entradas vazias são ignoradas");
    Check(!IsExcludedPath(L"", true, kExcluded), "item vazio não é excluído");
}

} // namespace

int main() {
    TestExactAndDescendant();
    TestSiblingPrefixIsNotExcluded();
    TestDirectoryContainingExcludedPath();
    TestDriveRoot();
    TestAccentedCaseInsensitive();
    TestEmptyInputs();

    if (g_failures == 0) {
        std::printf("Todos os testes de exclusão de caminho passaram.\n");
        return 0;
    }
    std::fprintf(stderr, "%d teste(s) falharam.\n", g_failures);
    return 1;
}
