#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// Forward declaration para não obrigar quem inclui este header a puxar
// <knownfolders.h>/<shlobj.h> só para chamar KnownFolderPath.
typedef struct _GUID GUID;

namespace storagecleaner::util {

// Formata bytes em string legível (KB, MB, GB, TB).
std::string FormatSize(std::uint64_t bytes);

// Hash SHA-256 de arquivos via BCrypt (CNG). Abre o algoritmo uma vez e
// reaproveita o objeto de hash e o buffer de leitura — antes cada arquivo
// pagava um CryptAcquireContext. Não é thread-safe: um FileHasher por thread.
// Devolve hex vazio em falha de leitura.
class FileHasher {
public:
    FileHasher();
    ~FileHasher();
    FileHasher(const FileHasher&) = delete;
    FileHasher& operator=(const FileHasher&) = delete;

    // Arquivo inteiro.
    std::string Full(const std::wstring& path);
    // Só os primeiros e os últimos `edgeBytes` (o arquivo inteiro se
    // `sizeBytes` <= 2 * edgeBytes): filtro barato antes do hash completo
    // na busca por duplicados.
    std::string Edges(const std::wstring& path, std::uint64_t sizeBytes, std::uint32_t edgeBytes);

private:
    std::string Finish();
    void* algorithm_ = nullptr; // BCRYPT_ALG_HANDLE
    void* hash_ = nullptr;      // BCRYPT_HASH_HANDLE (reutilizável)
    std::vector<unsigned char> buffer_;
};

// true se a unidade (letra) é SSD, false se é HD com cabeça móvel (penalidade
// de seek), nullopt se não deu para saber. Quem decide paralelismo de leitura
// trata nullopt como HD (o lado seguro: leitura concorrente piora em HD).
std::optional<bool> IsSolidStateDrive(wchar_t driveLetter);

// Quantas leituras simultâneas fazer na unidade: até 4 (limitado aos núcleos)
// em SSD, 1 em HD ou desconhecido. Fonte única do limite para o ScanEngine
// (categorias) e para o hash de duplicados.
int ReadParallelism(wchar_t driveLetter);

// Converte wstring para string UTF-8 (para impressão em console).
std::string WideToUtf8(const std::wstring& wide);

// Converte string UTF-8 para wstring (para ler paths salvos em JSON).
std::wstring Utf8ToWide(const std::string& utf8);

// Retorna true se o arquivo foi modificado há mais de `hours`/`days`, lendo a
// data no próprio arquivo (uma consulta por chamada). Dentro de
// ForEachFileRecursive, filtrar primeiro por FileEntry::lastWriteTicks e
// chamar isto só para quem passou, quando a decisão leva a exclusão: a data
// da listagem pode estar atrasada para um arquivo ainda aberto.
bool IsOlderThanHours(const std::wstring& path, int hours);
bool IsOlderThanDays(const std::wstring& path, int days);

// Instante atual no formato de FileEntry::lastWriteTicks (FILETIME UTC, 100 ns
// desde 1601).
std::int64_t NowFileTimeTicks();

inline constexpr std::int64_t kTicksPerHour = 36'000'000'000LL;

// Horas inteiras entre `lastWriteTicks` e `nowTicks` (0 se no futuro). Pura,
// testada em tests/test_progress.cpp.
inline std::int64_t AgeHoursAt(std::int64_t lastWriteTicks, std::int64_t nowTicks) {
    return nowTicks > lastWriteTicks ? (nowTicks - lastWriteTicks) / kTicksPerHour : 0;
}

// Soma recursivamente o tamanho de todos os arquivos sob `dir`. Usado para
// dimensionar pastas inteiras (ex: cache de navegador) como um único item.
// Implementada em cima de ForEachFileRecursive (não reimplementa a iteração)
// e por isso observa `cancel` como as demais varreduras — chamador sem um
// atomic de cancelamento real (ex.: varredura leve em segundo plano) pode
// passar um `std::atomic<bool>` local sempre false.
std::uint64_t DirectorySize(const std::wstring& dir, std::atomic<bool>& cancel);

struct DirectoryStats {
    std::uint64_t totalBytes = 0;
    // true se algum arquivo da árvore (em qualquer profundidade) foi escrito
    // há menos de `thresholdDays` dias, conforme passado a ComputeDirectoryStats.
    bool hasRecentActivity = false;
    // Arquivos .exe na árvore (sinal de app portátil/instalado por usuário).
    std::uint32_t executableCount = 0;
    // Dias desde a escrita mais recente de qualquer arquivo da árvore (0 se vazia).
    int daysSinceNewestWrite = 0;
};

// Um arquivo encontrado por ForEachFileRecursive, com os metadados que a
// própria listagem da pasta já traz (sem consulta extra por arquivo).
struct FileEntry {
    std::wstring path;
    std::uint64_t sizeBytes = 0;
    std::int64_t lastWriteTicks = 0; // FILETIME UTC (ver NowFileTimeTicks)
    std::uint32_t attributes = 0;    // FILE_ATTRIBUTE_*
};

// Callback chamado para cada arquivo encontrado por ForEachFileRecursive.
using FileVisitor = std::function<void(const FileEntry& entry)>;

// Percorre `dir` recursivamente chamando `visitor` para cada arquivo, com
// tamanho/data/atributos vindos da própria listagem (FindFirstFileExW com
// FindExInfoBasic + FIND_FIRST_EX_LARGE_FETCH: sem nomes 8.3, lotes maiores).
// Pastas sem permissão são puladas; symlinks e junctions de pasta não são
// seguidos, para não entrar em laço nem sair da árvore (outras pastas reparse
// point, como as do OneDrive, são percorridas normalmente).
// Interrompe assim que `cancel` for sinalizado. Compartilhado pelos scanners
// e por DirectorySize/ComputeDirectoryStats — não reimplementar a iteração.
void ForEachFileRecursive(const std::wstring& dir, std::atomic<bool>& cancel,
                           const FileVisitor& visitor);

// Percorre `dir` recursivamente uma única vez (via ForEachFileRecursive),
// somando o tamanho de todos os arquivos e verificando se algum foi escrito
// há menos de `thresholdDays` dias. Existe para não fazer duas varreduras
// separadas (uma para tamanho, outra para idade) e, principalmente, para não
// decidir "atividade recente" olhando só o mtime da pasta de topo: no NTFS
// esse mtime só muda quando entradas são criadas/removidas diretamente nela,
// não quando arquivos em subpastas são escritos — então um app que só grava
// em subpastas profundas pareceria "inativo" mesmo em uso diário se só o
// topo fosse checado.
// `readDatesFromFiles`: lê a data no próprio arquivo em vez da listagem (mais
// lento) — para reconfirmar quando a decisão leva a exclusão, já que a data
// da listagem pode estar atrasada para arquivo aberto.
DirectoryStats ComputeDirectoryStats(const std::wstring& dir, int thresholdDays,
                                     std::atomic<bool>& cancel, bool readDatesFromFiles = false);

// Executa um comando externo (ex: vssadmin, schtasks) capturando stdout.
// Usado em vez de uma dependência externa de processo, já que os comandos
// necessários fazem parte do próprio Windows.
struct CommandResult {
    int exitCode = -1;
    std::string output;
};
CommandResult RunCommandCaptureOutput(const std::wstring& commandLine);

// Caminho absoluto para um executavel em System32 (ex.: "vssadmin" ->
// "C:\Windows\System32\vssadmin.exe"). Usado para montar linhas de comando
// sem depender da resolucao implicita de PATH do CreateProcessW, que
// permitiria a um executavel malicioso no PATH do usuario ser executado no
// lugar da ferramenta do sistema quando o processo estiver elevado.
// Retorna string vazia em caso de falha (GetSystemDirectoryW) — o chamador
// deve tratar isso como falha e NAO executar o comando, nunca cair para o
// nome sem caminho, o que reabriria a busca por PATH.
std::wstring SystemToolPath(const wchar_t* toolName);

// Wrapper fino sobre SHGetKnownFolderPath; retorna string vazia em falha.
// O chamador inclui <knownfolders.h> para ter as constantes FOLDERID_*.
std::wstring KnownFolderPath(const GUID& folderId);

// Lista as pastas de perfil de usuário em C:\Users (uma por usuário). Usado
// pelos scanners que precisam varrer temporários/cache/AppData de todos os
// usuários da máquina, não só do usuário atual.
std::vector<std::wstring> EnumerateUserProfileDirs();

struct DriveSpace {
    std::wstring root;  // ex.: L"C:\\"
    std::uint64_t totalBytes = 0;
    std::uint64_t freeBytes = 0;
    bool isRemovable = false;
};

// Unidades fixas e removíveis com mídia. Unidades de rede/ópticas ficam de
// fora: GetDiskFreeSpaceExW nelas pode bloquear por segundos (rede caída,
// disco girando). Mesmo assim, chamar fora da UI thread.
std::vector<DriveSpace> EnumerateDrives();

// Forma longa e absoluta de um caminho (GetFullPathNameW + GetLongPathNameW).
// Necessária antes de comparar com a lista de pastas protegidas: %TEMP%
// costuma vir em formato 8.3 ("C:\Users\JOAOSI~1\...") e escaparia de uma
// comparação puramente textual com o caminho longo escolhido pelo usuário.
// Se o caminho não existir mais, devolve só a forma absoluta.
std::wstring CanonicalPath(const std::wstring& path);

} // namespace storagecleaner::util
