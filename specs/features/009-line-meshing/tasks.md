# Tarefas — 009 line-meshing

Status: em andamento
Design: [design.md](design.md) · Requisitos: [requirements.md](requirements.md)

Regras:
- Uma tarefa = uma mudança pequena, compilável e testável.
- Testes vêm antes ou junto da implementação que verificam.
- `[P]` marca tarefas que podem ser feitas em paralelo com a anterior.
- `[M]` marca tarefas que dependem de ação do mantenedor.
- Marque `[x]` somente com build e testes passando.
- Os IDs abaixo omitem o prefixo: `01` significa `REQ-009-01`; `N01`
  significa `NFR-009-01`.
- Em toda tarefa, as suítes da 001 a 008d continuam passando.

## Fase 1 — Módulo e geometria dos segmentos

- [x] **T01** O módulo `mesher` ganha conteúdo:
  - sai o marcador `module.hpp`/`module.cpp` da 001 e o seu teste;
  - `line_mesher.hpp` com as interfaces do design §4 (`LineSizing`,
    `Grading`, `LineMeshOptions`, mapa, relatório, `Overlap`,
    `LineMeshError`, `meshLines` devolvendo `NoLines` por ora,
    `entityGroupName`);
  - oneTBB privado no `CMakeLists.txt`;
  - `femapp_add_test(mesher …)` com um teste de `entityGroupName`.
  Arquivos: `src/mesher/*`, `tests/CMakeLists.txt`,
  `tests/unit/mesher/test_line_mesher.cpp`. Cobre: 09, 17.
- [x] **T02** Testes da geometria de segmentos (devem falhar), com a
  referência das contas à mão em 1e−12 relativo:
  - casos: cruzamento, T, paralelos, colineares com e sem pedaço comum,
    reversas, quase-T a menos e a mais de tol, segmento e ponto
    degenerados;
  - os mesmos com translação (1e6, −1e6, 1e6) e escalas 1, 1e3 e 1e6,
    dando a mesma classificação.
  Arquivo: `tests/unit/mesher/test_segment_geometry.cpp`. Cobre: 03, 04,
  N02.
- [x] **T03** `detail/segment_geometry` até a T02 passar: pontos mais
  próximos (Ericson §5.1.9) em coordenadas relativas, ponto-segmento e a
  classificação do contato (design §5.3). Arquivos:
  `src/mesher/detail/segment_geometry.hpp/.cpp`. Cobre: 03, 04, N02.
- [x] **T04** [P] BVH estática:
  - testes primeiro: os pares iguais aos da força bruta em nuvens
    aleatórias de segmentos (semente fixa; 10, 1 000 e 20 000 segmentos;
    escalas misturadas), com 1, 2 e todas as threads; os pares ponto-caixa;
  - depois a implementação (design §5.2).
  Arquivos: `tests/unit/mesher/test_segment_bvh.cpp`,
  `src/mesher/detail/segment_bvh.hpp/.cpp`. Cobre: 08, N01.

## Fase 2 — O gerador

- [x] **T05** Testes do caminho básico (devem falhar):
  - pórtico 2×2 com linhas que se tocam nas pontas: contagens, IDs
    externos na ordem do design §5.6, nós fundidos, `Point1` dos pontos;
  - polilinha aberta e fechada;
  - o mapa de elementos e nós;
  - os grupos de dimensão 1 e 0 com `entityGroupName`;
  - o subconjunto por IDs;
  - `NoLines` (só pontos; vazio) e `UnknownEntity`.
  Arquivo: `tests/unit/mesher/test_line_mesher.cpp`. Cobre: 01, 02, 05,
  08, 09, 10, 14.
- [x] **T06** `meshLines`, coleta, fusão e numeração até a T05 passar:
  - coleta (§5.1, inclusive trechos ignorados);
  - fusão por grade de hash e union-find (§5.4);
  - um elemento por trecho, numeração, grupos, mapa e relatório (§5.6);
  - ainda sem as divisões por contato.
  Arquivo: `src/mesher/line_mesher.cpp`. Cobre: 01, 02, 05, 08, 09, 10,
  11.
- [x] **T07** Testes das ligações (devem falhar), cada divisão com o nó
  comum e listada no relatório com o tipo e as entidades:
  - viga que termina no meio de um pilar;
  - diagonais que se cruzam;
  - linha que cruza uma polilinha num vértice;
  - ponto sobre uma viga;
  - cruzamento em 3D;
  - retas reversas que não se ligam;
  - vários pilares e vigas num mesmo nó;
  - quase-T a menos de tol;
  - duas linhas sobrepostas (`Overlaps` com o par e o pedaço);
  - trecho nulo ignorado;
  - volta colinear de uma polilinha sobre si mesma.
  Arquivo: `tests/unit/mesher/test_line_mesher.cpp`. Cobre: 03, 04, 11.
- [x] **T08** Contatos no gerador até a T07 passar: a BVH, a
  classificação, os parâmetros de divisão levados aos nós pela fusão e os
  trechos entre nós obrigatórios (§5.2, §5.3, §5.5). Arquivo:
  `src/mesher/line_mesher.cpp`. Cobre: 03, 04, 11.
- [x] **T09** Divisão:
  - testes primeiro, depois a implementação (§5.5);
  - divisões;
  - comprimento alvo (L/h inteiro e 3 + 1e−12);
  - graduação TowardEnd e TowardBoth (n par e ímpar), com as razões
    medidas e a soma em 1e−12;
  - a direção da entidade mantida depois de uma divisão;
  - padrão sobreposto por entidade;
  - `Line3` com o nó do meio no ponto médio;
  - `InvalidSizing`.
  Arquivos: `tests/unit/mesher/test_line_mesher.cpp`,
  `src/mesher/line_mesher.cpp`. Cobre: 05, 06, 07.
- [x] **T10** Verificação e determinismo:
  - o pórtico 2×2 gerado, analisado pela `runLinearStatic` com
    atribuições, apoios e cargas por `GroupRef` de `entityGroupName`,
    contra o mesmo pórtico montado à mão (deslocamentos em 1e−12
    relativo);
  - a malha inteira idêntica com 1 thread e com todas;
  - mover uma entidade e gerar de novo mantém o nome do grupo;
  - a translação e a escala da NFR-009-02 sobre o pórtico com ligações.
  Arquivo: `tests/unit/mesher/test_line_mesher.cpp`. Cobre: 08, 09, N02,
  AC-1, AC-5.

## Fase 3 — Comando MESH no app

- [x] **T11** `LineMeshJob`:
  - cópia do desenho e da revisão;
  - `meshLines` e `render::buildMeshScene` no `QThreadPool`;
  - sinais `finished`/`failed` na thread do objeto;
  - um teste em `test_app` (`QCoreApplication` e um `QEventLoop` até o
    sinal; o `test_app` hoje compila só `cli_options`, e ganha estas
    fontes e o `fem::mesher`) que espera o `finished` de um desenho
    pequeno e o `failed` de um sem linhas, e confere que um segundo
    `start` durante a geração é ignorado.
  Arquivos: `src/app/line_mesh_job.hpp/.cpp`,
  `tests/unit/app/test_line_mesh_job.cpp`, `tests/CMakeLists.txt`. Cobre:
  13, 14.
- [ ] **T12** MESH no `DraftingController` e na `MainWindow`:
  - o estado `meshing`, com o pedido, o número, Enter, Esc e "Invalid
    size.";
  - o Tab com MESH;
  - "Meshing is already running.";
  - a malha no viewport com o desenho, o rótulo e o relatório no console
    (20 linhas e o total);
  - a falha mantendo a malha;
  - o aviso de desatualizada uma vez por malha.
  A leitura do tamanho (`parseMeshSize`: Enter, número > 0, inválido) e os
  textos do relatório e dos erros (`meshReportLines`, `meshErrorText`)
  ficam em `line_mesh_text.hpp/.cpp`, testados em `test_app`. O
  controlador e a janela, que dependem dos widgets e do viewport, ficam no
  roteiro da T14.
  Arquivos: `src/app/line_mesh_text.hpp/.cpp`,
  `src/app/drafting_controller.*`, `src/app/main_window.*`,
  `tests/unit/app/test_line_mesh_text.cpp`. Cobre: 12, 13, 14, 15.
- [ ] **T13** Traduções pt_BR dos textos novos (`lupdate`, `.ts`
  preenchido, `lrelease` no build). Arquivo:
  `src/app/i18n/femapp_pt_BR.ts`. Cobre: 16.
- [ ] **T14** [M] Roteiro manual do MESH na interface, pelo mantenedor ou
  com ele. Cobre: 12 a 16, AC-6.
  1. Desenhar um pórtico 2×2 com uma diagonal cruzando outra e uma viga
     em T; MESH, Enter: a malha aparece com o desenho, e o console mostra
     as contagens e as divisões.
  2. MESH, `0.5`: os elementos ficam com cerca de 0,5.
  3. Desenhar uma linha sobre outra; MESH: o console lista a sobreposição,
     e a malha anterior continua.
  4. Mover uma linha: o aviso de desatualizada aparece uma vez; uma
     segunda edição não repete.
  5. Desfazer e refazer: só o desenho muda; a malha fica.
  6. Interface em pt_BR: os textos do MESH traduzidos.
  7. MESH, `abc` e `-1`: "Invalid size."; Esc cancela.

## Fase 4 — Desempenho e fechamento

- [ ] **T15** `bench_line_mesh`: grade 3D de pilares, vigas e diagonais
  cruzadas, cerca de 100 mil trechos, com o tempo de cada etapa. A medida
  em Release fica registrada nas notas, ≤ 2 s. Arquivos:
  `tests/perf/bench_line_mesh.cpp`, `tests/perf/CMakeLists.txt`. Cobre:
  N01.
- [ ] **T16** Cobertura de `src/mesher/` ≥ 80% no job de cobertura do CI,
  com um segundo `gcovr`. Arquivo: `.github/workflows/ci.yml`. Cobre: N03.
- [ ] **T17** Documentação: `architecture.md` §3b com o `LineMesher` feito
  e o comando MESH no `app`; `README.md` com o MESH na lista de comandos.
  Cobre: 17.
- [ ] **T90** [M] Build e testes verdes nos jobs do CI, inclusive a
  cobertura de `core` e de `mesher` ≥ 80%; push com sua confirmação.
  Cobre: N03.
- [ ] **T91** `clang-format` e `clang-tidy` sem pendências (também o do
  Linux, pelo CI); `mesher` e `geom` sem headers Qt. Cobre: 17.
- [ ] **T92** Conferir os sete critérios de aceite de `requirements.md`.
- [ ] **T93** Atualizar `specs/roadmap.md` para `concluída`.

## Matriz de rastreabilidade

| Requisito | Tarefas | Testes |
|-----------|---------|--------|
| REQ-009-01 | T05, T06 | `test_line_mesher.cpp` |
| REQ-009-02 | T05, T06 | `test_line_mesher.cpp` |
| REQ-009-03 | T02, T03, T07, T08 | `test_segment_geometry.cpp`, `test_line_mesher.cpp` |
| REQ-009-04 | T02, T03, T07, T08 | `test_segment_geometry.cpp`, `test_line_mesher.cpp` |
| REQ-009-05 | T05, T06, T09 | `test_line_mesher.cpp` |
| REQ-009-06 | T09 | `test_line_mesher.cpp` |
| REQ-009-07 | T09 | `test_line_mesher.cpp` |
| REQ-009-08 | T04, T05, T06, T10 | `test_segment_bvh.cpp`, `test_line_mesher.cpp` |
| REQ-009-09 | T01, T05, T06, T10 | `test_line_mesher.cpp` |
| REQ-009-10 | T05, T06 | `test_line_mesher.cpp` |
| REQ-009-11 | T06, T07, T08 | `test_line_mesher.cpp` |
| REQ-009-12 | T12, T14 | `test_line_mesh_text.cpp`, roteiro manual |
| REQ-009-13 | T11, T12, T14 | `test_line_mesh_job.cpp`, `test_line_mesh_text.cpp`, roteiro manual |
| REQ-009-14 | T05, T11, T12, T14 | `test_line_mesher.cpp`, `test_line_mesh_job.cpp`, `test_line_mesh_text.cpp`, roteiro manual |
| REQ-009-15 | T12, T14 | roteiro manual |
| REQ-009-16 | T13, T14 | roteiro manual |
| REQ-009-17 | T01, T17, T91 | `check_no_qt_includes` |
| NFR-009-01 | T04, T15 | `test_segment_bvh.cpp`, `bench_line_mesh` |
| NFR-009-02 | T02, T03, T10 | `test_segment_geometry.cpp`, `test_line_mesher.cpp` |
| NFR-009-03 | T16, T90 | Job de cobertura no CI |

| Critério | Tarefas |
|----------|---------|
| AC-1 | T05, T06, T10 |
| AC-2 | T07, T08 |
| AC-3 | T05, T07, T08 |
| AC-4 | T09 |
| AC-5 | T05, T06, T10 |
| AC-6 | T11 a T14 |
| AC-7 | T10, T15, T16, T90, T91 |

## Notas de implementação
