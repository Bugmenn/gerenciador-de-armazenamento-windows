#include "storagecleaner/DuplicateGrouping.h"
#include "storagecleaner/Scanner.h"
#include "storagecleaner/Utils.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace storagecleaner {

namespace {

// Bytes lidos de cada ponta no hash parcial: o suficiente para separar quase
// todos os arquivos diferentes de mesmo tamanho (cabeçalho + final) lendo só
// 128 KB em vez do arquivo inteiro.
constexpr std::uint32_t kEdgeBytes = 64 * 1024;

std::vector<std::wstring> DefaultDuplicateRoots() {
    std::vector<std::wstring> roots;
    for (const GUID& folder : {FOLDERID_Documents, FOLDERID_Desktop, FOLDERID_Downloads,
                                FOLDERID_Pictures}) {
        std::wstring path = util::KnownFolderPath(folder);
        if (!path.empty()) roots.push_back(path);
    }
    return roots;
}

struct Candidate {
    std::wstring path;
    std::uint64_t sizeBytes = 0;
    std::int64_t lastWriteTicks = 0;
    std::string edgeHash;
    std::string fullHash;
};

// Leitura concorrente só ajuda em SSD; em HD (ou sem saber) a cabeça do disco
// pularia entre arquivos e ficaria mais lento que um por vez.
int HashThreadCount(const std::vector<std::wstring>& roots) {
    int threads = 4;
    for (const auto& root : roots) {
        if (root.size() < 2 || root[1] != L':') return 1;
        threads = std::min(threads, util::ReadParallelism(root[0]));
    }
    return roots.empty() ? 1 : threads;
}

// Roda `work` para cada job em `threads` workers (um FileHasher por worker,
// que não é thread-safe) e chama `onTick` a cada ~100 ms na thread chamadora
// — só ela publica progresso.
void HashInParallel(std::vector<Candidate*>& jobs, int threads, std::atomic<bool>& cancel,
                    const std::function<void(util::FileHasher&, Candidate&)>& work,
                    const std::function<void()>& onTick) {
    if (jobs.empty()) return;
    std::atomic<std::size_t> next{0};
    std::atomic<int> running{threads};
    std::vector<std::thread> workers;
    for (int t = 0; t < threads; ++t) {
        workers.emplace_back([&] {
            util::FileHasher hasher;
            for (std::size_t i = next++; i < jobs.size() && !cancel.load(); i = next++) work(hasher, *jobs[i]);
            --running;
        });
    }
    while (running.load() > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        onTick();
    }
    for (auto& worker : workers) worker.join();
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
    // precisar ler o conteúdo de nenhum deles. A data vem da própria listagem.
    std::unordered_map<std::uint64_t, std::vector<Candidate>> bySize;
    for (const auto& root : roots) {
        if (cancel.load()) break;
        util::ForEachFileRecursive(root, cancel, [&](const util::FileEntry& entry) {
            if (entry.sizeBytes < config.minFileSizeBytes) return;
            // Arquivo só na nuvem (OneDrive "sob demanda"): ler para hashear
            // baixaria o arquivo inteiro da internet.
            if (entry.attributes & (FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS | FILE_ATTRIBUTE_RECALL_ON_OPEN |
                                    FILE_ATTRIBUTE_OFFLINE))
                return;
            bySize[entry.sizeBytes].push_back(Candidate{entry.path, entry.sizeBytes, entry.lastWriteTicks, {}, {}});
        });
    }

    const int threads = HashThreadCount(roots);
    std::mutex currentMutex;
    std::wstring current;
    std::atomic<std::uint64_t> done{0};
    std::uint64_t phaseBase = 0, phaseTotal = 0;
    float phaseOffset = 0.0f;
    // Metade do anel para o hash das pontas, metade para os completos (cujo
    // total só se conhece depois das pontas).
    auto publish = [&] {
        std::uint64_t d = done.load();
        snapshot.itemsProcessed = phaseBase + d;
        {
            std::lock_guard<std::mutex> lock(currentMutex);
            snapshot.currentItem = current;
        }
        float within = phaseTotal ? static_cast<float>(static_cast<double>(d) / static_cast<double>(phaseTotal)) : 1.0f;
        snapshot.fractionComplete = phaseOffset + 0.5f * within;
        progress.Update(snapshot);
    };
    auto track = [&](const Candidate& c) {
        ++done;
        std::lock_guard<std::mutex> lock(currentMutex);
        current = c.path;
    };

    // Passo 2: hash das pontas (primeiros e últimos 64 KB) de todo arquivo de
    // tamanho repetido — separa quase todos os diferentes sem ler o resto.
    std::vector<Candidate*> edgeJobs;
    for (auto& [size, group] : bySize)
        if (group.size() >= 2)
            for (auto& c : group) edgeJobs.push_back(&c);
    phaseTotal = edgeJobs.size();
    HashInParallel(
        edgeJobs, threads, cancel,
        [&](util::FileHasher& hasher, Candidate& c) {
            c.edgeHash = hasher.Edges(c.path, c.sizeBytes, kEdgeBytes);
            // Arquivo de até 2 pontas já foi lido inteiro: o hash das pontas
            // é o SHA-256 completo dele.
            if (c.sizeBytes <= 2ull * kEdgeBytes) c.fullHash = c.edgeHash;
            track(c);
        },
        publish);

    // Passo 3: SHA-256 completo só de quem empatou nas pontas com outro
    // arquivo de mesmo tamanho. É ele que decide "é duplicado".
    std::vector<Candidate*> fullJobs;
    for (auto& [size, group] : bySize) {
        if (group.size() < 2) continue;
        std::unordered_map<std::string, int> edgeCount;
        for (const auto& c : group)
            if (!c.edgeHash.empty()) ++edgeCount[c.edgeHash];
        for (auto& c : group)
            if (!c.edgeHash.empty() && c.fullHash.empty() && edgeCount[c.edgeHash] >= 2) fullJobs.push_back(&c);
    }
    phaseBase = phaseTotal;
    phaseTotal = fullJobs.size();
    phaseOffset = 0.5f;
    done = 0;
    HashInParallel(
        fullJobs, threads, cancel,
        [&](util::FileHasher& hasher, Candidate& c) {
            c.fullHash = hasher.Full(c.path);
            track(c);
        },
        publish);

    if (!cancel.load()) {
        // O agrupamento final é o mesmo de antes (função pura, testada em
        // tests/test_duplicate_grouping.cpp), só que com menos arquivos lidos.
        for (auto& [size, group] : bySize) {
            if (group.size() < 2) continue;
            std::vector<HashedFile> hashed;
            for (auto& c : group)
                if (!c.fullHash.empty()) hashed.push_back(HashedFile{std::move(c.path), size, std::move(c.fullHash), c.lastWriteTicks});
            for (auto& item : GroupDuplicatesByHash(std::move(hashed))) {
                snapshot.bytesFoundSoFar += item.sizeBytes;
                items.push_back(std::move(item));
            }
        }
    }

    snapshot.fractionComplete = 1.0f;
    progress.Update(snapshot);
    return items;
}

} // namespace storagecleaner
