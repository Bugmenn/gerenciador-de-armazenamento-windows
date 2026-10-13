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
| Cache do Windows Update | Conteúdo de `Windows\SoftwareDistribution\Download` sem atividade no último dia (a pasta em si é mantida) | Lixeira (reversível) — requer Administrador |
| Despejos e relatórios de erro | `Windows\Minidump`, `Windows\MEMORY.DMP`, `AppData\Local\CrashDumps` de cada usuário e relatórios do WER em `ProgramData` | Lixeira (reversível) |
| Cache de miniaturas | `thumbcache_*.db` e `iconcache_*.db` do Explorer (os que o Explorer estiver usando no momento são pulados) | Lixeira (reversível) |

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
  (só tamanhos agregados, sem hash de duplicados) para manter o Painel
  atualizado. Fechar a janela pela barra de título apenas a esconde; a saída
  real do processo é pelo item "Sair" no menu da bandeja.
- **Limpeza automática agendada**: configurável para rodar todo dia, a cada
  7 dias, a cada 15 dias ou uma vez por mês, num horário definido. Dois
  modos: excluir automaticamente o que for encontrado, ou apenas escanear e
  avisar por um balão na bandeja quanto foi encontrado. **O app nunca abre a
  janela nem rouba o foco sozinho** (o usuário pode estar jogando, digitando
  ou apresentando): a janela só abre se ele clicar no balão ou em "Abrir" no
  menu da bandeja. O próprio Windows adia os balões durante jogos em tela
  cheia, apresentações e "Não perturbe". Se a varredura não encontrar nada,
  ou se a janela já estiver aberta, não há balão.
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

Avisos em balão (clicar em qualquer um abre a janela):

| Balão | Quando aparece |
|---|---|
| Varredura concluída | Varredura pedida pelo usuário (tela ou "Escanear agora") termina com a janela escondida na bandeja ou minimizada — inclusive quando não há nada a limpar. Cancelada não avisa. |
| Varredura agendada concluída | Agendamento no modo "pedir confirmação" encontrou algo e a janela está escondida na bandeja ou minimizada |
| Limpeza automática concluída | Agendamento no modo "excluir automaticamente" terminou (em `--dry-run`, mostra o que *seria* liberado) |
| Pouco espaço em disco | Unidade fixa abaixo do percentual configurado (uma vez por queda) |

Os avisos saem com pelo menos 8 segundos entre um e outro: o Windows troca um
balão pelo seguinte, então dois avisos seguidos fariam o primeiro sumir.

`StorageCleaner.exe --test-balloons` mostra os seis textos possíveis em
sequência, um a cada 8 segundos, com o título prefixado por "[Teste n/6]" —
serve para conferir que os avisos aparecem nesta máquina. Nesse modo o
agendamento fica parado e o modo simulação (`--dry-run`) é ligado junto, então
nada é excluído.

### Durante uma varredura

O Painel mostra um indicador circular de progresso, a fase atual, o
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
| `config.json` | Configurações da aba "Configurações" (gravadas quando o usuário clica em "Salvar"); as pastas protegidas são gravadas na hora, e a posição/tamanho/maximizado da janela ao sair do app |
| `history.json` | Histórico de limpezas executadas |

O app não grava nada na pasta do `.exe` (versões anteriores deixavam um
`imgui.ini` ali ao fechar; pode ser apagado). Se a posição salva cair fora de
qualquer monitor (monitor desconectado, resolução menor), a janela abre na
posição padrão.

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
| Alerta de pouco espaço (balão na bandeja, só unidades fixas) | ligado, abaixo de 10% livre (aceita 1-50%) |
| Pastas protegidas | nenhuma |
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

## Modo simulação

`StorageCleaner.exe --dry-run` faz tudo como de costume (varredura, revisão,
confirmação, histórico), mas a limpeza **não exclui nada**: o histórico
registra o que *seria* liberado, marcado como "(simulação)". Uma limpeza
automática simulada não adia a próxima limpeza automática real.

## Pastas protegidas e alerta de espaço

Na aba Configurações dá para proteger pastas: nada dentro delas é oferecido
para limpeza, nem uma pasta que as contenha (apagar a pasta-pai levaria a
protegida junto). O Painel mostra o espaço livre de cada unidade fixa ou
removível, e um balão na bandeja avisa quando uma unidade fixa cai abaixo do
percentual configurado — uma vez por queda, não a cada varredura.

## Testes

```powershell
cmake --build build --target test_duplicate_grouping test_category_keys test_path_exclusion test_progress
ctest --test-dir build
```

Só o agrupamento de duplicados (`tests/test_duplicate_grouping.cpp`) e as
chaves de persistência das categorias (`tests/test_category_keys.cpp`) são
testáveis fora do Windows, porque são as únicas peças de lógica sem
dependência de API do Windows — o restante (scanners, config, histórico)
depende de Win32/COM/VSS e só builda numa máquina Windows real.

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
