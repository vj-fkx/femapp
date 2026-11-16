# Tarefas — 008 beam-elements

Status: em andamento
Design: [design.md](design.md) · Requisitos: [requirements.md](requirements.md)

Regras:
- Uma tarefa = uma mudança pequena, compilável e testável.
- Testes vêm antes ou junto da implementação que verificam.
- `[P]` marca tarefas que podem ser feitas em paralelo com a anterior.
- `[M]` marca tarefas que dependem de ação do mantenedor.
- Marque `[x]` somente com build e testes passando.
- Os IDs abaixo omitem o prefixo: `01` significa `REQ-008-01`; `N01` significa
  `NFR-008-01`.

## Fase 1 — Seções

- [x] **T01** `section.hpp`: `Section` movida de `material.hpp` (que passa a
  incluí-la) com os campos da seção genérica e `shape`; as oito formas e
  `SectionProperties`; novos fontes no `CMakeLists.txt`. Os testes da 007
  continuam passando sem mudança — arquivos:
  `src/core/include/fem/core/section.hpp`, `material.hpp`,
  `src/core/section.cpp`, `src/core/CMakeLists.txt` — cobre: 02, 26
- [x] **T02** Testes de `sectionProperties`:
  - A, centroide, Iy, Iz, Iyz, J, centro de cisalhamento e áreas de
    cisalhamento das oito formas contra as fórmulas (1e−12);
  - retângulo, círculo e tubos contra os valores exatos (1e−6), e perfis
    abertos contra tabelas (A e I ≤ 5%, J ≤ 25%);
  - eixos principais da cantoneira, e centros de cisalhamento do U e do T;
  - seção genérica com Iyz ≠ 0 levada aos eixos principais;
  - dimensões inválidas, com a mensagem citando a dimensão.

  — arquivo: `tests/unit/core/test_section.cpp` — cobre: 02 a 04
- [x] **T03** Implementar `sectionProperties` (§5.1 do design) até a T02
  passar, com as referências das fórmulas no código — cobre: 02 a 05

## Fase 2 — Modelo

- [x] **T04** `model.hpp`:
  - `ElementKind` com `Beam2D` e `Beam3D`; `BeamTheory`, `Axes`, `BarEnd`
    e `BarPosition`;
  - campos novos de `Assignment` e o momento em `NodalForce`;
  - itens novos: `EndConnection`, `RigidOffset`, `ElasticFoundation`,
    `DistributedLoad`, `ConcentratedLoad` e `TemperatureGradient`, com as
    listas no `Model`;
  - `truss_meshes.hpp` renomeado para `bar_meshes.hpp`, e as inclusões
    ajustadas.

  — arquivos: `src/core/include/fem/core/model.hpp`,
  `tests/unit/core/bar_meshes.hpp`, testes da 007 — cobre: 09, 11 a 16
- [x] **T05** [P] Ampliar `test_model`: incluir, alterar, remover e copiar
  os itens novos; a cópia compartilha a malha — arquivo:
  `tests/unit/core/test_model.cpp` — cobre: 11 a 16

## Fase 3 — Elemento de viga

- [x] **T06** Testes de `beamAxes`:
  - padrão em barra horizontal (y vertical) e em barra vertical (X global);
  - vetor dado e ângulo β;
  - 2D;
  - vetor paralelo recusado.

  — arquivo: `tests/unit/core/test_beam_element.cpp` — cobre: 09, 10
- [x] **T07** `beam_element.hpp/.cpp`: `BeamData` e `beamAxes` até a T06
  passar; novos fontes no `CMakeLists.txt` — cobre: 09, 10
- [x] **T08** Testes do trecho flexível:
  - K de Euler-Bernoulli e de Timoshenko igual à fórmula fechada em
    orientação qualquer, em 2D e 3D;
  - invariância sob rotação;
  - 6 (3D) e 3 (2D) modos rígidos com energia nula;
  - Timoshenko com As = 1e12·A igual a Euler-Bernoulli;
  - `dofsAt` em 2D e 3D.

  — arquivo: `test_beam_element.cpp` — cobre: 05 a 07
- [x] **T09** `BeamElement` sem ligações, excentricidades nem base elástica
  (§5.3 e rotação do §5.4), até a T08 passar — cobre: 05 a 07
- [x] **T10** Testes e implementação do centro de cisalhamento e das
  excentricidades rígidas (T_sc e T_off do §5.4):
  - modos rígidos continuam com energia nula;
  - com excentricidade nula, o resultado é o mesmo da T09.

  — arquivos: `beam_element.cpp`, `test_beam_element.cpp` — cobre: 05, 14
- [x] **T11** Testes e implementação das ligações por condensação (§5.4):
  - mola 0 igual à liberação;
  - mola 1e14 igual à ligação rígida (1e−8);
  - barra rotulada nas duas pontas com rigidez transversal nula;
  - detecção de K_ii singular, exposta para a validação.

  — arquivos: `beam_element.cpp`, `test_beam_element.cpp` — cobre: 11 a 13
- [x] **T12** Testes e implementação da base elástica:
  - matriz consistente com 4 pontos de Gauss, simétrica e positiva com
    k > 0;
  - uma barra livre sobre base elástica deixa de ser singular.

  — arquivos: `beam_element.cpp`, `test_beam_element.cpp` — cobre: 15
- [x] **T13** [P] Ampliar `test_allocations`: `BeamElement::compute` com
  ligações, excentricidades e base elástica não aloca — arquivo:
  `tests/unit/core/test_allocations.cpp` — cobre: apoio a 05

## Fase 4 — Cargas no vão

- [ ] **T14** Testes de `beam_loads`:
  - polinômios por trechos (avaliar, integrar, saltos);
  - carga global em eixos locais;
  - posições em fração e em comprimento;
  - cargas equivalentes da uniforme, trapezoidal, parcial e concentrada
    contra os momentos de engastamento tabelados, nas duas teorias;
  - gradiente: EI·κ;
  - peso ρ·A·g;
  - torção da carga no centroide de seção com centro de cisalhamento
    excêntrico.

  — arquivo: `tests/unit/core/test_beam_loads.cpp` — cobre: 05, 16, 18, 19
- [ ] **T15** `beam_loads.hpp/.cpp` até a T14 passar; `fixedEndLoads` no
  `BeamData`, e as ligações e excentricidades atuando sobre ele — cobre: 05,
  16, 18, 19

## Fase 5 — Validação e análise

- [ ] **T16** Mover o `Validator` para `model_validation.cpp`, sem mudar
  comportamento; a suíte da 007 passa sem alteração — arquivos:
  `src/core/model_validation.cpp`, `src/core/detail/model_validation.hpp`,
  `static_analysis.cpp` — cobre: 26
- [ ] **T17** Testes de validação: cada problema do §5.8 isolado, e vários
  juntos — arquivo: `tests/unit/core/test_beam_validation.cpp` — cobre: 01,
  04, 06, 10, 13, 14, 17, 24
- [ ] **T18** Implementar as verificações do §5.8 até a T17 passar — cobre:
  01, 04, 06, 10, 13, 14, 17, 24
- [ ] **T19** `runLinearStatic` misto:
  - conjuntos de viga, `BeamData` por elemento e momentos nodais;
  - rotações e momentos de reação no `StaticResult`;
  - N, ε e σ também para vigas;
  - `BeamResult` com os esforços de extremidade;
  - nomes rx, ry e rz nas mensagens de mecanismo, com a dica de restringir a
    rotação.

  Testes de fumaça: balanço 2D e 3D, e treliça mais tirante.

  — arquivos: `static_analysis.hpp/.cpp`, `beam_result.hpp/.cpp`,
  `test_beam_verification.cpp` — cobre: 20, 21, 25, 26
- [ ] **T20** Benchmark `bench_frame`: edifício de 11 × 11 pilares e 41
  níveis (cerca de 30 mil GDL); registrar a medição abaixo. Se passar de
  5 s, parar e propor a revisão do NFR-008-02 — arquivos:
  `tests/perf/bench_frame.cpp`, `tests/CMakeLists.txt` — cobre: N02
- [ ] **T21** Testes e implementação de `BeamResult::at`:
  - esforços, rotações e deslocamentos ao longo da barra, com os saltos das
    concentradas (`Side`);
  - com base elástica, a reação interpolada.

  — arquivos: `beam_result.cpp`, `test_beam_loads.cpp` — cobre: 15, 22

## Fase 6 — Verificação

- [ ] **T22** Patch tests: barra em 4 trechos desiguais sob momento, normal
  e torção constantes — arquivo: `test_beam_verification.cpp` — cobre: 08,
  N01
- [ ] **T23** Vigas analíticas, nas duas teorias:
  - balanço com força e com momento;
  - biapoiada e biengastada com carga uniforme, trapezoidal, parcial e
    concentrada (flechas, reações e diagramas M e V em 11 pontos);
  - contínua de dois vãos;
  - recalque e rotação prescrita.

  — arquivos: `test_beam_verification.cpp`, `test_beam_loads.cpp` — cobre:
  08, 16, 21, 22, N01
- [ ] **T24** [P] Pórticos e grelhas: pórtico plano com carga lateral;
  pórtico espacial em L com flexão e torção; grelha; viga com tirante de
  treliça — arquivo: `test_beam_verification.cpp` — cobre: 08, 20, 21, N01
- [ ] **T25** [P] Temperatura e peso: biengastada e biapoiada com gradiente;
  ΔT e ε₀ em pórtico; peso de viga inclinada — arquivo:
  `test_beam_loads.cpp` — cobre: 16, 18, 19, N01
- [ ] **T26** [P] Ligações:
  - rótula interna (Gerber);
  - barra rotulada igual à treliça;
  - liberação de torção;
  - mola de rotação contra a ligação semirrígida analítica, com os limites
    0 e ∞;
  - excentricidade igual à barra rígida explícita (1e−6);
  - cargas com rótula, com mola e com excentricidade.

  — arquivo: `tests/unit/core/test_beam_connections.cpp` — cobre: 11, 12,
  14, 19, N01
- [ ] **T27** [P] Timoshenko: balanço curto, com flecha PL³/3EI + PL/(G·As);
  viga esbelta (L/h = 1000) igual a Euler-Bernoulli (1e−6) — arquivo:
  `test_beam_verification.cpp` — cobre: 07, N01
- [ ] **T28** [P] Seções assimétricas: balanço de cantoneira em flexão
  oblíqua; perfil U com carga no centroide (torção); ambos iguais ao modelo
  girado com a carga no centro de cisalhamento — arquivo:
  `test_beam_verification.cpp` — cobre: 05, 09, N01
- [ ] **T29** Convergência:
  - quarto de anel com n = 8, 16 e 32 contra Castigliano (taxa 2 ± 0,2);
  - viga sobre base elástica (Hetényi) com n = 4, 8 e 16 (taxa 4 ± 0,3).

  Se uma taxa não aparecer, parar e propor o ajuste da spec — arquivo:
  `test_beam_verification.cpp` — cobre: 08, 15
- [ ] **T30** Testes e implementação de `stressPoints`, `sectionStresses` e
  `normalStress`: retângulo, perfil I, tubo e perfil U sob N, M, V e T
  contra a resistência dos materiais; seção genérica em pontos dados —
  arquivos: `section.cpp`, `beam_result.cpp`, `test_section.cpp` — cobre:
  23, N01

## Fase 7 — Documentação

- [ ] **T31** Atualizar `specs/architecture.md` §3 com seções, viga,
  resultados de viga e validação — cobre: 27

## Fase 8 — Fechamento

- [ ] **T90** Build e testes verdes nos jobs do CI, incluindo o de cobertura
  com `core` ≥ 80% (push com sua confirmação) — cobre: N03
- [ ] **T91** `clang-format` e `clang-tidy` sem pendências (também o do Linux,
  pelo CI); `core` sem headers Qt — cobre: 27
- [ ] **T92** Conferir os doze critérios de aceite de `requirements.md`
- [ ] **T93** Atualizar `specs/roadmap.md` para `concluída`

## Matriz de rastreabilidade

| Requisito | Tarefas | Testes |
|-----------|---------|--------|
| REQ-008-01 | T17, T18 | `test_beam_validation` (ν ausente ou fora do intervalo) |
| REQ-008-02 | T01 a T03 | `test_section` |
| REQ-008-03 | T02, T03 | `test_section` |
| REQ-008-04 | T02, T03, T17, T18 | `test_section`, `test_beam_validation` |
| REQ-008-05 | T03, T10, T14, T15, T28 | `test_beam_element`, `test_beam_loads`, `test_beam_verification` (assimétricas) |
| REQ-008-06 | T08, T09, T17, T18 | `test_beam_element`, `test_beam_validation` (não `Line2`, fora do plano) |
| REQ-008-07 | T08, T09, T27 | `test_beam_element`, `test_beam_verification` (Timoshenko) |
| REQ-008-08 | T22 a T24, T29 | `test_beam_verification` |
| REQ-008-09 | T04, T06, T07, T28 | `test_beam_element`, `test_beam_verification` |
| REQ-008-10 | T06, T07, T17, T18 | `test_beam_element`, `test_beam_validation` |
| REQ-008-11 | T04, T05, T11, T26 | `test_beam_element`, `test_beam_connections` |
| REQ-008-12 | T04, T05, T11, T26 | `test_beam_element`, `test_beam_connections` |
| REQ-008-13 | T11, T17, T18 | `test_beam_element`, `test_beam_validation` |
| REQ-008-14 | T04, T05, T10, T17, T18, T26 | `test_beam_element`, `test_beam_validation`, `test_beam_connections` |
| REQ-008-15 | T04, T05, T12, T21, T29 | `test_beam_element`, `test_beam_loads`, `test_beam_verification` (Hetényi) |
| REQ-008-16 | T04, T05, T14, T15, T23, T25 | `test_beam_loads`, `test_beam_verification` |
| REQ-008-17 | T17, T18 | `test_beam_validation` |
| REQ-008-18 | T14, T15, T25 | `test_beam_loads` |
| REQ-008-19 | T14, T15, T25, T26 | `test_beam_loads`, `test_beam_connections` |
| REQ-008-20 | T19, T24 | `test_beam_verification` (misto) |
| REQ-008-21 | T19, T23, T24 | `test_beam_verification` |
| REQ-008-22 | T21, T23 | `test_beam_loads` |
| REQ-008-23 | T30 | `test_section` (tensões) |
| REQ-008-24 | T17, T18 | `test_beam_validation` |
| REQ-008-25 | T19 | `test_beam_validation` (mecanismo de rotação) |
| REQ-008-26 | T01, T16, T19 | suíte da 007 sem alteração |
| REQ-008-27 | T31, T91 | `check_no_qt_includes` |
| NFR-008-01 | T22 a T30 | `test_beam_verification`, `test_beam_loads`, `test_beam_connections`, `test_section` |
| NFR-008-02 | T20 | `bench_frame` |
| NFR-008-03 | T90 | Job de cobertura no CI |
