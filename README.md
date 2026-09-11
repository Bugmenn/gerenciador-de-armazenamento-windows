# Otimizador/Limpador de Armazenamento (Windows)

Aplicativo desktop em C++ (Dear ImGui) para Windows que varre o disco,
identifica arquivos e dados que podem ser removidos com segurança para
liberar espaço, e permite ao usuário revisar e limpar — manualmente ou de
forma agendada.

## Download

Não precisa compilar para usar: baixe `StorageCleaner.exe` na
[página de releases](https://github.com/Bugmenn/gerenciador-de-armazenamento-windows/releases/latest).

Requer o [Visual C++ Redistributable 2015-2022 (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe)
— a maioria dos PCs Windows já tem instalado. Pontos de restauração (VSS)
exigem rodar o `.exe` como Administrador; sem isso essa categoria é pulada
e a UI mostra um aviso em vez de falhar.

## O que ele varre

| Categoria | O que é | Exclusão |
|---|---|---|
| Arquivos temporários | `%TEMP%`, `C:\Windows\Temp`, `AppData\Local\Temp` de todos os usuários | Lixeira (reversível) |
| Cache de navegador | Chrome, Edge e Firefox — todos os perfis de cada navegador (não só um), incluindo `Cache` e `Code Cache` do Chromium; cada pasta vira um item separado nos resultados | Lixeira (reversível) |
| Arquivos duplicados | Documentos, Área de trabalho, Downloads e Imagens por padrão (customizável via `duplicateScanRoots` no `config.json`, sem campo na UI); agrupados por tamanho e depois por hash SHA-256, mantém sempre a cópia mais antiga | Lixeira (reversível) |
| Logs antigos | `*.log` mais antigos que um limite configurável, em `Windows\Logs`, `ProgramData` e `AppData\Local` | Lixeira (reversível) |
| Lixeira | Tamanho total ocupado pela própria Lixeira | **Permanente** (esvaziar não tem "desfazer") |
| Pontos de restauração | Shadow copies (VSS) além do número configurado a manter | **Permanente** (requer executar como Administrador) |
| Dados órfãos | Pastas em `AppData` de aplicativos que não aparecem mais na lista de programas instalados nem em `Program Files`, e que estão inativas há um tempo mínimo configurável | Lixeira (reversível) — **desabilitado por padrão** |

**Sobre "Dados órfãos":** essa categoria é heurística — compara nomes de
pastas de AppData contra o registro de programas instalados e as pastas de
`Program Files`, mas pode gerar falsos positivos (ex.: um aplicativo portátil
raramente aberto). Por isso ela: vem desligada até o usuário habilitar
manualmente nas Configurações; nunca vem pré-selecionada na revisão de
resultados (cada item precisa ser marcado manualmente); nunca entra na
limpeza automática agendada, mesmo que esteja habilitada.

## Como funciona

- **Revisão antes de excluir**: toda varredura mostra os itens encontrados,
  agrupados por categoria, com checkboxes — nada é apagado sem confirmação,
  a menos que a limpeza automática esteja configurada no modo "Excluir
  automaticamente".
- **Segundo plano**: o app pode iniciar junto com o Windows e ficar
  minimizado na bandeja do sistema, fazendo varreduras leves periódicas
  (só tamanhos agregados, sem hash de duplicados) para manter o Dashboard
  atualizado. Fechar a janela pela barra de título apenas a esconde; a saída
  real do processo é pelo item "Sair" no menu da bandeja.
- **Limpeza automática agendada**: configurável para rodar todo dia, a cada
  7 dias, a cada 15 dias ou uma vez por mês, num horário definido. Dois
  modos: excluir automaticamente o que for encontrado, ou apenas escanear e
  trazer a janela para frente pedindo confirmação do usuário.
- **Histórico**: toda limpeza executada (manual ou automática) fica
  registrada — data, espaço liberado por categoria, e a lista de itens
  removidos. Passar o mouse sobre o espaço liberado de uma entrada mostra
  o detalhamento por categoria; o status de cada execução pode ser "OK",
  "OK (N item(ns) ignorado(s))" — quando algum item selecionado sumiu ou
  mudou entre a varredura e a limpeza, sem ser erro — ou uma mensagem de
  erro específica quando algo falhou de fato.

### Bandeja do sistema

O ícone na bandeja (próprio do app, o mesmo usado na janela e na barra de
tarefas) tem um menu de contexto com três opções — **Abrir**, **Escanear
agora** e **Sair** — sem atalhos de teclado associados.

### Durante uma varredura

O Dashboard mostra um indicador circular de progresso, a fase atual, o
item sendo processado no momento, quantos itens já foram processados e
quantos bytes já foram encontrados. Fora de uma varredura, a mesma aba
mostra permanentemente dois totais atualizados automaticamente em segundo
plano: espaço ocupado pela Lixeira e por temporários do usuário atual.

### Confirmação antes de limpar

Ao clicar em "Limpar selecionados", um modal mostra quantos itens e quantos
bytes serão liberados, com o aviso padrão de que a maioria das categorias
vai para a Lixeira. Se algum item **permanente** (Lixeira ou pontos de
restauração) estiver entre os selecionados, aparece um aviso extra em
destaque avisando que aquela parte da remoção não tem "desfazer".

## Configurações e persistência

Tudo fica salvo em `%APPDATA%\StorageCleaner\` (`C:\Users\<usuário>\AppData\Roaming\StorageCleaner\`):

| Arquivo | Conteúdo |
|---|---|
| `config.json` | Configurações da aba "Configurações" (só é gravado quando o usuário clica em "Salvar") |
| `history.json` | Histórico de limpezas executadas |

A escrita é sempre atômica (grava num arquivo `.tmp` e substitui o original
via `MoveFileExW`), para não corromper o arquivo se o app fechar no meio de
uma gravação.

**Valores padrão** (editáveis na aba Configurações):

| Configuração | Padrão |
|---|---|
| Tamanho mínimo de arquivo para contar como "removível" | 4 KB |
| Idade mínima de um arquivo temporário para virar candidato | 1 hora |
| Idade mínima de um log para virar "antigo" | 30 dias |
| Pontos de restauração a manter | 2 (os demais são candidatos a remoção) |
| Inatividade mínima para uma pasta de AppData virar "órfã" | 180 dias |
| Intervalo da varredura leve em segundo plano | 60 minutos (mínimo forçado: 5 minutos) |
| Iniciar com o Windows | desligado |
| Minimizar para a bandeja ao fechar | ligado |
| Limpeza automática agendada | desligada; quando ligada, padrão é a cada 7 dias às 03:00, no modo "pedir confirmação" |

**Regras gerais de funcionamento:**
- **Instância única**: um mutex nomeado (`Local\StorageCleanerSingleInstance`)
  impede abrir uma segunda cópia do app — evita duas instâncias lendo/gravando
  `config.json`/`history.json` ao mesmo tempo e rodando varreduras/limpezas
  concorrentes sobre os mesmos arquivos.
- **Início com o Windows**: quando habilitado, o app é registrado para abrir
  já minimizado na bandeja (flag interna `--startup`), sem interromper o login
  com uma janela em primeiro plano.
- **Histórico com teto**: no máximo 500 execuções ficam guardadas em
  `history.json` (as mais antigas são descartadas); dentro de cada execução,
  no máximo 5.000 itens individuais são listados (os totais de bytes
  liberados continuam exatos mesmo quando a lista de itens é truncada).
- **Dados órfãos é sempre a única categoria heurística**: vem desligada por
  padrão, nunca pré-selecionada numa varredura, e nunca entra na limpeza
  automática mesmo se o usuário a habilitar manualmente — sempre exige
  revisão item a item.
- **Pontos de restauração sem privilégio de Administrador**: a listagem e a
  remoção de shadow copies simplesmente retornam vazio/sem efeito na hora,
  sem chegar a chamar o `vssadmin` — e quando chamado, é sempre resolvido
  por caminho absoluto (nunca via busca no PATH do sistema), por segurança.

## Build

Requer Windows (o projeto usa Win32, COM/`IFileOperation`, Shell APIs e VSS
via `vssadmin`, além de um backend gráfico DirectX 11 — não compila nem
executa em outro sistema operacional).

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Alternativa com MinGW:

```powershell
cmake -B build -G "MinGW Makefiles"
cmake --build build
```

O Dear ImGui e o `nlohmann::json` são baixados automaticamente pelo CMake
(`FetchContent`) — não é preciso instalar nenhum SDK separado.

Algumas operações (pontos de restauração) exigem executar o `.exe` como
Administrador; sem isso, essa categoria é pulada e a UI mostra um aviso.

## Testes

```powershell
cmake --build build --target test_duplicate_grouping
ctest --test-dir build
```

Só o agrupamento de duplicados (`tests/test_duplicate_grouping.cpp`) é
testável fora do Windows, porque é a única peça de lógica sem dependência de
API do Windows — o restante (scanners, config, histórico) depende de
Win32/COM/VSS e só builda numa máquina Windows real.

## Verificado

Build completo (MSVC 19.44, Debug e Release) já foi compilado e rodado numa
máquina Windows real: janela abre, renderização DirectX 11 funciona, varredura
de disco real (temporários, cache de navegador, duplicados, logs, dados
órfãos) encontra itens de verdade, seleção/limpeza pela UI funciona, e a
release `v0.1.0` builda com 0 erros/0 warnings.

Ainda não verificado em uso real: comportamento do `vssadmin` em outras
versões/idiomas do Windows, `IFileOperation` enviando para a Lixeira em
todos os cenários de permissão (arquivo em uso, sem permissão, etc.),
registro de início automático, e a limpeza automática agendada rodando por
um período longo sem supervisão.

## Créditos e licenças de terceiros

Bibliotecas baixadas automaticamente pelo CMake (`FetchContent`), ambas
sob licença MIT:
- [Dear ImGui](https://github.com/ocornut/imgui) (v1.91.0)
- [nlohmann::json](https://github.com/nlohmann/json) (v3.11.3)

Este repositório ainda não declara uma licença própria (sem arquivo
`LICENSE`).
