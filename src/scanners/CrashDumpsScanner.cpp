#include "storagecleaner/Scanner.h"
#include "storagecleaner/Utils.h"
#include "storagecleaner/VssRestorePoints.h"

#include <windows.h>
#include <shlobj.h>

#include <filesystem>

namespace fs = std::filesystem;

namespace storagecleaner {

namespace {

void AddItem(std::vector<ScanItem>& items, ProgressChannel& progress, ProgressSnapshot& snapshot,
             const std::wstring& path, std::uint64_t size, bool isDirectory) {
    items.push_back(ScanItem{path, size, Category::CrashDumps, L"", isDirectory});
    snapshot.itemsProcessed++;
    snapshot.bytesFoundSoFar += size;
    snapshot.currentItem = path;
    if (snapshot.itemsProcessed % 16 == 0) progress.Update(snapshot);
}

void ScanDumpFiles(const fs::path& dir, const Config& config, ProgressChannel& progress,
                   std::atomic<bool>& cancel, std::vector<ScanItem>& items,
                   ProgressSnapshot& snapshot) {
    util::ForEachFileRecursive(dir.wstring(), cancel, [&](const std::wstring& path,
                                                          std::uint64_t size) {
        if (size < config.minFileSizeBytes) return;
        // Um dump sendo gravado agora (processo acabou de travar) ainda está
        // em uso pelo WER; espera o mesmo intervalo mínimo dos temporários.
        if (!util::IsOlderThanHours(path, config.tempFilesMinAgeHours)) return;
        AddItem(items, progress, snapshot, path, size, false);
    });
}

// Cada relatório do WER é uma pasta com vários arquivos pequenos; listar como
// um item por pasta mantém a tabela legível.
void ScanWerReports(const fs::path& dir, const Config& config, ProgressChannel& progress,
                    std::atomic<bool>& cancel, std::vector<ScanItem>& items,
                    ProgressSnapshot& snapshot) {
    std::error_code ec;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (cancel.load()) break;
        std::error_code entryEc;
        if (!it->is_directory(entryEc)) continue;
        if (!util::IsOlderThanHours(it->path().wstring(), config.tempFilesMinAgeHours)) continue;
        std::uint64_t size = util::DirectorySize(it->path().wstring(), cancel);
        if (size < config.minFileSizeBytes) continue;
        AddItem(items, progress, snapshot, it->path().wstring(), size, true);
    }
}

} // namespace

std::vector<ScanItem> ScanCrashDumps(const Config& config, ProgressChannel& progress,
                                     std::atomic<bool>& cancel) {
    std::vector<ScanItem> items;
    ProgressSnapshot snapshot;
    snapshot.phase = Phase::ScanningCrashDumps;
    progress.Update(snapshot);

    // Minidump, MEMORY.DMP e WER só podem ser excluídos (e, no WER, nem
    // medidos) com o processo elevado. Sem elevação, fs::file_size do
    // MEMORY.DMP ainda funciona (lê só metadados) e o item apareceria
    // pré-selecionado para depois falhar com acesso negado — então essas
    // partes ficam de fora e a UI avisa (IsPartiallyAdminCategory).
    const bool elevated = IsRunningElevated();
    std::wstring windowsDir = elevated ? util::KnownFolderPath(FOLDERID_Windows) : L"";
    if (!windowsDir.empty()) {
        ScanDumpFiles(fs::path(windowsDir) / L"Minidump", config, progress, cancel, items, snapshot);

        fs::path memoryDump = fs::path(windowsDir) / L"MEMORY.DMP";
        std::error_code ec;
        std::uint64_t size = fs::file_size(memoryDump, ec);
        if (!ec && size >= config.minFileSizeBytes &&
            util::IsOlderThanHours(memoryDump.wstring(), config.tempFilesMinAgeHours))
            AddItem(items, progress, snapshot, memoryDump.wstring(), size, false);
    }

    for (const auto& profileDir : util::EnumerateUserProfileDirs()) {
        if (cancel.load()) break;
        ScanDumpFiles(fs::path(profileDir) / L"AppData" / L"Local" / L"CrashDumps", config, progress,
                      cancel, items, snapshot);
    }

    std::wstring programData = elevated ? util::KnownFolderPath(FOLDERID_ProgramData) : L"";
    if (!programData.empty() && !cancel.load()) {
        fs::path wer = fs::path(programData) / L"Microsoft" / L"Windows" / L"WER";
        ScanWerReports(wer / L"ReportArchive", config, progress, cancel, items, snapshot);
        ScanWerReports(wer / L"ReportQueue", config, progress, cancel, items, snapshot);
    }

    snapshot.fractionComplete = 1.0f;
    progress.Update(snapshot);
    return items;
}

} // namespace storagecleaner
