# Tarefas — 006 fem-kernel

Status: em andamento
Design: [design.md](design.md) · Requisitos: [requirements.md](requirements.md)

Regras:
- Uma tarefa = uma mudança pequena, compilável e testável.
- Testes vêm antes ou junto da implementação que verificam.
- `[P]` marca tarefas que podem ser feitas em paralelo com a anterior.
- `[M]` marca tarefas que dependem de ação do mantenedor.
- Marque `[x]` somente com build e testes passando.
- Os IDs abaixo omitem o prefixo: `01` significa `REQ-006-01`; `N01` significa
  `NFR-006-01`.

## Fase 1 — Preparação

- [x] **T01** oneTBB no `vcpkg.json` e como dependência privada de `core`;
  build e testes atuais verdes no Windows — arquivos: `vcpkg.json`,
  `src/core/CMakeLists.txt` — cobre: 18

## Fase 2 — Funções de forma, quadratura e mapeamento

- [x] **T02** Testes das funções de forma: delta de Kronecker, partição da
  unidade, soma nula das derivadas (50 pontos aleatórios) e derivadas por
  diferenças finitas, para os 17 tipos; `hasShapeFunctions` falso para um tipo
  de ordem 3 — arquivo: `tests/unit/core/test_shape_functions.cpp` — cobre:
  01, 02
- [x] **T03** Implementar `shape_functions.hpp/.cpp` (espaços por tipo,
  Vandermonde invertida uma vez, avaliação em memória fixa, pirâmide racional)
  até a T02 passar — cobre: 01, 02, 05
- [x] **T04** `[P]` Testes das quadraturas: exatidão por monômio até o grau
  declarado de cada regra, pesos positivos nos simplexos, soma dos pesos igual
  à medida do elemento de referência — arquivo: `test_quadrature.cpp` — cobre:
  06, 07
- [x] **T05** Implementar `quadrature.hpp/.cpp` (Gauss 1–5, produtos, Dunavant,
  tetraedro de 1, 4 e 14 pontos, cunha, pirâmide colapsada) até a T04 passar —
  cobre: 06, 07
- [x] **T06** Testes e implementação do mapeamento: elementos afins (jacobiano
  constante, volume pela soma dos pesos) e distorcidos; elemento invertido
  recusado; comprimento de linha e área de triângulo e quadrilátero em 3D;
  derivadas físicas de um campo linear exatas — arquivos:
  `isoparametric.hpp/.cpp`, `test_isoparametric.cpp` — cobre: 03, 04
- [x] **T07** Teste sem alocação: contador global de `new` em torno da
  avaliação de forma e geometria para todos os tipos (0 alocações depois da
  primeira chamada) — arquivo: `test_allocations.cpp` (executável próprio) —
  cobre: 05

## Fase 3 — Graus de liberdade

- [x] **T08** Testes do `DofMap`: máscaras misturadas (U, V numa barra e T
  noutra, com nó comum), numeração determinística, `find`/`nodeOf`/`typeOf`,
  prescrições válidas (zero e não nula) e inválidas (nó ou tipo inexistente),
  equações só dos livres — arquivo: `test_dof_map.cpp` — cobre: 08, 09, 10, 11
- [x] **T09** Implementar `dof_map.hpp/.cpp` e `element_formulation.hpp` até a
  T08 passar — cobre: 08 a 12

## Fase 4 — Montagem e solução

- [x] **T10** Elemento de teste e malhas: `poisson_element.hpp` (rigidez e
  carga, também para linhas e superfícies em 3D) e `unit_meshes.hpp` (malhas
  estruturadas de [0,1]^d lineares e quadráticas) — arquivos em
  `tests/unit/core/` — cobre: 12
- [x] **T11** Testes da montagem: matriz de dois elementos calculada à mão;
  vetor e cargas nodais; lado direito com prescrição não nula; montagem
  paralela igual à sequencial (1e−12) numa malha de 20 mil Tet4; erro de
  jacobiano com o ID do elemento — arquivo: `test_assembler.cpp` — cobre: 12,
  13, 14
- [x] **T12** Implementar `assembler.hpp/.cpp` (padrão esparso pela adjacência
  de nós, coloração gulosa, `tbb::parallel_for` por cor com buffers por
  thread, controle do número de threads) até a T11 passar — cobre: 12, 13, 14
- [x] **T13** Testes e implementação de `linear_solver.hpp/.cpp`
  (`SparseCholesky`): sistema SPD conhecido; mecanismo recusado; problema bem
  posto e mal escalado aceito — cobre: 15, 16
- [x] **T14** Testes e implementação de `linear_static.hpp/.cpp`
  (`solveLinear`): valores livres e prescritos, reações por segunda passada,
  caso sem GDL livre — cobre: 17

## Fase 5 — Verificação

- [x] **T15** Patch test de Poisson (u = 1 + 2x − y + 3z) em malhas
  distorcidas 1D, 2D (Tri3, Tri6, Quad4, Quad8, Quad9) e 3D (Tet4, Tet10, Hex8,
  Hex20, Hex27, Wedge6, Wedge15, Wedge18), lineares e quadráticas; reações
  equilibrando a fonte — arquivo: `test_poisson.cpp` — cobre: 01 a 04, 12 a 17
- [x] **T16** Convergência de Poisson contra solução analítica em três
  refinamentos: taxa do erro L2 ≈ 2 (lineares) e ≈ 3 (quadráticas) em Tri,
  Quad, Tet, Hex e Wedge — arquivo: `test_poisson.cpp` — cobre: 01, 03, 06, 13,
  15, 17
- [x] **T17** Atualizar `specs/architecture.md` (§1 e §3: o que já existe do
  núcleo, oneTBB, coloração) — cobre: 18

## Fase 6 — Desempenho e cobertura

- [x] **T18** `bench_kernel` (fora do ctest): montagem de 1 milhão de Tet4 com
  1 thread e com todas, e solução de Poisson 3D com cerca de 25 mil GDL (NFR-006-03 revisto) —
  arquivo: `tests/perf/bench_kernel.cpp` — cobre: N01, N02, N03
- [x] **T19** Preset `coverage` e job de cobertura no CI (Linux, GCC,
  `gcovr --filter src/core --fail-under-line 80`) — arquivos:
  `CMakePresets.json`, `.github/workflows/ci.yml` — cobre: N04
- [x] **T20** `[M]` Rodar o `bench_kernel` em Release na máquina do mantenedor
  e registrar as medições — cobre: N01, N02, N03

## Fase 7 — Fechamento

- [ ] **T90** Build e testes verdes nos jobs do CI, incluindo o de cobertura
  (push com sua confirmação) — cobre: AC-8, N04
- [ ] **T91** `clang-format` e `clang-tidy` sem pendências (também o do Linux,
  pelo CI)
- [ ] **T92** Conferir os oito critérios de aceite de `requirements.md`
- [ ] **T93** Atualizar `specs/roadmap.md` para `concluída`

## Matriz de rastreabilidade

| Requisito | Tarefas | Testes |
|-----------|---------|--------|
| REQ-006-01 | T02, T03, T15, T16 | `test_shape_functions`, `test_poisson` (AC-1, AC-5) |
| REQ-006-02 | T02, T03 | `test_shape_functions` (AC-1) |
| REQ-006-03 | T06, T15, T16 | `test_isoparametric`, `test_poisson` (AC-2, AC-5) |
| REQ-006-04 | T06, T11 | `test_isoparametric`, `test_assembler` (AC-2) |
| REQ-006-05 | T03, T07 | `test_allocations` (AC-6) |
| REQ-006-06 | T04, T05, T16 | `test_quadrature` (AC-3) |
| REQ-006-07 | T04, T05 | `test_quadrature` (AC-3) |
| REQ-006-08 | T08, T09 | `test_dof_map` (AC-4) |
| REQ-006-09 | T08, T09 | `test_dof_map` (AC-4) |
| REQ-006-10 | T08, T09, T11 | `test_dof_map`, `test_assembler` (AC-4) |
| REQ-006-11 | T08, T09 | `test_dof_map` (AC-4) |
| REQ-006-12 | T09, T10, T11, T12, T15 | `test_assembler`, `test_poisson` (AC-5) |
| REQ-006-13 | T11, T12, T16 | `test_assembler` (AC-5) |
| REQ-006-14 | T11, T12 | `test_assembler` (AC-5) |
| REQ-006-15 | T13, T16 | `test_assembler` (solver), `test_poisson` (AC-5) |
| REQ-006-16 | T13 | `test_assembler` (solver) (AC-5) |
| REQ-006-17 | T14, T15, T16 | `test_poisson` (AC-5) |
| REQ-006-18 | T01, T17 | `check_no_qt_includes` (AC-6) |
| NFR-006-01 | T18, T20 | `bench_kernel` (AC-7) |
| NFR-006-02 | T18, T20 | `bench_kernel` (AC-7) |
| NFR-006-03 | T18, T20 | `bench_kernel` (AC-7) |
| NFR-006-04 | T19, T90 | Job de cobertura no CI (AC-7) |

## Notas de implementação

**2026-11-15 — Fases 1 a 3 (T01–T09):**

- oneTBB 2023.1.0 instalado pelo vcpkg (com hwloc) e ligado como dependência
  privada de `core` (ADR 0004).
- 154 testes passando no Windows (12 novos), todos de primeira: as funções de
  forma geradas pela Vandermonde nos nós do Gmsh passam no delta de Kronecker,
  na partição da unidade e nas diferenças finitas para os 16 tipos; todas as
  regras de quadratura integram os monômios até o grau declarado com erro
  ≤ 1e−12, o que confere as constantes tabeladas (Dunavant e a regra de 14
  pontos do tetraedro).
- O teste de alocação roda num executável próprio (`test_allocations`), que
  substitui o `operator new` global: zero alocações na avaliação de forma e
  geometria depois da primeira chamada.
- Desvios do design: `DofMap` numera as equações sob demanda (prescrever
  muitos apoios não fica quadrático), sem o `finalize()` explícito; a lista de
  tipos tem 16 entradas (a pirâmide de 5 nós é a 16ª), como no REQ-006-01;
  `dofName` e `firstDof` acrescentados.

**2026-11-15 — Fase 4 (T10–T14):**

- 159 testes passando no Windows; `clang-tidy` sem apontamentos.
- Conferido: matriz de duas barras calculada à mão, lado direito com
  recalque, carga nodal, valores e reação (u = 2, 5, 11; reação −3); montagem
  paralela igual à sequencial numa malha de 20 250 Tet4 distorcida (diferença
  ≤ 1e−12, na prática zero: a ordem das somas é a mesma); erro de jacobiano
  com o ID do elemento; carga em GDL inexistente; mecanismo recusado pelo
  solver; sistema mal escalado (1e−6 a 1e6) aceito; problema sem GDL livre com
  reações.
- Desvio do design: as reações ficam numa função `reactions` do
  `assembler.hpp` (a mesma passada por cores da montagem), chamada por
  `solveLinear`.

**2026-11-15 — Fase 5 (T15–T17):**

- Patch test (u = 1 + 2x − y + 3z) reproduzido até 1e−10 nos 15 tipos, em
  malhas distorcidas por um mapa suave (elementos quadráticos com lados
  curvos), com a soma das reações nula; com fonte unitária, as reações somam
  −1 (o volume do domínio).
- Convergência do erro L2 em três refinamentos, com a taxa entre os dois
  últimos dentro de ±0,2 do esperado: ≈ 2 para Line2, Tri3, Quad4, Tet4, Hex8
  e Wedge6; ≈ 3 para Line3, Tri6, Quad8, Quad9, Tet10, Hex20, Hex27, Wedge15 e
  Wedge18.
- Arquitetura atualizada (§3: o que já existe do núcleo).- O teste de convergência leva cerca de 34 s em Debug (o caso mais pesado,
  8 s); para isso o erro L2 usa quadratura de grau 2p + 3 e as malhas 3D
  lineares começam em n = 3. Em Release (CI) leva poucos segundos.

**2026-11-15 — T18 (`bench_kernel`, Release, máquina do mantenedor, 12 threads):**

| Medida | Resultado | Meta |
|--------|-----------|------|
| Montagem de 1 057 916 Tet4 (174 014 GDL), todas as threads | 0,89 s | < 2 s (NFR-006-01) |
| Mesma montagem, 1 thread | 3,88 s | — |
| Ganho do paralelismo | 4,35× | ≥ 2,5× (NFR-006-02) |
| Solução direta, Poisson em 20³ Hex8 (6 859 GDL livres) | 0,34 s | — |
| Solução direta, 30³ Hex8 (24 389 GDL livres) | 4,9 s | < 10 s com ~25 mil GDL (NFR-006-03 revisto) |
| Solução direta, 46³ Hex8 (91 125 GDL livres) | 99 s | fora da meta revista (só registro) |

O tempo da solução direta cresce como o quadrado do número de equações: a
fatoração simplicial (`SimplicialLDLT`) sofre com o preenchimento em
problemas 3D. O mantenedor reviu o NFR-006-03 para cerca de 25 mil GDL 3D;
problemas maiores ficam com as features 025 e 038.

**2026-11-15 — T19 e T20:**

- T19: preset `linux-coverage` (GCC, Debug, `--coverage`) e job `coverage` no
  CI, que roda os testes e o `gcovr --filter src/core/ --fail-under-line 80`.
  Os triplets de Linux e macOS passam a ligar o oneTBB dinamicamente, como diz
  o ADR 0004 (eram estáticos por padrão). A cobertura só é medida no CI (T90).
- T20: o `bench_kernel` rodou em Release na máquina do mantenedor (tabela da
  T18); com o NFR-006-03 revisto, as três metas de desempenho estão atendidas.

