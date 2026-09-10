#pragma once

#include <cstdint>
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

// Retorna true se o arquivo foi modificado há mais de `days` dias.
bool IsOlderThanDays(const std::wstring& path, int days);

// Soma recursivamente o tamanho de todos os arquivos sob `dir`. Usado para
// dimensionar pastas inteiras (ex: cache de navegador) como um único item.
std::uint64_t DirectorySize(const std::wstring& dir);

// Executa um comando externo (ex: vssadmin, schtasks) capturando stdout.
// Usado em vez de uma dependência externa de processo, já que os comandos
// necessários fazem parte do próprio Windows.
struct CommandResult {
    int exitCode = -1;
    std::string output;
};
CommandResult RunCommandCaptureOutput(const std::wstring& commandLine);

// Wrapper fino sobre SHGetKnownFolderPath; retorna string vazia em falha.
// O chamador inclui <knownfolders.h> para ter as constantes FOLDERID_*.
std::wstring KnownFolderPath(const GUID& folderId);

// Lista as pastas de perfil de usuário em C:\Users (uma por usuário). Usado
// pelos scanners que precisam varrer temporários/cache/AppData de todos os
// usuários da máquina, não só do usuário atual.
std::vector<std::wstring> EnumerateUserProfileDirs();

} // namespace storagecleaner::util
