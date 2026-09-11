# Diretrizes de Execução - Claude Code

## Idioma
- **SEMPRE** responder em Português Brasil (pt-BR), independente do idioma da pergunta

---

## Início de Cada Atividade — Obrigatório
1. **Ler** este arquivo `diretrizes.md`
2. **Ler** o `.memory` do projeto envolvido
3. Só então iniciar a tarefa com o contexto carregado

---

## Escolha do Modelo — Regra de Uso
| Situação | Modelo |
|---|---|
| Buscas, leituras, grep, exploração de código | **Haiku** (mais rápido e barato) |
| Implementação, refactoring, análise lógica | **Sonnet** |
| Sonnet falhou 2 vezes na mesma tarefa | **Opus** (escalada automática) |

> Usar Haiku sempre que a tarefa for investigativa/leitura. Só escalar para Sonnet quando for gerar ou modificar código. Opus apenas como último recurso.

---

## Informar Modelo — Obrigatório
Toda resposta deve começar com: `**Modelo: [Haiku | Sonnet | Opus]** — [motivo]`

---

## Economia de Tokens
- Ler **apenas o trecho relevante** (use `offset` + `limit`) — nunca ler arquivos grandes inteiros
- **NUNCA** repetir código já mostrado; referenciar por `arquivo:linha`
- Respostas curtas e diretas; sem resumo do que acabou de fazer
- Usar `Grep` antes de `Read` para localizar exatamente onde está o código
- Paralelizar chamadas de ferramentas independentes no mesmo turno
- Evitar re-explorar contexto já conhecido na conversa

---

## Fim de Cada Atividade — Preenchimento Obrigatório
Ao concluir qualquer atividade, **registrar no `.memory` do projeto** na seção `## Registro de Atividades Recentes`:

```markdown
### [YYYY-MM-DD] Título da Atividade
**O que foi feito:** Descrição concisa do que foi implementado/corrigido
**Arquivos alterados:**
- `Caminho/Do/Arquivo.ext` — o que foi alterado
- `Outro/Arquivo.ext` *(novo)* — se criado do zero
- Removido: `Arquivo/Removido.ext` — se deletado
```

Também atualizar `diretrizes.md` se houver nova regra ou padrão descoberto.

### Compactação Obrigatória — Regra de 800 Linhas
**Antes de adicionar qualquer registro**, verificar o número de linhas do `.memory`:
- Se o arquivo tiver **mais de 800 linhas**: compactar primeiro, depois adicionar o novo registro.
- **Como compactar:**
  1. Manter intactas as seções de contexto fixo (stack, arquitetura, padrões, pitfalls)
  2. Fundir registros antigos em `### [período] Histórico Compactado`, preservando apenas o que tem valor arquitetural
  3. Remover registros rotineiros que não acrescentam contexto duradouro

---

## Projetos nesta Máquina/Repositório

| Projeto | Caminho | Memory |
|---|---|---|
| Otimizador de Armazenamento (Windows) | `.` (raiz deste repositório) | `gerenciador-de-armazenamento-windows.memory` |
