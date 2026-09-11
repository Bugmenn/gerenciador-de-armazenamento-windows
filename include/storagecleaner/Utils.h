#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Forward declaration para não obrigar quem inclui este header a puxar
// <knownfolders.h>/<shlobj.h> só para chamar KnownFolderPath.
typedef struct _GUID GUID;

namespace storagecleaner::util {

// Formata bytes em string legível (KB, MB, GB, TB).
std::string FormatSize(std::uint64_t bytes);

// Calcula o hash SHA-256 de um arquivo (via Windows CryptoAPI). Retorna string
// hexadecimal vazia em caso de falha de leitura.
std::string HashFileSHA256(const std::wstring& path);

// Converte wstring para string UTF-8 (para impressão em console).
std::string WideToUtf8(const std::wstring& wide);

// Converte string UTF-8 para wstring (para ler paths salvos em JSON).
std::wstring Utf8ToWide(const std::string& utf8);

// Retorna true se o arquivo foi modificado há mais de `hours`/`days`.
bool IsOlderThanHours(const std::wstring& path, int hours);
bool IsOlderThanDays(const std::wstring& path, int days);

// Soma recursivamente o tamanho de todos os arquivos sob `dir`. Usado para
// dimensionar pastas inteiras (ex: cache de navegador) como um único item.
std::uint64_t DirectorySize(const std::wstring& dir);

struct DirectoryStats {
    std::uint64_t totalBytes = 0;
    // true se algum arquivo da árvore (em qualquer profundidade) foi escrito
    // há menos de `thresholdDays` dias, conforme passado a ComputeDirectoryStats.
    bool hasRecentActivity = false;
};

// Percorre `dir` recursivamente uma única vez, somando o tamanho de todos os
// arquivos e verificando se algum foi escrito há menos de `thresholdDays`
// dias. Existe para não fazer duas varreduras separadas (uma para tamanho,
// outra para idade) e, principalmente, para não decidir "atividade recente"
// olhando só o mtime da pasta de topo: no NTFS esse mtime só muda quando
// entradas são criadas/removidas diretamente nela, não quando arquivos em
// subpastas são escritos — então um app que só grava em subpastas profundas
// pareceria "inativo" mesmo em uso diário se só o topo fosse checado.
DirectoryStats ComputeDirectoryStats(const std::wstring& dir, int thresholdDays);

// Callback chamado para cada arquivo regular encontrado por ForEachFileRecursive.
using FileVisitor = std::function<void(const std::wstring& path, std::uint64_t sizeBytes)>;

// Percorre `dir` recursivamente (ignorando erros de permissão, na prática
// pastas de outros usuários sem privilégio de leitura), chamando `visitor`
// para cada arquivo regular encontrado. Interrompe a varredura assim que
// `cancel` for sinalizado. Compartilhado pelos scanners de temporários, logs
// antigos e duplicados para não reimplementar a mesma iteração de
// recursive_directory_iterator + skip_permission_denied em cada um.
void ForEachFileRecursive(const std::wstring& dir, std::atomic<bool>& cancel,
                           const FileVisitor& visitor);

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

} // namespace storagecleaner::util
