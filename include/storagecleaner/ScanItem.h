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
    WindowsUpdateCache,
    CrashDumps,
    ThumbnailCache,
};

// Ordem usada para iterar todas as categorias (UI, config, etc.).
inline constexpr Category kAllCategories[] = {
    Category::TempFiles,          Category::BrowserCache, Category::Duplicate,
    Category::OldLogs,            Category::RecycleBin,   Category::RestorePoints,
    Category::OrphanedApps,       Category::WindowsUpdateCache, Category::CrashDumps,
    Category::ThumbnailCache,
};

// Nome exibido na UI — pode mudar livremente (texto, acentuação).
inline const char* CategoryName(Category c) {
    switch (c) {
        case Category::TempFiles:     return "Arquivos temporários";
        case Category::BrowserCache:  return "Cache de navegador";
        case Category::Duplicate:     return "Arquivos duplicados";
        case Category::OldLogs:       return "Logs antigos";
        case Category::RecycleBin:    return "Lixeira";
        case Category::RestorePoints: return "Pontos de restauração";
        case Category::OrphanedApps:  return "Dados órfãos (apps desinstalados)";
        case Category::WindowsUpdateCache: return "Cache do Windows Update";
        case Category::CrashDumps:    return "Despejos e relatórios de erro";
        case Category::ThumbnailCache: return "Cache de miniaturas";
    }
    return "Desconhecido";
}

// Chave de persistência em config.json/history.json — NUNCA alterar: são os
// nomes antigos (sem acento) com que arquivos já gravados foram salvos. Mudar
// aqui faz o app ignorar a config/histórico existentes sem erro (travado por
// tests/test_category_keys.cpp).
inline const char* CategoryKey(Category c) {
    switch (c) {
        case Category::TempFiles:     return "Arquivos temporarios";
        case Category::BrowserCache:  return "Cache de navegador";
        case Category::Duplicate:     return "Arquivos duplicados";
        case Category::OldLogs:       return "Logs antigos";
        case Category::RecycleBin:    return "Lixeira";
        case Category::RestorePoints: return "Pontos de restauracao";
        case Category::OrphanedApps:  return "Dados orfaos (apps desinstalados)";
        case Category::WindowsUpdateCache: return "windows_update_cache";
        case Category::CrashDumps:    return "crash_dumps";
        case Category::ThumbnailCache: return "thumbnail_cache";
    }
    return "Desconhecido";
}

inline bool CategoryFromKey(const std::string& key, Category& out) {
    for (Category c : kAllCategories) {
        if (key == CategoryKey(c)) {
            out = c;
            return true;
        }
    }
    return false;
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

// Categorias cujas pastas/APIs só são acessíveis com o processo elevado. Sem
// elevação, o ScanEngine nem chama o scanner e a UI lista a categoria como
// "requer Administrador" em vez de mostrar zero itens como se estivesse limpa.
inline constexpr bool IsAdminRequiredCategory(Category c) {
    return c == Category::RestorePoints || c == Category::WindowsUpdateCache;
}

// Varridas mesmo sem elevação, mas só em parte (ex.: dumps do usuário sim,
// os do sistema não) — a UI avisa que o resultado está incompleto.
inline constexpr bool IsPartiallyAdminCategory(Category c) {
    return c == Category::CrashDumps;
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
