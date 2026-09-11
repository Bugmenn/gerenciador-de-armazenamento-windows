# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

---

## Regra 00 — Memória entre Sessões (executar ANTES de qualquer coisa)

1. Verificar se existe `Memory/diretrizes.md` e `Memory/*.memory` neste repositório.
2. **Ler obrigatoriamente** antes de iniciar qualquer tarefa.
3. Ler especificamente `Memory/gerenciador-de-armazenamento-windows.memory` para o contexto deste projeto.

### Fim de cada atividade — preenchimento obrigatório
Registrar em `Memory/gerenciador-de-armazenamento-windows.memory`, na seção "Registro de Atividades Recentes":

```
### [YYYY-MM-DD] Titulo da Atividade
**O que foi feito:** descricao concisa
**Arquivos alterados:**
- `Caminho/Arquivo.ext` — o que mudou
- `Caminho/Novo.ext` *(novo)*
- Removido: `Caminho/Removido.ext`
```

### Compactação obrigatória — 800 linhas
Antes de adicionar qualquer registro, verificar o número de linhas do arquivo de memória.
Se tiver **mais de 800 linhas**, compactar primeiro:
- Manter intactas as seções de contexto fixo (stack, arquitetura, padrões, pitfalls)
- Fundir registros antigos em `### [período] Histórico Compactado`, preservando apenas decisões arquiteturais, padrões novos e pitfalls com valor duradouro
- Descartar registros rotineiros sem impacto arquitetural

---

## Regra 01 — Informar Modelo Obrigatoriamente

**Toda resposta deve começar** com a linha indicando qual modelo está sendo usado:

```
**Modelo: [Haiku | Sonnet | Opus]** — [motivo em uma linha]
```

---

## Regra 02 — Escolha do Modelo

| Situação | Modelo |
|---|---|
| Buscas, leituras, grep, exploração de código | **Haiku** |
| Implementação, refactoring, análise lógica, geração de código | **Sonnet** |
| Sonnet falhou 2 vezes na mesma tarefa | **Opus** |

Nunca usar Opus como primeira escolha. Escalar apenas após 2 falhas do Sonnet.

---

## Regra 03 — Economia de Tokens

- Ler **apenas o trecho relevante** — usar `offset` + `limit`; nunca ler arquivos grandes inteiros
- Usar `Grep` antes de `Read` para localizar o trecho exato
- Paralelizar chamadas de ferramentas independentes no mesmo turno
- **Nunca** repetir código já mostrado; referenciar por `arquivo:linha`
- Respostas curtas e diretas; sem resumo do que acabou de fazer
- Não re-explorar contexto já conhecido na sessão

---

## Regra 04 — Idioma

Responder **sempre em Português Brasil (pt-BR)**, independente do idioma da pergunta ou do código.

---

## Regra 05 — Diretrizes do Projeto

Sempre seguir as diretrizes específicas do projeto quando existirem:
- `Memory/diretrizes.md`
- `Memory/gerenciador-de-armazenamento-windows.memory`

---

## Sobre este projeto

Otimizador/limpador de armazenamento para Windows: app desktop em C++20 com
Dear ImGui (backend Win32 + DirectX 11) que varre o disco em busca de
arquivos/dados removíveis (temporários, cache de navegador, duplicados, logs
antigos, Lixeira, pontos de restauração VSS, dados órfãos de apps
desinstalados) e permite ao usuário revisar e limpar manualmente ou por
agendamento automático. Veja `README.md` para a visão funcional completa
(categorias varridas, comportamento de segundo plano/bandeja, agendamento) e
`Memory/gerenciador-de-armazenamento-windows.memory` para decisões técnicas e
histórico de mudanças.

Este repositório também traz skills e subagentes pessoais em `.claude/skills/`
e `.claude/agents/`, replicados de `github.com/Bugmenn/claude`.

**Só compila/executa em Windows** (Win32, COM/`IFileOperation`, Shell APIs,
VSS via `vssadmin`, DirectX 11). A tool **Bash** desta sessão roda tipicamente
num container Linux sem toolchain Windows (só dá pra rodar a lógica livre de
Win32, `tests/test_duplicate_grouping.cpp`) — mas a tool **PowerShell** roda
na máquina Windows real do usuário. Se o Visual Studio (com a carga "Desktop
development with C++") estiver instalado aí, **é possível compilar o projeto
inteiro de verdade via PowerShell** (ver "Build real via PowerShell" abaixo)
em vez de só revisar código por leitura — confirmado nesta sessão: build
limpo, 0 erros, 0 warnings, `ctest` passando.

## Comandos

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

Dear ImGui e `nlohmann::json` são baixados automaticamente via
`FetchContent` — não precisa instalar SDK separado.

### Build real via PowerShell (fora de um Developer Prompt)

A tool PowerShell não herda as variáveis de ambiente do MSVC (`INCLUDE`,
`LIB`, `PATH` com `cl.exe`) só por existir o Visual Studio instalado — é
preciso popular o ambiente chamando `VsDevCmd.bat` antes do CMake/Ninja, na
mesma invocação de `cmd /c` (variáveis não sobrevivem entre chamadas
separadas da tool):
```powershell
$vsPath = "C:\Program Files\Microsoft Visual Studio\2022\Community"
$vsDevCmd = "$vsPath\Common7\Tools\VsDevCmd.bat"
$buildDir = "<repo>\out\build\x64-Debug"   # ou o dir configurado pelo VS/CMakePresets
$ninja = "$vsPath\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
$cmake = "$vsPath\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

cmd /c "call `"$vsDevCmd`" -arch=x64 -no_logo >nul && `"$cmake`" `"$buildDir`" >nul && cd /d `"$buildDir`" && `"$ninja`""
```
Se o Visual Studio ainda não tiver a carga "Desktop development with C++"
(compilador MSVC + CMake tools), o erro será `No CMAKE_CXX_COMPILER could be
found` — instalar via Visual Studio Installer > Modificar.

Testes (só `test_duplicate_grouping` roda fora do Windows — é a única lógica
sem dependência de Win32/COM/VSS):
```powershell
cmake --build build --target test_duplicate_grouping
ctest --test-dir build                          # todos os testes
ctest --test-dir build -R duplicate_grouping     # um teste específico
```

Pontos de restauração (VSS) exigem rodar o `.exe` como Administrador; sem
isso essa categoria é pulada e a UI mostra um aviso em vez de falhar.

**Flags de compilador obrigatórias** (já em `CMakeLists.txt`, não repetir em
código): `UNICODE`/`_UNICODE` (senão macros genéricas de recurso do Win32
como `IDI_APPLICATION` resolvem para `LPSTR` e quebram chamadas `...W`),
`NOMINMAX` (senão `windows.h` define macros `max`/`min` que colidem com
`std::max`/`std::min`), `/EHsc` no MSVC (senão o `try/catch` real do projeto,
ex. parsing de JSON em `HistoryStore.cpp`, fica sem garantia de unwind).

## Arquitetura

Três camadas, com dependência sempre em uma direção (`ui` → `scanners`/`cleaner` → `include/storagecleaner`):

- **`include/storagecleaner/`** — modelo de dados e contratos compartilhados,
  independentes de UI: `ScanItem`/`Category` (`IsPermanentCategory`,
  `IsHeuristicCategory`), `Config` (inclui `SanitizeConfig()`, fonte única
  para os limites válidos de config — chamar sempre que um campo clampado
  for editado ou carregado), `ScanResult`, `HistoryEntry`, `ProgressState`
  (`ProgressChannel`, canal de progresso thread-safe via mutex+snapshot),
  `Scanner.h` (assinatura comum dos scanners + `ScanEngine`), `Cleaner.h`,
  `ConfigStore.h`/`HistoryStore.h` (persistência JSON em
  `%APPDATA%\StorageCleaner\`, escrita atômica via `.tmp` + `MoveFileExW`).
  `DuplicateGrouping.h` é a única lógica 100% livre de Win32 — por isso é a
  única testável neste ambiente sem toolchain Windows.
- **`src/scanners/`** — um scanner por categoria, todos com a mesma
  assinatura (`Config`, `ProgressChannel&`, `std::atomic<bool>& cancel`),
  orquestrados sequencialmente por `ScanEngine` (`ScanEngine.cpp`) numa
  worker thread própria. `ScanEngine::StopAndWait()` deve ser chamado
  explicitamente antes de destruir qualquer `ProgressChannel` referenciado
  pela worker — não depender só da ordem de destruição de membros. Todos os
  scanners que percorrem arquivos recursivamente usam `util::ForEachFileRecursive`
  (`Utils.h`) em vez de reimplementar `recursive_directory_iterator` +
  `skip_permission_denied` — qualquer scanner novo que precise disso deve usar
  o mesmo helper. `util::ComputeDirectoryStats`/`util::DirectorySize` também
  são implementados em cima dele (não reimplementar a iteração de novo).
- **`src/cleaner/`** — `Cleaner.cpp` decide, por categoria, se a exclusão é
  reversível (via `IFileOperation` + Lixeira, com `IFileOperationProgressSink`
  para saber item a item se a remoção realmente aconteceu) ou permanente
  (esvaziar a Lixeira, remover shadow copy via `vssadmin`). `VssRestorePoints.cpp`
  resolve `vssadmin` por caminho absoluto (`Utils::SystemToolPath`, nunca por
  busca implícita de PATH) e sempre com timeout, para evitar hijacking/hang
  num processo elevado.
- **`src/ui/`** — Dear ImGui: `App.cpp` (bootstrap Win32+DX11+ImGui, loop
  principal, ícone na bandeja), `UiState.*` (todo o estado mutável de UI:
  seleção de itens, varredura leve em thread própria, agendamento de
  auto-clean — `Tick()` roda uma vez por frame e concentra essa lógica fora
  dos `*Panel.cpp`, que só desenham), painéis por aba (`DashboardPanel`,
  `ResultsPanel`, `SettingsPanel`, `HistoryPanel`), `TrayIcon.*` (bandeja +
  auto-início via registro do Windows), `Theme.h`/`Theme.cpp` (paleta central
  `theme::k*`, `ApplyTheme()`/`LoadFonts()` chamados uma vez em `RunApp()`,
  `AppFonts` guardado em `UiState::fonts`, e `DrawCircularGauge`/
  `DrawInlineCircularGauge` — gauge circular customizado via `ImDrawList`
  que substitui `ImGui::ProgressBar` nos pontos de progresso de scan/clean).
  Qualquer cor de status (sucesso/aviso/erro) nova deve usar as constantes
  de `theme::`, nunca `ImVec4` hardcoded.
- **`resources/`** — recurso Win32 do executável: `app.ico` (multi-resolução),
  `app.rc` e `resource.h` (`IDI_APP_ICON`, compartilhado entre o `.rc` e o C++
  via `#include "resource.h"`). Usado por `App.cpp` (ícone da janela/taskbar)
  e `TrayIcon.cpp` (ícone da bandeja) via `LoadImageW(..., MAKEINTRESOURCEW(IDI_APP_ICON), ...)`.

**Threading**: cada operação longa (scan, clean, varredura leve em segundo
plano) roda em worker thread própria; progresso é publicado via
`ProgressChannel` e lido pela UI thread a cada frame — nunca ler campos de
progresso/resultado compartilhados diretamente sem passar pelo canal/mutex
correspondente (ex.: `UiState::GetLightTotals()`).

**Bug conhecido, ainda sem correção**: em `ResultsPanel.cpp`, dentro de
`DrawCategoryTable`, o checkbox da linha 0 (primeiro item de qualquer
categoria expandida) nunca responde a clique — confirmado por reprodução
real (mouse físico) em pelo menos duas categorias diferentes, sempre a
linha 0, nunca as demais. Tentativa de correção via
`ImGui::TableSetupScrollFreeze(0, 1)` (hipótese: linha 0 compartilhando
região de scroll com o cabeçalho) foi testada e **revertida** — não
resolveu o problema. Causa raiz ainda não identificada; não reintroduzir
essa mesma tentativa sem antes confirmar visualmente que ela funciona.

**Categorias reversíveis vs. permanentes** (`IsPermanentCategory` em
`ScanItem.h`): temp, cache, duplicados, logs e dados órfãos vão para a
Lixeira; só Lixeira (esvaziar) e pontos de restauração são permanentes.
"Dados órfãos" é heurística (`IsHeuristicCategory`): desabilitada por padrão,
nunca pré-selecionada, nunca entra na limpeza automática mesmo se habilitada.
