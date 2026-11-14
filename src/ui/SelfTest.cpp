#include "SelfTest.h"

#include "storagecleaner/OrphanClassifier.h"
#include "storagecleaner/PathExclusion.h"
#include "storagecleaner/Utils.h"

#include <imgui_internal.h>
#include <windows.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>

namespace storagecleaner::ui {

namespace {

constexpr std::uint64_t kKB = 1ull << 10;
constexpr std::uint64_t kMB = 1ull << 20;
constexpr std::uint64_t kGB = 1ull << 30;

template <class T>
void SafeRelease(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

} // namespace

SelfTest::SelfTest(UiState& state, std::wstring outDir) : state_(state), outDir_(std::move(outDir)) {
    BuildScript();
}

std::size_t SelfTest::BucketIndex(Category c) const {
    const auto& buckets = state_.lastScanResult.buckets;
    for (std::size_t i = 0; i < buckets.size(); ++i)
        if (buckets[i].category == c) return i;
    return 0;
}

void SelfTest::LoadFixture() {
    ScanResult result;
    result.scannedAt = std::chrono::system_clock::now();
    auto bucketFor = [&](Category c) -> CategoryBucket& {
        for (auto& b : result.buckets)
            if (b.category == c) return b;
        result.buckets.push_back(CategoryBucket{c, {}, 0});
        return result.buckets.back();
    };
    auto add = [&](Category c, std::wstring path, std::uint64_t size, bool isDir = false,
                   std::wstring keep = L"") -> ScanItem& {
        if (path.size() > 2 && path[1] == L':') expectedByDrive_[path[0]] += size;
        CategoryBucket& b = bucketFor(c);
        b.totalBytes += size;
        b.items.push_back(ScanItem{std::move(path), size, c, std::move(keep), isDir});
        return b.items.back();
    };
    // Órfão classificado pela mesma regra do scanner real.
    auto addOrphan = [&](std::wstring path, std::uint64_t size, const OrphanEvidence& evidence) {
        OrphanVerdict verdict = ClassifyOrphan(evidence);
        ScanItem& item = add(Category::OrphanedApps, std::move(path), size, true);
        item.existenceChance = verdict.chance;
        item.evidence = JoinReasons(verdict.reasons);
    };

    // Mesma ordem de kAllCategories, como o ScanEngine entrega.
    for (Category c : kAllCategories) bucketFor(c);

    struct TempFolder {
        const wchar_t* folder;
        int count;
    };
    const TempFolder tempFolders[] = {
        {L"C:\\Users\\Ana\\AppData\\Local\\Temp", 1200},
        {L"C:\\Users\\Ana\\AppData\\Local\\Temp\\chrome_installer", 300},
        {L"C:\\Windows\\Temp", 800},
        {L"C:\\Users\\Ana\\AppData\\Local\\Temp\\{6F2A9C1E-3B7D-4E8F-A1C2-9D0E5B4A7F63}\\extracao\\"
         L"pacote-de-instalacao\\recursos\\idiomas\\pt-BR",
         400},
        {L"D:\\Projetos\\build\\tmp", 299},
        {L"C:\\Users\\Ana\\Downloads\\parcial", 1},
    };
    int serial = 0;
    for (const auto& tf : tempFolders) {
        for (int i = 0; i < tf.count; ++i, ++serial) {
            wchar_t name[32];
            std::swprintf(name, 32, L"\\tmp%04d.tmp", serial);
            add(Category::TempFiles, std::wstring(tf.folder) + name,
                kKB + (static_cast<std::uint64_t>(serial) * 7919u) % (900 * kKB));
        }
    }

    add(Category::BrowserCache, L"C:\\Users\\Ana\\AppData\\Local\\Google\\Chrome\\User Data\\Default\\Cache",
        850 * kMB, true);
    add(Category::BrowserCache, L"C:\\Users\\Ana\\AppData\\Local\\Microsoft\\Edge\\User Data\\Default\\Cache",
        420 * kMB, true);
    add(Category::BrowserCache,
        L"C:\\Users\\Ana\\AppData\\Local\\Mozilla\\Firefox\\Profiles\\x1y2z3.default\\cache2", 96 * kMB, true);

    for (int i = 1; i <= 6; ++i) {
        wchar_t keep[64], dup[64];
        std::swprintf(keep, 64, L"D:\\Fotos\\2023\\IMG_%04d.jpg", i);
        std::swprintf(dup, 64, L"D:\\Fotos\\Backup antigo\\IMG_%04d (1).jpg", i);
        add(Category::Duplicate, dup, (3 + i) * kMB, false, keep);
    }

    add(Category::OldLogs, L"C:\\ProgramData\\Contabil\\logs\\relat\u00F3rio-2024-01.log", 15 * kMB);
    for (int i = 0; i < 19; ++i) {
        wchar_t path[96];
        std::swprintf(path, 96, L"C:\\ProgramData\\Contabil\\logs\\servico-%02d.log", i);
        add(Category::OldLogs, path, (200 + i * 37) * kKB);
    }

    add(Category::RecycleBin, L"(Lixeira)", 2 * kGB + 100 * kMB, true);
    add(Category::RestorePoints, L"{3808876b-c176-4e48-b7ae-04046e6cc752}", 5 * kGB);
    OrphanEvidence noSignal;
    noSignal.daysSinceLastWrite = 1100;
    addOrphan(L"C:\\Users\\Ana\\AppData\\Roaming\\EditorAntigo", 300 * kMB, noSignal);
    OrphanEvidence portable;
    portable.hasExecutables = true;
    portable.daysSinceLastWrite = 240;
    addOrphan(L"C:\\Users\\Ana\\AppData\\Local\\JogoPortatil", 180 * kMB, portable);
    OrphanEvidence startup;
    startup.startupEntry = true;
    startup.softwareRegistryKey = true;
    startup.daysSinceLastWrite = 200;
    addOrphan(L"C:\\Users\\Ana\\AppData\\Roaming\\Sincronizador", 90 * kMB, startup);
    add(Category::WindowsUpdateCache, L"C:\\Windows\\SoftwareDistribution\\Download\\a1b2c3d4e5", 900 * kMB, true);
    add(Category::WindowsUpdateCache, L"C:\\Windows\\SoftwareDistribution\\Download\\f6e7d8c9b0", 410 * kMB, true);
    add(Category::CrashDumps, L"C:\\Users\\Ana\\AppData\\Local\\CrashDumps\\jogo.exe.11872.dmp", 640 * kMB);
    add(Category::ThumbnailCache, L"C:\\Users\\Ana\\AppData\\Local\\Microsoft\\Windows\\Explorer\\thumbcache_256.db",
        2 * kMB);

    state_.AdoptScanResult(std::move(result));
    state_.requestedTab = 0;

    LightScanTotals totals;
    totals.tempBytes = 1200 * kMB;
    totals.recycleBinBytes = 2 * kGB + 100 * kMB;
    // C: abaixo dos 10% do alerta (cor de perigo); E: removível.
    totals.drives = {
        {L"C:\\", 476 * kGB, 18 * kGB, false},
        {L"D:\\", 931 * kGB, 412 * kGB, false},
        {L"E:\\", 32 * kGB, 5 * kGB, true},
    };
    std::lock_guard<std::mutex> lock(state_.lightScanMutex);
    state_.lightTotals = std::move(totals);
}

void SelfTest::BuildScript() {
    // A lista de categorias empurra as unidades para baixo da dobra: rola a
    // janela principal até o fim.
    steps_.push_back({"painel-unidades",
                      [this] {
                          state_.requestedTab = 0;
                          pendingScrollMainY_ = 100000.0f; // ImGui limita ao máximo
                      },
                      [this] {
        Check(state_.cleanableByDrive == expectedByDrive_,
              "limpável por unidade (C:, D:) bate com a soma do fixture, sem Lixeira/shadow copy");
        Check(state_.cleanableByDrive.count(L'E') == 0, "unidade sem itens (E:) não tem parte limpável");
                      },
                      6});

    steps_.push_back({"resultados-rosca",
                      [this] {
                          mousePos_ = ImVec2(-FLT_MAX, -FLT_MAX);
                          state_.requestedTab = 1;
                      },
                      [this] {
        Check(state_.selfTestProbe.donutRadius > 0.0f, "rosca por categoria desenhada na aba Resultados");
    }});

    steps_.push_back({"rosca-hover",
                      [this] {
                          // Mira no meio da fatia dos pontos de restauração (a maior).
                          const auto& buckets = state_.lastScanResult.buckets;
                          double total = static_cast<double>(state_.lastScanResult.GrandTotalBytes());
                          std::size_t target = BucketIndex(Category::RestorePoints);
                          double before = 0.0;
                          for (std::size_t i = 0; i < target; ++i) before += static_cast<double>(buckets[i].totalBytes);
                          double mid = (before + buckets[target].totalBytes * 0.5) / total;
                          float angle = static_cast<float>(-3.14159265358979 / 2.0 + mid * 2.0 * 3.14159265358979);
                          const SelfTestProbe& p = state_.selfTestProbe;
                          mousePos_ = ImVec2(p.donutCenter.x + std::cos(angle) * p.donutRadius,
                                             p.donutCenter.y + std::sin(angle) * p.donutRadius);
                      },
                      [this] {
                          Check(state_.selfTestProbe.donutHovered ==
                                    static_cast<int>(BucketIndex(Category::RestorePoints)),
                                "mouse sobre a fatia destaca a categoria certa (pontos de restauração)");
                      }});

    steps_.push_back({"categoria-aberta",
                      [this] {
                          mousePos_ = ImVec2(-FLT_MAX, -FLT_MAX);
                          state_.RequestOpenCategory(BucketIndex(Category::TempFiles));
                      },
                      [this] {
                          std::size_t b = BucketIndex(Category::TempFiles);
                          const BucketView& view = state_.bucketViews[b];
                          Check(!state_.pendingOpenBucket.has_value(), "pedido de abrir categoria foi consumido");
                          Check(view.builtVersion == state_.resultVersion, "tabela da categoria aberta foi montada");
                          Check(view.groups.size() == 6 && view.rows.size() == 6,
                                "3000 temporários viram 6 linhas de pasta (todas recolhidas)");
                          Check(view.visibleItems == 3000, "sem filtro, os 3000 itens estão visíveis");
                      }});

    steps_.push_back({"grupos-abertos-tamanho",
                      [this] {
                          std::size_t b = BucketIndex(Category::TempFiles);
                          BucketView& view = state_.GetBucketView(b);
                          for (std::size_t g = 0; g < view.groups.size(); ++g)
                              if (!state_.IsGroupOpen(view, g)) state_.ToggleGroup(b, g);
                      },
                      [this] {
                          std::size_t b = BucketIndex(Category::TempFiles);
                          BucketView& view = state_.GetBucketView(b);
                          std::size_t expectedRows = 0;
                          bool sorted = true;
                          for (std::size_t g = 0; g < view.groups.size(); ++g) {
                              std::size_t n = view.groups[g].itemIndices.size();
                              expectedRows += n == 1 ? 1 : n + 1;
                              if (g > 0 && view.groups[g - 1].totalBytes < view.groups[g].totalBytes) sorted = false;
                          }
                          Check(view.rows.size() == expectedRows, "grupos abertos mostram cabeçalho + itens");
                          Check(sorted, "ordenação padrão: pastas do maior para o menor total");
                      },
                      30, true});

    steps_.push_back({"ordenado-por-nome",
                      [this] {
                          state_.SetResultsSort(BucketIndex(Category::TempFiles), ItemSortKey::Name, false, false);
                      },
                      [this] {
                          BucketView& view = state_.GetBucketView(BucketIndex(Category::TempFiles));
                          bool sorted = true;
                          for (std::size_t g = 1; g < view.groups.size(); ++g)
                              if (NormalizePathForCompare(view.groups[g - 1].folder) >
                                  NormalizePathForCompare(view.groups[g].folder))
                                  sorted = false;
                          Check(view.sortKey == ItemSortKey::Name && sorted, "ordenação por nome: pastas em ordem alfabética");
                          Check(!view.sortChangedExternally, "seta do cabeçalho sincronizada com a ordenação");
                      }});

    steps_.push_back({"filtro-texto",
                      [this] {
                          state_.SetResultsSort(BucketIndex(Category::TempFiles), ItemSortKey::Size, true, false);
                          std::snprintf(state_.resultsFilter, sizeof(state_.resultsFilter), "RELAT");
                          state_.RequestOpenCategory(BucketIndex(Category::OldLogs));
                      },
                      [this] {
                          std::size_t total = 0;
                          for (std::size_t b = 0; b < state_.lastScanResult.buckets.size(); ++b)
                              total += state_.GetBucketView(b).visibleItems;
                          BucketView& logs = state_.GetBucketView(BucketIndex(Category::OldLogs));
                          Check(total == 1 && logs.visibleItems == 1,
                                "filtro 'RELAT' acha só 'relatório-2024-01.log' (ignora maiúsculas)");
                          Check(!logs.rows.empty() && logs.rows.back().item != ResultRow::kGroupRow,
                                "com texto no filtro o item aparece sem precisar abrir a pasta");
                      }});

    steps_.push_back({"filtro-tamanho",
                      [this] {
                          state_.resultsFilter[0] = '\0';
                          state_.resultsMinSizeIdx = 2; // > 10 MB
                          state_.RequestOpenCategory(BucketIndex(Category::Duplicate));
                      },
                      [this] {
                          bool ok = true;
                          for (std::size_t b = 0; b < state_.lastScanResult.buckets.size(); ++b) {
                              std::size_t expected = 0;
                              for (const auto& item : state_.lastScanResult.buckets[b].items)
                                  if (item.sizeBytes >= 10 * kMB) ++expected;
                              if (state_.GetBucketView(b).visibleItems != expected) ok = false;
                          }
                          Check(ok, "tamanho mínimo > 10 MB: cada categoria mostra só os itens >= 10 MB");
                      }});

    auto selectedBefore = std::make_shared<std::uint64_t>(0);
    auto groupBytes = std::make_shared<std::uint64_t>(0);
    steps_.push_back({"grupo-desmarcado",
                      [this, selectedBefore, groupBytes] {
                          state_.resultsMinSizeIdx = 0;
                          std::size_t b = BucketIndex(Category::TempFiles);
                          state_.RequestOpenCategory(b);
                          *selectedBefore = state_.GetSelectionSummary().bytes;
                          BucketView& view = state_.GetBucketView(b);
                          *groupBytes = view.groups[0].totalBytes;
                          state_.SetGroupSelected(b, 0, false);
                      },
                      [this, selectedBefore, groupBytes] {
                          std::size_t b = BucketIndex(Category::TempFiles);
                          Check(state_.GetSelectionSummary().bytes == *selectedBefore - *groupBytes,
                                "desmarcar a pasta tira exatamente o total dela do 'Total selecionado'");
                          Check(state_.GroupSelectionState(b, 0) == TriState::None, "checkbox da pasta desmarcado");
                          Check(state_.VisibleSelectionState(b) == TriState::Some,
                                "'Selecionar todos' da categoria fica em estado misto");
                          Check(state_.VisibleSelectionState(BucketIndex(Category::OrphanedApps)) == TriState::All,
                                "órfãos de todos os níveis vêm pré-marcados");
                      }});

    steps_.push_back({"duplicados",
                      [this] {
                          std::size_t b = BucketIndex(Category::Duplicate);
                          state_.RequestOpenCategory(b);
                          BucketView& view = state_.GetBucketView(b);
                          if (!view.groups.empty() && !state_.IsGroupOpen(view, 0)) state_.ToggleGroup(b, 0);
                      },
                      [this] {
                          BucketView& view = state_.GetBucketView(BucketIndex(Category::Duplicate));
                          Check(view.groups.size() == 1 && view.visibleItems == 6,
                                "6 duplicados agrupados na mesma pasta");
                      }});

    steps_.push_back({"orfaos-niveis",
                      [this] { state_.RequestOpenCategory(BucketIndex(Category::OrphanedApps)); },
                      [this] {
                          std::size_t b = BucketIndex(Category::OrphanedApps);
                          BucketView& view = state_.GetBucketView(b);
                          Check(view.groups.size() == 3 && view.groups[0].chance == ExistenceChance::Low &&
                                    view.groups[1].chance == ExistenceChance::Medium &&
                                    view.groups[2].chance == ExistenceChance::High,
                                "órfãos agrupados na ordem Baixa, Média, Alta");
                          Check(view.rows.size() == 6, "cada nível tem cabeçalho aberto com seu item");
                          Check(state_.GetSelectionSummary().riskyHeuristic == 2,
                                "resumo conta 2 órfãos de chance média/alta marcados");
                          std::size_t eligible = 0;
                          bool onlyLow = true;
                          for (const auto& item : state_.lastScanResult.buckets[b].items) {
                              if (!IsAutoCleanEligible(item)) continue;
                              ++eligible;
                              if (item.existenceChance != ExistenceChance::Low) onlyLow = false;
                          }
                          Check(eligible == 1 && onlyLow, "limpeza automática só levaria o órfão de chance Baixa");
                      }});

    steps_.push_back({"confirmacao-orfaos",
                      [this] { state_.showCleanConfirmModal = true; },
                      [this] {
                          Check(state_.showCleanConfirmModal && !state_.cleaning.load(),
                                "modal de confirmação aberto, nada sendo limpo");
                          Check(state_.GetSelectionSummary().riskyHeuristic == 2,
                                "modal recebe as 2 pastas de chance média/alta para o aviso");
                      }});
}

void SelfTest::Check(bool ok, const std::string& description) {
    if (!ok) ++failures_;
    report_.push_back((ok ? "PASS  " : "FALHA ") + std::string(steps_[stepIndex_].name) + ": " + description);
}

void SelfTest::Info(const std::string& line) { report_.push_back("INFO  " + line); }

void SelfTest::BeforeNewFrame() {
    if (stepIndex_ < steps_.size() && !actionDone_) {
        steps_[stepIndex_].action();
        actionDone_ = true;
        framesInStep_ = 0;
    }
    ImGui::GetIO().AddMousePosEvent(mousePos_.x, mousePos_.y);
    // Só depois de um frame do passo: no primeiro frame do app a janela
    // principal ainda não existe.
    if (pendingScrollMainY_ >= 0.0f && framesInStep_ > 0) {
        if (ImGuiWindow* main = ImGui::FindWindowByName("StorageCleanerMain")) {
            ImGui::SetScrollY(main, pendingScrollMainY_);
            pendingScrollMainY_ = -1.0f;
        }
    }
    frameStart_ = std::chrono::steady_clock::now();
}

bool SelfTest::AfterRender(IDXGISwapChain* swapChain, ID3D11Device* device, ID3D11DeviceContext* context) {
    if (stepIndex_ >= steps_.size()) return true;
    Step& step = steps_[stepIndex_];
    ++framesInStep_;
    // Primeiro frame do passo fica de fora da medição: é o que reconstrói a lista.
    if (step.measureFrameTime && framesInStep_ > 1) {
        measuredMs_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameStart_).count();
        ++measuredFrames_;
    }
    if (framesInStep_ < step.frames) return false;

    step.check();
    if (step.measureFrameTime && measuredFrames_ > 0) {
        double avg = measuredMs_ / measuredFrames_;
        char buf[160];
        std::snprintf(buf, sizeof(buf), "tempo médio de frame (UI + render) com 3000 itens abertos: %.2f ms%s",
                      avg, avg > 16.0 ? " - AVISO: acima de 16 ms" : "");
        Info(buf);
    }
    wchar_t file[96];
    std::swprintf(file, 96, L"%02zu-%hs.png", stepIndex_ + 1, step.name);
    std::wstring path = outDir_ + L"\\" + file;
    Check(Capture(swapChain, device, context, path), "captura " + util::WideToUtf8(file));

    ++stepIndex_;
    actionDone_ = false;
    if (stepIndex_ < steps_.size()) return false;
    WriteReport();
    return true;
}

bool SelfTest::Capture(IDXGISwapChain* swapChain, ID3D11Device* device, ID3D11DeviceContext* context,
                       const std::wstring& path) {
    ID3D11Texture2D* backBuffer = nullptr;
    ID3D11Texture2D* staging = nullptr;
    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    bool ok = false;
    // Mesmo padrão de FileDialogs.cpp: CoInitializeEx é contado por referência.
    HRESULT comInit = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    do {
        if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) break;
        D3D11_TEXTURE2D_DESC desc{};
        backBuffer->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        if (FAILED(device->CreateTexture2D(&desc, nullptr, &staging))) break;
        context->CopyResource(staging, backBuffer);

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) break;
        // Cópia com alpha opaco: o ImGui escreve alpha < 1 nas bordas
        // suavizadas e o PNG sairia com "furos" transparentes.
        const UINT width = desc.Width, height = desc.Height, stride = width * 4;
        std::vector<BYTE> pixels(static_cast<std::size_t>(stride) * height);
        for (UINT y = 0; y < height; ++y)
            std::memcpy(&pixels[static_cast<std::size_t>(y) * stride],
                        static_cast<const BYTE*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch, stride);
        context->Unmap(staging, 0);
        for (std::size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;

        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&factory))))
            break;
        if (FAILED(factory->CreateStream(&stream)) ||
            FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)))
            break;
        if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
            FAILED(encoder->Initialize(stream, WICBitmapEncoderNoCache)))
            break;
        if (FAILED(encoder->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr)) ||
            FAILED(frame->SetSize(width, height)))
            break;
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppRGBA;
        if (FAILED(frame->SetPixelFormat(&format))) break;
        if (format == GUID_WICPixelFormat32bppBGRA) {
            for (std::size_t i = 0; i + 2 < pixels.size(); i += 4) std::swap(pixels[i], pixels[i + 2]);
        } else if (format != GUID_WICPixelFormat32bppRGBA) {
            break;
        }
        if (FAILED(frame->WritePixels(height, stride, static_cast<UINT>(pixels.size()), pixels.data())) ||
            FAILED(frame->Commit()) || FAILED(encoder->Commit()))
            break;
        ok = true;
    } while (false);

    SafeRelease(frame);
    SafeRelease(encoder);
    SafeRelease(stream);
    SafeRelease(factory);
    SafeRelease(staging);
    SafeRelease(backBuffer);
    if (SUCCEEDED(comInit)) ::CoUninitialize();
    return ok;
}

void SelfTest::WriteReport() {
    std::ofstream out(std::filesystem::path(outDir_) / L"report.txt", std::ios::binary | std::ios::trunc);
    for (const auto& line : report_) out << line << "\n";
    out << (failures_ == 0 ? "RESULTADO: OK" : "RESULTADO: " + std::to_string(failures_) + " falha(s)") << "\n";
}

} // namespace storagecleaner::ui
