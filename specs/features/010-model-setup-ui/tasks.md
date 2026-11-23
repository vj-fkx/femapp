# Tarefas — 010 model-setup-ui

Status: em andamento
Design: [design.md](design.md) · Requisitos: [requirements.md](requirements.md)

Regras:
- Uma tarefa = uma mudança pequena, compilável e testável.
- Testes vêm antes ou junto da implementação que verificam.
- `[P]` marca tarefas que podem ser feitas em paralelo com a anterior.
- `[M]` marca tarefas que dependem de ação do mantenedor.
- Marque `[x]` somente com build e testes passando.
- Os IDs abaixo omitem o prefixo: `01` significa `REQ-010-01`; `N01`
  significa `NFR-010-01`.
- Em toda tarefa, as suítes da 001 a 009 continuam passando.
- Textos da interface em inglês, por `tr()` (REQ-010-24).

## Fase 1 — Índice dos grupos no `core`

- [x] **T01** O índice dos grupos (design §5.7):
  - testes primeiro: `findGroup` e `findGroups` com muitos grupos, nomes
    repetidos em dimensões diferentes e a ordem de `groups()` mantida;
  - depois o índice ordenado por (nome, dimensão), feito em
    `MeshBuilder::build()`, e as buscas binárias.
  Arquivos: `core/include/fem/core/mesh.hpp`, `mesh.cpp`,
  `mesh_builder.cpp`, `tests/unit/core/test_mesh.cpp`. Cobre: 25.

## Fase 2 — Módulo `setup`: unidades e documento

- [x] **T02** O módulo `setup`:
  - `CMakeLists`, `femapp_add_test(setup …)` e o `check_no_qt_includes`
    com `setup`;
  - a arquitetura com a linha do módulo e o grafo do ADR 0010;
  - um teste trivial.
  Arquivos: `src/setup/*`, `CMakeLists.txt` (o `add_subdirectory`),
  `tests/CMakeLists.txt`, `cmake/CheckNoQtIncludes.cmake`,
  `specs/architecture.md`. Cobre: 01.
- [x] **T03** Unidades (design §5.1):
  - testes primeiro: os fatores dos cinco sistemas; a coerência
    (E·A/L numa rigidez de cada sistema, ρ·g numa força por volume); ida e
    volta de cada grandeza em 1e−12; os rótulos;
  - depois `units.hpp/.cpp`.
  Arquivos: `src/setup/include/fem/setup/units.hpp`, `units.cpp`,
  `tests/unit/setup/test_units.cpp`. Cobre: 02, N02.
- [x] **T04** O documento:
  - testes primeiro: ids que não voltam; `find`; aplicar e reverter cada
    espécie de item; revisão; caso ativo; unidades na edição;
  - depois `document.hpp/.cpp` (itens, `Targets`, `Document`,
    `DocumentEdit`).
  Arquivos: `src/setup/include/fem/setup/document.hpp`, `document.cpp`,
  `tests/unit/setup/test_document.cpp`. Cobre: 01, 23.
- [x] **T05** Conversão dos tipos do `core` (design §5.1):
  - testes primeiro: um documento com cada campo de cada tipo não nulo,
    convertido e de volta em 1e−12, e cada campo que tem grandeza mudando
    na ida;
  - depois `convert.hpp/.cpp` e `changeUnits`.
  Arquivos: `src/setup/include/fem/setup/convert.hpp`, `convert.cpp`,
  `editing.cpp`, `tests/unit/setup/test_units.cpp`. Cobre: 03, N02.
- [x] **T06** As edições (design §5.2):
  - testes primeiro:
    - criar, alterar, duplicar;
    - apagar recusado com os usos (material, seção, caso com
      combinação);
    - apagar um caso apaga as cargas dele;
    - atribuir substituindo nos alvos (barras, ligação por ponta,
      excentricidade, base, empenamento, GDL de apoio);
    - cargas que somam;
    - copiar cargas entre casos;
    - 10⁵ entidades numa edição só, desfeita e refeita;
  - depois `editing.hpp/.cpp`.
  Arquivos: `src/setup/include/fem/setup/editing.hpp`, `editing.cpp`,
  `tests/unit/setup/test_editing.cpp`. Cobre: 04, 05, 07, 08, 15 a 18, 23.
- [x] **T07** A biblioteca de materiais por norma (design §5.9, ampliada na
  implementação a pedido do mantenedor):
  - testes primeiro:
    - cada material contra a tabela;
    - o E dos concretos pela fórmula da norma em cada classe;
    - `fromLibrary` nas cinco unidades;
    - as propriedades extras de um `MaterialItem` convertidas por
      grandeza, ida e volta;
  - depois:
    - `MaterialItem` com `standard`, `grade` e `properties`, convertidos
      por `convertItem`;
    - `library.hpp/.cpp` como tabela de dados, uma função por norma.
  Arquivos: `src/setup/include/fem/setup/document.hpp`, `convert.cpp`,
  `src/setup/include/fem/setup/library.hpp`, `library.cpp`,
  `tests/unit/setup/test_library.cpp`, `test_convert.cpp`. Cobre: 04.

## Fase 3 — Montagem do `core::Model`

- [x] **T08** Alvos e montagem básica (design §5.3, §5.4):
  - testes primeiro:
    - o pórtico 2×2 da T10 da 009 preparado pelo documento (materiais,
      seções, vigas por entidade, apoios em pontos, a carga) dá a mesma
      análise linear que o modelo do teste da 009 (1e−12);
    - `NoMesh`, `MeshOutOfDate`, `MissingEntity`, `EntityNotMeshed`,
      `UnknownMaterial`, `UnknownSection`;
    - `targetState` de cada caso;
  - depois `build.hpp/.cpp` (o índice do mapa, as barras, os apoios, os
    nós de linhas e de pontos).
  Arquivos: `src/setup/include/fem/setup/build.hpp`, `build.cpp`,
  `tests/unit/setup/test_build.cpp`. Cobre: 07, 11, 12, 14, 15, 20.
- [x] **T09** Detalhes de viga e alvos da malha:
  - testes primeiro:
    - viga 3D com ligações, excentricidades, base e empenamento,
      montada = feita à mão;
    - malha importada com alvos por grupo e por seleção;
    - seleções de uma geração anterior (`StaleSelection`), deixadas de
      fora;
    - grupo inexistente;
  - depois o resto da montagem dos alvos.
  Arquivos: `build.cpp`, `tests/unit/setup/test_build.cpp`. Cobre: 08, 12,
  13.
- [x] **T10** Ações, casos e combinações:
  - testes primeiro:
    - um caso com cada ação do `core` (nodal com momento e bimomento,
      distribuída local parcial trapezoidal com torque e ponto,
      concentrada, peso próprio, ΔT, gradiente, deformação, recalque),
      montado = feito à mão (análise linear em 1e−12);
    - a combinação 1,4G + 1,5Q com a segunda ordem igual à das ações
      somadas à mão;
    - recalques somados por (nó, GDL);
    - recalque sem apoio é erro;
  - depois as ações e a soma.
  Arquivos: `build.cpp`, `tests/unit/setup/test_build.cpp`. Cobre: 15, 16,
  17, 20.
- [x] **T10b** Categorias de ação por norma (design §5.10, ampliada na
  implementação a pedido do mantenedor):
  - testes primeiro:
    - cada categoria contra a tabela;
    - códigos únicos por norma;
    - `findCategory`;
    - o caso de carga com norma e categoria nas edições (criar, mudar,
      duplicar);
  - depois `LoadCase::standard` e `category`, e
    `actions.hpp/.cpp` como tabela de dados.
  Arquivos: `src/setup/include/fem/setup/document.hpp`,
  `src/setup/include/fem/setup/actions.hpp`, `actions.cpp`,
  `tests/unit/setup/test_actions.cpp`. Cobre: 26.
- [x] **T11** Conferência:
  - testes primeiro: `checkModel` com os problemas da montagem (com o
    item), os do `core::validate` (sem item) e o aviso de treliça por
    atribuição;
  - depois `checkModel`.
  Arquivos: `build.cpp`, `tests/unit/setup/test_build.cpp`. Cobre: 10, 21.
- [x] **T12** `bench_setup`: atribuir a 10⁵ entidades do pórtico do
  `bench_line_mesh`, desfazer e montar uma combinação de três casos. A
  medida em Release fica nas notas (metas: 1 s, 1 s, 2 s). Arquivos:
  `tests/perf/bench_setup.cpp`, `tests/CMakeLists.txt`. Cobre: N01.

## Fase 4 — App: controlador, árvore e painel

- [x] **T13** O MESH devolve o mapa:
  - `LineMeshJob::finished` com o `LineMeshMap`;
  - o `ModelController` com o documento, o `MeshContext` (geração na
    MESH e na importação, revisão) e as edições num `QUndoCommand` na pilha
    do desenho;
  - o teste do job confere o mapa.
  Arquivos: `src/app/line_mesh_job.*`, `model_controller.hpp/.cpp`,
  `drafting_controller.*`, `main_window.*`,
  `tests/unit/app/test_line_mesh_job.cpp`. Cobre: 13, 23.
- [x] **T14** A árvore do modelo:
  - `ModelTree` (`QAbstractItemModel`) num dock;
  - os grupos de itens, o caso ativo em negrito e os estados de alvo
    com ícone e dica;
  - o menu de contexto (novo, duplicar, renomear, apagar com a recusa,
    copiar cargas);
  - o duplo clique que ativa um caso;
  - o realce dos alvos no viewport;
  - um teste de app do `ModelTree` sobre um documento (linhas, papéis,
    atualização numa edição).
  Arquivos: `src/app/model_tree.hpp/.cpp`, `main_window.*`,
  `tests/unit/app/test_model_tree.cpp`. Cobre: 13, 14, 18, 19.
- [x] **T15** O painel de propriedades:
  - os descritores de campo de cada tipo de item, com grandeza e
    validação;
  - o formulário; cada edição como `changeItem`;
  - a unidade em cada campo;
  - a barra de status com o sistema;
  - o diálogo de unidades com o aviso do desenho;
  - teste de app dos descritores (leitura, escrita, recusa de valor
    inválido).
  Arquivos: `src/app/fields.hpp/.cpp`, `property_panel.hpp/.cpp`,
  `model_dialogs.*`, `tests/unit/app/test_fields.cpp`. Cobre: 02, 03, 19.
- [x] **T16** Seções no painel:
  - a tabela das propriedades do `core` e a mensagem dele quando
    inválida;
  - o `SectionView` (contorno, eixos principais, centroide, centro de
    cisalhamento, pontos de tensão);
  - o editor de paredes finas;
  - o polígono maciço a partir das polilinhas fechadas selecionadas.
  Arquivos: `src/app/section_view.*`, `thin_walled_table.*`,
  `property_panel.cpp`, `model_controller.cpp`. Cobre: 05, 06.
  Nota: a geometria do desenho (contornos, eixos, polígono a partir das
  polilinhas) ficou em `section_geometry.*`, sem widgets, testada em
  `test_section_geometry.cpp`; o comando é
  `ModelController::setSolidFromPolylines`, testado em
  `test_model_controller.cpp`. O desenho em si fica para o roteiro T23.

## Fase 5 — App: criar e atribuir

- [x] **T17** O menu Model, primeira parte:
  - New material (biblioteca), New section;
  - Assign bars (tipo, material, seção, teoria, orientação,
    empenamento, formulação), sobre a seleção do desenho ou da malha, ou
    os grupos sem seleção;
  - o aviso de treliça no painel.
  Arquivos: `src/app/model_dialogs.*`, `model_controller.cpp`,
  `main_window.cpp`. Cobre: 04, 05, 07, 10, 12.
  Nota: as seções novas saem com as medidas de um perfil comum nas
  unidades do modelo (`newSection`, em `section_geometry.*`); os alvos
  vêm de `ModelController::targetsFrom` (o desenho, ou os elementos e nós
  da malha do modelo) e, sem seleção, dos grupos 1D marcados no diálogo.
  Testados: `newSection`, `targetsFrom`, `meshGroups` e o aviso de
  treliça; os diálogos ficam para o roteiro T23.
- [x] **T18** Detalhes de viga e apoios: os diálogos de ligações (por ponta
  e esforço), excentricidades, base elástica, pontas de empenamento e
  apoios (engaste, articulação, rolete, por GDL). Arquivos:
  `model_dialogs.*`, `model_controller.cpp`. Cobre: 08, 15.
  Nota: um só diálogo (`chooseDetail`) monta o formulário pelos mesmos
  campos do painel (`fieldsOf`); o editor de cada campo saiu do painel
  para `field_editor.*`, usado pelos dois. Os atalhos de apoio são
  `supportOf` (engaste: translações e rotações; articulação:
  translações; rolete: a translação em Y global, vertical no plano do
  desenho), testados em `test_fields.cpp`. Apoios usam os grupos 0D sem
  seleção; os detalhes, os 1D.
- [x] **T19** Casos, cargas e combinações:
  - o diálogo dos casos (criar, renomear, ativar, classificar pela norma
    e pela categoria, mostrando os ψ);
  - Add load com cada ação, sobre a seleção, no caso ativo;
  - o diálogo das combinações (casos e fatores).
  Arquivos: `model_dialogs.*`, `model_controller.cpp`. Cobre: 16, 17, 18,
  26.
  Nota: o diálogo dos casos é a lista com o mesmo painel de propriedades
  (nome, norma, categoria, ψ), cada mudança um passo de desfazer. Add
  load usa o diálogo de detalhes, que agora refaz o formulário quando os
  campos mudam (posição em comprimento ou fração) e aceita cargas sem
  alvo (peso próprio, gravidade em −Y nas unidades do modelo). Sem caso,
  o primeiro é criado; com uma combinação ativa, a carga é recusada. As
  combinações se criam pelo menu e se editam pela árvore ("Edit
  factors..."). Testados: `newAction`, `loadOnNodes`, `termsOf`.

## Fase 6 — Símbolos no viewport

- [x] **T20** O `SymbolLayer` no `render`:
  - os moldes dos glifos (engaste, articulação, rolete, genérico, seta,
    arco, tríade);
  - o shader instanciado com tamanho constante na tela;
  - `Viewport::setSymbols` e `setSymbolsVisible`;
  - teste com o backend Null: o passe desenha, e as instâncias saem de um
    `SymbolSet`;
  - os moldes testados como geometria.
  Arquivos: `src/render/include/fem/render/symbols.hpp`,
  `symbol_layer.*`, `shaders/symbol.vert/.frag`, `viewport.*`,
  `scene_renderer.cpp`, `tests/unit/render/test_symbols.cpp`. Cobre: 22.
  Nota: oito moldes (engaste, articulação, rolete, genérico, seta, arco,
  tríade e o elo entre as pontas das setas distribuídas, cuja parte ao
  longo da barra fica em unidades do mundo). O glifo do apoio sai de
  `supportShape` (no plano só U, V e RZ contam; o rolete aponta para o
  GDL preso). Os símbolos vão por cima do modelo, sem teste de
  profundidade. O desenho na tela fica para a T21 e o roteiro T23.
- [ ] **T21** O `ModelController` monta o `SymbolSet`:
  - do caso ou da combinação ativa, com as posições da malha ou, sem
    malha, do desenho;
  - Show symbols e Show local axes (selecionadas ou todas);
  - refeito a cada edição, malha ou troca de caso.
  Arquivos: `model_controller.cpp`, `main_window.cpp`. Cobre: 09, 22.

## Fase 7 — Conferência e fechamento

- [ ] **T22** O dock da conferência (Check model): a lista com gravidade,
  texto e item. O clique seleciona o item na árvore, ou realça os
  elementos e nós de um problema do `core`. Arquivos:
  `src/app/check_panel.*`, `model_controller.cpp`, `main_window.cpp`.
  Cobre: 21.
- [ ] **T23** [M] Roteiro manual com o mantenedor (AC-6):
  1. Unidades kN-m: a barra de status; trocar para N-mm, conferir um
     material e uma carga convertidos, ver o aviso do desenho; desfazer.
  2. Desenhar o pórtico 2×2, pontos nas bases; criar aço e uma seção I
     (ver o desenho e as propriedades); atribuir vigas a todas as linhas;
     apoios engastados nos pontos; um caso com carga nodal e
     distribuída.
  3. MESH; a árvore mostra a malha; selecionar um item realça os alvos.
  4. Símbolos do caso ativo; ligar e desligar; eixos locais; girar a
     câmera e conferir o tamanho constante.
  5. Combinação de dois casos ativa: os símbolos somados.
  6. Apagar uma viga com atribuição: o aviso na árvore; desfazer.
  7. Check model com um apoio retirado: o mecanismo do `core` listado e
     realçado; um problema de item selecionando o item.
  8. Desfazer e refazer alternando desenho e modelo.
  9. FPS com o pórtico do `bench_line_mesh` e 10⁴ cargas (≥ 30).
  Cobre: 09, 19, 21, 22, 23, N01.
- [ ] **T24** Documentação: `architecture.md` (o `setup` feito, a §5 do
  `app` com a árvore, o painel e a conferência); README (o menu Model, as
  unidades, os casos e as combinações). Cobre: 01.
- [ ] **T25** Cobertura de `src/setup/` ≥ 80% no CI, num terceiro `gcovr`.
  Arquivo: `.github/workflows/ci.yml`. Cobre: N03.
- [ ] **T90** [M] Build e testes verdes nos jobs do CI, inclusive a
  cobertura de `core`, `mesher` e `setup`; push com sua confirmação.
  Cobre: N03.
- [ ] **T91** `clang-format` e `clang-tidy` sem pendências (também o do
  Linux); `core`, `geom`, `mesher` e `setup` sem headers Qt. Cobre: 01.
- [ ] **T92** Conferir os sete critérios de aceite.
- [ ] **T93** Atualizar `specs/roadmap.md` para `concluída`.

## Matriz de rastreabilidade

| Requisito | Tarefas | Testes |
|-----------|---------|--------|
| REQ-010-01 | T02, T04, T24, T91 | `test_document.cpp`, `check_no_qt_includes` |
| REQ-010-02 | T03, T15 | `test_units.cpp`, `test_fields.cpp` |
| REQ-010-03 | T05, T15 | `test_units.cpp`, roteiro T23 |
| REQ-010-04 | T06, T07, T17 | `test_editing.cpp`, `test_units.cpp` |
| REQ-010-05 | T06, T16, T17 | `test_editing.cpp`, roteiro T23 |
| REQ-010-06 | T16 | roteiro T23 |
| REQ-010-07 | T06, T08, T17 | `test_editing.cpp`, `test_build.cpp` |
| REQ-010-08 | T06, T09, T18 | `test_editing.cpp`, `test_build.cpp` |
| REQ-010-09 | T21, T23 | roteiro T23 |
| REQ-010-10 | T11, T17 | `test_build.cpp` |
| REQ-010-11 | T08 | `test_build.cpp` |
| REQ-010-12 | T08, T09, T17 | `test_build.cpp` |
| REQ-010-13 | T09, T13, T14 | `test_build.cpp`, `test_line_mesh_job.cpp`, `test_model_tree.cpp` |
| REQ-010-14 | T08, T14 | `test_build.cpp`, `test_model_tree.cpp` |
| REQ-010-15 | T06, T08, T10, T18 | `test_editing.cpp`, `test_build.cpp` |
| REQ-010-16 | T06, T10, T19 | `test_editing.cpp`, `test_build.cpp` |
| REQ-010-17 | T06, T10, T19 | `test_editing.cpp`, `test_build.cpp` |
| REQ-010-18 | T06, T14, T19 | `test_editing.cpp`, `test_model_tree.cpp` |
| REQ-010-19 | T14, T15, T23 | `test_model_tree.cpp`, `test_fields.cpp`, roteiro T23 |
| REQ-010-20 | T08, T10 | `test_build.cpp` |
| REQ-010-21 | T11, T22, T23 | `test_build.cpp`, roteiro T23 |
| REQ-010-22 | T20, T21, T23 | `test_symbols.cpp`, roteiro T23 |
| REQ-010-23 | T04, T06, T13, T23 | `test_document.cpp`, `test_editing.cpp`, roteiro T23 |
| REQ-010-24 | todas as de app | revisão |
| REQ-010-25 | T01 | `test_mesh.cpp`, suítes 003 a 009 |
| REQ-010-26 | T10b, T19 | `test_actions.cpp`, roteiro T23 |
| NFR-010-01 | T06, T12, T23 | `bench_setup`, roteiro T23 (FPS) |
| NFR-010-02 | T03, T05 | `test_units.cpp` |
| NFR-010-03 | T25, T90 | Job de cobertura no CI |

| Critério | Tarefas |
|----------|---------|
| AC-1 | T03, T05, T15, T23 |
| AC-2 | T06, T07, T16, T17, T23 |
| AC-3 | T08, T09, T17, T18, T21 |
| AC-4 | T08, T09, T13, T14 |
| AC-5 | T06, T10, T10b, T19 |
| AC-6 | T14, T15, T20, T21, T22, T23 |
| AC-7 | T01, T12, T23, T25, T90, T91 |

## Notas de implementação

- T06 (2026-11-23): `duplicateItem` recusa atribuições, detalhes e apoios
  (`EditRefusal::Unsupported`): dois itens da mesma espécie nos mesmos
  alvos não fazem sentido, porque o novo tiraria os alvos do outro. Os
  nomeados (material, seção, caso, combinação) e as cargas são duplicados.
  Ligação de extremidade para as duas pontas vira um item por ponta, como
  no design §5.2.
- T09 (2026-11-23): a montagem dos detalhes de viga e dos alvos por grupo
  e por seleção já tinha sido escrita na T08, junto com o resto da
  montagem. Por isso os testes da T09 passaram de primeira e não foram
  vistos falhando antes do código.
- T10 (2026-11-23): como a T09, os testes confirmaram o código da T08 sem
  fase vermelha própria. Duas falhas foram do teste, não da montagem:
  - o empenamento só existe onde as duas barras colineares se encontram;
    apoiar o GDL de empenamento na ponta livre foi recusado pelo `core`,
    com razão. O bimomento foi para um ponto no meio;
  - a soma dos recalques, comparada com `==`, passou a ser comparada com
    tolerância de 1e−15.
- T12 (2026-11-23, Release, máquina do mantenedor): o `bench_setup` usa o
  pórtico do `bench_line_mesh`, com 101 817 entidades (729 pilares, 33 696
  vigas), malha em 0,64 s (51 921 nós, 185 976 elementos). Medidas:
  - atribuir uma barra a todas as entidades: 0,011 s (meta: ≤ 1 s);
  - uma segunda atribuição que toma metade delas: 0,008 s;
  - desfazer e refazer: abaixo de 1 ms (meta: ≤ 1 s), porque só os itens
    mudados vão e voltam;
  - montar o `core::Model` de 1,35G + 1,5Q + 0,9W, com peso próprio, uma
    carga em cada viga e outra em cada pilar: 0,110 s, o melhor de três
    (meta: ≤ 2 s).
  Os FPS com os símbolos ficam para o roteiro da T23.
