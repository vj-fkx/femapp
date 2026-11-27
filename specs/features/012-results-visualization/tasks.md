# Tarefas — 012 results-visualization

Status: em andamento
Design: [design.md](design.md) · Requisitos: [requirements.md](requirements.md)

Regras:
- Uma tarefa = uma mudança pequena, compilável e testável.
- Testes vêm antes ou junto da implementação que verificam.
- `[P]` marca tarefas que podem ser feitas em paralelo com a anterior.
- `[M]` marca tarefas que dependem de ação do mantenedor.
- Marque `[x]` somente com build e testes passando.
- Os IDs abaixo omitem o prefixo: `01` significa `REQ-012-01`; `N01`
  significa `NFR-012-01`.
- Em toda tarefa, as suítes da 001 a 011 continuam passando.
- Textos da interface em inglês, por `tr()`.
- Antes de cada push: o g++ 13 com `-Wall -Wextra -Wpedantic -Werror` e
  `-O2` nos arquivos alterados, e o clang-tidy (como na 010 e na 011).

## Fase 1 — `core`: onde fica cada ponto e a amostragem

- [x] **T01** A posição de um ponto da barra (design §5.1):
  - testes primeiro:
    - `placementAt(0)` e `placementAt(length)` são os nós mais os
      `offsets`, numa viga reta com e sem excentricidade, em 1e−12;
    - numa `Line3` curva, as pontas são as da curva flexível, o meio está
      na curva, e `axes · at(x).displacement` nos nós é o deslocamento
      nodal global;
    - os resultados existentes não mudam (as suítes da 008 a 011 passam);
  - depois `BeamPlacement`, `Data::origin`, `placementAt`, `concentrated()`
    e `junctions()`, e o `origin` preenchido onde cada `BeamResult` é
    montado.
  Arquivos: `core/include/fem/core/beam_result.hpp`, `beam_result.cpp`,
  `static_analysis.cpp`, `stability_analysis.cpp` (ou onde o `BeamResult`
  da flambagem é montado), `beam_iso.cpp`,
  `tests/unit/core/test_result_sampling.cpp`. Cobre: 05.
  *Feito:* os arcos exatos têm a posição em forma fechada (a rotação
  integrada, Rodrigues), e as `Line3` usam o mesmo `pointAt` de
  `isoBeamAt` (`isoPlacement`). Na `Line3`, a metade do comprimento cai no
  nó do meio só em 1e−6, porque o comprimento da parábola é integrado
  numericamente no `core`.
- [x] **T02** As estações e a forma (design §5.2):
  - testes primeiro:
    - **viga biapoiada com carga q (AC-1):** as amostras são
      `placementAt` e `axes · at().displacement` em 1e−12, e o meio tem a
      flecha 5qL⁴/384EI em 1e−9;
    - **estações:** ímpares e com o meio; os dois lados de uma carga
      concentrada; as junções de um modo; o nó antes e depois de uma
      ligação rígida; a treliça com 2 amostras; os elementos que não são
      barras de fora;
    - **`perBeam`:** pelo orçamento (17, 9 e 5 nos limites), ou o valor
      dado;
  - depois `ResultQuantity`, `ResultFields`, `fieldsOf` (estático e modo),
    `Station`, `SampledBar`, `ShapeSamples`, `SampleOptions` e
    `sampleShape`, com o `tbb::parallel_for` em dois passos.
  Arquivos: `core/include/fem/core/result_sampling.hpp`,
  `result_sampling.cpp`, `core/CMakeLists.txt`, `test_result_sampling.cpp`.
  Cobre: 05, 10.
  *Feito:* `samplesPerBeam` e `beamStations` ficaram públicas para os
  testes. A ligação rígida é reconhecida pela distância entre o nó e a
  ponta flexível. As estações de nó (ligações e treliças) levam o
  deslocamento nodal.
- [x] **T03** A grandeza, os extremos e a consulta (design §5.3):
  - testes primeiro:
    - na viga biapoiada, Mz máximo = qL²/8 no meio em 1e−9 (AC-2);
    - Vy antes e depois de uma carga concentrada = `at(Before)` e
      `at(After)` (AC-2);
    - num nó de pórtico, cada barra mantém os seus esforços, sem média
      (AC-2);
    - grandezas globais numa barra girada 30° em torno de z, em 1e−12;
    - na treliça, só N; os modos dão deslocamentos e rotações;
    - os extremos com a amostra certa, e o primeiro no empate;
    - `valueAt` e `displacementAt` num x fora das amostras = `at(x)`,
      diferente da amostra vizinha (AC-6);
    - um valor não finito fica fora dos extremos;
  - depois `FieldSamples`, `Extreme`, `sampleField`, `valueAt`,
    `displacementAt` e `isForce`.
  Arquivos: `result_sampling.hpp/.cpp`, `test_result_sampling.cpp`.
  Cobre: 09, 10, 17, 18.
  *Feito:* sem valor (os esforços de um modo, um elemento que não é
  barra), `valueAt` dá NaN. Na treliça, a consulta interpola os nós
  linearmente.
- [x] **T04** [P] O fator automático e o determinismo:
  - testes primeiro: `autoScale` = 10% da maior dimensão sobre o maior
    deslocamento, em 1e−12; 1 sem deslocamento; a mesma `sampleShape` e
    a mesma `sampleField` com 1 thread e com todas, bit a bit;
  - depois `kAutoScaleFraction` e `autoScale`.
  Arquivos: `result_sampling.hpp/.cpp`, `test_result_sampling.cpp`.
  Cobre: 06, N02.
  *Feito:* o determinismo é conferido num pórtico de 40 vãos, com
  cargas distribuídas e concentradas, na forma e nos valores.

## Fase 2 — `render` sem GPU

- [x] **T05** A escala de cores (design §5.5):
  - testes primeiro: as cores das pontas e do meio; com N faixas, há
    exatamente N cores; um valor no limite cai na faixa de cima, e o
    máximo na última; fora da faixa, as cores das pontas (AC-3); a
    imagem tem 256×1 e segue `colormapColor`;
  - depois `colormapColor`, `bandOf`, `valueColor` e `colormapImage`.
  Arquivos: `render/include/fem/render/colormap.hpp`, `colormap.cpp`,
  `render/CMakeLists.txt`, `tests/unit/render/test_colormap.cpp`,
  `tests/CMakeLists.txt`. Cobre: 11, 12.
  *Feito:* `noValueColor()` (a cor 1D da 004) para os valores que não são
  finitos, e faixa vazia (min = max) dá a cor do mínimo.
- [x] **T06** [P] A cena de resultados:
  - testes primeiro:
    - o número de segmentos; os segmentos de comprimento zero (os dois
      lados de uma carga) pulados; o elemento de cada segmento;
    - `nodeSamples`, e as posições relativas à origem;
    - `segmentValues` com dois valores por segmento, cada um da sua
      ponta;
    - `gpuBytes() ≤ 64 × amostras` (N04);
  - depois `ResultScene`, `buildResultScene` e `segmentValues`.
  Arquivos: `render/include/fem/render/result_scene.hpp`,
  `result_scene.cpp`, `tests/unit/render/test_result_scene.cpp`.
  Cobre: 05, 10, N04.
  *Feito:* 60 bytes por segmento (`kSegmentGpuBytes`). O nó de cada ponta
  de barra aponta a primeira amostra nele, e a amostra do nó de uma
  ligação rígida é a ponta da barra.
- [x] **T07** O clique sobre a forma desenhada:
  - testes primeiro: um clique no meio de um segmento deformado dá a
    barra e o x interpolado (1e−6 relativo); com escala 0, a forma
    original; um nó dentro da tolerância ganha do segmento; um clique
    longe não dá nada;
  - depois `ResultPick` e `pickResult`.
  Arquivos: `render/include/fem/render/result_picker.hpp`,
  `result_picker.cpp`, `test_result_scene.cpp`. Cobre: 18.
  *Feito:* o nó devolve também a barra da sua amostra e a estação dela; o
  clique numa ligação rígida dá o x da ponta flexível (0 ou o
  comprimento).
- [ ] **T08** [P] As reações nos símbolos:
  - testes primeiro: `instancesOf` gera as setas e os arcos de
    `reactionForces` e `reactionMoments` na cor das reações, com a seta
    terminando no nó;
  - depois os campos no `SymbolSet` e a cor em `symbol_layer.cpp`.
  Arquivos: `render/include/fem/render/symbols.hpp`, `symbol_layer.cpp`,
  `tests/unit/render/test_symbols.cpp`. Cobre: 16.

## Fase 3 — `render` na GPU e no viewport

- [ ] **T09** A camada de resultados (design §5.5):
  - `ResultDisplay`, `ResultLayer` e os shaders `result_line.vert`,
    `result_line.frag` e `result_point.vert`:
    - os buffers de geometria, valores e bandeiras;
    - a textura 256×1 da escala;
    - os uniformes de escala, faixa, faixas, cor e bandeira da forma
      original;
  - o passe no `SceneRenderer`: no lugar da malha, com a exibição ligada;
  - teste de fumaça no backend `Null`: um quadro com a deformada, a
    original e as faixas, sem erro; trocar a escala e a faixa não reenvia
    a geometria, e trocar os valores reenvia só eles (contadores de
    upload da camada).
  Arquivos: `render/include/fem/render/result_display.hpp`,
  `result_layer.hpp/.cpp`, `shaders/result_*.{vert,frag}`,
  `scene_renderer.hpp/.cpp`, `render/CMakeLists.txt`,
  `tests/unit/render/test_scene_renderer.cpp`. Cobre: 05, 07, 08, 09,
  12, 13, N03.
- [ ] **T10** O viewport:
  - `setResultScene`, `setResultValues`, `setResultDisplay`,
    `setResultsShown`, `resultsShown`;
  - a animação: o `QElapsedTimer`, um quadro novo a cada vsync enquanto
    toca, e `animatedScale` (`Static` de 0 a f, `Mode` de −f a +f);
  - o clique na exibição de resultados chama `pickResult` com a escala do
    quadro, emite `resultPicked` e seleciona o nó ou o elemento;
  - as bandeiras de seleção e os nós realçados, só quando a malha do
    resultado é a da cena;
  - testes: `animatedScale` em t = 0, T/4 e T/2 nos dois modos (função
    pura do tempo, sem relógio); com o Null, ligar e desligar a exibição
    alterna os passes.
  Arquivos: `render/include/fem/render/viewport.hpp`, `viewport.cpp`,
  `test_scene_renderer.cpp`. Cobre: 01, 08, 15, 18.
- [ ] **T11** A sobreposição de textos (design §5.6):
  - testes primeiro, numa função pura de disposição: os rótulos
    projetados com a escala do quadro; a prioridade consulta > extremos >
    reações; um rótulo que cobriria outro é pulado; os rótulos fora da
    tela saem; os ticks da legenda (6 contínuos, ou um por limite de
    faixa até 11);
  - depois `ResultAnnotations`, `ResultLegend`, `ResultLabel` e o
    `ResultOverlay` (`QPainter`, transparente aos eventos, repintado
    depois de cada quadro), e `Viewport::setResultAnnotations`.
  Arquivos: `result_display.hpp`, `result_overlay.hpp/.cpp`, `viewport.cpp`,
  `tests/unit/render/test_result_overlay.cpp`. Cobre: 02, 11, 14, 16, 17,
  18.

## Fase 4 — App

- [ ] **T12** As opções e as grandezas, sem widgets (design §5.7):
  - testes primeiro:
    - `quantitiesOf`: 13 grandezas num resultado estático, 7 na
      flambagem; `shownQuantity` volta a |u| quando um esforço é pedido
      num modo;
    - `quantityName` e `quantityUnit`;
    - `formatResult` = `QLocale().toString(v, 'g', 6)`;
    - `displayFor`: o fator automático, o do usuário e o real; a faixa
      automática e a fixada; min = max dá [v − 1, v + 1] ou [−1, 1];
      `Mode` na flambagem e `Static` nos outros;
  - depois `ResultDisplaySettings`, `ScaleMode` e as funções.
  Arquivos: `app/result_display_settings.hpp/.cpp`, `app/CMakeLists.txt`,
  `tests/unit/app/test_result_display.cpp`, `tests/CMakeLists.txt`.
  Cobre: 06, 09, 11, 12, 15, 19.
- [ ] **T13** Os textos (`annotationsFor`):
  - testes primeiro:
    - **reações no pórtico 2×2 da 010:** os componentes, os valores com
      as unidades do documento, e a soma igual à carga aplicada em 1e−9
      (AC-5); os componentes desprezíveis de fora;
    - **extremos:** "max Mz = … · bar 17, x = …", ou o nó;
    - **cabeçalho:** a escala, o modo com o fator do `core` (AC-4) e o
      aviso de desatualizado;
    - **consulta:** o id, o x, `valueAt` e `displacementAt`;
    - as reações e os extremos desligados somem;
  - depois `AnnotationInput` e `annotationsFor`.
  Arquivos: `result_display_settings.hpp/.cpp`, `test_result_display.cpp`.
  Cobre: 02, 14, 16, 17, 18, 19.
- [ ] **T14** [P] O dock Results:
  - um delegate com `formatResult` nas tabelas (teste: o texto da célula
    é o de `formatResult`);
  - o sinal `resultChosen(item, mode)` quando muda o caso ou o modo.
  Arquivos: `app/results_panel.hpp/.cpp`, `results_model.*` (se preciso),
  `tests/unit/app/test_results_model.cpp`. Cobre: 04, 19.
- [ ] **T15** [P] Os símbolos na exibição de resultados:
  - testes primeiro: com `loadsShown = false`, os apoios continuam e as
    cargas somem; `reactionSymbols` dá as setas e os arcos do resultado
    nos nós apoiados, no sentido da reação;
  - depois a opção em `SymbolOptions`, `reactionSymbols` e o uso pelo
    `ModelController`.
  Arquivos: `app/symbol_builder.hpp/.cpp`, `model_controller.*`,
  `tests/unit/app/test_symbol_builder.cpp`. Cobre: 03, 16.
- [ ] **T16** O `ResultView` (design §6):
  - o trabalho na `QThread` (forma, grandeza, cena e valores), com
    gerações e um trabalho por vez;
  - a ligação com o viewport e com os símbolos;
  - os contadores `sceneBuilds` e `valueBuilds`;
  - testes (offscreen, com o pórtico 2×2):
    - trocar de caso mantém a grandeza e a escala do usuário;
    - trocar a escala, as faixas ou a original não aumenta nenhum
      contador;
    - trocar a grandeza aumenta só `valueBuilds`;
    - editar o modelo põe o aviso, e o desenho segue sobre a malha
      analisada;
    - apagar o item desliga a exibição;
    - um pedido velho que chega depois de um novo é descartado.
  Arquivos: `app/result_view.hpp/.cpp`,
  `tests/unit/app/test_result_view.cpp`. Cobre: 01, 02, 04, 08, 13.
- [ ] **T17** A interface:
  - a `ResultDisplayBar` no dock Results: a grandeza (com "None"), a
    escala (campo, Auto, Real), deformada, original, faixas (contínuo ou
    N), faixa fixada (min e max), reações, extremos, tocar e pausar, e a
    velocidade;
  - View ▸ Show Results, desabilitado sem resultado;
  - a `MainWindow` cria o `ResultView` e liga o dock, o viewport e o
    clique de consulta;
  - teste: a barra reflete e altera as `ResultDisplaySettings`.
  Arquivos: `app/result_display_bar.hpp/.cpp`, `main_window.hpp/.cpp`,
  `results_panel.cpp`, `tests/unit/app/test_result_view.cpp`. Cobre: 01,
  06, 07, 11, 12, 15, 16, 17.
- [ ] **T18** O benchmark da preparação:
  - o `ResultView` com o pórtico do `bench_line_mesh` em Release: o maior
    intervalo do laço de eventos ≤ 100 ms e a preparação ≤ 2 s;
  - se falhar, o `at(span<const Station>)` do risco do design §10, com
    uma nova medida.
  Arquivos: `tests/bench/bench_result_view.cpp`, `tests/CMakeLists.txt`.
  Cobre: N02.
- [ ] **T19** [M] Roteiro manual (AC-8, N01):
  - ligar e desligar a exibição; a deformada sobre a original;
  - as cores contínuas e em faixas, e a faixa fixada;
  - a animação de um modo e de um caso estático;
  - as reações, os extremos e a consulta pelo clique;
  - trocar de caso e de modo no dock;
  - 30 FPS ao orbitar e animando o pórtico grande.
  Adiado para a sessão de testes manuais, como os da 009 a 011.
- [ ] **T20** [P] Documentação: `architecture.md` (§3: `placementAt` e
  `result_sampling`; §4: a camada de resultados, a sobreposição e a
  animação; §5: o `ResultView`) e a seção do `README.md` sobre a
  exibição de resultados.

## Fase 5 — Fechamento

- [ ] **T90** Build e testes verdes nos presets das três plataformas (CI)
- [ ] **T91** `clang-format` e `clang-tidy` sem pendências; `core` sem
  headers do Qt (`check_no_qt_includes`)
- [ ] **T92** Conferir todos os critérios de aceite de `requirements.md`
- [ ] **T93** Atualizar `specs/roadmap.md` (status) e documentação afetada

## Matriz de rastreabilidade

| Requisito | Tarefas | Testes |
|-----------|---------|--------|
| REQ-012-01 | T10, T16, T17 | `test_scene_renderer.cpp`, `test_result_view.cpp`, roteiro T19 |
| REQ-012-02 | T11, T13, T16 | `test_result_overlay.cpp`, `test_result_display.cpp`, `test_result_view.cpp` |
| REQ-012-03 | T15 | `test_symbol_builder.cpp`, roteiro T19 |
| REQ-012-04 | T14, T16 | `test_results_model.cpp`, `test_result_view.cpp` |
| REQ-012-05 | T01, T02, T06, T09 | `test_result_sampling.cpp`, `test_result_scene.cpp`, `test_scene_renderer.cpp` |
| REQ-012-06 | T04, T12, T17 | `test_result_sampling.cpp`, `test_result_display.cpp` |
| REQ-012-07 | T09, T17 | `test_scene_renderer.cpp`, roteiro T19 |
| REQ-012-08 | T09, T10, T16 | `test_scene_renderer.cpp`, `test_result_view.cpp` |
| REQ-012-09 | T03, T09, T12 | `test_result_sampling.cpp`, `test_result_display.cpp` |
| REQ-012-10 | T02, T03, T06 | `test_result_sampling.cpp`, `test_result_scene.cpp` |
| REQ-012-11 | T05, T11, T12, T17 | `test_colormap.cpp`, `test_result_overlay.cpp`, `test_result_display.cpp` |
| REQ-012-12 | T05, T09, T12, T17 | `test_colormap.cpp`, `test_scene_renderer.cpp`, `test_result_display.cpp` |
| REQ-012-13 | T09, T16 | `test_scene_renderer.cpp`, `test_result_view.cpp` |
| REQ-012-14 | T11, T13 | `test_result_overlay.cpp`, `test_result_display.cpp` |
| REQ-012-15 | T10, T12, T17 | `test_scene_renderer.cpp`, `test_result_display.cpp`, roteiro T19 |
| REQ-012-16 | T08, T11, T13, T15, T17 | `test_symbols.cpp`, `test_result_display.cpp`, `test_symbol_builder.cpp` |
| REQ-012-17 | T03, T11, T13, T17 | `test_result_sampling.cpp`, `test_result_overlay.cpp`, `test_result_display.cpp` |
| REQ-012-18 | T03, T07, T10, T11, T13 | `test_result_sampling.cpp`, `test_result_scene.cpp`, `test_result_display.cpp` |
| REQ-012-19 | T12, T13, T14 | `test_result_display.cpp`, `test_results_model.cpp` |
| NFR-012-01 | T19 | roteiro T19 |
| NFR-012-02 | T02, T04, T18 | `test_result_sampling.cpp`, `bench_result_view.cpp` |
| NFR-012-03 | T09, T91 | `test_scene_renderer.cpp` (Null), testes sem GPU |
| NFR-012-04 | T06 | `test_result_scene.cpp` |
