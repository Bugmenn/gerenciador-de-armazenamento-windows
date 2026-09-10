#pragma once

#include <cstdint>
#include <string>

namespace storagecleaner {

enum class Category {
    TempFiles,
    BrowserCache,
    Duplicate,
    OldLogs,
    RecycleBin,
    RestorePoints,
    OrphanedApps,
};

// Ordem usada para iterar todas as categorias (UI, config, etc.).
inline constexpr Category kAllCategories[] = {
    Category::TempFiles,   Category::BrowserCache,  Category::Duplicate,
    Category::OldLogs,     Category::RecycleBin,    Category::RestorePoints,
    Category::OrphanedApps,
};

inline const char* CategoryName(Category c) {
    switch (c) {
        case Category::TempFiles:     return "Arquivos temporarios";
        case Category::BrowserCache:  return "Cache de navegador";
        case Category::Duplicate:     return "Arquivos duplicados";
        case Category::OldLogs:       return "Logs antigos";
        case Category::RecycleBin:    return "Lixeira";
        case Category::RestorePoints: return "Pontos de restauracao";
        case Category::OrphanedApps:  return "Dados orfaos (apps desinstalados)";
    }
    return "Desconhecido";
}

// A Lixeira e os pontos de restauração não têm conceito de "desfazer": esvaziar
// a Lixeira ou apagar um shadow copy é definitivo. Todo o resto é enviado para
// a Lixeira do Windows (reversível) ao ser removido.
inline constexpr bool IsPermanentCategory(Category c) {
    return c == Category::RecycleBin || c == Category::RestorePoints;
}

// "Dados orfaos" é heurístico (pode dar falso positivo em apps portáteis ou
// pouco usados) — por isso nunca participa da limpeza automática nem vem
// pré-selecionado na revisão manual; o usuário sempre marca item a item.
inline constexpr bool IsHeuristicCategory(Category c) {
    return c == Category::OrphanedApps;
}

// Representa um item encontrado pelo scanner que pode ser removido para liberar espaço.
struct ScanItem {
    std::wstring path;
    std::uint64_t sizeBytes = 0;
    Category category;
    // Para duplicatas: caminho do arquivo "original" que será mantido.
    std::wstring keepPath;
    // Se true, o "path" é um diretório inteiro a remover (ex: pasta de cache),
    // caso contrário é um arquivo único.
    bool isDirectory = false;
};

} // namespace storagecleaner
