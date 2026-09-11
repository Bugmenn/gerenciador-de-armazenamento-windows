# CLAUDE.md

Este arquivo orienta o Claude Code ao trabalhar neste repositório.

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

Otimizador/limpador de armazenamento para Windows, app desktop em C++ com
Dear ImGui. Veja `README.md` para a visão funcional completa e
`Memory/gerenciador-de-armazenamento-windows.memory` para o contexto de
arquitetura, stack e decisões técnicas.

Este repositório também traz skills e subagentes pessoais em `.claude/skills/`
e `.claude/agents/`, replicados de `github.com/Bugmenn/claude`.
