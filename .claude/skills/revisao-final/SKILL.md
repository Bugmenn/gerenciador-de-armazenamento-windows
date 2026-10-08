---
name: revisao-final
description: Revisão final de uma mudança de código antes de dar a tarefa como concluída — roda revisões em paralelo (uma delas cega), junta os achados, corrige tudo de uma vez e fecha cada achado com evidência. Use ao terminar qualquer mudança de código (mesmo sem o usuário pedir), quando o usuário pedir "revisão final", "revisa antes de fechar", "confere tudo antes do commit/PR", ou quando a Regra 08 do CLAUDE.md global se aplicar.
---

# Revisão final da entrega

Esta skill executa a Regra 08 do `CLAUDE.md` global: **uma** rodada de revisão completa, em paralelo, com pelo menos uma revisão cega, seguida de **uma** rodada de correção. O objetivo é pegar problemas reais antes de reportar conclusão — não revisar o mesmo diff em fatias até o cansaço.

## Quando não usar

- Mudança que não tocou código (só documentação, comentário, texto de commit): não precisa.
- O usuário pediu só para revisar algo, sem corrigir: use o subagente `revisor-codigo` direto e entregue o relatório dele.

## 1. Delimitar o que foi mudado

Antes de disparar qualquer revisão, fixe o escopo exato — é isso que os revisores vão receber:

- Mudanças não commitadas: `git status --short` + `git diff` + `git diff --staged`
- Já commitado numa branch: `git diff <base>...HEAD` (descubra a base com `git remote show origin` ou `git branch -a`)
- Liste os arquivos alterados (`git diff --name-only ...`). Arquivos novos não rastreados também entram (`git status --short` mostra com `??`).

Se o escopo for maior do que a tarefa (mudanças de outra tarefa misturadas), avise o usuário antes de seguir.

## 2. Disparar as revisões em paralelo

Na **mesma mensagem**, dispare duas instâncias do subagente `revisor-codigo` (ferramenta `Agent`, `subagent_type: revisor-codigo`):

**Revisão A — com contexto.** Recebe:
- o objetivo da tarefa (o que o usuário pediu, em 2-3 linhas);
- o comando git que mostra o diff (passo 1);
- restrições conhecidas (regras do `CLAUDE.md` do projeto que se aplicam, decisões já aprovadas pelo usuário).

**Revisão B — cega.** Recebe **somente**:
- o comando git que mostra o diff;
- esta instrução, sem acrescentar nada:

> Revise esta mudança sem nenhum contexto prévio. Descubra pelo próprio código o que a mudança se propõe a fazer e avalie se ela faz isso corretamente. Não existe revisão anterior para considerar: trate tudo como não aprovado.

Não conte à revisão B o que foi corrigido, o que já foi discutido nem o que você acha que está certo. Revisor que recebe a narrativa tende a validar a narrativa — é exatamente isso que a revisão cega existe para evitar.

Se a mudança envolver banco (migration, query, procedure), dispare também o `especialista-banco` em paralelo, focado só nos arquivos de banco. Se envolver testes novos ou a falta deles for uma suspeita, o `testador` pode ser acionado **depois**, na fase de correção.

Espere **todas** as revisões terminarem antes de mexer em qualquer coisa.

## 3. Consolidar os achados

Junte tudo numa lista única:

- Achados repetidos entre revisões viram um item só (anote que mais de um revisor apontou — aumenta a confiança).
- Achado que reaponta uma decisão de design já discutida e aprovada pelo usuário: marque como **já decidido**, não corrija, mas registre no relatório que a revisão levantou o ponto de novo.
- Ordene por gravidade: Crítico → Atenção → Sugestão.

## 4. Corrigir tudo de uma vez

Corrija os achados da lista numa passada só. **Não** rode a revisão de novo depois de cada correção — isso vira loop (revisar → corrigir 1 → revisar → corrigir 1…). Uma segunda rodada completa só se justifica quando a correção mudou o código de forma substancial (ex.: reescreveu uma função inteira, mudou o fluxo); nesse caso, rode a revisão cega de novo só sobre o que mudou.

Depois das correções, rode os testes do projeto (ou peça ao `testador`) e confira na saída que eles executaram.

## 5. Fechar cada achado com evidência

Nenhum achado é fechado por raciocínio. Cada um sai com **uma** destas três saídas:

- **Corrigido** — mostre o que mudou (`arquivo:linha` ou o trecho antes/depois).
- **Validado como não aplicável** — com a medição que prova: query rodada, plano de execução, leitura do código que contradiz a premissa, teste executado. Cite o resultado (o número, a linha), não só a conclusão.
- **Escalado ao usuário** — quando é decisão de produto/negócio, não técnica.

Se não der para validar no ambiente local, diga explicitamente "não verificável aqui porque X".

## 6. Relatório

Entregue ao usuário uma tabela curta:

| # | Achado | Origem (A / B / ambos) | Saída | Evidência |
|---|---|---|---|---|

Seguida de: testes rodados (com a contagem que apareceu na saída), o que ficou escalado e o que ficou como "já decidido". Se nenhuma revisão achou nada, diga isso em uma linha — não invente melhoria para mostrar trabalho.
