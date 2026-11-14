#pragma once

#include "storagecleaner/ScanItem.h"

#include <string>
#include <vector>

namespace storagecleaner {

// Sinais, coletados pelo OrphanedAppDataScanner, de que o app dono de uma
// pasta de AppData ainda existe. Livre de Win32 para a regra ser testável
// (tests/test_orphan_classifier.cpp).
struct OrphanEvidence {
    // Fortes: o app está claramente presente.
    bool runningProcess = false; // processo em execução a partir da pasta
    bool startupEntry = false;   // Run/RunOnce do registro aponta para a pasta
    bool shortcutTarget = false; // atalho (Menu Iniciar, Área de Trabalho, Inicializar) aponta para dentro
    bool uninstallPath = false;  // InstallLocation/DisplayIcon/UninstallString de um app instalado dentro da pasta
    bool publisherMatch = false; // nome da pasta casa com o fabricante de um app instalado
    // Fracos: pode ser só sobra de um app removido.
    bool hasExecutables = false;      // há .exe dentro da pasta (app portátil ou instalado por usuário)
    bool softwareRegistryKey = false; // existe HKCU/HKLM\Software\<nome da pasta>
    int daysSinceLastWrite = 0;
    // Pasta de outro perfil (ou do modelo Default): os sinais do registro do
    // usuário, atalhos e processos são os de quem roda o app, não os do dono
    // da pasta — ausência de sinal não prova nada, então nunca fica em Baixa.
    bool otherProfile = false;
};

struct OrphanVerdict {
    ExistenceChance chance = ExistenceChance::Low;
    // Motivos em pt-BR (UTF-8, só Latin-1), na ordem do mais forte ao mais fraco.
    std::vector<std::string> reasons;
};

// Escrita recente (em dias) que ainda conta como sinal de uso: abaixo disso,
// sem nenhum outro sinal, a pasta fica em Média em vez de Baixa.
inline constexpr int kRecentUseDays = 365;

// Alta: qualquer sinal forte. Média: só sinais fracos, escrita há menos de
// kRecentUseDays ou pasta de outro perfil. Baixa: nenhum sinal.
OrphanVerdict ClassifyOrphan(const OrphanEvidence& evidence);

// Junta os motivos num texto curto para a UI ("a; b; c").
std::string JoinReasons(const std::vector<std::string>& reasons);

// true se `text` (linha de comando do Run, destino de atalho, caminho do
// registro) aponta para `folder` ou para algo dentro dela. Ignora maiúsculas
// (inclusive acentuadas) e trata '/' como '\'; "C:\x\App2" e "C:\x\App.exe"
// não contam como dentro de "C:\x\App".
bool MentionsFolder(const std::wstring& text, const std::wstring& folder);

// Igual a MentionsFolder, com os dois lados já passados por
// NormalizePathForCompare — para comparar uma lista grande contra várias
// pastas sem renormalizar a lista a cada pasta.
bool MentionsNormalizedFolder(const std::wstring& normalizedText, const std::wstring& normalizedFolder);

} // namespace storagecleaner
