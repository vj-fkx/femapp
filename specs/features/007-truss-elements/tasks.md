# Tarefas — 007 truss-elements

Status: concluído · 2026-11-16
Design: [design.md](design.md) · Requisitos: [requirements.md](requirements.md)

Regras:
- Uma tarefa = uma mudança pequena, compilável e testável.
- Testes vêm antes ou junto da implementação que verificam.
- `[P]` marca tarefas que podem ser feitas em paralelo com a anterior.
- `[M]` marca tarefas que dependem de ação do mantenedor.
- Marque `[x]` somente com build e testes passando.
- Os IDs abaixo omitem o prefixo: `01` significa `REQ-007-01`; `N01` significa
  `NFR-007-01`.

## Fase 1 — Material, seção e modelo

- [x] **T01** `material.hpp` (`Material`, `Section`, propriedades opcionais) e
  `model.hpp` (alvos, `Assignment`, `Restraint`, `NodalForce`,
  `TemperatureChange`, `InitialStrain`, `Model` com `findMaterial` e
  `findSection`); novos fontes no `CMakeLists.txt` — arquivos:
  `src/core/include/fem/core/material.hpp`, `model.hpp`, `src/core/model.cpp`,
  `src/core/CMakeLists.txt` — cobre: 01 a 07
- [x] **T02** Malhas de treliça para os testes: barras dadas por coordenadas
  e pares de nós, com grupos opcionais de linhas e de pontos — arquivo:
  `tests/unit/core/truss_meshes.hpp` — cobre: apoio a T03 e seguintes
- [x] **T03** Testes de `resolve` (grupo de elementos de dimensão 1, grupo de
  nós por nome em qualquer dimensão, listas de ids, grupo e id inexistentes)
  e do `Model` (incluir, alterar, remover, procurar por nome; a cópia
  compartilha a malha) — arquivo: `tests/unit/core/test_model.cpp` — cobre:
  03 a 07
- [x] **T04** Implementar `resolve` em `model.cpp` até a T03 passar — cobre:
  03 a 05

## Fase 2 — Elemento de treliça

- [x] **T05** Testes do elemento: K igual à fórmula fechada em orientação
  aleatória (2D e 3D); invariância por rotação; energia nula nos modos rígidos
  (2D: 3 translações e rotação; 3D: translações, rotações e rotação axial);
  vetores de ΔT, ε₀ e peso; `axialForce` com a deformação imposta; contador
  de alocações no `compute` — arquivos:
  `tests/unit/core/test_truss_element.cpp`, `test_allocations.cpp` — cobre:
  08, 11
- [x] **T06** Implementar `truss_element.hpp/.cpp` (`BarData`, `TrussElement`,
  `axialForce`) até a T05 passar — cobre: 08, 11

## Fase 3 — Validação e mecanismos

- [x] **T07** Teste de `SparseCholesky::singularEquation`: matriz com uma
  equação sem rigidez em posição conhecida, aponta a equação na numeração
  original; vazio depois de uma fatoração bem-sucedida — arquivo:
  `tests/unit/core/test_assembler.cpp` — cobre: 15
- [x] **T08** Implementar `singularEquation` (primeiro pivô não claramente
  positivo, levado pela permutação inversa) até a T07 passar — arquivos:
  `linear_solver.hpp/.cpp` — cobre: 15
- [x] **T09** Testes de `validate`: cada problema do design §5.3 isolado, com
  a mensagem citando o item (posição, nome ou id externo), e vários problemas
  juntos; `Point1` sem atribuição aceito; z ≠ 0 em treliça 2D; `Tri3`
  atribuído como treliça; w num nó só 2D — arquivo:
  `tests/unit/core/test_static_validation.cpp` — cobre: 09, 10, 14
- [x] **T10** Implementar `validate` e `ModelIssue` em
  `static_analysis.hpp/.cpp` até a T09 passar — cobre: 09, 10, 14, 16

## Fase 4 — Análise estática

- [x] **T11** Testes da análise: barra simples (u, reação, N, ε, σ); a análise
  não altera o `Model` (comparação antes e depois); modelo inválido devolve os
  problemas; nó sem rigidez (diagonal) lista nó e direção; quadrado
  articulado sem diagonal informa um mecanismo com nó e direção — arquivos:
  `tests/unit/core/test_static_validation.cpp`, `test_model.cpp` — cobre: 12,
  13, 15
- [x] **T12** Implementar `runLinearStatic` e `StaticResult` (tabela de barras,
  conjuntos por tipo, `DofMap`, restrições, forças nodais, verificação da
  diagonal, fatoração, reações, recuperação por barra) até a T11 passar —
  arquivos: `static_analysis.hpp/.cpp` — cobre: 12, 13, 15

## Fase 5 — Verificação numérica

- [x] **T13** Patch tests: deslocamento rígido qualquer numa treliça
  distorcida dá N = 0 (2D e 3D); deformação uniforme numa treliça estruturada
  simétrica é reproduzida nos nós internos — arquivo:
  `tests/unit/core/test_truss_verification.cpp` — cobre: 12, N01
- [x] **T14** Soluções analíticas, em 2D e 3D: barra axial, V simétrica,
  treliça plana isostática (método dos nós), tripé, três barras concorrentes
  hiperestática, recalque de apoio; erro relativo ≤ 1e−10 — arquivo:
  `test_truss_verification.cpp` — cobre: 08, 12, N01
- [x] **T15** Ações distribuídas: ΔT e ε₀ com a barra presa nas duas pontas e
  com uma livre; barra pendurada sob peso próprio — arquivo:
  `test_truss_verification.cpp` — cobre: 06, 11, 12, N01
- [x] **T16** Convergência da barra pendurada (n = 4, 8, 16): deslocamentos
  nodais exatos; taxa do erro L2 de N igual a 1 ± 0,1 — arquivo:
  `test_truss_verification.cpp` — cobre: 12, N01

## Fase 6 — Desempenho e documentação

- [x] **T17** `bench_truss` (fora do ctest): treliça espacial de 18³ nós (NFR-007-02 revisto),
  células cúbicas com diagonais; mede validação, montagem, solução e
  recuperação — arquivos: `tests/perf/bench_truss.cpp`, `tests/CMakeLists.txt`
  — cobre: N02
- [x] **T18** [M] Rodar o `bench_truss` em Release na máquina do mantenedor e
  registrar a medição abaixo — cobre: N02
- [x] **T19** Atualizar `specs/architecture.md` §3 com o que foi feito —
  cobre: 16

## Fase 7 — Fechamento

- [x] **T90** Build e testes verdes nos jobs do CI, incluindo o de cobertura
  com `core` ≥ 80% (push com sua confirmação) — cobre: AC-8, N03
- [x] **T91** `clang-format` e `clang-tidy` sem pendências (também o do Linux,
  pelo CI); `core` sem headers Qt
- [x] **T92** Conferir os oito critérios de aceite de `requirements.md`
- [x] **T93** Atualizar `specs/roadmap.md` para `concluída`

## Matriz de rastreabilidade

| Requisito | Tarefas | Testes |
|-----------|---------|--------|
| REQ-007-01 | T01, T03 | `test_model.cpp` |
| REQ-007-02 | T01, T03 | `test_model.cpp` |
| REQ-007-03 | T01, T03, T04 | `test_model.cpp` |
| REQ-007-04 | T01, T03, T04 | `test_model.cpp` |
| REQ-007-05 | T01, T03, T04 | `test_model.cpp` |
| REQ-007-06 | T01, T03, T15 | `test_model.cpp`, `test_truss_verification.cpp` |
| REQ-007-07 | T01 | `test_model.cpp` (números sem unidade; revisão) |
| REQ-007-08 | T05, T06, T14 | `test_truss_element.cpp`, `test_truss_verification.cpp` |
| REQ-007-09 | T09, T10 | `test_static_validation.cpp` |
| REQ-007-10 | T09, T10 | `test_static_validation.cpp` |
| REQ-007-11 | T05, T06, T15 | `test_truss_element.cpp`, `test_truss_verification.cpp` |
| REQ-007-12 | T11 a T16 | `test_static_validation.cpp`, `test_truss_verification.cpp` |
| REQ-007-13 | T11, T12 | `test_model.cpp` |
| REQ-007-14 | T09, T10 | `test_static_validation.cpp` |
| REQ-007-15 | T07, T08, T11, T12 | `test_assembler.cpp`, `test_static_validation.cpp` |
| REQ-007-16 | T10, T19, T91 | Verificação de includes em `core` |
| NFR-007-01 | T13 a T16 | `test_truss_verification.cpp` |
| NFR-007-02 | T17, T18 | `bench_truss` |
| NFR-007-03 | T90 | Job de cobertura no CI |

## Notas de implementação

- T01: `ElementTarget` e `NodeTarget` como `std::variant<GroupRef,
  std::vector<ExternalId>>` eram o mesmo tipo, e as duas sobrecargas de
  `resolve` colidiam; as listas viraram tipos distintos (`ElementIds`,
  `NodeIds`). Ajuste registrado no design §4.
- T15: com ΔT só numa das duas barras em série presas nas pontas, o esforço é
  o mesmo nas duas, −EA(ε₁ + ε₂)/2 (o teste foi escrito assim).
- T18: a meta original do NFR-007-02 (30 mil GDL em 5 s) não foi atingida
  (17 s); o mantenedor escolheu rever a meta para ~16 mil GDL em 5 s, como no
  NFR-006-03. O padrão do `bench_truss` passou a n = 18.

## Medições

**2026-11-15 — T18 (`bench_truss`, Release, máquina do mantenedor):** treliça
espacial n³ nós com as arestas de uma malha de Kuhn (até 14 barras por nó),
base presa, peso próprio e carga no topo. Tempo de `runLinearStatic`
(validação, montagem, fatoração, solução, reações e esforços):

| n | Nós | Barras | GDL livres | Tempo |
|---|-----|--------|-----------|-------|
| 12 | 1 728 | 10 439 | 4 752 | 0,17 s |
| 15 | 3 375 | 21 014 | 9 450 | 0,99 s |
| 18 | 5 832 | 37 043 | 16 524 | 3,7 s |
| 22 | 10 648 | 68 859 | 30 492 | 17 s (meta original: < 5 s) |

Com a meta revista (~16 mil GDL em < 5 s), n = 18 atende: 3,7 s.

A validação leva 3 ms. O tempo cresce com o quadrado do número de GDL, como
na 006: a fatoração simplicial de um grafo 3D com 3 GDL por nó domina.

**2026-11-16 — Fechamento (T90 a T93):**

- T90: execução `5e770ee` verde nos seis jobs (Windows, macOS, linux-gcc,
  linux-clang, cobertura e formatação):
  <https://github.com/vj-fkx/femapp/actions/runs/43856149069>. Cobertura de
  `core`: 96,5% das linhas (`static_analysis.cpp` 97%, `model.cpp` 100%,
  `truss_element.cpp` 97%).
- T91: `clang-format` e `clang-tidy` limpos no Windows e no Linux;
  `check_no_qt_includes` verde. Duas correções só apareceram no CI: o
  `-Wmaybe-uninitialized` do GCC 13 em `test_static_validation.cpp` (pragma
  com comentário) e o `-Wmismatched-new-delete` em `test_allocations.cpp`
  (vetor trocado por `std::array`).
- T92, os oito critérios de aceite:
  - AC-1: `test_model.cpp` (alvos por grupo e por lista, edição e cópia por
    valor; a análise recebe o `Model` como `const`).
  - AC-2: `test_truss_element.cpp` (fórmula fechada, rotação, modos rígidos,
    cargas equivalentes).
  - AC-3 a AC-6: `test_truss_verification.cpp` (patch tests, casos
    analíticos, ΔT e ε₀, barra pendurada e convergência com taxa ≈ 1).
  - AC-7: `test_static_validation.cpp` e o teste do pivô singular em
    `test_assembler.cpp`.
  - AC-8: `core` sem Qt, benchmark registrado acima (meta revista), CI verde
    com cobertura ≥ 80%.
- T93: roadmap atualizado.
