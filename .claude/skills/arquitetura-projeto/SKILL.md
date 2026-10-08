---
name: arquitetura-projeto
description: Analisa a estrutura de um projeto de software, avalia como ele está desenhado (camadas, módulos, dependências, padrões usados) e propõe mudanças arquiteturais quando fizer sentido. Use esta skill sempre que o usuário pedir para "revisar a arquitetura", "analisar a estrutura do projeto", "entender como o projeto está organizado", "fazer um brainstorm de design", "avaliar se o projeto está bem desenhado", ou pedir sugestões de reorganização de pastas/módulos/camadas — mesmo que não use a palavra "arquitetura" explicitamente. Também use quando o usuário estiver iniciando um projeto novo e quiser validar o desenho antes de codificar.
---

# Análise e Brainstorm de Arquitetura de Projeto

Esta skill guia uma revisão estrutural de um projeto de software: primeiro entender objetivamente como ele está montado, depois avaliar criticamente esse desenho, e só então propor mudanças — sempre justificando o porquê.

## Quando não usar

Para pedidos pontuais de "otimizar essa função" ou "melhorar a performance desse trecho", use a skill `otimizacao-codigo` em vez desta. Esta skill é sobre a organização estrutural do projeto como um todo (pastas, camadas, módulos, dependências, padrões), não sobre a qualidade de trechos de código isolados.

## Dois modos

- **Projeto existente** — o caso mais comum: mapear, avaliar e propor (passos 1 a 5).
- **Projeto novo ou decisão de design** ("vou começar um projeto X", "uso Kafka ou RabbitMQ?", "como desenho o módulo de notificações?") — não há código para mapear. Comece pelo passo 0 (requisitos) e vá direto para o desenho e o registro da decisão (passos 3 e 4).

### 0. Levantar requisitos (projeto novo / decisão de design)

Antes de desenhar, deixe explícito:

- **Requisitos funcionais** — o que o sistema precisa fazer.
- **Requisitos não funcionais** — volume/escala, latência, disponibilidade, custo.
- **Restrições** — tamanho e experiência do time, prazo, stack que já existe e não pode mudar.

Se o usuário não informou algo que muda a resposta (ex.: "precisa aguentar 10 mil req/s" ou "tem que ficar pronto em 2 semanas"), pergunte ou declare a suposição que você está fazendo. Depois, desenhe em alto nível: componentes, fluxo de dados, contratos de API e escolha de armazenamento — e aprofunde só onde a decisão é difícil (modelo de dados, cache, filas/eventos, tratamento de erro e retentativa).

## Fluxo de trabalho (projeto existente)

### 1. Mapear a estrutura real do projeto

Antes de opinar sobre qualquer coisa, explore o projeto de fato — nunca assuma a estrutura pelo nome da linguagem/framework.

- Liste a árvore de diretórios (ignorando `node_modules`, `.git`, `target`, `build`, `dist`, `__pycache__`, etc.)
- Identifique linguagem(ns), framework(s) e ferramentas de build/gerenciador de dependências (`pom.xml`, `package.json`, `requirements.txt`, `go.mod`, etc.)
- Identifique o(s) padrão(ões) arquitetural(is) aparente(s): MVC, camadas (controller/service/repository), hexagonal, feature-based, monólito modular, microsserviços, etc.
- Mapeie as dependências entre módulos/pacotes — quem depende de quem, e se essa direção faz sentido (ex.: domínio não deveria depender de infraestrutura)
- Note convenções de nomenclatura e onde elas são inconsistentes
- Se existirem, leia os registros de decisão já tomados (ADRs, `docs/`, `CLAUDE.md`, README de arquitetura) — uma "esquisitice" do desenho pode ser uma decisão consciente já documentada

Use `view`, `bash_tool` (ex.: `find`, `tree`, `grep`) para isso. Não pule esta etapa nem infira a estrutura sem checar.

### 2. Brainstorm crítico do desenho atual

Com o mapa em mãos, avalie o desenho — não apenas descreva-o. Pergunte-se e responda no seu output:

- **Separação de responsabilidades**: as camadas/módulos têm responsabilidade única e clara, ou há mistura (ex.: lógica de negócio dentro de controllers)?
- **Acoplamento e coesão**: módulos estão fortemente acoplados quando não precisariam estar? Coisas que mudam juntas estão no mesmo lugar?
- **Direção de dependências**: as dependências fluem na direção correta (ex.: camadas externas dependem de internas, não o contrário)?
- **Escalabilidade do desenho**: essa estrutura aguenta o projeto crescer (mais features, mais devs, mais dados)?
- **Consistência**: o padrão é aplicado de forma consistente ou há partes do projeto que fogem do padrão sem motivo aparente?
- **Testabilidade**: a estrutura atual facilita ou dificulta escrever testes unitários/integração?
- **Convenções da linguagem/framework**: o projeto segue as convenções idiomáticas do ecossistema (ex.: estrutura padrão do Spring Boot, do Next.js, etc.) ou se desvia sem razão clara?
- **Confiabilidade e operação** (quando o projeto roda em produção): pontos únicos de falha, tratamento de erro/retentativa entre serviços, monitoramento e logs suficientes para diagnosticar problemas.

Gere hipóteses de múltiplos ângulos antes de convergir — é um brainstorm, então vale listar mais de uma leitura possível do desenho antes de decidir qual é a mais relevante.

Ao classificar os problemas, use o tipo de débito técnico que cada um representa — isso ajuda a explicar o risco:

| Tipo | Exemplos | Risco se não tratar |
|---|---|---|
| Arquitetura | módulo que deveria ser separado, banco/armazenamento errado para o uso, dependência na direção errada | limite de escala, mudanças caras |
| Código | lógica duplicada entre módulos, abstração ruim | bugs, desenvolvimento lento |
| Testes | estrutura que impede teste, falta de teste de integração | regressões chegam em produção |
| Dependências | bibliotecas desatualizadas ou sem manutenção | vulnerabilidades |
| Documentação | decisão importante que só existe "na cabeça" de alguém | onboarding difícil, decisão desfeita por engano |
| Infraestrutura | deploy manual, sem monitoramento | incidentes, recuperação lenta |

### 3. Apresentar o diagnóstico

Estruture a resposta assim, de forma objetiva e sem enrolação:

1. **Resumo da estrutura atual** — 3-5 linhas, o que existe hoje
2. **Pontos fortes** — o que já está bem desenhado (sempre existe algo, mesmo em projetos com problemas)
3. **Pontos de atenção** — problemas reais encontrados, cada um com o porquê é um problema (não só "isso está errado") e o tipo de débito (tabela acima)
4. **Propostas de mudança** — apenas se houver problemas genuínos. Cada proposta deve ter: o que mudar, por que, o impacto esperado (custo/benefício) e as alternativas consideradas. Não proponha mudança por mudança — se o desenho atual é razoável, diga isso claramente.
5. **O que revisitar quando crescer** — decisões que hoje estão adequadas, mas que deixam de estar a partir de certo tamanho (ex.: "com mais de um time mexendo, vale separar o módulo X").

**Priorização das propostas.** Dê uma nota de 1 a 5 para cada proposta em:
- **Impacto** — quanto isso atrapalha o desenvolvimento hoje;
- **Risco** — o que acontece se não for corrigido;
- **Esforço** — quão difícil é corrigir.

Prioridade = (Impacto + Risco) × (6 − Esforço). Ordene pela prioridade e mostre as notas, para o usuário poder discordar de uma nota específica em vez de discordar da lista inteira. Para mudanças grandes, sugira um plano em fases que possa andar junto com o desenvolvimento de features, em vez de uma reescrita parada.

Se for útil, ofereça um diagrama (via Visualizer, quando fizer sentido) mostrando a estrutura atual e/ou a proposta — arquitetura se beneficia de visual quando há várias camadas/módulos.

### 4. Registrar decisões importantes (ADR)

Quando a conversa resultar numa decisão de design relevante (escolha de tecnologia, mudança de camada, quebra de módulo) ou quando o usuário pedir para comparar opções, ofereça registrar como um ADR (Architecture Decision Record). No Claude Code, o lugar natural é `docs/adr/ADR-NNN-titulo.md` no repositório — verifique se o projeto já tem uma pasta/numeração de ADRs e siga ela. Formato:

```markdown
# ADR-<número>: <Título>

**Status:** Proposto | Aceito | Obsoleto | Substituído por ADR-<n>
**Data:** <dd/mm/aaaa>
**Decisores:** <quem precisa aprovar>

## Contexto
<Qual é a situação e quais forças estão em jogo (requisitos, restrições).>

## Decisão
<O que foi decidido.>

## Opções consideradas

### Opção A: <nome>
| Dimensão | Avaliação |
|---|---|
| Complexidade | Baixa / Média / Alta |
| Custo | ... |
| Escalabilidade | ... |
| Familiaridade do time | ... |

**Prós:** ...
**Contras:** ...

### Opção B: <nome>
<mesmo formato>

## Análise de trade-offs
<Por que a opção escolhida vence nas dimensões que mais importam aqui.>

## Consequências
- O que fica mais fácil
- O que fica mais difícil
- O que precisaremos revisitar

## Próximos passos
1. [ ] ...
```

Sempre apresente pelo menos duas opções, mesmo quando uma é claramente melhor — explicitar a alternativa descartada é o que torna a decisão revisável no futuro.

### 5. Nunca aplicar mudanças estruturais sem confirmação

Mudanças de arquitetura (mover pastas, quebrar módulos, inverter dependências) são de alto impacto e geralmente difíceis de reverter. Depois de apresentar o diagnóstico e as propostas, pergunte ao usuário quais mudanças ele quer que você implemente antes de tocar em arquivos. Pequenas limpezas óbvias (ex.: renomear um arquivo mal nomeado) podem ser sugeridas inline, mas reestruturações maiores sempre esperam confirmação explícita. Criar o arquivo de ADR também espera o usuário aceitar.

## Notas

- Seja honesto quando o desenho atual está bom — não invente problemas para justificar a skill.
- Priorize as propostas por impacto (o que traz mais benefício com menos risco primeiro), usando a fórmula da seção 3.
- Se o projeto for pequeno/prototipagem, calibre as recomendações — over-engineering é tão problema quanto under-engineering.
- Requisitos não funcionais (latência, custo, experiência do time, manutenção) pesam tanto quanto funcionalidades — uma solução tecnicamente melhor que o time não domina pode ser a pior escolha.
