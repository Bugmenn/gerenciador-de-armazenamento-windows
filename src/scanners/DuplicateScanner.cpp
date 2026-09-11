#include "storagecleaner/DuplicateGrouping.h"
#include "storagecleaner/Scanner.h"
#include "storagecleaner/Utils.h"

#include <knownfolders.h>

#include <filesystem>
#include <unordered_map>

namespace fs = std::filesystem;

namespace storagecleaner {

namespace {

std::vector<std::wstring> DefaultDuplicateRoots() {
    std::vector<std::wstring> roots;
    for (const GUID& folder : {FOLDERID_Documents, FOLDERID_Desktop, FOLDERID_Downloads,
                                FOLDERID_Pictures}) {
        std::wstring path = util::KnownFolderPath(folder);
        if (!path.empty()) roots.push_back(path);
    }
    return roots;
}

} // namespace

std::vector<ScanItem> ScanDuplicates(const Config& config, ProgressChannel& progress,
                                      std::atomic<bool>& cancel) {
    std::vector<ScanItem> items;
    ProgressSnapshot snapshot;
    snapshot.phase = Phase::ScanningDuplicates;
    progress.Update(snapshot);

    std::vector<std::wstring> roots =
        config.duplicateScanRoots.empty() ? DefaultDuplicateRoots() : config.duplicateScanRoots;

    // Passo 1: agrupa por tamanho exato. Arquivos de tamanhos diferentes
    // nunca são duplicados, então isso descarta a maioria dos arquivos sem
    // precisar ler o conteúdo de nenhum deles. Guarda o caminho como wstring
    // (não fs::path) para não pagar a conversão extra toda vez que for lido
    // depois no passo 2.
    std::unordered_map<std::uint64_t, std::vector<std::wstring>> bySize;
    for (const auto& root : roots) {
        if (cancel.load()) break;
        util::ForEachFileRecursive(root, cancel, [&](const std::wstring& path, std::uint64_t size) {
            if (size < config.minFileSizeBytes) return;
            bySize[size].push_back(path);
        });
    }

    // Passo 2: só faz hash dentro de cada grupo de mesmo tamanho — o custo de
    // I/O de hashear o disco inteiro seria proibitivo, mas hashear apenas os
    // candidatos plausíveis (mesmo tamanho) é viável. O agrupamento por hash
    // em si (GroupDuplicatesByHash) é uma função pura, coberta por
    // tests/test_duplicate_grouping.cpp sem depender de I/O real.
    for (auto& [size, paths] : bySize) {
        if (cancel.load()) break;
        if (paths.size() < 2) continue; // tamanho único: não pode ter duplicata

        std::vector<HashedFile> hashed;
        for (const auto& path : paths) {
            if (cancel.load()) break;

            HashedFile file;
            file.path = path;
            file.sizeBytes = size;
            file.sha256Hex = util::HashFileSHA256(file.path);

            std::error_code timeEc;
            auto writeTime = fs::last_write_time(path, timeEc);
            file.lastWriteTicks = timeEc ? 0 : writeTime.time_since_epoch().count();

            hashed.push_back(std::move(file));

            snapshot.itemsProcessed++;
            snapshot.currentItem = path;
            if (snapshot.itemsProcessed % 16 == 0) progress.Update(snapshot);
        }

        for (auto& item : GroupDuplicatesByHash(std::move(hashed))) {
            snapshot.bytesFoundSoFar += item.sizeBytes;
            items.push_back(std::move(item));
        }
    }

    snapshot.fractionComplete = 1.0f;
    progress.Update(snapshot);
    return items;
}

} // namespace storagecleaner
