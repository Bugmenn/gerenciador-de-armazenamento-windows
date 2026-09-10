# Otimizador/Limpador de Armazenamento (Windows)

Aplicativo desktop em C++ (Dear ImGui) para Windows que varre o disco,
identifica arquivos e dados que podem ser removidos com segurança para
liberar espaço, e permite ao usuário revisar e limpar — manualmente ou de
forma agendada.

## O que ele varre

| Categoria | O que é | Exclusão |
|---|---|---|
| Arquivos temporários | `%TEMP%`, `C:\Windows\Temp`, `AppData\Local\Temp` de todos os usuários | Lixeira (reversível) |
| Cache de navegador | Cache do Chrome, Edge e Firefox, por perfil de usuário | Lixeira (reversível) |
| Arquivos duplicados | Agrupados por tamanho e depois por hash SHA-256; mantém sempre a cópia mais antiga | Lixeira (reversível) |
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
  removidos.

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

## Verificado neste ambiente de desenvolvimento

Este projeto foi escrito e revisado num container Linux sem toolchain
Windows disponível. O que foi possível verificar aqui:
- Revisão de código de todos os arquivos.
- Compilação e execução real de `tests/test_duplicate_grouping.cpp` (lógica
  pura, sem I/O nem APIs do Windows).
- Checagem de sintaxe dos headers compartilhados (`include/storagecleaner`).

Ainda precisa ser verificado numa máquina Windows real: build completo com
MSVC/MinGW, varreduras reais em disco, `IFileOperation` enviando de fato
para a Lixeira, comportamento do `vssadmin` (varia por versão/idioma do
Windows) e do modo não-elevado, `SHQueryRecycleBinW`/`SHEmptyRecycleBinW`,
registro de início automático, e a renderização DirectX 11 em hardware real.
