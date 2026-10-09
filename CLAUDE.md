# CLAUDE.md — gerenciador-de-armazenamento-windows

Guia para o Claude Code neste repositório.

## Leitura obrigatória ao iniciar

Antes de qualquer tarefa, ler nesta ordem:
1. Memória do agente (`~/.claude/projects/c--Users-gabri--rea-de-Trabalho-gerenciador-de-armazenamento-windows/memory/MEMORY.md`) — preferências do usuário, carregada automaticamente
2. `Memory/gerenciador-de-armazenamento-windows.memory` — histórico do que foi feito (Regra 12 global)
3. `README.md` — visão funcional (categorias varridas, segundo plano/bandeja, agendamento), quando a tarefa tocar comportamento do app

Regras gerais de processo estão no `CLAUDE.md` global — não duplicar aqui. O que segue é específico deste repositório.

O hook `UserPromptSubmit` em `.claude/settings.json` (versionado) injeta a cada mensagem o lembrete dessa releitura.

**Registro de fim de atividade** (Regra 12 global): na seção "Registro de Atividades Recentes" de `Memory/gerenciador-de-armazenamento-windows.memory`. **Teto: 800 linhas** — passando disso, compactar antes de adicionar.

## Autorização específica deste projeto

- Além da Regra 05 global: publicar release no GitHub (tag, upload de `.exe`) exige autorização explícita.
- **Nunca commitar** `.claude/settings.local.json` (gitignored — permissões pessoais desta máquina), `build/`, `out/`, binários (`*.exe`, `*.pdb`, `*.obj`) nem `imgui.ini`.
- `.claude/agents/`, `.claude/skills/`, `.claude/settings.json`, `CLAUDE.md` e `Memory/` **são versionados** neste repositório (diferente do padrão comum) — aparecem no `git status` e entram no commit quando autorizado.

## Comandos essenciais

Build (MSVC, dentro do Visual Studio ou de um "Developer PowerShell"):
```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Build (MinGW, alternativa):
```powershell
cmake -B build -G "MinGW Makefiles"
cmake --build build
```

Testes (`test_duplicate_grouping`, `test_category_keys` e `test_path_exclusion` — só lógica sem dependência de Win32/COM/VSS):
```powershell
cmake --build build --target test_duplicate_grouping test_category_keys test_path_exclusion
ctest --test-dir build                          # todos os testes
ctest --test-dir build -R duplicate_grouping     # um teste específico
```

**Estado limpo da suíte:** 3 testes (`duplicate_grouping`, `category_keys`, `path_exclusion`), 0 falhas pré-existentes. Conferir na saída do `ctest` que todos de fato executaram (`100% tests passed, 0 tests failed out of 3`).

Subir a aplicação: `build\Release\StorageCleaner.exe` (MSVC) ou `build\StorageCleaner.exe` (MinGW/Ninja); `--startup` abre direto na bandeja; `--dry-run` liga o modo simulação (nenhuma limpeza exclui nada — **único modo permitido para testar limpeza nesta máquina**, ver memória do agente); `--test-balloons` mostra os 6 balões da bandeja em sequência (8 s cada) para o usuário conferir visualmente — esta sessão não enxerga a área de trabalho. Textos dos balões só em `src/ui/Notifications.cpp`. Rodar como Administrador para incluir pontos de restauração e cache do Windows Update.

Lint: não há lint configurado. Migrations: não se aplica.

**Armadilhas de nomenclatura:** a pasta é `gerenciador-de-armazenamento-windows`, mas o alvo CMake/executável é `StorageCleaner`, o namespace C++ é `storagecleaner`, a pasta de dados é `%APPDATA%\StorageCleaner\` e o título exibido na janela/bandeja é "Otimizador de Armazenamento".

Dear ImGui e `nlohmann::json` são baixados automaticamente via `FetchContent` — não precisa instalar SDK separado.

Pontos de restauração (VSS) exigem rodar o `.exe` como Administrador; sem isso essa categoria é pulada e a UI mostra um aviso em vez de falhar.

### Build real via PowerShell (fora de um Developer Prompt)

A tool PowerShell não herda as variáveis de ambiente do MSVC (`INCLUDE`, `LIB`, `PATH` com `cl.exe`) só por existir o Visual Studio instalado — é preciso popular o ambiente chamando `VsDevCmd.bat` antes do CMake/Ninja, na mesma invocação de `cmd /c` (variáveis não sobrevivem entre chamadas separadas da tool):
```powershell
$vsPath = "C:\Program Files\Microsoft Visual Studio\2022\Community"
$vsDevCmd = "$vsPath\Common7\Tools\VsDevCmd.bat"
$buildDir = (Resolve-Path "out\build\x64-Debug").Path   # rodar na raiz do repo; x64-Release também existe
$ninja = "$vsPath\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
$cmake = "$vsPath\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

cmd /c "call `"$vsDevCmd`" -arch=x64 -no_logo >nul && `"$cmake`" `"$buildDir`" >nul && cd /d `"$buildDir`" && `"$ninja`""
```
Se o Visual Studio ainda não tiver a carga "Desktop development with C++" (compilador MSVC + CMake tools), o erro será `No CMAKE_CXX_COMPILER could be found` — instalar via Visual Studio Installer > Modificar.

## Arquitetura

Otimizador/limpador de armazenamento para Windows: app desktop que varre o disco em busca de arquivos/dados removíveis (temporários, cache de navegador, duplicados, logs antigos, Lixeira, pontos de restauração VSS, dados órfãos de apps desinstalados) e permite ao usuário revisar e limpar manualmente ou por agendamento automático.

**Stack:** C++20 · Dear ImGui (Win32 + DirectX 11) · CMake (MSVC ou MinGW) · `nlohmann::json` (persistência em arquivo, sem banco) · COM/`IFileOperation`, Shell APIs, `vssadmin`

Três camadas, com dependência sempre em uma direção (`ui` → `scanners`/`cleaner` → `include/storagecleaner`):

- **`include/storagecleaner/`** — modelo de dados e contratos compartilhados, independentes de UI: `ScanItem`/`Category` (`IsPermanentCategory`, `IsHeuristicCategory`), `Config` (inclui `SanitizeConfig()`), `ScanResult`, `HistoryEntry`, `ProgressState` (`ProgressChannel`, canal de progresso thread-safe via mutex+snapshot), `Scanner.h` (assinatura comum dos scanners + `ScanEngine`), `Cleaner.h`, `ConfigStore.h`/`HistoryStore.h` (persistência JSON em `%APPDATA%\StorageCleaner\`, escrita atômica via `.tmp` + `MoveFileExW`). `DuplicateGrouping.h`, `PathExclusion.h` (pastas protegidas) e as funções de categoria de `ScanItem.h` são as únicas lógicas 100% livres de Win32 — por isso são as únicas testáveis sem toolchain Windows.
- **`src/scanners/`** — um scanner por categoria, todos com a mesma assinatura (`Config`, `ProgressChannel&`, `std::atomic<bool>& cancel`), orquestrados sequencialmente por `ScanEngine` (`ScanEngine.cpp`) numa worker thread própria a partir da tabela `kScanners` (um `static_assert` exige uma linha por categoria). O `ScanEngine` também pula categorias de `IsAdminRequiredCategory` sem elevação (vão para `ScanResult::skippedNeedAdmin`) e aplica a lista de pastas protegidas em `AddBucket` — **categoria nova** = enum + `kAllCategories` + `CategoryName`/`CategoryKey` + `Phase`/`PhaseLabel` + scanner + linha em `kScanners` + `CMakeLists.txt` + chave em `tests/test_category_keys.cpp`.
- **`src/cleaner/`** — `Cleaner.cpp` decide, por categoria, se a exclusão é reversível (via `IFileOperation` + Lixeira, com `IFileOperationProgressSink` para saber item a item se a remoção realmente aconteceu) ou permanente (esvaziar a Lixeira, remover shadow copy via `vssadmin`). `CleanItems` é o **único ponto do app que exclui algo** — o `dryRun` (`--dry-run`) é verificado ali; qualquer caminho novo de exclusão precisa passar por ele ou respeitar a mesma flag.
- **`src/ui/`** — Dear ImGui: `App.cpp` (bootstrap Win32+DX11+ImGui, loop principal, ícone na bandeja), `UiState.*` (todo o estado mutável de UI: seleção de itens, varredura leve em thread própria, agendamento de auto-clean — `Tick()` roda uma vez por frame e concentra essa lógica fora dos `*Panel.cpp`, que só desenham), painéis por aba (`DashboardPanel`, `ResultsPanel`, `SettingsPanel`, `HistoryPanel`), `TrayIcon.*` (bandeja + auto-início via registro do Windows), `Theme.h`/`Theme.cpp` (paleta central `theme::k*`, `ApplyTheme()`/`LoadFonts()` chamados uma vez em `RunApp()`, `AppFonts` guardado em `UiState::fonts`, e `DrawCircularGauge`/`DrawInlineCircularGauge` — gauge circular customizado via `ImDrawList` que substitui `ImGui::ProgressBar` nos pontos de progresso de scan/clean).
- **`resources/`** — recurso Win32 do executável: `app.ico` (multi-resolução), `app.rc` e `resource.h` (`IDI_APP_ICON`, compartilhado entre o `.rc` e o C++ via `#include "resource.h"`). Usado por `App.cpp` (janela/taskbar) e `TrayIcon.cpp` (bandeja) via `LoadImageW(..., MAKEINTRESOURCEW(IDI_APP_ICON), ...)`.

**Categorias reversíveis vs. permanentes** (`IsPermanentCategory` em `ScanItem.h`): temp, cache, duplicados, logs, dados órfãos, cache do Windows Update, despejos de erro e miniaturas vão para a Lixeira; só Lixeira (esvaziar) e pontos de restauração são permanentes. "Dados órfãos" é heurística (`IsHeuristicCategory`): desabilitada por padrão, nunca pré-selecionada, nunca entra na limpeza automática mesmo se habilitada.

## Padrões críticos

- **Threading:** cada operação longa (scan, clean, varredura leve em segundo plano) roda em worker thread própria; progresso é publicado via `ProgressChannel` e lido pela UI thread a cada frame — nunca ler campos de progresso/resultado compartilhados diretamente sem passar pelo canal/mutex correspondente (ex.: `UiState::GetLightTotals()`).
- **`ScanEngine::StopAndWait()`** deve ser chamado explicitamente antes de destruir qualquer `ProgressChannel` referenciado pela worker — não depender só da ordem de destruição de membros.
- **Iteração recursiva de arquivos:** sempre `util::ForEachFileRecursive` (`Utils.h`), nunca reimplementar `recursive_directory_iterator` + `skip_permission_denied`. `util::ComputeDirectoryStats`/`util::DirectorySize` já são implementados em cima dele.
- **Config:** `SanitizeConfig()` é a fonte única dos limites válidos — chamar sempre que um campo clampado for editado ou carregado.
- **Ferramentas do sistema** (`vssadmin` etc.) resolvidas por caminho absoluto (`Utils::SystemToolPath`, nunca busca implícita de PATH) e sempre com timeout — evita hijacking/hang num processo elevado.
- **Texto de UI em pt-BR com acentuação** (Regra 03 global), em literais UTF-8 — funciona porque `CMakeLists.txt` passa `/utf-8` ao MSVC e a fonte (Segoe UI, faixa padrão do ImGui) cobre Latin-1 (U+0020–U+00FF). **Nada acima disso em texto de UI** — travessão "—", aspas curvas, reticências "…" aparecem como "?"; usar "-", aspas retas, "...".
- **Nome exibido ≠ chave persistida:** `CategoryName()` é só exibição (pode mudar); `CategoryKey()`/`CategoryFromKey()` (`ScanItem.h`) são as chaves em `config.json`/`history.json` e **nunca mudam** — alterá-las faz o app ignorar config/histórico existentes sem erro.
- **Comentários** no código só explicam o "porquê" em pontos não óbvios (regras de segurança, motivos de design) — nunca o "o quê".
- **Nunca roubar o foco:** nada que roda sozinho (agendamento, varredura leve, alertas) mostra a janela ou chama `SetForegroundWindow` — avisa por `ShowTrayBalloon` (o Windows já adia balões em tela cheia/apresentação/"Não perturbe"; `NIIF_RESPECT_QUIET_TIME` **não** é isso — só cobre a 1ª hora após o primeiro logon). Um `ShowTrayBalloon` substitui o anterior — por isso todo aviso passa pelos `Notify*` de `src/ui/Notifications.cpp`, que enfileiram e saem com 8 s de intervalo (`PumpNotifications()` no `Tick`); nunca chamar `ShowTrayBalloon` direto. A janela só abre por ação explícita do usuário: "Abrir" no menu da bandeja ou clique num balão (`NIN_BALLOONUSERCLICK` → `ShowMainWindow` em `App.cpp`). Decisão do usuário: ele pode estar jogando, digitando ou apresentando.
- **Cores de status** (sucesso/aviso/erro) sempre pelas constantes de `theme::`, nunca `ImVec4` hardcoded.
- **Flags de compilador obrigatórias** (já em `CMakeLists.txt`, não repetir em código): `UNICODE`/`_UNICODE`, `NOMINMAX`, `/EHsc` e `/utf-8` no MSVC — ver Pitfalls.
- **Testes:** bug em lógica livre de Win32 → teste em `tests/` que reproduz o bug. O resto do app só se valida rodando o `.exe` (ver skill `run`).

## Pitfalls conhecidos

- **Só compila/executa em Windows:** a tool **Bash** pode rodar num ambiente sem toolchain Windows (só dá pra rodar os testes de `tests/`); a tool **PowerShell** roda na máquina Windows real e compila o projeto inteiro via `VsDevCmd.bat` (ver "Build real via PowerShell"). Preferir build real a revisão só por leitura.
- **A tool PowerShell não enxerga a janela do app:** `CopyFromScreen` falha ("identificador inválido") e `PrintWindow` devolve o cliente DX11 em branco — a validação visual da UI tem de ser feita pelo usuário. Dá para medir pelo PowerShell (só leitura) as pastas que um scanner vai encontrar, e controlar a janela por Win32: `GetWindowRect`/`SetWindowPos`/`ShowWindow` funcionam, e `PostMessage(hwnd, WM_COMMAND, 3, 0)` (= "Sair" da bandeja, `kMenuExit`) encerra o app **pelo caminho normal** (grava a janela no config.json; `Stop-Process` não grava). Com `--startup` a janela nasce escondida e `MainWindowHandle` vem 0 — achar o hwnd com `EnumWindows` filtrando pelo PID.
- **Barras invertidas em heredoc pela tool Bash:** escapes como `L'\\'` chegam corrompidos (viram `L'\'` e um byte NUL) ao editar C++ via `python - <<EOF` — para trechos com `\`, usar a tool Edit.
- **Sem `UNICODE`/`_UNICODE`:** macros genéricas de recurso do Win32 (ex. `IDI_APPLICATION`) resolvem para `LPSTR` e quebram chamadas `...W`.
- **Sem `NOMINMAX`:** `windows.h` define macros `max`/`min` que colidem com `std::max`/`std::min`.
- **Sem `/utf-8` no MSVC:** fontes UTF-8 sem BOM são lidas na code page 1252 e todo literal acentuado (inclusive `L"..."`) sai corrompido na UI.
- **Sem `/EHsc` no MSVC:** o `try/catch` real do projeto (ex. parsing de JSON em `HistoryStore.cpp`) fica sem garantia de unwind.
- **Saída do `vssadmin` varia por versão/idioma do Windows:** o parser em `VssRestorePoints.cpp` é propositalmente simples (linha a linha) e pode precisar de ajuste em outras localidades.
- **IDs em tabelas ImGui:** `TableHeadersRow()` identifica o cabeçalho da coluna N como `PushID(N)` + rótulo. Widget de linha com `PushID(linha)` + o mesmo rótulo colide com o cabeçalho e perde o clique (era o "checkbox da linha 0 não marca" em `ResultsPanel.cpp`, corrigido usando `"##sel"`/`"##row"`). Nunca deixar rótulo vazio `""` em coluna nem em widget de linha.

## Skills e agentes deste projeto

Replicados de `github.com/Bugmenn/claude` em `.claude/skills/` e `.claude/agents/` — ao alterar, sincronizar com o repositório (skill `sincronizar-skills-agentes`). O repositório é o **padrão**: em divergência, a versão de lá vence. O setup de referência (global, template de projeto, hooks, permissões) é o `SETUP_CLAUDE_PADRAO.md` de lá — consultar no repositório, sem cópia local.

| Skill / Agente | Quando usar |
|---|---|
| `revisao-final` (skill) | ao terminar qualquer mudança de código (Regra 08): dispara `revisor-codigo` duas vezes em paralelo, uma cega |
| `arquitetura-projeto` (skill) | revisar/propor mudança de estrutura, camadas, módulos |
| `otimizacao-codigo` (skill) | performance, memória, duplicação, código morto |
| `frontend-design` (skill) | direção visual da UI ImGui (tema, tipografia, layout) |
| `revisor-codigo` (agente) | revisar diff/branch/PR sem modificar código |
| `depurador` (agente) | erro, crash, comportamento inesperado — causa raiz antes de corrigir |
| `testador` (agente) | escrever/rodar testes; teste que reproduz bug |
| `documentador` (agente) | README e documentação a partir do código |
| `descritor-pr` (agente) | descrição de PR a partir do diff |
| `especialista-banco` (agente) | não se aplica aqui (sem banco) |

`revisao-final` × `revisor-codigo`: a skill orquestra a revisão de fechamento e corrige; o agente só revisa e reporta.

## Documentação por domínio (`.claude/docs/`)

Ainda não há. Quando os pitfalls de um domínio (ex.: UI ImGui, VSS/limpeza) passarem de meia dúzia de linhas, mover para `.claude/docs/<dominio>.md` e deixar aqui só a linha da tabela.

## Credenciais locais

O projeto não usa credenciais, tokens nem connection strings. A configuração do usuário fica em `%APPDATA%\StorageCleaner\` (fora do repositório).
