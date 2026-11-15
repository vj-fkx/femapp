# Requisitos — 006 fem-kernel

Status: aprovado · Data: 2026-11-13

## 1. Contexto e objetivo

O femapp já lê e mostra malhas, mas ainda não calcula nada. Esta feature cria
o núcleo de elementos finitos em `core`, independente da física: funções de
forma e quadraturas para as famílias de elementos do produto, o mapeamento
isoparamétrico, a numeração dos graus de liberdade (GDL) por nó com apoios
eliminados, a montagem paralela de matrizes esparsas e um solver direto. As
features de elementos (treliça 007, viga 008, sólidos 024/025/028, térmica
030) só vão acrescentar a formulação de cada elemento. Como ainda não há
elemento físico, o núcleo é verificado de ponta a ponta com um elemento
escalar de Poisson que existe só nos testes.

## 2. Histórias de usuário

- **US-1** Como desenvolvedor de elementos, quero funções de forma, derivadas,
  quadraturas e mapeamento isoparamétrico prontos e verificados, para escrever
  só a formulação de cada elemento.
- **US-2** Como desenvolvedor de análises, quero numerar GDL por nó conforme os
  elementos conectados, impor apoios e recalques, montar o sistema em paralelo
  e resolvê-lo, para ter a base de qualquer análise linear.
- **US-3** Como engenheiro, quero resultados corretos e rápidos, para confiar
  no programa em modelos grandes.

## 3. Requisitos funcionais (EARS)

### Funções de forma e mapeamento

| ID | Requisito | História |
|----|-----------|----------|
| REQ-006-01 | O sistema DEVE fornecer funções de forma de Lagrange e suas derivadas nas coordenadas do elemento de referência, na ordem local de nós do Gmsh (a mesma de `core::referenceNodeCoordinates`), para: linha de 2 e 3 nós; triângulo de 3 e 6; quadrilátero de 4, 8 e 9; tetraedro de 4 e 10; hexaedro de 8, 20 e 27; cunha de 6, 15 e 18; pirâmide de 5. | US-1 |
| REQ-006-02 | Para cada tipo do REQ-006-01, as funções de forma DEVEM valer 1 no próprio nó e 0 nos demais (delta de Kronecker) e somar 1 em qualquer ponto (partição da unidade), e suas derivadas DEVEM somar 0. | US-1 |
| REQ-006-03 | O sistema DEVE calcular, num ponto do elemento de referência e para as coordenadas dos nós de um elemento, a posição física, a matriz jacobiana, seu determinante (ou, para linhas e superfícies imersas em 3D, a medida de comprimento ou área) e as derivadas das funções de forma em coordenadas físicas. | US-1 |
| REQ-006-04 | SE o determinante jacobiano de um elemento sólido ou plano for nulo ou negativo num ponto de integração, ENTÃO O sistema DEVE recusar o elemento com um erro que informe o elemento. | US-1, US-3 |
| REQ-006-05 | O sistema NÃO DEVE alocar memória dinâmica por elemento ou por ponto de integração ao avaliar funções de forma e o mapeamento, para não limitar a montagem paralela. | US-3 |

### Quadratura

| ID | Requisito | História |
|----|-----------|----------|
| REQ-006-06 | O sistema DEVE fornecer regras de quadratura, por família e grau de exatidão pedido: Gauss–Legendre na linha (até 5 pontos); produtos de Gauss no quadrilátero e no hexaedro; regras simétricas no triângulo e no tetraedro com pesos positivos; produto triângulo × linha na cunha; e uma regra para a pirâmide. | US-1 |
| REQ-006-07 | Cada regra DEVE integrar exatamente, até o arredondamento, os polinômios até o seu grau de exatidão declarado, que DEVE chegar pelo menos a 5 na linha, no quadrilátero e no hexaedro, e a 4 no triângulo, no tetraedro e na cunha. | US-1 |

### Graus de liberdade

| ID | Requisito | História |
|----|-----------|----------|
| REQ-006-08 | O sistema DEVE oferecer os tipos de GDL por nó u, v, w (translações), θx, θy, θz (rotações) e T (temperatura); cada elemento declara quais tipos usa em cada um de seus nós. | US-2 |
| REQ-006-09 | O sistema DEVE numerar os GDL de uma malha só nos nós e tipos usados pelos elementos considerados (um nó só ganha θz se algum elemento ligado a ele o usa), de forma determinística para a mesma entrada. | US-2 |
| REQ-006-10 | O sistema DEVE permitir prescrever o valor de qualquer GDL existente (zero para apoio, outro valor para recalque); os GDL prescritos DEVEM sair do sistema a resolver (eliminação), e o efeito dos valores não nulos DEVE entrar no lado direito. | US-2 |
| REQ-006-11 | SE um GDL prescrito não existir na numeração (nó ou tipo não usado por nenhum elemento), ENTÃO O sistema DEVE recusar a prescrição com um erro que informe o nó e o tipo. | US-2 |

### Elementos e montagem

| ID | Requisito | História |
|----|-----------|----------|
| REQ-006-12 | O sistema DEVE definir uma interface de formulação de elemento que, para um elemento da malha, declare os GDL por nó e calcule a matriz local e o vetor local em memória fornecida pelo chamador, sem alocar por elemento. | US-1, US-2 |
| REQ-006-13 | O sistema DEVE montar a matriz global esparsa simétrica e o vetor global a partir das contribuições locais de um conjunto de elementos, em paralelo, com resultado igual ao da montagem sequencial até o arredondamento (diferença relativa ≤ 1e−12). | US-2, US-3 |
| REQ-006-14 | O sistema DEVE aceitar cargas nodais concentradas em qualquer GDL livre e somá-las ao vetor global. | US-2 |

### Solução e pós-processamento

| ID | Requisito | História |
|----|-----------|----------|
| REQ-006-15 | O sistema DEVE resolver o sistema linear simétrico definido positivo com um solver direto esparso, atrás de uma interface que permita trocar o solver sem mudar o restante. | US-2 |
| REQ-006-16 | SE a fatoração falhar (matriz singular ou não definida positiva, por exemplo um mecanismo por falta de apoio), ENTÃO O sistema DEVE informar a falha com uma mensagem e não devolver solução. | US-2, US-3 |
| REQ-006-17 | Depois de resolver, O sistema DEVE devolver o valor de cada GDL (livres e prescritos) e as reações nos GDL prescritos (forças internas menos cargas aplicadas nesses GDL). | US-2 |
| REQ-006-18 | O sistema DEVE manter tudo desta feature em `core`, sem Qt nem GPU, com a nova dependência de paralelismo (oneTBB) só em `core`. | US-2 |

## 4. Requisitos não funcionais

| ID | Requisito | Como medir |
|----|-----------|------------|
| NFR-006-01 | A montagem do elemento de teste (Poisson, Tet4) numa malha de 1 milhão de tetraedros DEVE levar menos de 2 s em Release na máquina do mantenedor. | Benchmark |
| NFR-006-02 | Com 4 ou mais núcleos, a montagem paralela DEVE ser pelo menos 2,5 vezes mais rápida que a sequencial na mesma malha. | Benchmark com 1 thread e com todas |
| NFR-006-03 | A solução direta de um problema de Poisson 3D com cerca de 100 mil GDL DEVE levar menos de 10 s em Release. | Benchmark |
| NFR-006-04 | A cobertura de linhas de `core` DEVE ser de pelo menos 80% (constituição §6.5). | Relatório de cobertura (gcov/llvm-cov no CI ou local) |

## 5. Critérios de aceite

- [ ] **AC-1** (REQ-006-01, 02) Testes: delta de Kronecker nos nós de
  referência, partição da unidade e soma nula das derivadas em pontos
  aleatórios, para todos os tipos; derivadas conferidas por diferenças finitas
  (tolerância 1e−7).
- [ ] **AC-2** (REQ-006-03, 04) Testes: mapeamento de elementos afins e
  distorcidos (posição, jacobiano, volume pela soma dos pesos); elemento
  invertido recusado; comprimento de uma linha e área de um triângulo
  imersos em 3D.
- [ ] **AC-3** (REQ-006-06, 07) Testes: cada regra integra monômios até o grau
  declarado com erro relativo ≤ 1e−12; pesos positivos nos simplexos.
- [ ] **AC-4** (REQ-006-08 a 11) Testes: numeração com elementos que usam
  conjuntos diferentes de GDL; determinismo; prescrições válidas e inválidas.
- [ ] **AC-5** (REQ-006-12 a 17) Testes com o elemento de Poisson: patch test
  (solução linear reproduzida até o arredondamento) em malhas 1D, 2D e 3D
  lineares e quadráticas; problema com solução analítica conhecida com taxa
  de convergência do erro L2 próxima de p + 1 (linear ≈ 2, quadrático ≈ 3)
  em refinamentos sucessivos; reações equilibrando as cargas; montagem
  paralela igual à sequencial; mecanismo detectado.
- [ ] **AC-6** (REQ-006-05, 18) Verificação de que `core` segue sem Qt e de que
  a avaliação por elemento não aloca (teste com contador de alocações ou
  revisão documentada).
- [ ] **AC-7** (NFR-006-01 a 04) Medições e cobertura registradas em
  `tasks.md`.
- [ ] **AC-8** Build e testes verdes nos quatro jobs do CI, com o oneTBB
  instalado pelo vcpkg em todas as plataformas.

## 6. Fora de escopo

- Elementos físicos: treliça (007), viga (008), sólidos (024, 025, 028), térmica
  (030), cascas (035); o elemento de Poisson existe só nos testes.
- `Material`, `Section`, `Model`, atribuições pela interface e cargas
  distribuídas — features 007, 008 e 010.
- Restrições entre GDL (vínculos multiponto, rótulas, ligações entre barra,
  casca e sólido) — features 008 e 036.
- Solvers iterativos (CG) — feature 025; autovalores — 033; integração no
  tempo — 031 e 034.
- Execução assíncrona com progresso e cancelamento — feature 011.
- Pirâmide quadrática e elementos de ordem maior que 2.

## 7. Requisitos globais relacionados

REQ-G-16 (correção numérica: patch test e convergência, aqui com o elemento
de teste), REQ-G-21 e REQ-G-22 (desempenho e uso de todos os núcleos, na
montagem), REQ-G-13 (base do `DofMap` com GDL por nó para modelos mistos).

## 8. Questões em aberto

- [x] **Funções de forma:** decidido em 2026-11-13 — todas as famílias, ordens
  1 e 2 (mais a pirâmide linear).
- [x] **Paralelismo:** decidido em 2026-11-13 — oneTBB já nesta feature.
- [x] **Apoios:** decidido em 2026-11-13 — eliminação dos GDL prescritos.
- [x] **Verificação:** decidido em 2026-11-13 — elemento escalar de Poisson só
  nos testes.
- [x] **Malhas de 1 milhão de Tet4 para o NFR-006-01:** decidido em
  2026-11-13 — `build/perf-data/box_1m_tet4_bin.msh` da feature 003.
