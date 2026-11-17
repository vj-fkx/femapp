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

- [x] **T04** Testes de Iw e ω das oito formas (§5.1.1 do design) contra as
  fórmulas (1e−12) e valores tabelados de I e U; `Section::warpingConstant`
  na seção genérica (≥ 0); `StressPoint::sectorial` nos pontos — arquivo:
  `tests/unit/core/test_section.cpp` — cobre: 01
- [x] **T05** Implementar a T04 em `section.cpp`, com as referências das
  fórmulas — cobre: 01
- [x] **T06** Testes da seção de paredes finas (§5.1.2): I, U, Z e cartola
  abertos; caixão de 1 e de 2 células contra Bredt generalizado; centro de
  cisalhamento do U; Iw e ω contra as fórmulas setoriais; As pelo fluxo;
  equivalência com as formas paramétricas I e U nas linhas médias (1e−10);
  recusas (parede nula, t ≤ 0, desconexa, ponto repetido) — arquivo:
  `tests/unit/core/test_section_thin_walled.cpp` — cobre: 03, 04
- [x] **T07** Implementar `ThinWalled` em `section_thin_walled.cpp` até a
  T06 passar — cobre: 03, 04
- [x] **T08** Testes da triangulação da seção (ADR 0005; constituição
  §7.4): conformidade (sem buracos nem sobreposições, área igual à do
  polígono), Delaunay restrito, ângulo mínimo preservado pelo refinamento,
  furos, contornos não convexos, casos degenerados (vértices quase
  colineares, furo tocando o contorno) e recusas do REQ-008b-04 — arquivo:
  `tests/unit/core/test_section_mesh.cpp` — cobre: 02, 04
- [x] **T09** Implementar `detail/section_mesh.hpp/.cpp` até a T08 passar:
  orelhas com pontes, Lawson, Rivara, nós T6 — cobre: 02, 04
- [x] **T10** Testes de `SolidPolygon` (§5.1.3): geometria exata (retângulo,
  polígono com furo, regiões disjuntas; 1e−10); J do retângulo contra a
  série e do tubo contra o exato; centro de cisalhamento e Iw de I e U
  poligonais contra paredes finas (tolerância documentada); As 5/6 e 6/7;
  erro ≤ 0,5% com o tamanho padrão; taxa de J (4 ± 0,5) — arquivo:
  `tests/unit/core/test_section_solid.cpp` — cobre: 02, N02
- [x] **T11** Implementar `section_solid.cpp` (empenamento, Trefftz, Iw,
  funções de cisalhamento de Pilkey) e `SectionSolution`/`solveSection`;
  `sectionProperties` passa a chamar `solveSection` — cobre: 02, 05
- [x] **T12** Testes e implementação das tensões das seções novas: na
  poligonal, `stressAt` e `largestVonMises` (retângulo sob torção: máximo no
  meio do lado maior contra a série); nas paredes finas, as duas faces nos
  pontos e nós da linha média contra as fórmulas — arquivos:
  `section_solid.cpp`, `section_thin_walled.cpp`, testes — cobre: 06
- [x] **T13** [P] Benchmark `bench_section`: polígono de 200 vértices com o
  tamanho padrão, tempo registrado nas notas — arquivos:
  `tests/perf/bench_section.cpp`, `tests/CMakeLists.txt` — cobre: N02

## Fase 3 — Motor exato

- [x] **T14** Testes do motor (§5.3) com Ω = 0 e EIw = 0: rigidez igual à
  `flexibleStiffness` da 008 nas duas teorias, com centro de cisalhamento e
  base elástica desligada (1e−12); simetria; modos rígidos — arquivo:
  `test_beam_state.cpp` — cobre: 08, 23
- [x] **T15** Implementar `StateBar`, a matriz A, a transferência, a rigidez
  e as cargas de engaste de trechos polinomiais até a T14 passar — cobre:
  08
- [x] **T16** Testes e implementação da torção com empenamento no motor:
  rigidez 4×4 de torção contra a fórmula fechada em senh/cosh (kL = 0,1, 1,
  10, 50); limites GJ → 0 e EIw → 0; subdivisão (kL = 50) igual à solução
  fechada; base elástica de torção contra Hetényi — cobre: 07, 08, 11
- [x] **T17** Testes e implementação da curvatura Ω: modos rígidos do arco
  (6 com energia nula, em qualquer orientação de Ω em relação à seção);
  R → ∞ igual à reta; cargas globais pelos estados aumentados (g' = −Ω×g)
  contra a integração numérica de alta ordem (1e−10) — cobre: 13, 15
- [x] **T18** `stateAt`: estado em qualquer s, com saltos de concentradas e
  bimomentos, a partir das junções guardadas; contra a solução do motor
  recalculada num trecho mais curto (1e−12) — cobre: 19

## Fase 4 — Viga com empenamento

- [x] **T19** `BeamData` ampliado (EIw, condições, curvatura, comprimento de
  arco) e `ExactBeamElement` para `Line2` com empenamento: cadeia de
  centro de cisalhamento, ligações (até 28 GDL), excentricidades e rotação;
  pontas livres condensadas e impedidas eliminadas; `dofsAt` pelas pontas
  contínuas; `test_allocations` ampliado — arquivos: `beam_element.hpp/.cpp`,
  testes — cobre: 07, 09, 11
- [x] **T20** Resolução das condições por ponta e do GDL `Warp` por nó
  (§5.4): padrão por colinearidade, `WarpingEnd`, nós com e sem o GDL;
  testes unitários da resolução — arquivos: `static_analysis.cpp` ou
  `beam_element.cpp`, testes — cobre: 09
- [x] **T21** `runLinearStatic` com empenamento: conjuntos por (formulação,
  pontas contínuas); prescrição de `Warp`; bimomento nodal; `warping` e
  `reactionBimoments` no resultado; B, Tsv e Tw em `BeamForces`; `BeamResult`
  pelo motor nas vigas exatas — arquivos: `static_analysis.hpp/.cpp`,
  `beam_result.hpp/.cpp` — cobre: 10, 18, 19
- [x] **T22** ~~[M] Benchmark `bench_frame` com empenamento em todas as vigas   (argumento de linha de comando), registrado nas notas. Se passar de 6 s,   parar e propor a mudança da meta antes de seguir — cobre: N03~~ *Removida em 2026-11-16 com o empenamento (requirements §6).*
- [x] **T23** ~~Verificação da viga com empenamento (AC-3): balanço de I com   torque na ponta e engaste impedido; biapoiada com torque distribuído e   concentrado, pontas livres e impedidas; bimomento nodal; B, Tsv e Tw ao   longo da barra contra senh/cosh (1e−8) — arquivo:   `tests/unit/core/test_beam_warping.cpp` — cobre: 07, 08, 10, 18, 19, N01~~ *Removida em 2026-11-16 com o empenamento (requirements §6).*
- [x] **T24** ~~[P] Condições de empenamento (AC-4): viga de dois vãos   contínua igual a uma barra; T de três barras; chapa de topo; liberação de   torção e excentricidade com empenamento; base elástica de torção contra a   solução analítica — cobre: 09, 11~~ *Removida em 2026-11-16 com o empenamento (requirements §6).*
- [x] **T25** ~~Tensões de empenamento: σw = B·ω/Iw e τw nos pontos com ω;   `PointStress::warpingNormal` — arquivos: `beam_result.cpp`, testes —   cobre: 20~~ *Removida em 2026-11-16 com o empenamento (requirements §6).*
- [x] **T21b** Retirar o empenamento do que o usuário vê, mantendo o núcleo
  (decisão de 2026-11-16): sai do `Model` (`Assignment::warping`,
  `WarpingEnd`, bimomentos nodal e concentrado), do `DofType` (`Warp`), da
  análise (condições por ponta, conjuntos por pontas contínuas) e dos
  resultados (`warping`, `reactionBimoments`, B, Tsv e Tw); ficam Iw e ω das
  seções, o torque distribuído e, no motor exato e na `ExactBeamElement`,
  o empenamento interno com pontas livres ou impedidas, com os seus
  testes — cobre: 01, 17, 23

## Fase 5 — Viga curva em arco

- [x] **T26** Geometria do arco e eixos ao longo dele (§5.2): círculo pelos
  três nós, Ω em eixos principais, transporte do referencial, colineares
  como reta, recusas (abertura, nó do meio na ponta, referência paralela à
  tangente); testes unitários — arquivos: `beam_element.cpp`, testes —
  cobre: 12, 13, 15, 16
- [x] **T27** `ExactBeamElement` para `Line3` arco (nó do meio sem GDL,
  rotação diferente em cada ponta) e cargas por comprimento de arco no
  `beam_loads` (posições, torque distribuído, bimomento concentrado);
  conjuntos na análise — cobre: 12, 13, 15, 17
- [x] **T28** Verificação do arco (AC-5): quarto de anel em balanço no plano
  e fora do plano; anel com carga diametral; arco semicircular
  biarticulado com carga uniforme e com temperatura; viga curva em planta
  biengastada; at(s) ao longo do arco; 2D e 3D — arquivo:
  `tests/unit/core/test_beam_curved.cpp` — cobre: 12, 13, 19, N01
- [x] **T29** [P] Recursos da 008 no arco (AC-8): liberações, molas,
  excentricidades, base elástica, cargas parciais e concentradas em eixos
  globais e locais, peso próprio, gradiente — cobre: 15, 17

## Fase 6 — Viga curva isoparamétrica

- [x] **T30** Testes e implementação de `IsoBeamElement` (§5.5): campos
  quadráticos, deformações generalizadas, integração seletiva, penalidades
  de Euler-Bernoulli e de Vlasov, cargas consistentes, jacobiano recusado;
  patch test reto (1e−10); sem alocação — arquivos: `beam_element.hpp/.cpp`,
  testes — cobre: 14, 16
- [x] **T31** `BeamResult` da isoparamétrica e a análise com `Line3`
  isoparamétrico (nó do meio com GDL, apoio e carga) — cobre: 14, 19
- [x] **T32** Convergência (AC-6): os casos do arco com n = 2, 4, 8, 16
  elementos (taxa 4 ± 0,5, registrada nas notas); L/h = 1000 sem travamento;
  arco parabólico contra a referência documentada — cobre: 14
- [x] **T33** ~~Viga curva com empenamento (AC-7): R grande igual à reta;   arco e isoparamétrica concordando com o refinamento; caso de Yang e Kuo   (1987), 1% — cobre: 07, 13, 14~~ *Removida em 2026-11-16 com o empenamento (requirements §6).*
## Fase 7 — Validação

- [x] **T34** Testes de validação: cada problema do §5.8 isolado e vários
  juntos; mecanismo com "warp" e a sugestão de impedir ou restringir —
  arquivo: `tests/unit/core/test_beam_validation.cpp` — cobre: 16, 21, 22
- [x] **T35** Implementar as verificações do §5.8 até a T34 passar —
  arquivos: `model_validation.cpp`, `detail/model_validation.hpp`,
  `static_analysis.cpp` — cobre: 04, 16, 21, 22

## Fase 8 — Documentação

- [x] **T36** `specs/architecture.md` §3: seções novas, motor exato,
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
| REQ-008b-07 | removido (empenamento adiado) | — |
| REQ-008b-08 | removido (empenamento adiado) | — |
| REQ-008b-09 | removido (empenamento adiado) | — |
| REQ-008b-10 | removido (empenamento adiado) | — |
| REQ-008b-11 | removido (empenamento adiado) | — |
| REQ-008b-12 | T02, T26 a T28 | `test_beam_curved.cpp` |
| REQ-008b-13 | T03, T17, T26 a T28, T33 | `test_beam_state.cpp`, `test_beam_curved.cpp` |
| REQ-008b-14 | T30 a T33 | `test_beam_curved.cpp` |
| REQ-008b-15 | T17, T26, T27, T29 | `test_beam_state.cpp`, `test_beam_curved.cpp` |
| REQ-008b-16 | T26, T30, T34, T35 | `test_beam_curved.cpp`, `test_beam_validation.cpp` |
| REQ-008b-17 | T02, T21, T27, T29 | `test_beam_curved.cpp` |
| REQ-008b-18 | removido (empenamento adiado) | — |
| REQ-008b-19 | T18, T21, T28, T31 | `test_beam_state.cpp`, `test_beam_curved.cpp` |
| REQ-008b-20 | removido (empenamento adiado) | — |
| REQ-008b-21 | T34, T35 | `test_beam_validation.cpp` |
| REQ-008b-22 | removido (empenamento adiado) | — |
| REQ-008b-23 | todas, T14, T21b | suítes da 007 e da 008 inalteradas |
| REQ-008b-24 | T36, T91 | `check_no_qt_includes` |
| NFR-008b-01 | T28 | `test_beam_curved.cpp` |
| NFR-008b-02 | T10, T13 | `test_section_solid.cpp`, `bench_section` |
| NFR-008b-03 | T21b | `bench_frame` sem empenamento |
| NFR-008b-04 | T90 | Job de cobertura no CI |

## Notas de implementação

**Fase 2 — Seções (T04 a T13):**

- T04: o Iw do perfil U é conferido contra a fórmula fechada de Galambos
  (1e−12), e não contra tabelas: os UPN laminados têm mesas inclinadas e
  não seguem a idealização de linha média. O IPE 300 bate com a tabela
  (125,9·10³ cm⁶) em 1%.
- T05/T12: a comparação entre a seção de paredes finas e o tubo
  retangular paramétrico mostrou o sinal de ω nos cantos do tubo trocado; o
  certo é ω = b₀h₀(h₀ − b₀)/(4(b₀ + h₀)) onde y·z > 0, corrigido.
- T07: numa seção de paredes finas toda numa reta (placa), a inércia
  transversal é nula; o centro de cisalhamento sai da solução de norma
  mínima (fica na reta) e a área de cisalhamento transversal é 0 (a viga
  recusa Iy = 0 de qualquer modo).
- T10/T11: com o tamanho padrão, J do retângulo fica a 1,5·10⁻⁷ da série,
  As dá 5/6 e 6/7 (Cowper, ν = 0) a 10⁻⁵, e J converge com h⁴. Tolerâncias
  contra as paredes finas: Iw do I 1% (observado 0,07%), centro de
  cisalhamento do U 1% (0,4%), Iw do U 2% (0,5%). Na placa fina, o Iw
  numérico fica a menos de 5% de b³h³/144; num retângulo compacto (0,2 ×
  0,5) a fórmula de placa dá o dobro do valor numérico, o que confirma que
  ela é só uma estimativa pequena para a forma paramétrica.
- T12: `stressAt`, `largestVonMises` e `sectionStresses(SectionSolution)`
  ficaram como funções livres em `beam_result.hpp`, e não como métodos da
  `SectionSolution` (design §4): `BeamForces` e `PointStress` moram em
  `beam_result.hpp`, que já inclui `section.hpp`. `PointStress` ganhou a
  posição (eixos principais), para o máximo de von Mises. Na poligonal, o
  cisalhamento é o módulo do vetor (exato), não a soma dos módulos.
- T13: `bench_section` (200 + 48 vértices, tamanho padrão) em 0,17 a
  0,19 s em Release (meta: 0,5 s).

**Fase 3 — Motor exato (T14 a T18):**

- T14/T15: sem curvatura, empenamento nem base elástica, o motor reproduz
  `flexibleStiffness` e `equivalentLoads` da 008 em 1e−12, nas duas teorias
  e com centro de cisalhamento fora do centroide. Os estados são escalados
  (comprimentos h, forças S/h², momentos S/h) antes da exponencial.
- T15/T17: os pontos de quebra das cargas dentro de cada parte são
  achados um a um, sem vetor de tamanho fixo (um primeiro vetor de 130
  posições perdia cargas com muitos trechos).
- T16: a rigidez de torção com empenamento bate com a forma fechada (base
  estável 1, x, e^−kx, e^−k(L−x)) em 1e−10 para kL = 0,1, 1, 10 e 50 (25
  partes); GJ → 0 dá a viga de flexão com EIw (1e−9); a base elástica de
  torção dá a viga de Hetényi em 1e−8.
- T17: `SpanLoads::fixedPieces` leva as forças fixas no espaço sobre arcos
  (estados aumentados c e d, 6 a mais: `kMaxStates` passou de 19 para 20;
  ADR 0006 e design falavam em 19 antes de separar a carga global em dois
  passes). A conferência "contra integração numérica" foi feita pelo
  equilíbrio global (força e momento resultantes, 1e−5 com a regra do ponto
  médio em 400 pontos por trecho) e pela concordância com 600 trechos
  locais lineares (1e−4), que convergem com h².
- T18: `stateAt` recupera os deslocamentos das junções internas resolvendo
  a cadeia de partes (alocação permitida: só nos resultados) e integra a
  partir do início da parte que contém s; conferido contra as pontas e
  contra a barra cortada em x, inclusive com carga concentrada numa junção.

**Fase 4 (parte feita) e retirada do empenamento (T19 a T21b):**

- T22: com empenamento em todas as barras, o pórtico levou 6,2 a 7,1 s
  (meta de 6 s); antes disso, os pilares-caixão (kL ≈ 150) levavam 116 s
  pelas subdivisões. Na mesma sessão, sem empenamento: 3,4 a 4,1 s. Ganhos
  que ficaram: parte com crescimento até e⁸ (`kGrowthPerPart`), uma só
  chamada do motor por viga nos resultados e resultados em paralelo. O
  mantenedor adiou então o empenamento (T21b; feature 008d).

**Fase 5 — Viga em arco (T26 a T29):**

- T26: o vetor de referência define só os eixos no início do arco; eles
  são transportados ao longo dele com a taxa de giro constante (design
  §5.2). Por isso a recusa por referência paralela vale na tangente
  inicial, onde a regra da 008 já a aplica (no meio do arco o referencial
  transportado nunca degenera).
- T28: as soluções de referência saem de uma implementação independente
  no teste (`arc_reference.hpp`): esforços estáticos exatos, trabalho
  virtual por Gauss-Legendre de 64 pontos e método das forças para os
  redundantes, com flexão nos dois planos, torção, normal e cortante — as
  mesmas hipóteses das tabelas de Roark com as correções α e β. Todos os
  casos batem em 1e−8.
- T29: a base elástica é conferida na viga de Hetényi com o elemento exato
  (1e−8); no arco, o acoplamento com a curvatura já é coberto pelos testes
  do motor (T17, T18).

**Fase 6 — Viga isoparamétrica (T30 a T32):**

- T30: no patch test, uma barra ao longo de X tem o y local no Z global
  (regra da 008): o momento em torno de Z trabalha com EIy — o teste usa
  essa correspondência.
- T31: as cargas da isoparamétrica ficam como o modelo as dá
  (`IsoLoads`), com posições pelo comprimento de arco da curva quadrática;
  os esforços ao longo da barra extrapolam N, Vy e Vz dos dois pontos
  reduzidos e tiram os momentos das curvaturas no ponto.
- T32: taxas medidas no quarto de anel (erro relativo da ponta, n = 1 a
  32): 3,28, 3,74, 3,93, 3,98, 4,00 — a taxa 4 do design. Com L/h = 1000 o
  erro com 8 elementos fica abaixo de 10⁻³ nas duas teorias (sem
  travamento). O arco parabólico é conferido como triarticulado: a curva
  quadrática representa a parábola exatamente, e o empuxo bate com a
  estática em 1e−9.

