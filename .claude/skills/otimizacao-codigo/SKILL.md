---
name: otimizacao-codigo
description: Analisa código existente no projeto em busca de oportunidades de otimização e melhoria — performance, uso de memória, leitura e uso de recursos (arquivos, conexões, threads, rede), código duplicado, complexidade desnecessária, código morto, más práticas da linguagem/framework — e propõe (ou aplica, com confirmação) refatorações concretas. Use esta skill sempre que o usuário pedir para "otimizar", "melhorar", "limpar", "refatorar" código, perguntar se um trecho "pode ficar mais rápido/eficiente" ou "consumir menos memória/recursos", pedir revisão de performance ou de uso de recursos, ou pedir para reduzir duplicação/complexidade — seja em um arquivo específico ou no projeto como um todo. Também use proativamente quando o usuário pedir uma revisão geral de qualidade do código.
---

# Otimização e Melhoria de Código

Esta skill guia uma revisão de código focada em performance, legibilidade e boas práticas, indo do diagnóstico à proposta de refatoração concreta — sempre com o antes/depois e o motivo da mudança.

## Quando não usar

Para pedidos sobre a organização estrutural do projeto (pastas, camadas, módulos, padrões arquiteturais), use a skill `arquitetura-projeto` em vez desta. Esta skill foca na qualidade do código em si, não na estrutura do projeto.

## Fluxo de trabalho

### 1. Delimitar o escopo

Confirme (ou infira do pedido) se a análise é sobre:
- Um arquivo/função específica
- Um módulo/pasta
- O projeto inteiro

Se o escopo não estiver claro e o projeto for grande, pergunte antes de varrer tudo — analisar um projeto inteiro linha a linha é caro e pode não ser o que o usuário quer.

### 2. Levantar o código real

Leia o código de fato (`view`, `grep`, `bash_tool`) antes de sugerir qualquer coisa. Nunca proponha otimizações "genéricas" sem ter visto o código real.

Se houver ferramentas de análise estática disponíveis no ecossistema do projeto (linters, profilers, analisadores de complexidade — ex. SonarQube, ESLint, Checkstyle, PMD, `go vet`, etc.), considere usá-las via `bash_tool` quando disponíveis no ambiente, mas não bloqueie a análise manual caso não estejam.

### 3. Diagnosticar por categoria

Percorra o código buscando, nesta ordem de prioridade (maior impacto primeiro):

1. **Complexidade algorítmica** — loops aninhados evitáveis, buscas lineares onde uma estrutura de dados melhor resolveria (ex. O(n²) que vira O(n log n) ou O(n)), recomputações desnecessárias dentro de loops
2. **Uso de memória** — alocações desnecessárias/repetidas dentro de loops, cópias de estruturas grandes que poderiam ser referências/views, coleções carregadas inteiras na memória quando streaming/paginação resolveria, objetos mantidos vivos além do necessário (referências que impedem garbage collection), estruturas de dados maiores do que o necessário para o caso de uso (ex. lista quando um set/map resolveria com menos overhead)
3. **Leitura e uso de recursos em geral** — arquivos, conexões de banco, sockets, streams e handles abertos sem fechamento garantido (falta de `try-with-resources`/`using`/`defer`/context manager conforme a linguagem), leitura de arquivo/rede inteira em memória quando leitura em chunks/streaming seria suficiente, conexões de banco/HTTP não reaproveitadas (falta de connection pooling), threads/tasks criadas sem controle ou não finalizadas, queries N+1, chamadas de rede/banco dentro de loops, falta de cache onde faria sentido
4. **Duplicação de código** — lógica repetida que deveria ser extraída (DRY), mas sem forçar abstrações prematuras
5. **Complexidade desnecessária** — funções/métodos longos demais, aninhamento excessivo de condicionais, código morto, complexidade ciclomática alta
6. **Idiomas da linguagem/framework** — uso de padrões não-idiomáticos quando o ecossistema já oferece algo melhor (ex. stream API em Java, list comprehension em Python, hooks corretos em React)
7. **Legibilidade e nomenclatura** — nomes pouco claros, falta de comentários onde a lógica é não-óbvia (sem exagerar em comentários óbvios)

Ao reportar problemas de memória e recursos, seja concreto sobre o mecanismo: diga o que fica retido na memória, por quanto tempo, e o que dispara o vazamento/desperdício (ex. "esse `List` acumula todos os registros antes de processar, então com um arquivo de 2GB o processo tenta alocar os 2GB de uma vez — trocar para leitura em stream processa linha a linha").

Não force problemas que não existem — se o código já está bom em uma categoria, não invente sugestão ali.

### 4. Apresentar o diagnóstico e as propostas

Para cada problema relevante encontrado:

- **O que é** — localização (arquivo/linha/função) e descrição objetiva do problema
- **Por que importa** — impacto real (ex. "esse loop faz uma query por iteração, então com 1000 registros são 1000 idas ao banco")
- **Proposta** — o código sugerido, mostrando antes/depois
- **Trade-offs**, se houver (ex. "isso troca legibilidade por performance" ou "essa mudança exige adicionar uma dependência nova")

Priorize as sugestões por impacto: comece pelas mudanças de maior ganho e menor risco.

### 5. Aplicar mudanças apenas com confirmação

Depois de apresentar o diagnóstico:
- Para correções pequenas e de baixo risco (ex. remover código morto óbvio, trocar um loop simples por uma função nativa equivalente), pode aplicar diretamente se o usuário já sinalizou que quer isso feito.
- Para refatorações maiores ou que alteram comportamento observável, sempre confirme com o usuário antes de editar os arquivos.
- Depois de aplicar qualquer mudança, quando houver testes no projeto, rode-os (`bash_tool`) para confirmar que nada quebrou. Se não houver testes, avise o usuário que a mudança não foi validada automaticamente.

## Notas

- Otimização prematura é um risco real — para código que não é hot path nem tem problema de performance perceptível, priorize legibilidade sobre ganhos marginais de performance.
- Sempre meça/justifique impacto quando possível (ex. complexidade Big-O, número de chamadas evitadas) em vez de afirmações vagas como "isso fica mais rápido".
