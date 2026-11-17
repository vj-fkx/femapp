# Tarefas — 008c beam-second-order

Status: em andamento
Design: [design.md](design.md) · Requisitos: [requirements.md](requirements.md)

Regras:
- Uma tarefa = uma mudança pequena, compilável e testável.
- Testes vêm antes ou junto da implementação que verificam.
- `[P]` marca tarefas que podem ser feitas em paralelo com a anterior.
- `[M]` marca tarefas que dependem de ação do mantenedor.
- Marque `[x]` somente com build e testes passando.
- Os IDs abaixo omitem o prefixo: `01` significa `REQ-008c-01`; `N01`
  significa `NFR-008c-01`.
- Em toda tarefa, as suítes da 007, da 008 e da 008b continuam passando sem
  mudança de resultado (14).

## Fase 1 — Fundações

- [x] **T01** `SparseCholesky::factorizeIndefinite` e `negativePivots`, com
  reuso da análise simbólica enquanto o padrão não muda (design §4, ADR
  0007). Testes: inércia de matrizes indefinidas conhecidas (diagonais,
  tridiagonais com autovalores sabidos), falha só em pivô zero, mesmo
  resultado com e sem reuso, `factorize` inalterado — arquivos:
  `linear_solver.hpp/.cpp`, `tests/unit/core/test_linear_solver.cpp` —
  cobre: 08, 10
- [x] **T02** [P] `AxialProfile` em `BeamData` (`at`, `largestCompression`,
  tamanho fixo); testes do perfil (constante, linear por parte, pontos de
  Gauss) e `test_allocations` com o perfil cheio — arquivos:
  `beam_element.hpp/.cpp`, `tests/unit/core/test_beam_second_order.cpp`,
  `test_allocations.cpp` — cobre: 02, 04
- [x] **T03** [M] Medida de custo antes dos laços (design §5.7): no
  `bench_frame`, montar todas as barras pelo motor exato com um N constante
  fictício e fatorar com `factorizeIndefinite`; registrar os tempos de
  montagem e fatoração contra a análise linear nas notas. Se a estimativa
  (segunda ordem ≤ 4×, flambagem ≤ 6×) não fechar, parar e propor a mudança
  da meta antes de seguir — arquivos: `tests/perf/bench_frame.cpp` —
  cobre: N02

## Fase 2 — A barra exata com esforço normal

- [ ] **T04** Testes do motor com N constante (deve falhar): rigidez da
  barra reta contra as funções de estabilidade fechadas (2D e 3D,
  compressão e tração, Euler-Bernoulli e Engesser), barra subdividida igual
  à inteira, N → 0 igual à 008b; cargas equivalentes da carga uniforme
  contra a forma fechada — arquivo: `test_beam_second_order.cpp` — cobre: 02
- [ ] **T05** Implementar o termo de N em `systemMatrix` (equilíbrio na
  geometria deformada e cortante de Engesser, design §5.1) e a regra de
  partes por N (crítica biengastada da parte acima de 4|N|; crescimento em
  tração) até a T04 passar; N ≤ −G As informado como instável — arquivos:
  `beam_state.cpp` — cobre: 02
- [ ] **T06** Viga-coluna (AC-1): biapoiada com carga uniforme e
  concentrada sob compressão e tração (Timoshenko-Gere), balanço com força
  lateral e compressão, momentos e deslocamentos por `stateAt` ao longo,
  Engesser contra a sua forma fechada, base elástica com N (viga de
  Hetényi comprimida) — arquivo: `test_beam_second_order.cpp` — cobre: 02, 06
- [ ] **T07** N variável (design §5.2): testes de viga-coluna com carga
  axial uniforme e trapezoidal contra integração muito refinada (deve
  falhar); implementar `axialParts` e o Magnus de 4ª ordem (com a matriz
  aumentada das cargas e as forças fixas no espaço) até passar com erro
  ≤ 1e−8; calibrar as subdivisões e registrar nas notas — arquivos:
  `beam_state.hpp/.cpp`, `test_beam_second_order.cpp` — cobre: 02, N01
- [ ] **T08** Pivôs negativos das junções (design §5.4): testes de barra
  biengastada subdividida com N entre as suas cargas críticas (4π²EI/L²,
  8,18π²EI/L², …) dando a contagem certa; implementar a inércia das junções
  em `stateBarMatrices` (`negativePivots`) — arquivos: `beam_state.cpp`,
  `test_beam_second_order.cpp` — cobre: 08, 10

## Fase 3 — Elementos

- [ ] **T09** `ExactBeamElement` com N: ligações condensadas pela
  decomposição de autovalores (blocos indefinidos permitidos), contagem por
  elemento num `span<int>`, termo de segunda ordem do braço rígido (design
  §5.3) contra a barra muito rígida (1e−6), contagem de um pilar com as
  pontas liberadas além da sua crítica — arquivos: `beam_state.hpp`,
  `beam_exact.cpp`, `test_beam_second_order.cpp` — cobre: 02, 08, 10
- [ ] **T10** [P] Rigidez geométrica da treliça (2D e 3D) contra a forma
  fechada; corda tensa — arquivos: `truss_element.hpp/.cpp`,
  `test_beam_second_order.cpp` — cobre: 03
- [ ] **T11** [P] Rigidez geométrica da isoparamétrica (design §5.3):
  simetria, modos rígidos sem energia geométrica em translação,
  convergência contra a barra exata com N (reta e arco), taxa registrada —
  arquivos: `beam_iso.hpp/.cpp`, `test_beam_second_order.cpp` — cobre: 04
- [ ] **T12** Arcos com perfil de N: o motor no arco com N variável contra
  uma cadeia refinada de barras retas exatas (extrapolação de Richardson);
  tolerância documentada nas notas — arquivos: `beam_state.cpp`,
  `test_beam_second_order.cpp` — cobre: 04

## Fase 4 — Segunda ordem

- [ ] **T13** Partir `runLinearStatic` em etapas reaproveitáveis
  (`detail/static_stages.hpp`: preparar, montar com perfis, resolver,
  recuperar), sem mudar resultados; nenhum teste novo, todas as suítes
  passam — arquivos: `static_analysis.cpp`, `detail/static_stages.hpp` —
  cobre: 14
- [ ] **T14** Perfis a partir de uma solução (design §5.2): barra reta
  (esforço de ponta mais carga axial), arco (`stateAt` nos pontos de
  Gauss), isoparamétrica (pontos reduzidos), treliça; testes contra os
  esforços da análise linear — arquivos: `static_stages`,
  `test_second_order_analysis.cpp` — cobre: 05
- [ ] **T15** Testes da segunda ordem (devem falhar): pórtico plano de um
  andar contra funções de estabilidade, ordem de grandeza de
  1/(1 − P/Pcr), iterações e variação no resultado, P/Pcr = 0,95
  convergindo em menos de 50 iterações, treliça com corda tensa e P-Δ
  global (AC-2, AC-3) — arquivo: `test_second_order_analysis.cpp` — cobre:
  01, 03, 05, 06
- [ ] **T16** `stability_analysis.hpp` e `runSecondOrderStatic` (design §5.5):
  iteração com Anderson, contagem de Sturm, meio passo, mensagens de
  instabilidade (nó e direção, ou elemento) e de não convergência, até a
  T15 e os testes de falha (AC-7) passarem — arquivos:
  `stability_analysis.hpp`, `second_order.cpp`, `src/core/CMakeLists.txt` —
  cobre: 01, 05, 06, 07, 08, 13
- [ ] **T17** Segunda ordem com vigas curvas (AC-6): arco biarticulado
  semicircular com carga no topo e distribuída, arco e isoparamétrica
  convergindo um para o outro e para a cadeia de barras retas — arquivo:
  `test_second_order_analysis.cpp` — cobre: 04

## Fase 5 — Flambagem

- [ ] **T18** Testes de flambagem de barras (devem falhar, AC-4): Euler nos
  quatro casos em 2D e 3D, Euler-Bernoulli e Engesser, mola rotacional,
  liberação, Hetényi com o número de semiondas, modos normalizados, forma
  senoidal ao longo da barra — arquivo: `test_buckling.cpp` — cobre: 09,
  10, 11
- [ ] **T19** `runLinearBuckling` (design §5.6, ADR 0007): contagem J(λ),
  deslocamento com iteração de subespaço (K' central e depois secante),
  garantia pela contagem com bissecção, fatores repetidos, modos e
  `BeamResult` dos modos, até a T18 passar — arquivos: `buckling.cpp`,
  `stability_analysis.hpp` — cobre: 09, 10, 11
- [ ] **T20** Flambagem de pórticos (AC-5): deslocável e indeslocável
  contra funções de estabilidade, fatores em ordem, pilar com
  excentricidade, fatores repetidos por simetria, fator perdido de
  propósito (estimativa ruim) e recuperado pela contagem — arquivo:
  `test_buckling.cpp` — cobre: 09, 10
- [ ] **T21** [P] Flambagem de arcos (AC-6): arco e isoparamétrica contra a
  cadeia de barras retas exatas — arquivo: `test_buckling.cpp` — cobre: 04,
  10
- [ ] **T22** Falhas da flambagem (AC-7): caso só com tração recusado,
  menos fatores que o pedido com a `note` — arquivo: `test_buckling.cpp` —
  cobre: 12

## Fase 6 — Desempenho e documentação

- [ ] **T23** [M] `bench_frame --second-order` e `--buckling 5`: registrar
  os tempos contra a análise linear nas notas. Se passarem de 4× e 6×,
  parar e propor a mudança da meta — arquivos: `tests/perf/bench_frame.cpp`
  — cobre: N02
- [ ] **T24** `specs/architecture.md` §3 (segunda ordem, flambagem, solver
  indefinido, ADR 0007); notas de implementação aqui — cobre: 15

## Fase 9 — Fechamento

- [ ] **T90** [M] Build e testes verdes nos jobs do CI, incluindo o de
  cobertura com `core` ≥ 80% (push com sua confirmação). Conferir também o
  job do macOS, que não rodou no commit e0cad92 da 008b por falta de
  runner — cobre: N03
- [ ] **T91** `clang-format` e `clang-tidy` sem pendências (também o do
  Linux, pelo CI); `core` sem headers Qt — cobre: 15
- [ ] **T92** Conferir os oito critérios de aceite de `requirements.md`
- [ ] **T93** Atualizar `specs/roadmap.md` para `concluída`

## Matriz de rastreabilidade

| Requisito | Tarefas | Testes |
|-----------|---------|--------|
| REQ-008c-01 | T15, T16 | `test_second_order_analysis.cpp` |
| REQ-008c-02 | T02, T04 a T07, T09 | `test_beam_second_order.cpp` |
| REQ-008c-03 | T10, T15 | `test_beam_second_order.cpp`, `test_second_order_analysis.cpp` |
| REQ-008c-04 | T02, T11, T12, T17, T21 | `test_beam_second_order.cpp`, `test_second_order_analysis.cpp`, `test_buckling.cpp` |
| REQ-008c-05 | T14 a T16 | `test_second_order_analysis.cpp` |
| REQ-008c-06 | T06, T15, T16 | `test_beam_second_order.cpp`, `test_second_order_analysis.cpp` |
| REQ-008c-07 | T16 | `test_second_order_analysis.cpp` |
| REQ-008c-08 | T01, T08, T09, T16 | `test_linear_solver.cpp`, `test_beam_second_order.cpp`, `test_second_order_analysis.cpp` |
| REQ-008c-09 | T18 a T20 | `test_buckling.cpp` |
| REQ-008c-10 | T01, T08, T09, T18 a T21 | `test_buckling.cpp`, `test_beam_second_order.cpp` |
| REQ-008c-11 | T18, T19 | `test_buckling.cpp` |
| REQ-008c-12 | T22 | `test_buckling.cpp` |
| REQ-008c-13 | T16 | `test_second_order_analysis.cpp` |
| REQ-008c-14 | todas, T13 | suítes da 007 a 008b inalteradas |
| REQ-008c-15 | T24, T91 | `check_no_qt_includes` |
| NFR-008c-01 | T04 a T07, T15, T18, T20 | os testes de verificação |
| NFR-008c-02 | T03, T23 | `bench_frame` |
| NFR-008c-03 | T90 | Job de cobertura no CI |

## Notas de implementação

**Fase 1 — Fundações (T01 a T03):**

- T01: sem pivoteamento, a LDLᵀ pode encontrar um pivô zero numa matriz
  regular (o laplaciano deslocado de 1,0 dá pivôs 1, 0, …): o teste usa
  outros deslocamentos, e a flambagem desloca σ quando isso acontece
  (design §7).
- T03 (`bench_frame --cost`, Release, máquina do mantenedor, 29 040 GDL):
  análise linear 3,05 a 3,16 s; o motor exato em todas as 13 640 barras,
  sequencial, 0,20 a 0,23 s; montagem exata + LDLᵀ indefinida + solução
  2,99 a 3,48 s, quase tudo na fatoração (cerca de 3,0 s cada; o reuso da
  análise simbólica quase não muda); **uma solução com a fatoração pronta,
  0,021 s**. Com uma fatoração por iteração, a estimativa do design §5.7
  não fecha: segunda ordem com 5 iterações 5,9× a linear (meta 4×),
  flambagem com 15 fatorações 15,8× (meta 6×). Parado para propor a
  mudança do design (reaproveitar fatorações).
