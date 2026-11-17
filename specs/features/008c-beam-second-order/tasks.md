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

- [x] **T04** Testes do motor com N constante (deve falhar): rigidez da
  barra reta contra as funções de estabilidade fechadas (2D e 3D,
  compressão e tração, Euler-Bernoulli e Engesser), barra subdividida igual
  à inteira, N → 0 igual à 008b; cargas equivalentes da carga uniforme
  contra a forma fechada — arquivo: `test_beam_second_order.cpp` — cobre: 02
- [x] **T05** Implementar o termo de N em `systemMatrix` (equilíbrio na
  geometria deformada e cortante de Engesser, design §5.1) e a regra de
  partes por N (crítica biengastada da parte acima de 4|N|; crescimento em
  tração) até a T04 passar; N ≤ −G As informado como instável — arquivos:
  `beam_state.cpp` — cobre: 02
- [x] **T06** Viga-coluna (AC-1): biapoiada com carga uniforme e
  concentrada sob compressão e tração (Timoshenko-Gere), balanço com força
  lateral e compressão, momentos e deslocamentos por `stateAt` ao longo,
  Engesser contra a sua forma fechada, base elástica com N (viga de
  Hetényi comprimida) — arquivo: `test_beam_second_order.cpp` — cobre: 02, 06
- [x] **T07** N variável (design §5.2): testes de viga-coluna com carga
  axial uniforme e trapezoidal contra integração muito refinada (deve
  falhar); implementar `axialParts` e o Magnus de 4ª ordem (com a matriz
  aumentada das cargas e as forças fixas no espaço) até passar com erro
  ≤ 1e−8; calibrar as subdivisões e registrar nas notas — arquivos:
  `beam_state.hpp/.cpp`, `test_beam_second_order.cpp` — cobre: 02, N01
- [x] **T08** Pivôs negativos das junções (design §5.4): testes de barra
  biengastada subdividida com N entre as suas cargas críticas (4π²EI/L²,
  8,18π²EI/L², …) dando a contagem certa; implementar a inércia das junções
  em `stateBarMatrices` (`negativePivots`) — arquivos: `beam_state.cpp`,
  `test_beam_second_order.cpp` — cobre: 08, 10

## Fase 3 — Elementos

- [x] **T09** `ExactBeamElement` com N: ligações condensadas pela
  decomposição de autovalores (blocos indefinidos permitidos), contagem por
  elemento num `span<int>`, termo de segunda ordem do braço rígido (design
  §5.3) contra a barra muito rígida (1e−6), contagem de um pilar com as
  pontas liberadas além da sua crítica — arquivos: `beam_state.hpp`,
  `beam_exact.cpp`, `test_beam_second_order.cpp` — cobre: 02, 08, 10
- [x] **T10** [P] Rigidez geométrica da treliça (2D e 3D) contra a forma
  fechada; corda tensa — arquivos: `truss_element.hpp/.cpp`,
  `test_beam_second_order.cpp` — cobre: 03
- [x] **T11** [P] Rigidez geométrica da isoparamétrica (design §5.3):
  simetria, modos rígidos sem energia geométrica em translação,
  convergência contra a barra exata com N (reta e arco), taxa registrada —
  arquivos: `beam_iso.hpp/.cpp`, `test_beam_second_order.cpp` — cobre: 04
- [x] **T12** Arcos com perfil de N: o motor no arco com N variável contra
  uma cadeia refinada de barras retas exatas (extrapolação de Richardson);
  tolerância documentada nas notas — arquivos: `beam_state.cpp`,
  `test_beam_second_order.cpp` — cobre: 04

## Fase 4 — Segunda ordem

- [x] **T13** Partir `runLinearStatic` em etapas reaproveitáveis
  (`detail/static_stages.hpp`: preparar, montar com perfis, resolver,
  recuperar), sem mudar resultados; nenhum teste novo, todas as suítes
  passam — arquivos: `static_analysis.cpp`, `detail/static_stages.hpp` —
  cobre: 14
- [x] **T14** Perfis a partir de uma solução (design §5.2): barra reta
  (esforço de ponta mais carga axial), arco (`stateAt` nos pontos de
  Gauss), isoparamétrica (pontos reduzidos), treliça; testes contra os
  esforços da análise linear — arquivos: `static_stages`,
  `test_second_order_analysis.cpp` — cobre: 05
- [x] **T15** Testes da segunda ordem (devem falhar): pórtico plano de um
  andar contra funções de estabilidade, ordem de grandeza de
  1/(1 − P/Pcr), iterações e variação no resultado, P/Pcr = 0,95
  convergindo em menos de 50 iterações, treliça com corda tensa e P-Δ
  global (AC-2, AC-3) — arquivo: `test_second_order_analysis.cpp` — cobre:
  01, 03, 05, 06
- [x] **T16** `stability_analysis.hpp` e `runSecondOrderStatic` (design §5.5):
  iteração com Anderson, gradiente conjugado pré-condicionado pela
  fatoração linear (nova fatoração se estagnar), fatoração final com a
  contagem de Sturm, meio passo, mensagens de instabilidade (nó e direção,
  ou elemento) e de não convergência, até a T15 e os testes de falha (AC-7)
  passarem — arquivos:
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
  estimativas por subespaço com a fatoração linear, fatoração em σ₁,
  iteração inversa residual com produtos K(λ)x elemento a elemento,
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
  flambagem com 15 fatorações 15,8× (meta 6×). O mantenedor aprovou
  reaproveitar fatorações (design §5.5 a 5.7 e ADR 0007 alterados em
  2026-11-17), com as mesmas metas.

**Fase 2 — A barra exata com esforço normal (T04 a T08):**

- T04/T05: as funções de estabilidade (s, c), o balanço de Euler-Bernoulli
  e de Engesser, a barra cortada e condensada e o momento de engastamento
  da carga uniforme batem em 1e−10 já com o termo de N em A. As junções das
  partes com N usam a decomposição de autovalores (inércia e solução de
  blocos indefinidos). Compressão acima de 0,8 G As (nenhuma parte cabe)
  devolve `kUnstableBar`.
- Ambiente: o MSVC local em português perdia as dependências de cabeçalho
  no Ninja conforme a página de código do console (objetos com "#deps 0"
  não eram recompilados, e o construtor implícito antigo de `BeamData`
  ficava no executável). Resolvido com `VSLANG=1033` antes de configurar
  (`--fresh`) e uma recompilação completa; o CI não é afetado.
- T06: no balanço o teste fica abaixo da crítica do balanço (π²EI/4L²);
  acima dela o deslocamento muda de sinal, como deve.
- T07: o Magnus de 4ª ordem com um número fixo de pedaços (4 por parte do
  perfil) errava 2,4e−4 no pilar de Greenhill. A subdivisão passou a
  depender da variação de N: cada pedaço muda (kh)² = |ΔN|h²/EI no máximo
  5e−5, o que dá m = ∛(|ΔN|ℓ²/(EI·5e−5)) pedaços por parte do perfil (até
  256). Com isso, Greenhill (qL³/EI = 7,8373474…, pela série de θ'' = −βxθ
  calculada no próprio teste) bate em 1e−8, e o mesmo N linear em 1 ou 3
  partes dá a mesma barra em 1e−8. No pilar do benchmark (peso próprio de
  um andar) são cerca de 4 pedaços. As junções com N são resolvidas por LU
  (a inversa pela decomposição de autovalores perdia dígitos), e a
  contagem usa só os autovalores.

**Fase 3 — Elementos (T09 a T12):**

- T09: o termo do braço rígido foi conferido contra a solução analítica
  do balanço com braço no topo (fórmulas de viga-coluna com cortante e
  momento na ponta da parte flexível, o braço girando com ela), em 1e−10,
  em vez da barra muito rígida do design (1e−6): a referência analítica é
  mais forte. As ligações com N contam pelos autovalores e resolvem por LU;
  sem N, o caminho LDLᵀ da 008b não mudou.
- T10: o esforço de referência da treliça ficou em `BarData::axialForce`,
  e não num `span` à parte como no design §4, do mesmo jeito que o perfil
  fica em `BeamData`; a rigidez geométrica N/L (I − e eᵀ) entra direto nos
  blocos de translação.
- T11: a isoparamétrica com N lê o perfil de `BeamData` no comprimento de
  arco de cada ponto. Balanço 3D com N constante, contra Engesser fechado,
  com 2, 4, 8 e 16 elementos: compressão 6,3e−3, 4,1e−4, 2,6e−5, 1,6e−6;
  tração 2,1e−4, 1,2e−5, 7,7e−7, 4,8e−8 — taxa 4 (3,96 a 4,05).
- T12: o quarto de anel com N variável (perfil em 8 partes) contra cadeias
  de 64 e 128 barras retas exatas nas cordas, extrapoladas por Richardson
  (as cordas erram com h²): a flexibilidade da ponta bate em 1e−6, a
  tolerância documentada dos arcos com N variável.

**Fase 4 — Segunda ordem (T13 a T17):**

- T13: `detail::StaticProblem` (preparar uma vez; montar, resolver e
  recuperar); 298 testes inalterados.
- T14: os perfis saem de `StaticProblem::profilesOf`; como o cabeçalho é
  interno, a conferência contra os esforços é feita pelos testes da T15
  (os N da treliça e do pilar são exatamente os da estática). Na barra
  reta com carga axial no vão, N(s) = N(0) − ∫q_x − ΣP_x, em 4 partes, sem
  `stateAt`; a base elástica axial (k_x) não entra no perfil.
- T15/T16: balanço (Euler-Bernoulli e Engesser) em 1e−8 com um elemento,
  momento na base h l + p δ, amplificação ≈ 1/(1 − P/Pcr), P/Pcr = 0,95
  convergindo, pilar em 1 ou 4 elementos igual em 1e−8, dois pilares com
  tirante de treliça (duas molas em série) em 1e−8, pórtico em 1 ou 4
  elementos por barra igual em 1e−8, corda tensa δ = F/(k + 2T/L) em 1e−8,
  mastro de treliça δ = H/(k − P/L) em 1e−6 (a segunda ordem da própria
  mola, de ordem 1e−7). A referência do pórtico do AC-2 é a invariância
  com a subdivisão: as barras são exatas na T04 (funções de estabilidade
  em 1e−10). Falhas: acima da crítica (nó e direção), limite de iterações,
  pilar biarticulado além da sua crítica ("inside the element 1").
