# UI Dear ImGui — pitfalls e invariantes

Abrir ao mexer em `src/ui/` (painéis, tabelas, roscas, `UiState`, `--ui-selftest`). Regras que valem para qualquer tarefa (texto só Latin-1, cores via `theme::`, nunca roubar foco) ficam no `CLAUDE.md`.

## Tabelas

- **IDs:** `TableHeadersRow()` identifica o cabeçalho da coluna N como `PushID(N)` + rótulo. Widget de linha com `PushID(linha)` + o mesmo rótulo colide com o cabeçalho e perde o clique (era o "checkbox da linha 0 não marca" em `ResultsPanel.cpp`). Cabeçalho `"##sel"`, linhas `"##row"`/`"##grp"`/`"##node"`; nunca rótulo vazio `""` em coluna nem em widget de linha.
- **Seta de ordenação:** o ImGui guarda o sort da tabela pelo ID e só marca `SpecsDirty` na criação, mudança de colunas ou `TableSetColumnSortDirection` (`imgui_internal.h`). Ordenação mudada fora do cabeçalho (self-test) → `BucketView::sortChangedExternally`, que o painel aplica com `TableSetColumnSortDirection`. Por isso `AdoptScanResult` **não** zera a ordenação das `BucketView` — a seta continuaria na ordem antiga.
- **Checkbox tri-estado:** `PushItemFlag(ImGuiItemFlags_MixedValue, ...)` — o flag é interno (`imgui_internal.h`) no 1.91.0.

## Lista agrupada dos Resultados (`UiState::BucketView`)

- Cache por categoria, reconstruído só quando muda a chave (versão do resultado, filtro, tamanho mínimo, ordenação). Alterou `lastScanResult`/`selection` → chamar `OnResultChanged()` (incrementa `resultVersion`, recalcula `cleanableByDrive`).
- `itemIndices` aponta para `bucket.items`: `UiState::selection` (vetor paralelo) continua sendo a única fonte da seleção.
- Laço do `ImGuiListClipper`: copiar a `ResultRow` antes de desenhar e sair do laço se `rowsDirty` (abrir/fechar pasta reconstrói `rows`). Ordenação vinda do cabeçalho → `GetBucketView` **antes** do laço; reconstruir no meio invalida as referências a `groups`.
- `toggledFolders` guarda a inversão em relação ao padrão (fechado; aberto com texto no filtro) e é limpo quando o padrão muda.
- Categoria heurística (dados órfãos) agrupa por **nível** (`GroupItemsByExistenceChance`, `ItemGroup::chance`), não por pasta: grupos na ordem fixa Baixa/Média/Alta independente da ordenação, começam abertos, nunca viram linha direta mesmo com 1 item (`inGroup` em `DrawItemRow` considera o nível), rótulo colorido por `ChanceColor` (`kSuccess`/`kWarning`/`kDanger`). `ItemGroup::folder` vira chave estável `"chance:N"`.

## Roscas (`Theme.cpp`)

- `DrawInlineSegmentedDonut` reserva margem de 0,7x a espessura: a fatia em destaque é desenhada 1,4x mais grossa e sairia cortada na borda da janela/child.
- Hover usa `IsWindowHovered()` da janela atual (no Resultados, o child `"resultados"`).

## `--ui-selftest` (`SelfTest.cpp`)

- Ações do roteiro chamam os mesmos métodos de `UiState` que o painel (`ToggleGroup`, `SetGroupSelected`, `SetResultsSort`, `RequestOpenCategory`) — tela nova → passo + asserção em `BuildScript`.
- Mouse injetado com `io.AddMousePosEvent` entre o `NewFrame` do backend Win32 e `ImGui::NewFrame` (o backend só lê o cursor com a janela em foco, e ela fica escondida).
- Roda do mouse injetada (`AddMouseWheelEvent`) **não** rolou a janela escondida; rolar com `ImGui::SetScrollY(ImGui::FindWindowByName("StorageCleanerMain"), y)`, só depois de 1 frame do passo (no 1º frame do app a janela ainda não existe).
- Texto de aviso longo em cor: `PushStyleColor(ImGuiCol_Text)` + `TextWrapped` — `TextColored` não quebra linha e sai cortado (pego pelo PNG do passo `orfaos-niveis`).
- Esperar alguns frames entre ação e captura: abrir categoria, rolar e tooltip levam 2-3 frames para assentar.
