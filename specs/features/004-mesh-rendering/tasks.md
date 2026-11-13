# Tarefas — 004 mesh-rendering

Status: concluída
Design: [design.md](design.md) · Requisitos: [requirements.md](requirements.md)

Regras:
- Uma tarefa = uma mudança pequena, compilável e testável.
- Testes vêm antes ou junto da implementação que verificam.
- `[P]` marca tarefas que podem ser feitas em paralelo com a anterior.
- `[M]` marca tarefas que dependem de ação do mantenedor.
- Marque `[x]` somente com build e testes passando.
- Os IDs abaixo omitem o prefixo: `01` significa `REQ-004-01`; `N01` significa
  `NFR-004-01`.

## Fase 1 — Casca externa (`core`)

- [x] **T01** Testes de `elementFaces`: cada face do elemento de referência de
  tetraedro, hexaedro, cunha e pirâmide tem normal para fora — arquivo:
  `tests/unit/core/test_mesh_surface.cpp` — cobre: 09
- [x] **T02** Implementar `elementFaces` (tabela do design §5.1) até a T01
  passar — arquivos: `mesh_surface.hpp/.cpp` — cobre: 09
- [x] **T03** Testes de `extractSurface`: 1 hexaedro (6 faces, 12 arestas);
  n³ hexaedros (6n² faces); malha mista à mão (hexaedro + pirâmide +
  tetraedro) sem face interna; elementos 2D e 1D; elementos de ordem 2 pelos
  vértices — cobre: 09, 10, 30
- [x] **T04** Implementar `extractSurface` (chaves ordenadas, faces que aparecem
  uma vez, arestas únicas por classe) até a T03 passar — cobre: 09, 10, 30
- [x] **T05** Teste com os exemplos `box_tet4`, `box_wedge6` e `box_hex20` da
  feature 003: área 22, volume 6 pelo teorema da divergência, cada aresta em
  exatamente 2 faces — cobre: 09, 10, 30

## Fase 2 — Cena, cores e seleção (`render`, sem GPU)

- [x] **T06** `display_options.hpp` e testes de `buildMeshScene`: faixas por
  dimensão, contagens de triângulos (quadriláteros viram 2) e linhas, posições
  relativas ao centro, cena de malha vazia — arquivos: `mesh_scene.hpp/.cpp`,
  `tests/unit/render/test_mesh_scene.cpp` — cobre: 09, 16, 18
- [x] **T07** Implementar `buildMeshScene` até a T06 passar — cobre: 09, 16, 18
- [x] **T08** Testes e implementação de `elementColors`: por dimensão; por
  grupo, com elemento em dois grupos (cor do primeiro) e elemento sem grupo
  (cinza) — cobre: 14, 15
- [x] **T09** `[P]` Testes e implementação de `Selection`: acrescentar, retirar,
  limpar, trocar de modo, retirar elementos de uma dimensão — arquivos:
  `selection.hpp/.cpp`, `tests/unit/render/test_selection.cpp` — cobre: 24, 25,
  28
- [x] **T10** Testes de `MeshPicker::pick`: elemento da frente num cubo de
  hexaedros em cada vista padrão; linha 1D dentro e fora da tolerância; ponto
  0D; nó escondido atrás de uma face não escolhido; dimensão oculta ignorada —
  arquivo: `tests/unit/render/test_mesh_picker.cpp` — cobre: 20, 30
- [x] **T11** Implementar `MeshPicker::pick` até a T10 passar — arquivos:
  `mesh_picker.hpp/.cpp` — cobre: 20
- [x] **T12** Testes e implementação de `MeshPicker::pickRect`: janela e
  cruzamento sobre malha 2D em vista superior; elemento parcialmente dentro só
  no cruzamento; retângulo dentro de um elemento grande; itens escondidos
  incluídos; nós — cobre: 21, 22, 30

## Fase 3 — Desenho (`render`, GPU)

- [x] **T13** Shaders `mesh_face`, `mesh_line`, `mesh_point` e `mesh_gpu`:
  buffers e pipelines criados a partir de uma `MeshScene`; o teste do
  `SceneRenderer` com backend `Null` passa a desenhar uma cena com faces,
  linhas e pontos — arquivos: `src/render/shaders/mesh_*`, `mesh_gpu.hpp/.cpp`,
  `scene_renderer.cpp`, `test_scene_renderer.cpp` — cobre: 09, 12
- [x] **T14** `Viewport::setScene`, deslocamento centro−alvo em `double` por
  quadro, "Enquadrar tudo" pela caixa da malha, sinal `sceneShown` — cobre: 05,
  17, 18
- [x] **T15** Modos de exibição e visibilidade por dimensão (faixas desenhadas),
  viés de profundidade nas faces — cobre: 11, 13, 16
- [x] **T16** Cores: buffer de cor reescrito ao trocar o modo de cor; arestas
  escuras no sólido com arestas e coloridas no aramado — cobre: 14, 15
- [x] **T17** Seleção no `Viewport`: gesto de clique e de retângulo (limiar de 4
  px), Shift, Esc, chamada ao `MeshPicker`, realce pelo canal alfa e nós
  selecionados como pontos, sinal `selectionChanged`; ocultar dimensão limpa a
  seleção dela — cobre: 19 a 22, 24 a 28
- [x] **T18** Retângulo de janela/cruzamento: widget filho transparente com
  `QPainter` (azul contínuo, verde tracejado) — arquivos:
  `selection_overlay.hpp/.cpp` — cobre: 23

## Fase 4 — Interface e importação (`app`)

- [x] **T19** Testes de `parseCliOptions` com arquivo de malha; implementar
  `meshFile` — arquivos: `cli_options.hpp/.cpp`,
  `tests/unit/app/test_cli_options.cpp` — cobre: 03
- [x] **T20** `MeshImport` (`QThread` com `readGmsh` + `buildMeshScene`, sinais
  de início, fim e falha) — arquivos: `mesh_import.hpp/.cpp` — cobre: 04
- [x] **T21** `MainWindow`: Arquivo ▸ Importar malha (diálogo `.msh`), arrastar
  arquivo, arquivo da linha de comando, barra de progresso, ação desabilitada
  durante a importação, caixas de aviso e de erro (saída padrão no smoke
  test), barra de status com arquivo, nós, elementos e grupos — cobre: 01 a 08
- [x] **T22** Menus: Exibir ▸ modos, cor e dimensões; Editar ▸ modo de seleção e
  Limpar seleção; contagem da seleção na barra de status — cobre: 11, 14, 16,
  19, 25, 27
- [x] **T23** Smoke test com malha: sair com 0 depois de `sceneShown`;
  `femapp_smoke_mesh` com `box_tet4.msh` e backend `null` no `ctest` — arquivos:
  `main.cpp`, `tests/CMakeLists.txt` — cobre: 31
- [x] **T24** Traduções pt_BR dos textos novos — arquivo:
  `i18n/femapp_pt_BR.ts` — cobre: 29
- [x] **T25** Atualizar `specs/architecture.md` §4 (seleção na CPU, ADR 0003;
  `MeshScene`) e §3 (`extractSurface`) — cobre: 30

## Fase 4b — Órbita pelo centro da tela (pedido do mantenedor)

- [x] **T29** Testes e implementação de `Camera::setPivot`: troca o alvo para
  um ponto sobre a linha de visada central sem mudar a imagem (perspectiva e
  ortográfica) — arquivos: `camera.hpp/.cpp`, `test_camera.cpp` — cobre:
  REQ-002-13 (alterado)
- [x] **T30** Testes e implementação de `MeshPicker::surfacePoint` (ponto da
  malha visível sob um pixel); no `Viewport`, ao começar a órbita, pivô no
  ponto do centro da tela: malha, senão plano Z = 0, senão alvo atual —
  arquivos: `mesh_picker.hpp/.cpp`, `viewport.cpp`, `test_mesh_picker.cpp` —
  cobre: REQ-002-13 (alterado)

## Fase 4c — Faces e arestas de sólidos, tipos combináveis e nós (design §12)

- [x] **T31** Tipos de seleção: `SelectionKind`, `SelectionFilter` e
  `DisplayOptions::showNodes`; `Selection` com um conjunto por tipo
  (`SelectionItems`, `count(kind)`, `total`, `clear(kind)`,
  `removeElementsOfDimension` com 3D retirando faces e arestas). Testes em
  `test_mesh_scene.cpp` (onde já estão os de `Selection`): tipos misturados,
  contagem por tipo, `clear(kind)`, ocultar 3D. `SelectionMode` sai; o
  `Viewport`, o `MeshGpu` e a `MainWindow` passam a usar o tipo `Element`/`Node`
  sem mudar o comportamento ainda — arquivos: `display_options.hpp`,
  `selection.hpp/.cpp`, chamadas existentes — cobre: 19, 24, 25, 28, 33
- [x] **T32** `MeshScene::triangleFaces()` com teste: bloco 3×3×3 de hexaedros
  com 54 faces de casca e 108 triângulos, dois por face, e o índice de segmento
  de `lines(Edges3d)` batendo com `surface().edges3d` — arquivos:
  `mesh_scene.hpp/.cpp`, `test_mesh_scene.cpp` — cobre: 32
- [x] **T33** Testes de `MeshPicker::pick` com filtro: só faces → face central
  da frente; faces e elementos → a face; só elementos → elemento (1,0,1);
  arestas → clique perto do meio de uma aresta da frente; nós e arestas →
  clique num vértice pega o nó; aresta de trás não escolhida — arquivo:
  `test_mesh_picker.cpp` — cobre: 20, 32
- [x] **T34** Implementar o clique por prioridade (nó → aresta → face →
  elemento; `PickedItem`; `closestTriangle` com o índice do triângulo; até 16
  candidatos de aresta) até a T33 passar; adaptar os testes antigos de `pick`
  ao filtro — arquivos: `mesh_picker.hpp/.cpp` — cobre: 20, 32
- [x] **T35** Testes e implementação de `pickRect` com filtro e
  `SelectionItems`: janela em volta de um hexaedro com faces e arestas → 6 e
  12; cruzamento pequeno no meio da frente → só a face da frente; 3D oculto →
  nada; testes antigos adaptados — cobre: 21, 22, 32
- [x] **T36** `Viewport`: `setSelectionKindEnabled` (desligar limpa o tipo;
  último ligado não desliga), `selectionFilter()`, sinal `selectionChanged()`,
  clique e retângulo com o filtro — arquivos: `viewport.hpp/.cpp` — cobre: 19
  a 22, 24, 28, 33
- [x] **T37** Desenho: faces de sólido selecionadas com o bit de realce;
  shader `mesh_segment.vert` e arestas selecionadas de 3 px no passe de
  sobreposição (instância descartada com `w ≤ 0`); "Mostrar nós" com o
  pipeline de pontos, cor `kNodeColor`, refeito ao mudar `showNodes` ou a
  visibilidade; `Viewport::setNodesShown`. O teste do `SceneRenderer` com
  backend `Null` passa a desenhar faces, arestas e nós selecionados e nós
  mostrados — arquivos: `mesh_gpu.hpp/.cpp`, `shaders/mesh_segment.vert`,
  `src/render/CMakeLists.txt`, `viewport.cpp`, `test_scene_renderer.cpp` —
  cobre: 26, 34, 35
- [x] **T38** `MainWindow`: Editar ▸ Selecionar com quatro ações marcáveis,
  Exibir ▸ Mostrar nós, barra de status com contagem por tipo (`%n`) — arquivos:
  `main_window.hpp/.cpp` — cobre: 19, 27, 34
- [x] **T39** Traduções pt_BR dos textos novos — arquivo:
  `i18n/femapp_pt_BR.ts` — cobre: 29
- [x] **T40** `clang-format`, `clang-tidy` e verificação de que `core` segue sem
  Qt; atualizar `specs/architecture.md` se a seleção por tipo mudar algo
  descrito lá — cobre: 30

- [x] **T41** Clique sem Shift que não escolhe nenhum item limpa a seleção
  (pedido do mantenedor); retângulo vazio não limpa — arquivo: `viewport.cpp`
  — cobre: 25 (alterado)
- [x] **T42** `MeshScene`: `edgeCount`, `edge`, `edgeDimension` (arestas 2D e de
  sólido) e `surfaceNodes` com os nós intermediários (teste no elemento de
  referência), com testes: Tet10 → 10, Hex27 → 26, dois Hex27 colados → 42 —
  arquivos: `mesh_scene.hpp/.cpp`, `test_mesh_scene.cpp` — cobre: 32, 34
  (design §13)
- [x] **T43** `Selection` e `MeshPicker` com as arestas 2D (clique e
  retângulo; ocultar 2D retira só as arestas 2D), com testes — arquivos:
  `selection.hpp/.cpp`, `mesh_picker.cpp`, `viewport.cpp`, testes — cobre: 19,
  20, 21, 22, 28, 32
- [x] **T44** GPU: arestas selecionadas pelo índice novo e arestas visíveis dos
  elementos selecionados como segmentos grossos — arquivo: `mesh_gpu.cpp` —
  cobre: 26
- [x] **T45** Menu "Arestas" e tradução — arquivos: `main_window.cpp`,
  `femapp_pt_BR.ts` — cobre: 19, 29
- [x] **T46** Zoom ancorado no ponto da malha sob o cursor (a câmera não
  atravessa mais o sólido e a seleção continua funcionando de perto), com
  teste de regressão; ordem do clique trocada para elemento → face → aresta →
  nó (pedidos do mantenedor) — arquivos: `camera.hpp/.cpp`, `viewport.cpp`,
  `mesh_picker.hpp/.cpp`, `test_mesh_picker.cpp` — cobre: REQ-002-14
  (alterado), 20 (alterado)

- [x] **T47** Ortográfica e grade desligada por padrão, com as linhas dos
  eixos sempre visíveis (flag no uniforme da grade) — arquivos: `camera.hpp`,
  `viewport.cpp`, `scene_renderer.cpp`, `grid.frag`, `main_window.cpp`,
  `test_camera.cpp` — cobre: REQ-002-19, 22, 26 (alterados)
- [x] **T48** `planarView` com testes e vista automática ao abrir malha plana
  — arquivos: `mesh_scene.hpp/.cpp`, `viewport.cpp`, `test_mesh_scene.cpp` —
  cobre: 36

- [x] **T49** Clique num tet com triângulo 2D de fronteira em cima (grupos
  físicos de superfície do Gmsh): o sólido vence o empate de profundidade, como
  é ele que aparece na tela; antes o triângulo era escolhido e o realce dele
  ficava escondido pela face do tet (defeito relatado pelo mantenedor) —
  arquivos: `mesh_picker.cpp`, `test_mesh_picker.cpp` — cobre: 20, 26

- [x] **T50** `extractSurface` e `buildMeshScene` com filtro de elementos, com
  testes — arquivos: `mesh_surface.hpp/.cpp`, `mesh_scene.hpp/.cpp`,
  `test_mesh_surface.cpp`, `test_mesh_scene.cpp` — cobre: 37
- [x] **T51** `MeshGpu`: `indexScene`, modo isolado (sem realce de elemento,
  faces selecionadas num buffer à parte) — arquivos: `mesh_gpu.hpp/.cpp` —
  cobre: 37
- [x] **T52** `Viewport`: `setIsolated`, cena e picker isolados, só retirar,
  desligar com seleção vazia, sinal `isolationChanged` — arquivos:
  `viewport.hpp/.cpp` — cobre: 37, 38, 39
- [x] **T53** Menu Exibir ▸ Mostrar só a seleção e tradução — arquivos:
  `main_window.cpp`, `femapp_pt_BR.ts` — cobre: 29, 37

- [x] **T54** `Camera::orbitAbout` (no lugar de `setPivot`) e
  `MeshPicker::visibleCenter`, com testes; pivô no `Viewport` — arquivos:
  `camera.hpp/.cpp`, `mesh_picker.hpp/.cpp`, `viewport.cpp`, testes — cobre:
  REQ-002-13 (alterado)
- [x] **T55** Contorno dos elementos selecionados com teste de profundidade —
  arquivo: `mesh_gpu.cpp` — cobre: 26
- [x] **T56** Modo isolado sem realce, com arestas e nós forçados, exigindo
  elemento selecionado — arquivos: `mesh_gpu.cpp`, `viewport.cpp` — cobre: 37,
  39

- [x] **T57** Arestas e nós selecionados com teste de profundidade (sai o
  passe de sobreposição) — arquivos: `mesh_gpu.hpp/.cpp`,
  `scene_renderer.cpp` — cobre: 26

- [x] **T58** Só elementos ligado por padrão; clique de volta à ordem nó →
  aresta → face → elemento (pedido do mantenedor) — arquivos:
  `display_options.hpp`, `mesh_picker.hpp/.cpp`, `test_mesh_picker.cpp` —
  cobre: 19, 20

- [x] **T59** Face de sólido realçada quando o elemento 2D que a cobre está
  selecionado (`coveringElement`), com teste — arquivos: `mesh_scene.hpp/.cpp`,
  `mesh_gpu.cpp`, `test_mesh_scene.cpp` — cobre: 26

## Fase 5 — Conferência e medições

- [x] **T26** `[M]` Roteiro manual no Windows: importar pelos três caminhos;
  arquivo com aviso e arquivo inválido; os três modos; cores por dimensão e
  por grupo; ocultar dimensões; seleção por clique, janela, cruzamento, Shift e
  Esc com os quatro tipos de seleção, sozinhos e combinados; realce de
  elementos, faces, arestas e nós; contagem por tipo; "Mostrar nós" no sólido
  e no aramado; textos em português; malha deslocada 1e6; órbita pelo centro
  da tela — cobre: 01 a 29, 32 a 35
- [x] **T27** Gerar a malha de 1 milhão de Tet4 deslocada 1e6 da origem para o
  roteiro (opção no script de exemplos) — cobre: 18
- [x] **T28** `[M]` Medir em Release: FPS navegando com 1 milhão de Tet4 em
  1920×1080, tempo do diálogo até a malha aparecer, tempo de clique e de
  janela/cruzamento (com os quatro tipos ligados), consumo em repouso; registrar com o modelo da GPU — cobre:
  N01, N02, N03, N04

## Fase 6 — Fechamento

- [x] **T90** Build e testes verdes nos quatro jobs do CI (push com sua
  confirmação) — cobre: 31
- [x] **T91** `clang-format` e `clang-tidy` sem pendências (também o
  `clang-tidy` do Linux, pelo CI)
- [x] **T92** Conferir os quinze critérios de aceite de `requirements.md`
- [x] **T93** Atualizar `specs/roadmap.md` para `concluída` e o `README.md` se
  algo do uso tiver mudado

## Matriz de rastreabilidade

| Requisito | Tarefas | Testes |
|-----------|---------|--------|
| REQ-004-01 | T21, T26 | Roteiro manual (AC-1) |
| REQ-004-02 | T21, T26 | Roteiro manual (AC-1) |
| REQ-004-03 | T19, T21, T23 | `test_cli_options`; `femapp_smoke_mesh` (AC-1, AC-11) |
| REQ-004-04 | T20, T21, T26 | Roteiro manual com 1 milhão de Tet4 (AC-2) |
| REQ-004-05 | T14, T21 | `femapp_smoke_mesh`; roteiro manual (AC-1) |
| REQ-004-06 | T21, T26 | Roteiro manual com `extra_sections.msh` (AC-3) |
| REQ-004-07 | T21, T26 | Roteiro manual com arquivo inválido (AC-3) |
| REQ-004-08 | T21, T26 | Roteiro manual (AC-1) |
| REQ-004-09 | T01–T07, T13 | `test_mesh_surface`, `test_mesh_scene`, `test_scene_renderer` (AC-4, AC-5) |
| REQ-004-10 | T03–T05 | `test_mesh_surface` (AC-4) |
| REQ-004-11 | T15, T22, T26 | Roteiro manual (AC-5) |
| REQ-004-12 | T13, T26 | Roteiro manual (AC-5) |
| REQ-004-13 | T15, T26 | Roteiro manual (AC-5) |
| REQ-004-14 | T08, T16, T22 | `test_mesh_scene`; roteiro manual (AC-6) |
| REQ-004-15 | T08, T16 | `test_mesh_scene` (AC-6) |
| REQ-004-16 | T06, T07, T15, T22 | `test_mesh_scene`; roteiro manual (AC-6) |
| REQ-004-17 | T14, T26 | Roteiro manual (AC-7) |
| REQ-004-18 | T06, T07, T14, T27 | `test_mesh_scene`; roteiro manual (AC-7) |
| REQ-004-19 | T17, T22, T31, T36, T38 | `test_mesh_scene` (Selection); roteiro manual (AC-8, AC-13) |
| REQ-004-20 | T10, T11, T17, T33, T34, T36 | `test_mesh_picker`; roteiro manual (AC-8, AC-13) |
| REQ-004-21 | T12, T17, T35, T36 | `test_mesh_picker`; roteiro manual (AC-8, AC-14) |
| REQ-004-22 | T12, T17, T35, T36 | `test_mesh_picker`; roteiro manual (AC-8, AC-14) |
| REQ-004-23 | T18, T26 | Roteiro manual (AC-8) |
| REQ-004-24 | T09, T17, T31, T36 | `test_selection`; roteiro manual (AC-8) |
| REQ-004-25 | T09, T17, T22, T31, T41 | `test_selection`; roteiro manual (AC-8) |
| REQ-004-26 | T17, T26, T37, T44 | `test_scene_renderer`; roteiro manual (AC-9, AC-14) |
| REQ-004-27 | T17, T22, T38 | Roteiro manual (AC-9, AC-14) |
| REQ-004-28 | T09, T17, T31, T36 | `test_selection`; roteiro manual (AC-9) |
| REQ-004-29 | T24, T26, T39 | Roteiro manual (AC-10) |
| REQ-004-30 | T03–T05, T10–T12, T25 | `test_mesh_surface`, `test_mesh_picker` (AC-4, AC-8) |
| REQ-004-31 | T23, T90 | `femapp_smoke_mesh` nos quatro jobs (AC-11) |
| REQ-004-32 | T32–T35, T42, T43 | `test_mesh_scene`, `test_mesh_picker` (AC-13) |
| REQ-004-33 | T31, T36 | `test_mesh_scene` (Selection) (AC-13) |
| REQ-004-34 | T31, T37, T38, T42, T26 | `test_scene_renderer`; roteiro manual (AC-15) |
| REQ-004-35 | T37, T26 | Roteiro manual (AC-15) |
| REQ-004-36 | T48 | `test_mesh_scene`; roteiro manual |
| REQ-004-37 | T50–T53 | `test_mesh_surface`, `test_mesh_scene`; roteiro manual |
| REQ-004-38 | T52 | Roteiro manual |
| REQ-004-39 | T52 | Roteiro manual |
| NFR-004-01 | T28 | Medição manual (AC-12) |
| NFR-004-02 | T28 | Medição manual (AC-2, AC-12) |
| NFR-004-03 | T28 | Medição manual (AC-12) |
| NFR-004-04 | T28 | Medição manual (AC-12) |

## Notas de implementação

**2026-11-12 — Fase 1 (T01–T05):**

- 86 testes passando no Windows. A tabela de faces do design §5.1 está certa:
  normais para fora nos elementos de referência das quatro famílias, e as
  caixas da feature 003 (Tet4, Wedge6, Hex20) dão área 22, volume 6 pelo
  teorema da divergência e cada aresta em exatamente duas faces.
- Desvio: o teste da T05 ficou em `tests/unit/io/test_surface_of_samples.cpp`,
  junto dos testes de `io`, porque lê os arquivos `.msh` e o executável de
  testes de `core` não liga com `io`.
- Testes e implementação compilados juntos, como nas features anteriores.

**2026-11-12 — Fase 2 (T06–T12):**

- 98 testes passando no Windows. Clique acerta o elemento da frente nas seis
  vistas padrão, em perspectiva e ortográfica; linhas e pontos dentro da
  tolerância; nó atrás de uma face não é escolhido; janela e cruzamento numa
  grade 4×4 dão 1 e 9 quadriláteros; janela em volta de um bloco 3×3×3 pega os
  27 hexaedros, inclusive o interno.
- Desvios do design §4: `Selection::reset` recebe a malha (e não as
  contagens); `MeshScene` expõe também `nodePositions()` e `pointElements()`,
  usados pela seleção e pelos pontos; testes do `Selection` ficaram em
  `test_mesh_scene.cpp`, não num arquivo próprio.
- Nós de ordem alta (meio de aresta etc.) não são clicáveis, porque não são
  desenhados; a janela e o cruzamento os alcançam.

**2026-11-12 — Fases 3 e 4 (T13–T25):**

- 101 testes passando no Windows, incluindo o `SceneRenderer` com backend
  `Null` desenhando uma malha com faces, linhas, pontos e seleção nos três
  modos, e o novo `femapp_smoke_mesh` (`box_tet4.msh`, mensagem "mesh
  rendered").
- Conferido pela linha de comando: arquivo com tipo não suportado mostra o
  aviso; arquivo inválido mostra o erro com a linha e o smoke test termina em
  "mesh import failed".
- **Não verificado visualmente:** aparência das faces, arestas, pontos, cores,
  realce e retângulo de seleção; isso é o roteiro manual (T26).
- Desvios do design:
  - o arquivo da linha de comando é o primeiro argumento terminado em `.msh`
    (sem diferenciar maiúsculas), e não "o primeiro que não é opção", porque
    o Qt aceita opções com valor (`-platform offscreen`);
  - as funções auxiliares de QRhi saíram do `scene_renderer.cpp` para
    `rhi_util.hpp/.cpp`, compartilhadas com `mesh_gpu.cpp`;
  - a malha e a grade usam o intervalo de profundidade acima da fatia do
    indicador de eixos (0,01 a 1), o que resolve a pendência anotada na
    feature 002 (indicador escondido por objetos muito próximos da câmera);
  - os textos de contagem da seleção usam plural do Qt (`%n`); sem tradução
    para inglês, aparecem como "3 element(s) selected".
- T25: `specs/architecture.md` atualizado (`extractSurface`, `MeshScene`,
  seleção na CPU).

**2026-11-12 — T27 e T91:**

- T27: `generate_test_meshes.py --large <pasta>` grava também
  `box_shifted_1e6` (caixa 1×2×3 deslocada 1e6 em cada eixo), fora do
  repositório, junto da caixa de 1 milhão de Tet4.
- T91: `clang-format` sem diferenças e `clang-tidy` (Windows) sem apontamentos.
  Para isso a importação deixou de usar `QMetaObject::invokeMethod` (falso
  positivo de vazamento dentro do Qt): o resultado volta pela conexão com
  `QThread::finished`. O `clang-tidy` do Linux só será conferido no CI.

**2026-11-12 — Fase 4b (T29, T30):** órbita em torno do ponto no centro da
tela (`Camera::setPivot`, `MeshPicker::surfacePoint`); 104 testes passando.

**2026-11-12 — Fase 4c (T31–T40):**

- 107 testes passando no Windows; `clang-format` e `clang-tidy` sem
  pendências; `check_no_qt_includes` verde.
- Conferido por teste: clique com prioridade nó → aresta → face → elemento no
  bloco 3×3×3 (perspectiva e ortográfica); aresta de trás não escolhida; janela
  em volta de um hexaedro com faces e arestas dá 6 e 12; cruzamento pequeno no
  meio da frente dá 2 faces (a da frente e a de trás, escondida, como manda o
  REQ-004-22 — o design §12.8 dizia "só a da frente", o que contrariava o
  requisito); o `SceneRenderer` com backend `Null` desenha faces, arestas e
  nós selecionados e os nós mostrados nos três modos.
- **Não verificado visualmente:** realce de faces, arestas grossas, "Mostrar
  nós" e o submenu Selecionar; isso fica no roteiro manual (T26).
- Desvios do design §12:
  - `setSelectionKindEnabled` é um método que devolve `bool` (falso ao tentar
    desligar o último tipo), não um slot; a `MainWindow` remarca a ação;
  - a barra de status mostra "Selecionados: 2 elementos, 5 faces", com o
    plural por tipo;
  - a cor dos nós mostrados é magenta, não laranja escuro (o realce já é
    laranja); corrigido no design;
  - o `Viewport` passa ao `MeshGpu` um `nodeVersion`, que muda com "Mostrar
    nós" e com a visibilidade das dimensões.

**2026-11-12 — Pedidos do mantenedor (T41–T45):**

- Clique fora da malha limpa a seleção (T41); os quatro tipos de seleção e
  "Mostrar nós" (em preto) passaram a vir ligados por padrão.
- T42–T45: arestas de elementos 2D no tipo "Arestas" (menu renomeado), arestas
  visíveis dos elementos selecionados realçadas em linha grossa e nós
  intermediários dos elementos de ordem alta mostrados e clicáveis (no 3D, só
  os que ficam sobre a casca, achados pelo elemento de referência). 110 testes
  passando; `clang-tidy` sem apontamentos.
- **Não verificado visualmente:** fica no roteiro manual (T26).

**2026-11-12 — Desempenho com 1 milhão de Tet4 (apoio à T28):**

- Novo `tests/perf/bench_mesh_scene` (fora do ctest), que mede cada etapa.
  Na máquina do mantenedor (`box_1m_tet4_bin.msh`, 1920×1080):

  | Etapa | Release | Debug |
  |-------|---------|-------|
  | Leitura | 0,42 s | 5,8 s |
  | Casca externa | 0,94 s | 16,6 s |
  | Cena completa (inclui a casca) | 1,26 s | 19,6 s |
  | Clique (elementos / todos os tipos) | 0,002 / 0,004 s | 0,11 / 0,31 s |
  | Janela, elementos (516 mil) | 0,04 s | 1,4 s |
  | Cruzamento, todos os tipos | 0,12 s | 9,0 s |

- `femapp --smoke-test` com a malha, até o primeiro quadro: 2,4 s em Release,
  27,5 s em Debug. As metas do NFR-004-02 e 03 valem para Release; o build
  `dev` (Debug do MSVC, com iteradores verificados) é de 10 a 20 vezes mais
  lento e não serve para medir.

**2026-11-12 — T26, T28 e T92:**

- T26: roteiro manual no Windows aprovado pelo mantenedor ("tudo passou"),
  com as mudanças pedidas ao longo da conferência (T29–T59).
- T28: medições em Release aprovadas pelo mantenedor na NVIDIA GeForce RTX
  2060: navegação com 1 milhão de Tet4 dentro da meta de 30 FPS e sem consumo
  de CPU e GPU em repouso (os valores exatos não foram anotados); abertura em
  2,4 s e seleção dentro das metas (tabela acima).
- T92: critérios AC-1 a AC-10 e AC-12 a AC-15 conferidos (testes automáticos,
  roteiro e medições). AC-11 (smoke test com malha nos quatro jobs) depende do
  CI, depois do push (T90).

**2026-11-12 — T90 e T93:** CI verde nos cinco jobs (Windows, macOS,
linux-gcc, linux-clang com clang-tidy e formatação) no commit `7b13051`,
depois de corrigir um falso positivo do GCC 13 em `std::sort` parcial, a
fresta na diagonal dos quadriláteros vista no macOS (ARM) e dois apontamentos
do clang-tidy do Linux. AC-11 conferido. README atualizado; feature
concluída no roadmap.

