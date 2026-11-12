# Tarefas — 004 mesh-rendering

Status: em andamento
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

- [ ] **T06** `display_options.hpp` e testes de `buildMeshScene`: faixas por
  dimensão, contagens de triângulos (quadriláteros viram 2) e linhas, posições
  relativas ao centro, cena de malha vazia — arquivos: `mesh_scene.hpp/.cpp`,
  `tests/unit/render/test_mesh_scene.cpp` — cobre: 09, 16, 18
- [ ] **T07** Implementar `buildMeshScene` até a T06 passar — cobre: 09, 16, 18
- [ ] **T08** Testes e implementação de `elementColors`: por dimensão; por
  grupo, com elemento em dois grupos (cor do primeiro) e elemento sem grupo
  (cinza) — cobre: 14, 15
- [ ] **T09** `[P]` Testes e implementação de `Selection`: acrescentar, retirar,
  limpar, trocar de modo, retirar elementos de uma dimensão — arquivos:
  `selection.hpp/.cpp`, `tests/unit/render/test_selection.cpp` — cobre: 24, 25,
  28
- [ ] **T10** Testes de `MeshPicker::pick`: elemento da frente num cubo de
  hexaedros em cada vista padrão; linha 1D dentro e fora da tolerância; ponto
  0D; nó escondido atrás de uma face não escolhido; dimensão oculta ignorada —
  arquivo: `tests/unit/render/test_mesh_picker.cpp` — cobre: 20, 30
- [ ] **T11** Implementar `MeshPicker::pick` até a T10 passar — arquivos:
  `mesh_picker.hpp/.cpp` — cobre: 20
- [ ] **T12** Testes e implementação de `MeshPicker::pickRect`: janela e
  cruzamento sobre malha 2D em vista superior; elemento parcialmente dentro só
  no cruzamento; retângulo dentro de um elemento grande; itens escondidos
  incluídos; nós — cobre: 21, 22, 30

## Fase 3 — Desenho (`render`, GPU)

- [ ] **T13** Shaders `mesh_face`, `mesh_line`, `mesh_point` e `mesh_gpu`:
  buffers e pipelines criados a partir de uma `MeshScene`; o teste do
  `SceneRenderer` com backend `Null` passa a desenhar uma cena com faces,
  linhas e pontos — arquivos: `src/render/shaders/mesh_*`, `mesh_gpu.hpp/.cpp`,
  `scene_renderer.cpp`, `test_scene_renderer.cpp` — cobre: 09, 12
- [ ] **T14** `Viewport::setScene`, deslocamento centro−alvo em `double` por
  quadro, "Enquadrar tudo" pela caixa da malha, sinal `sceneShown` — cobre: 05,
  17, 18
- [ ] **T15** Modos de exibição e visibilidade por dimensão (faixas desenhadas),
  viés de profundidade nas faces — cobre: 11, 13, 16
- [ ] **T16** Cores: buffer de cor reescrito ao trocar o modo de cor; arestas
  escuras no sólido com arestas e coloridas no aramado — cobre: 14, 15
- [ ] **T17** Seleção no `Viewport`: gesto de clique e de retângulo (limiar de 4
  px), Shift, Esc, chamada ao `MeshPicker`, realce pelo canal alfa e nós
  selecionados como pontos, sinal `selectionChanged`; ocultar dimensão limpa a
  seleção dela — cobre: 19 a 22, 24 a 28
- [ ] **T18** Retângulo de janela/cruzamento: widget filho transparente com
  `QPainter` (azul contínuo, verde tracejado) — arquivos:
  `selection_overlay.hpp/.cpp` — cobre: 23

## Fase 4 — Interface e importação (`app`)

- [ ] **T19** Testes de `parseCliOptions` com arquivo de malha; implementar
  `meshFile` — arquivos: `cli_options.hpp/.cpp`,
  `tests/unit/app/test_cli_options.cpp` — cobre: 03
- [ ] **T20** `MeshImport` (`QThread` com `readGmsh` + `buildMeshScene`, sinais
  de início, fim e falha) — arquivos: `mesh_import.hpp/.cpp` — cobre: 04
- [ ] **T21** `MainWindow`: Arquivo ▸ Importar malha (diálogo `.msh`), arrastar
  arquivo, arquivo da linha de comando, barra de progresso, ação desabilitada
  durante a importação, caixas de aviso e de erro (saída padrão no smoke
  test), barra de status com arquivo, nós, elementos e grupos — cobre: 01 a 08
- [ ] **T22** Menus: Exibir ▸ modos, cor e dimensões; Editar ▸ modo de seleção e
  Limpar seleção; contagem da seleção na barra de status — cobre: 11, 14, 16,
  19, 25, 27
- [ ] **T23** Smoke test com malha: sair com 0 depois de `sceneShown`;
  `femapp_smoke_mesh` com `box_tet4.msh` e backend `null` no `ctest` — arquivos:
  `main.cpp`, `tests/CMakeLists.txt` — cobre: 31
- [ ] **T24** Traduções pt_BR dos textos novos — arquivo:
  `i18n/femapp_pt_BR.ts` — cobre: 29
- [ ] **T25** Atualizar `specs/architecture.md` §4 (seleção na CPU, ADR 0003;
  `MeshScene`) e §3 (`extractSurface`) — cobre: 30

## Fase 5 — Conferência e medições

- [ ] **T26** `[M]` Roteiro manual no Windows: importar pelos três caminhos;
  arquivo com aviso e arquivo inválido; os três modos; cores por dimensão e
  por grupo; ocultar dimensões; seleção por clique, janela, cruzamento, Shift e
  Esc nos dois modos; realce; textos em português; malha deslocada 1e6 —
  cobre: 01 a 29
- [ ] **T27** Gerar a malha de 1 milhão de Tet4 deslocada 1e6 da origem para o
  roteiro (opção no script de exemplos) — cobre: 18
- [ ] **T28** `[M]` Medir em Release: FPS navegando com 1 milhão de Tet4 em
  1920×1080, tempo do diálogo até a malha aparecer, tempo de clique e de
  janela/cruzamento, consumo em repouso; registrar com o modelo da GPU — cobre:
  N01, N02, N03, N04

## Fase 6 — Fechamento

- [ ] **T90** Build e testes verdes nos quatro jobs do CI (push com sua
  confirmação) — cobre: 31
- [ ] **T91** `clang-format` e `clang-tidy` sem pendências (também o
  `clang-tidy` do Linux, pelo CI)
- [ ] **T92** Conferir os doze critérios de aceite de `requirements.md`
- [ ] **T93** Atualizar `specs/roadmap.md` para `concluída` e o `README.md` se
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
| REQ-004-19 | T17, T22 | Roteiro manual (AC-8) |
| REQ-004-20 | T10, T11, T17 | `test_mesh_picker`; roteiro manual (AC-8) |
| REQ-004-21 | T12, T17 | `test_mesh_picker`; roteiro manual (AC-8) |
| REQ-004-22 | T12, T17 | `test_mesh_picker`; roteiro manual (AC-8) |
| REQ-004-23 | T18, T26 | Roteiro manual (AC-8) |
| REQ-004-24 | T09, T17 | `test_selection`; roteiro manual (AC-8) |
| REQ-004-25 | T09, T17, T22 | `test_selection`; roteiro manual (AC-8) |
| REQ-004-26 | T17, T26 | Roteiro manual (AC-9) |
| REQ-004-27 | T17, T22 | Roteiro manual (AC-9) |
| REQ-004-28 | T09, T17 | `test_selection`; roteiro manual (AC-9) |
| REQ-004-29 | T24, T26 | Roteiro manual (AC-10) |
| REQ-004-30 | T03–T05, T10–T12, T25 | `test_mesh_surface`, `test_mesh_picker` (AC-4, AC-8) |
| REQ-004-31 | T23, T90 | `femapp_smoke_mesh` nos quatro jobs (AC-11) |
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
