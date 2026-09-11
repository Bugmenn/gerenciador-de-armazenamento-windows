---
name: arquitetura-projeto
description: Analisa a estrutura de um projeto de software, avalia como ele está desenhado (camadas, módulos, dependências, padrões usados) e propõe mudanças arquiteturais quando fizer sentido. Use esta skill sempre que o usuário pedir para "revisar a arquitetura", "analisar a estrutura do projeto", "entender como o projeto está organizado", "fazer um brainstorm de design", "avaliar se o projeto está bem desenhado", ou pedir sugestões de reorganização de pastas/módulos/camadas — mesmo que não use a palavra "arquitetura" explicitamente. Também use quando o usuário estiver iniciando um projeto novo e quiser validar o desenho antes de codificar.
---

# Análise e Brainstorm de Arquitetura de Projeto

Esta skill guia uma revisão estrutural de um projeto de software: primeiro entender objetivamente como ele está montado, depois avaliar criticamente esse desenho, e só então propor mudanças — sempre justificando o porquê.

## Quando não usar

Para pedidos pontuais de "otimizar essa função" ou "melhorar a performance desse trecho", use a skill `otimizacao-codigo` em vez desta. Esta skill é sobre a organização estrutural do projeto como um todo (pastas, camadas, módulos, dependências, padrões), não sobre a qualidade de trechos de código isolados.

## Fluxo de trabalho

### 1. Mapear a estrutura real do projeto

Antes de opinar sobre qualquer coisa, explore o projeto de fato — nunca assuma a estrutura pelo nome da linguagem/framework.

- Liste a árvore de diretórios (ignorando `node_modules`, `.git`, `target`, `build`, `dist`, `__pycache__`, etc.)
- Identifique linguagem(ns), framework(s) e ferramentas de build/gerenciador de dependências (`pom.xml`, `package.json`, `requirements.txt`, `go.mod`, etc.)
- Identifique o(s) padrão(ões) arquitetural(is) aparente(s): MVC, camadas (controller/service/repository), hexagonal, feature-based, monólito modular, microsserviços, etc.
- Mapeie as dependências entre módulos/pacotes — quem depende de quem, e se essa direção faz sentido (ex.: domínio não deveria depender de infraestrutura)
- Note convenções de nomenclatura e onde elas são inconsistentes

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

Gere hipóteses de múltiplos ângulos antes de convergir — é um brainstorm, então vale listar mais de uma leitura possível do desenho antes de decidir qual é a mais relevante.

### 3. Apresentar o diagnóstico

Estruture a resposta assim, de forma objetiva e sem enrolação:

1. **Resumo da estrutura atual** — 3-5 linhas, o que existe hoje
2. **Pontos fortes** — o que já está bem desenhado (sempre existe algo, mesmo em projetos com problemas)
3. **Pontos de atenção** — problemas reais encontrados, cada um com o porquê é um problema (não só "isso está errado")
4. **Propostas de mudança** — apenas se houver problemas genuínos. Cada proposta deve ter: o que mudar, por que, e o impacto esperado (custo/benefício). Não proponha mudança por mudança — se o desenho atual é razoável, diga isso claramente.

Se for útil, ofereça um diagrama (via Visualizer, quando fizer sentido) mostrando a estrutura atual e/ou a proposta — arquitetura se beneficia de visual quando há várias camadas/módulos.

### 4. Nunca aplicar mudanças estruturais sem confirmação

Mudanças de arquitetura (mover pastas, quebrar módulos, inverter dependências) são de alto impacto e geralmente difíceis de reverter. Depois de apresentar o diagnóstico e as propostas, pergunte ao usuário quais mudanças ele quer que você implemente antes de tocar em arquivos. Pequenas limpezas óbvias (ex.: renomear um arquivo mal nomeado) podem ser sugeridas inline, mas reestruturações maiores sempre esperam confirmação explícita.

## Notas

- Seja honesto quando o desenho atual está bom — não invente problemas para justificar a skill.
- Priorize as propostas por impacto (o que traz mais benefício com menos risco primeiro).
- Se o projeto for pequeno/prototipagem, calibre as recomendações — over-engineering é tão problema quanto under-engineering.
