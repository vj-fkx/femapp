# Tarefas — 008b beam-warping-curved

Status: em andamento
Design: [design.md](design.md) · Requisitos: [requirements.md](requirements.md)

Regras:
- Uma tarefa = uma mudança pequena, compilável e testável.
- Testes vêm antes ou junto da implementação que verificam.
- `[P]` marca tarefas que podem ser feitas em paralelo com a anterior.
- `[M]` marca tarefas que dependem de ação do mantenedor.
- Marque `[x]` somente com build e testes passando.
- Os IDs abaixo omitem o prefixo: `01` significa `REQ-008b-01`; `N01`
  significa `NFR-008b-01`.
- Em toda tarefa, as suítes da 007 e da 008 continuam passando sem mudança
  de resultado (23).

## Fase 1 — Fundações

- [x] **T01** `DofType::Warp` (8 tipos, `DofMask` de 8 bits) e o nome
  "warp"; testes de `DofMap` com o novo tipo (máscara cheia, numeração por
  nó, prescrição) — arquivos: `dof_map.hpp`, `dof_map.cpp`,
  `tests/unit/core/test_dof_map.cpp` — cobre: 07, 10
- [x] **T02** `model.hpp`: `CurvedFormulation`, `WarpingCondition`,
  `Assignment::warping` e `::curved`, `WarpingEnd` e a lista no `Model`,
  `NodalForce::bimoment`, `DistributedLoad::startTorque/endTorque`,
  `ConcentratedLoad::bimoment`; ampliar `test_model` (incluir, alterar,
  copiar) — arquivos: `model.hpp`, `tests/unit/core/test_model.cpp` — cobre:
  07, 09, 10, 12, 17
- [x] **T03** Testes de `matrixExponential` (ADR 0006): matrizes nilpotentes
  e diagonais contra a fórmula; rotação (e^{Ω×}) contra Rodrigues; norma
  grande com escala; sem alocação em tamanho fixo. Implementar em
  `beam_state.cpp` até passar — arquivos: `beam_state.hpp/.cpp`,
  `tests/unit/core/test_beam_state.cpp`, `test_allocations.cpp`,
  `src/core/CMakeLists.txt` — cobre: 08, 13

## Fase 2 — Seções

- [ ] **T04** Testes de Iw e ω das oito formas (§5.1.1 do design) contra as
  fórmulas (1e−12) e valores tabelados de I e U; `Section::warpingConstant`
  na seção genérica (≥ 0); `StressPoint::sectorial` nos pontos — arquivo:
  `tests/unit/core/test_section.cpp` — cobre: 01
- [ ] **T05** Implementar a T04 em `section.cpp`, com as referências das
  fórmulas — cobre: 01
- [ ] **T06** Testes da seção de paredes finas (§5.1.2): I, U, Z e cartola
  abertos; caixão de 1 e de 2 células contra Bredt generalizado; centro de
  cisalhamento do U; Iw e ω contra as fórmulas setoriais; As pelo fluxo;
  equivalência com as formas paramétricas I e U nas linhas médias (1e−10);
  recusas (parede nula, t ≤ 0, desconexa, ponto repetido) — arquivo:
  `tests/unit/core/test_section_thin_walled.cpp` — cobre: 03, 04
- [ ] **T07** Implementar `ThinWalled` em `section_thin_walled.cpp` até a
  T06 passar — cobre: 03, 04
- [ ] **T08** Testes da triangulação da seção (ADR 0005; constituição
  §7.4): conformidade (sem buracos nem sobreposições, área igual à do
  polígono), Delaunay restrito, ângulo mínimo preservado pelo refinamento,
  furos, contornos não convexos, casos degenerados (vértices quase
  colineares, furo tocando o contorno) e recusas do REQ-008b-04 — arquivo:
  `tests/unit/core/test_section_mesh.cpp` — cobre: 02, 04
- [ ] **T09** Implementar `detail/section_mesh.hpp/.cpp` até a T08 passar:
  orelhas com pontes, Lawson, Rivara, nós T6 — cobre: 02, 04
- [ ] **T10** Testes de `SolidPolygon` (§5.1.3): geometria exata (retângulo,
  polígono com furo, regiões disjuntas; 1e−10); J do retângulo contra a
  série e do tubo contra o exato; centro de cisalhamento e Iw de I e U
  poligonais contra paredes finas (tolerância documentada); As 5/6 e 6/7;
  erro ≤ 0,5% com o tamanho padrão; taxa de J (4 ± 0,5) — arquivo:
  `tests/unit/core/test_section_solid.cpp` — cobre: 02, N02
- [ ] **T11** Implementar `section_solid.cpp` (empenamento, Trefftz, Iw,
  funções de cisalhamento de Pilkey) e `SectionSolution`/`solveSection`;
  `sectionProperties` passa a chamar `solveSection` — cobre: 02, 05
- [ ] **T12** Testes e implementação das tensões das seções novas: na
  poligonal, `stressAt` e `largestVonMises` (retângulo sob torção: máximo no
  meio do lado maior contra a série); nas paredes finas, as duas faces nos
  pontos e nós da linha média contra as fórmulas — arquivos:
  `section_solid.cpp`, `section_thin_walled.cpp`, testes — cobre: 06
- [ ] **T13** [P] Benchmark `bench_section`: polígono de 200 vértices com o
  tamanho padrão, tempo registrado nas notas — arquivos:
  `tests/perf/bench_section.cpp`, `tests/CMakeLists.txt` — cobre: N02

## Fase 3 — Motor exato

- [ ] **T14** Testes do motor (§5.3) com Ω = 0 e EIw = 0: rigidez igual à
  `flexibleStiffness` da 008 nas duas teorias, com centro de cisalhamento e
  base elástica desligada (1e−12); simetria; modos rígidos — arquivo:
  `test_beam_state.cpp` — cobre: 08, 23
- [ ] **T15** Implementar `StateBar`, a matriz A, a transferência, a rigidez
  e as cargas de engaste de trechos polinomiais até a T14 passar — cobre:
  08
- [ ] **T16** Testes e implementação da torção com empenamento no motor:
  rigidez 4×4 de torção contra a fórmula fechada em senh/cosh (kL = 0,1, 1,
  10, 50); limites GJ → 0 e EIw → 0; subdivisão (kL = 50) igual à solução
  fechada; base elástica de torção contra Hetényi — cobre: 07, 08, 11
- [ ] **T17** Testes e implementação da curvatura Ω: modos rígidos do arco
  (6 com energia nula, em qualquer orientação de Ω em relação à seção);
  R → ∞ igual à reta; cargas globais pelos estados aumentados (g' = −Ω×g)
  contra a integração numérica de alta ordem (1e−10) — cobre: 13, 15
- [ ] **T18** `stateAt`: estado em qualquer s, com saltos de concentradas e
  bimomentos, a partir das junções guardadas; contra a solução do motor
  recalculada num trecho mais curto (1e−12) — cobre: 19

## Fase 4 — Viga com empenamento

- [ ] **T19** `BeamData` ampliado (EIw, condições, curvatura, comprimento de
  arco) e `ExactBeamElement` para `Line2` com empenamento: cadeia de
  centro de cisalhamento, ligações (até 28 GDL), excentricidades e rotação;
  pontas livres condensadas e impedidas eliminadas; `dofsAt` pelas pontas
  contínuas; `test_allocations` ampliado — arquivos: `beam_element.hpp/.cpp`,
  testes — cobre: 07, 09, 11
- [ ] **T20** Resolução das condições por ponta e do GDL `Warp` por nó
  (§5.4): padrão por colinearidade, `WarpingEnd`, nós com e sem o GDL;
  testes unitários da resolução — arquivos: `static_analysis.cpp` ou
  `beam_element.cpp`, testes — cobre: 09
- [ ] **T21** `runLinearStatic` com empenamento: conjuntos por (formulação,
  pontas contínuas); prescrição de `Warp`; bimomento nodal; `warping` e
  `reactionBimoments` no resultado; B, Tsv e Tw em `BeamForces`; `BeamResult`
  pelo motor nas vigas exatas — arquivos: `static_analysis.hpp/.cpp`,
  `beam_result.hpp/.cpp` — cobre: 10, 18, 19
- [ ] **T22** [M] Benchmark `bench_frame` com empenamento em todas as vigas
  (argumento de linha de comando), registrado nas notas. Se passar de 6 s,
  parar e propor a mudança da meta antes de seguir — cobre: N03
- [ ] **T23** Verificação da viga com empenamento (AC-3): balanço de I com
  torque na ponta e engaste impedido; biapoiada com torque distribuído e
  concentrado, pontas livres e impedidas; bimomento nodal; B, Tsv e Tw ao
  longo da barra contra senh/cosh (1e−8) — arquivo:
  `tests/unit/core/test_beam_warping.cpp` — cobre: 07, 08, 10, 18, 19, N01
- [ ] **T24** [P] Condições de empenamento (AC-4): viga de dois vãos
  contínua igual a uma barra; T de três barras; chapa de topo; liberação de
  torção e excentricidade com empenamento; base elástica de torção contra a
  solução analítica — cobre: 09, 11
- [ ] **T25** Tensões de empenamento: σw = B·ω/Iw e τw nos pontos com ω;
  `PointStress::warpingNormal` — arquivos: `beam_result.cpp`, testes —
  cobre: 20

## Fase 5 — Viga curva em arco

- [ ] **T26** Geometria do arco e eixos ao longo dele (§5.2): círculo pelos
  três nós, Ω em eixos principais, transporte do referencial, colineares
  como reta, recusas (abertura, nó do meio na ponta, referência paralela à
  tangente); testes unitários — arquivos: `beam_element.cpp`, testes —
  cobre: 12, 13, 15, 16
- [ ] **T27** `ExactBeamElement` para `Line3` arco (nó do meio sem GDL,
  rotação diferente em cada ponta) e cargas por comprimento de arco no
  `beam_loads` (posições, torque distribuído, bimomento concentrado);
  conjuntos na análise — cobre: 12, 13, 15, 17
- [ ] **T28** Verificação do arco (AC-5): quarto de anel em balanço no plano
  e fora do plano; anel com carga diametral; arco semicircular
  biarticulado com carga uniforme e com temperatura; viga curva em planta
  biengastada; at(s) ao longo do arco; 2D e 3D — arquivo:
  `tests/unit/core/test_beam_curved.cpp` — cobre: 12, 13, 19, N01
- [ ] **T29** [P] Recursos da 008 no arco (AC-8): liberações, molas,
  excentricidades, base elástica, cargas parciais e concentradas em eixos
  globais e locais, peso próprio, gradiente — cobre: 15, 17

## Fase 6 — Viga curva isoparamétrica

- [ ] **T30** Testes e implementação de `IsoBeamElement` (§5.5): campos
  quadráticos, deformações generalizadas, integração seletiva, penalidades
  de Euler-Bernoulli e de Vlasov, cargas consistentes, jacobiano recusado;
  patch test reto (1e−10); sem alocação — arquivos: `beam_element.hpp/.cpp`,
  testes — cobre: 14, 16
- [ ] **T31** `BeamResult` da isoparamétrica e a análise com `Line3`
  isoparamétrico (nó do meio com GDL, apoio e carga) — cobre: 14, 19
- [ ] **T32** Convergência (AC-6): os casos do arco com n = 2, 4, 8, 16
  elementos (taxa 4 ± 0,5, registrada nas notas); L/h = 1000 sem travamento;
  arco parabólico contra a referência documentada — cobre: 14
- [ ] **T33** Viga curva com empenamento (AC-7): R grande igual à reta;
  arco e isoparamétrica concordando com o refinamento; caso de Yang e Kuo
  (1987), 1% — cobre: 07, 13, 14

## Fase 7 — Validação

- [ ] **T34** Testes de validação: cada problema do §5.8 isolado e vários
  juntos; mecanismo com "warp" e a sugestão de impedir ou restringir —
  arquivo: `tests/unit/core/test_beam_validation.cpp` — cobre: 16, 21, 22
- [ ] **T35** Implementar as verificações do §5.8 até a T34 passar —
  arquivos: `model_validation.cpp`, `detail/model_validation.hpp`,
  `static_analysis.cpp` — cobre: 04, 16, 21, 22

## Fase 8 — Documentação

- [ ] **T36** `specs/architecture.md` §3: seções novas, motor exato,
  formulações de viga, empenamento; notas de implementação em `tasks.md`
  — cobre: 24

## Fase 9 — Fechamento

- [ ] **T90** [M] Build e testes verdes nos jobs do CI, incluindo o de
  cobertura com `core` ≥ 80% (push com sua confirmação) — cobre: N04
- [ ] **T91** `clang-format` e `clang-tidy` sem pendências (também o do
  Linux, pelo CI); `core` sem headers Qt — cobre: 24
- [ ] **T92** Conferir os onze critérios de aceite de `requirements.md`
- [ ] **T93** Atualizar `specs/roadmap.md` para `concluída`

## Matriz de rastreabilidade

| Requisito | Tarefas | Testes |
|-----------|---------|--------|
| REQ-008b-01 | T04, T05 | `test_section.cpp` |
| REQ-008b-02 | T08 a T11 | `test_section_mesh.cpp`, `test_section_solid.cpp` |
| REQ-008b-03 | T06, T07 | `test_section_thin_walled.cpp` |
| REQ-008b-04 | T06 a T09, T35 | `test_section_thin_walled.cpp`, `test_section_mesh.cpp`, `test_beam_validation.cpp` |
| REQ-008b-05 | T11 | `test_section_solid.cpp` (seção poligonal em treliça e viga) |
| REQ-008b-06 | T12 | `test_section_solid.cpp`, `test_section_thin_walled.cpp` |
| REQ-008b-07 | T01, T02, T16, T19, T23, T33 | `test_beam_state.cpp`, `test_beam_warping.cpp`, `test_beam_curved.cpp` |
| REQ-008b-08 | T03, T14 a T16, T23 | `test_beam_state.cpp`, `test_beam_warping.cpp` |
| REQ-008b-09 | T02, T19, T20, T24 | `test_beam_warping.cpp` |
| REQ-008b-10 | T01, T02, T21, T23 | `test_dof_map.cpp`, `test_beam_warping.cpp` |
| REQ-008b-11 | T16, T19, T24 | `test_beam_state.cpp`, `test_beam_warping.cpp` |
| REQ-008b-12 | T02, T26 a T28 | `test_beam_curved.cpp` |
| REQ-008b-13 | T03, T17, T26 a T28, T33 | `test_beam_state.cpp`, `test_beam_curved.cpp` |
| REQ-008b-14 | T30 a T33 | `test_beam_curved.cpp` |
| REQ-008b-15 | T17, T26, T27, T29 | `test_beam_state.cpp`, `test_beam_curved.cpp` |
| REQ-008b-16 | T26, T30, T34, T35 | `test_beam_curved.cpp`, `test_beam_validation.cpp` |
| REQ-008b-17 | T02, T27, T29 | `test_beam_curved.cpp`, `test_beam_warping.cpp` |
| REQ-008b-18 | T21, T23 | `test_beam_warping.cpp` |
| REQ-008b-19 | T18, T21, T23, T28, T31 | `test_beam_state.cpp`, `test_beam_warping.cpp`, `test_beam_curved.cpp` |
| REQ-008b-20 | T25 | `test_beam_warping.cpp` |
| REQ-008b-21 | T34, T35 | `test_beam_validation.cpp` |
| REQ-008b-22 | T34, T35 | `test_beam_validation.cpp` |
| REQ-008b-23 | todas, T14 | suítes da 007 e da 008 inalteradas |
| REQ-008b-24 | T36, T91 | `check_no_qt_includes` |
| NFR-008b-01 | T23, T28 | `test_beam_warping.cpp`, `test_beam_curved.cpp` |
| NFR-008b-02 | T10, T13 | `test_section_solid.cpp`, `bench_section` |
| NFR-008b-03 | T22 | `bench_frame` |
| NFR-008b-04 | T90 | Job de cobertura no CI |

## Notas de implementação

(vazio)
