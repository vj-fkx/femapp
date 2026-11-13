# Tarefas — 005 drafting-basics

Status: em andamento
Design: [design.md](design.md) · Requisitos: [requirements.md](requirements.md)

Regras:
- Uma tarefa = uma mudança pequena, compilável e testável.
- Testes vêm antes ou junto da implementação que verificam.
- `[P]` marca tarefas que podem ser feitas em paralelo com a anterior.
- `[M]` marca tarefas que dependem de ação do mantenedor.
- Marque `[x]` somente com build e testes passando.
- Os IDs abaixo omitem o prefixo: `01` significa `REQ-005-01`; `N01` significa
  `NFR-005-01`.

## Fase 1 — Documento (`geom`)

- [x] **T01** Testes de `Drawing` e `DrawingEdit`: inserir, achar, remover;
  IDs crescentes e nunca reutilizados (também depois de desfazer); `apply` e
  `revert` de criar, mover e apagar restauram estado e IDs; `append`;
  `revision`; `bounds`; `tolerance` — arquivo:
  `tests/unit/geom/test_drawing.cpp` — cobre: 01, 02, 31, 32
- [x] **T02** Implementar `drawing.hpp/.cpp` (entidades, `Drawing`,
  `DrawingEdit`, `segmentsOf`, `translated`) até a T01 passar; registrar os
  arquivos no `CMakeLists.txt` de `geom` e o teste em `tests/CMakeLists.txt` —
  cobre: 01, 02, 03, 31, 32

## Fase 2 — Linha de comando e pontos (`geom`)

- [x] **T03** `[P]` Testes de `parseInput`, `findCommand`, `completeCommand` e
  `resolvePoint`: absolutos 2D/3D, relativos, polares absolutos e relativos
  (0°, 90°, 225°, ângulos negativos), distância com direção, expoentes,
  espaços, maiúsculas, atalhos, prefixos e entradas inválidas — arquivo:
  `test_command_input.cpp` — cobre: 05, 08, 10, 11, 12, 13
- [x] **T04** Implementar `command_input.hpp/.cpp` até a T03 passar — cobre:
  05, 08, 10 a 13
- [x] **T05** Testes do `PointResolver`: extremidade, meio e interseção dentro
  e fora da abertura; interseção de segmentos 3D que se cruzam e que passam
  perto sem se tocar; desempate entre modos; modos desligados; snap > grade >
  plano; grade com passo 0,5; ortogonal em X e em Y; plano na altura do último
  ponto; raio paralelo ao plano sem ponto — arquivo: `test_point_resolver.cpp`
  — cobre: 14, 19, 22, 23
- [x] **T06** Implementar `point_resolver.hpp/.cpp` (cache por revisão) até a
  T05 passar — cobre: 14, 19, 22, 23

## Fase 3 — Comandos (`geom`)

- [x] **T07** Testes da `CommandSession` para desenho: POINT; LINE com U, C
  (e C com menos de dois trechos), Enter e Esc; PLINE aberta, fechada e com
  um só vértice; pré-visualização em cada passo; um `DrawingEdit` por comando;
  repetição com Enter vazio; comando novo cancela o ativo; mensagens de erro;
  último ponto entre comandos — arquivo: `test_commands.cpp` — cobre: 06, 07,
  08, 15, 16, 17, 18, 31
- [x] **T08** Implementar `commands.hpp/.cpp` para POINT, LINE, PLINE, UNDO e
  REDO (pedidos de desfazer repassados ao chamador) até a T07 passar — cobre:
  06, 07, 15, 16, 17, 18, 31
- [x] **T09** Testes e implementação de MOVE, COPY e ERASE: com e sem
  pré-seleção; "Selecione objetos" e Enter; nada selecionado; fantasmas na
  pré-visualização; cópias múltiplas com IDs novos; desfazer de cada um
  restaura os IDs — arquivos: `commands.cpp`, `test_commands.cpp` — cobre: 27,
  28, 29, 30, 31, 32

## Fase 4 — Exibição e seleção (`render`)

- [x] **T10** Extrair `Projector`, `distanceToSegment`, `segmentTouchesBox` e
  `pointInTriangle` de `mesh_picker.cpp` para `pick_util.hpp` (sem mudar
  comportamento; testes da 004 continuam verdes) — cobre: 25
- [x] **T11** Testes e implementação do `DrawingPicker`: clique em linha,
  trecho de polilinha e ponto, dentro e fora da tolerância, o mais próximo da
  câmera; janela e cruzamento sobre um pórtico em vista superior — arquivos:
  `drawing_picker.hpp/.cpp`, `tests/unit/render/test_drawing_picker.cpp` —
  cobre: 25
- [x] **T12** `DrawingGpu`: buffers de segmentos e pontos por revisão (posições
  relativas à origem do desenho, deslocamento em `double`), realce das
  selecionadas, pré-visualização em buffer dinâmico; integração no
  `SceneRenderer`; o teste com backend `Null` desenha um desenho com seleção e
  pré-visualização — arquivos: `drawing_gpu.hpp/.cpp`, `scene_renderer.cpp`,
  `test_scene_renderer.cpp` — cobre: 18, 33
- [x] **T13** `Viewport`: `setDrawing`, `drawingChanged`, alvo da seleção
  (Desenho/Malha), seleção do desenho por clique, janela, cruzamento, Shift,
  Esc e clique fora; modo "pedir ponto" com `cursorMoved` e `pointRequested`;
  "Enquadrar tudo" com desenho e malha — arquivos: `viewport.hpp/.cpp` —
  cobre: 14, 25, 26, 34
- [x] **T14** Marcador e nome do snap no `SelectionOverlay` (quadrado,
  triângulo, X) e `Viewport::setSnapMarker` — arquivos:
  `selection_overlay.hpp/.cpp`, `viewport.cpp` — cobre: 20

## Fase 5 — Interface (`app`)

- [x] **T15** `CommandLineWidget`: histórico, entrada, pedido, Tab
  (`completeCommand`), setas, Espaço como Enter, Esc; teclas imprimíveis do
  viewport repassadas à entrada — arquivos: `command_line_widget.hpp/.cpp` —
  cobre: 04, 05, 06, 07, 09
- [x] **T16** `DraftingController`: dono do `Drawing`, da `CommandSession`, do
  `PointResolver`, das configurações e do `QUndoStack`; liga a linha de
  comando e o viewport (modo do mouse, pré-visualização, marcador), traduz
  pedidos e mensagens com `tr()`, manda as edições ao `QUndoStack` (primeiro
  `redo` ignorado) — arquivos: `drafting_controller.hpp/.cpp` — cobre: 04, 07,
  08, 14, 18, 27, 31
- [x] **T17** `MainWindow`: dock da linha de comando; menus Desenho, Modificar,
  Editar (Desfazer, Refazer, Alvo da seleção, modos de snap) e Ferramentas;
  barra de status com coordenadas e botões SNAP/GRADE/ORTO; atalhos F3, F8, F9,
  Delete, Ctrl+Z, Ctrl+Y/Ctrl+Shift+Z — arquivos: `main_window.hpp/.cpp` —
  cobre: 21, 24, 26, 30, 31
- [x] **T18** `DraftingSettingsDialog` (passo da grade e abertura do snap) —
  arquivos: `drafting_settings_dialog.hpp/.cpp` — cobre: 19, 22
- [x] **T19** Traduções pt_BR dos menus, botões, pedidos e mensagens — arquivo:
  `i18n/femapp_pt_BR.ts` — cobre: 35
- [x] **T20** Atualizar `specs/architecture.md` (§3a: o que já existe em `geom`
  e o desfazer por `DrawingEdit` + `QUndoStack`; §5: `DraftingController` e
  linha de comando) — cobre: 03

## Fase 6 — Conferência e medições

- [ ] **T21** `bench_drafting` (fora do ctest): 100 mil trechos, tempo de
  `PointResolver::resolve` por movimento — arquivo: `tests/perf/bench_drafting.cpp`
  — cobre: N01
- [ ] **T22** `[M]` Roteiro manual no Windows: pórtico plano e espacial pelo
  mouse e pela linha de comando; coordenadas absolutas, relativas, polares e
  distância direta; snaps, grade e ortogonal com marcadores e barra de status;
  histórico, Tab, Enter/Espaço, Esc; selecionar antes e depois de MOVE, COPY e
  ERASE; Delete; desfazer e refazer pelos três caminhos; alvo da seleção;
  desenho junto da malha; "Enquadrar tudo"; desenho a 1e6 da origem; textos em
  português — cobre: 04 a 35
- [ ] **T23** `[M]` Medir em Release: snap com 100 mil trechos (bench), FPS com
  100 mil trechos e a malha de 1 milhão de Tet4, consumo em repouso com um
  comando ativo — cobre: N01, N02, N03

## Fase 7 — Fechamento

- [ ] **T90** Build e testes verdes nos quatro jobs do CI (push com sua
  confirmação)
- [ ] **T91** `clang-format` e `clang-tidy` sem pendências (também o do Linux,
  pelo CI)
- [ ] **T92** Conferir os dez critérios de aceite de `requirements.md`
- [ ] **T93** Atualizar `specs/roadmap.md` para `concluída` e o `README.md`
  (comandos de desenho, atalhos)

## Matriz de rastreabilidade

| Requisito | Tarefas | Testes |
|-----------|---------|--------|
| REQ-005-01 | T01, T02 | `test_drawing` (AC-1) |
| REQ-005-02 | T01, T02 | `test_drawing` (AC-1) |
| REQ-005-03 | T02, T20 | `test_drawing`, `test_command_input`, `test_point_resolver`, `test_commands` sem Qt (AC-1) |
| REQ-005-04 | T15, T16, T22 | Roteiro manual (AC-5) |
| REQ-005-05 | T03, T04, T15 | `test_command_input` (AC-2) |
| REQ-005-06 | T07, T08, T15 | `test_commands`; roteiro manual (AC-3, AC-5) |
| REQ-005-07 | T07, T08, T15, T16 | `test_commands`; roteiro manual (AC-3, AC-5) |
| REQ-005-08 | T03, T04, T07, T16 | `test_command_input`, `test_commands` (AC-2) |
| REQ-005-09 | T15, T22 | Roteiro manual (AC-5) |
| REQ-005-10 | T03, T04 | `test_command_input` (AC-2) |
| REQ-005-11 | T03, T04 | `test_command_input` (AC-2) |
| REQ-005-12 | T03, T04 | `test_command_input` (AC-2) |
| REQ-005-13 | T03, T04 | `test_command_input` (AC-2) |
| REQ-005-14 | T05, T06, T13, T16 | `test_point_resolver`; roteiro manual (AC-4, AC-5) |
| REQ-005-15 | T07, T08 | `test_commands` (AC-3) |
| REQ-005-16 | T07, T08 | `test_commands` (AC-3) |
| REQ-005-17 | T07, T08 | `test_commands` (AC-3) |
| REQ-005-18 | T07, T08, T12, T16 | `test_commands`, `test_scene_renderer`; roteiro manual (AC-3, AC-5) |
| REQ-005-19 | T05, T06, T18 | `test_point_resolver` (AC-4) |
| REQ-005-20 | T14, T22 | Roteiro manual (AC-5) |
| REQ-005-21 | T17, T22 | Roteiro manual (AC-5) |
| REQ-005-22 | T05, T06, T18 | `test_point_resolver` (AC-4) |
| REQ-005-23 | T05, T06 | `test_point_resolver` (AC-4) |
| REQ-005-24 | T17, T22 | Roteiro manual (AC-5) |
| REQ-005-25 | T10, T11, T13 | `test_drawing_picker`; roteiro manual (AC-6) |
| REQ-005-26 | T13, T17, T22 | Roteiro manual (AC-6) |
| REQ-005-27 | T09, T16 | `test_commands`; roteiro manual (AC-3, AC-6) |
| REQ-005-28 | T09 | `test_commands` (AC-3) |
| REQ-005-29 | T09 | `test_commands` (AC-3) |
| REQ-005-30 | T09, T17 | `test_commands`; roteiro manual (AC-3, AC-6) |
| REQ-005-31 | T01, T02, T07, T08, T09, T16, T17 | `test_drawing`, `test_commands`; roteiro manual (AC-3, AC-7) |
| REQ-005-32 | T01, T02, T09 | `test_drawing`, `test_commands` (AC-1, AC-3) |
| REQ-005-33 | T12, T22 | `test_scene_renderer`; roteiro manual (AC-8) |
| REQ-005-34 | T13, T22 | Roteiro manual (AC-8) |
| REQ-005-35 | T19, T22 | Roteiro manual (AC-9) |
| NFR-005-01 | T21, T23 | `bench_drafting` (AC-10) |
| NFR-005-02 | T23 | Medição manual (AC-10) |
| NFR-005-03 | T23 | Medição manual (AC-10) |

## Notas de implementação

**2026-11-12 — Fase 1 (T01–T02):**

- 121 testes passando no Windows (5 novos de `geom`), sem Qt.
- Desvios do design §4: `Drawing::bounds` devolve `geom::Box` (e não
  `core::BoundingBox`), porque `geom` não depende de `core` (arquitetura §2);
  acrescentados `verticesOf`, `size` e `empty`, usados pelos snaps e pelos
  testes.

**2026-11-12 — Fase 2 (T03–T06):**

- 133 testes passando no Windows; `clang-tidy` sem apontamentos.
- Números lidos com `std::locale::classic()`: o ponto é sempre o separador
  decimal, mesmo com o Qt ajustando o locale do processo no Linux e no macOS.
- Desvios do design §4: `ScreenView` ganhou `orthographic` (na ortográfica o
  plano pode ficar atrás do olho e mesmo assim aparecer na tela, como na 004);
  `closestPoints` ficou público para ser testado à parte; `commandText`
  devolve o nome por extenso, para o histórico.

**2026-11-12 — Fase 3 (T07–T09):**

- 140 testes passando no Windows; `clang-tidy` sem apontamentos.
- Desvios do design §4 e §5.4: a `CommandSession` guarda o estado do comando
  ativo num único lugar, sem a interface polimórfica `Command` (mais simples
  para seis comandos curtos); Esc na PLINE cria a polilinha com os vértices já
  dados (como no AutoCAD e como a LINE, REQ-005-07); `feedSelection` recebe a
  seleção inteira do momento (o viewport mantém o realce), e Enter confirma;
  `activeCommand()` exposto para a barra de título do pedido e os testes.

**2026-11-12 — Fase 4 (T10–T14):**

- 142 testes passando no Windows, incluindo o `SceneRenderer` com backend
  `Null` desenhando linhas, polilinha fechada em 3D, ponto, seleção e
  pré-visualização; `clang-tidy` sem apontamentos.
- `DrawingPicker` testado em vista superior (clique em linha, trecho de
  polilinha e ponto; janela e cruzamento num pórtico).
- **Não verificado visualmente:** cores, realce, pré-visualização, marcador de
  snap e as interações do mouse; isso fica para o roteiro manual (T22), depois
  da interface da Fase 5.
- Desvios do design: o `Viewport` passa a acompanhar o mouse sempre
  (`setMouseTracking`) e emite `cursorMoved` em qualquer modo, para as
  coordenadas da barra de status (REQ-005-24); o marcador de snap some ao
  girar a roda (fica desatualizado) até o próximo movimento; `SnapMarker` é
  uma estrutura pública do `Viewport`; `clearDrawingSelection` e
  `setCommandSelection` acrescentados.

**2026-11-12 — Fase 5 (T15–T20):**

- 142 testes passando no Windows, inclusive os smoke tests, que abrem a janela
  com a linha de comando e os menus novos; `clang-tidy` sem apontamentos.
- **Não verificado visualmente nem por interação:** tudo o que depende do
  mouse e do teclado na janela; isso é o roteiro manual (T22).
- Decisões de implementação:
  - teclas imprimíveis com o viewport em foco vão para a linha de comando, e
    Enter/Espaço enviam a entrada (repetem o último comando se vazia);
  - Esc cancela o comando ativo; sem comando, limpa as seleções (desenho e
    malha);
  - os snaps a objetos só valem enquanto um ponto é pedido; as coordenadas da
    barra de status acompanham o cursor sempre;
  - depois de cada comando a seleção do desenho é limpa, como no AutoCAD;
  - os botões SNAP, GRADE e ORTO da barra de status e as ações do menu
    Ferramentas ficam sincronizados; F3, F9 e F8 como atalhos.

