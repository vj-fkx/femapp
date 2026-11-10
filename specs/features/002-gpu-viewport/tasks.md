# Tarefas — 002 gpu-viewport

Status: em andamento
Design: [design.md](design.md) · Requisitos: [requirements.md](requirements.md)

Regras:
- Uma tarefa = uma mudança pequena, compilável e testável.
- Testes vêm antes ou junto da implementação que verificam.
- `[P]` marca tarefas que podem ser feitas em paralelo com a anterior.
- `[M]` marca tarefas que dependem de ação do mantenedor.
- Marque `[x]` somente com build e testes passando.
- Os IDs abaixo omitem o prefixo: `01` significa `REQ-002-01`; `N03` significa
  `NFR-002-03`.

## Fase 0 — Pré-requisitos

- [x] **T00** `[M]` Autorizar a instalação local do módulo Qt Shader Tools
  (`aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -m qtshadertools -O C:\Qt`,
  cerca de 10 MB); conferir que `qsb.exe` existe em `C:\Qt\6.8.3\msvc2022_64\bin`.
- [x] **T01** Acrescentar `ShaderTools` ao `find_package(Qt6 …)` da raiz, com a
  mesma mensagem de erro orientada da 001; acrescentar `modules: qtshadertools`
  ao passo de instalação do Qt no CI; atualizar pré-requisitos no `README.md` —
  arquivos: `CMakeLists.txt`, `.github/workflows/ci.yml`, `README.md` — cobre: 30
- [x] **T02** Preparar o módulo `render` para as novas peças: dependências
  `Qt6::Widgets` (pública), `Qt6::GuiPrivate` e `Eigen3::Eigen`, `AUTOMOC`;
  permitir vários arquivos de teste por módulo em `femapp_add_test`; atualizar a
  tabela de dependências em `specs/architecture.md` (ADR 0002). Build e testes
  da 001 continuam verdes — arquivos: `src/render/CMakeLists.txt`,
  `tests/CMakeLists.txt`, `specs/architecture.md` — cobre: 10

## Fase 1 — Lógica sem GPU

- [x] **T03** Testes de `parseBackendName`, `isBackendSupported` e
  `planBackends` (três plataformas, sem variável, variável válida, inválida,
  não suportada, `null`, `fail`) — arquivo: `tests/unit/render/test_backend.cpp`
  — cobre: 02, 03, 04, 05
- [x] **T04** Implementar `backend.hpp/.cpp` até a T03 passar — cobre: 02–05
- [x] **T05** `[P]` Testes de `gridSpacing`: resultado é potência de 10,
  `major = 10·minor`, monotônico em relação ao zoom, `minorFade ∈ [0, 1]` e
  contínuo na troca de nível — arquivo: `tests/unit/render/test_grid_spacing.cpp`
  — cobre: 21
- [x] **T06** Implementar `grid_spacing.hpp/.cpp` até a T05 passar — cobre: 21
- [x] **T07** Testes da base da câmera: ortonormal, `up.z ≥ 0` após sequências
  de órbita, elevação limitada a ±90°, base definida nos polos; direção e "para
  cima" de cada vista padrão; estado inicial isométrico em perspectiva —
  arquivo: `tests/unit/render/test_camera.cpp` — cobre: 11, 13, 17, 19, 28
- [x] **T08** Implementar `Camera`: estado, base, `orbit`, `setStandardView`,
  `viewMatrix`, `viewMatrixRelativeToTarget` até a T07 passar —
  arquivos: `camera.hpp/.cpp` — cobre: 11, 13, 17, 19, 28
- [x] **T09** Testes de projeção e raio: `projectionMatrix` com profundidade em
  [0, 1], `rayThrough` consistente com as matrizes nas duas projeções, troca de
  projeção preserva foco e altura visível no plano de foco, viewport de tamanho
  zero é ignorado — cobre: 08, 16, 28
- [x] **T10** Implementar `setViewportSize`, `setProjection`,
  `projectionMatrix`, `rayThrough` até a T09 passar — cobre: 08, 16, 28
- [x] **T11** Testes de pan e zoom: o ponto sob o cursor permanece sob o cursor
  (tolerância 1e−6 px) nas duas projeções; limites de distância respeitados sem
  mover o foco — cobre: 12, 14, 15, 28
- [x] **T12** Implementar `pan` e `zoomAt` até a T11 passar — cobre: 12, 14, 15
- [x] **T13** Testes de `fit`: os oito cantos de uma caixa conhecida ficam
  dentro do quadro com folga ≤ 25 %, em janelas larga e alta; caixa vazia usa a
  região padrão — cobre: 18, 19, 28
- [x] **T14** Implementar `fit` até a T13 passar — cobre: 18, 19
- [x] **T15** Teste de precisão: com o foco em (1e6, 1e6, 0), a matriz relativa
  convertida para `float` projeta pontos próximos com erro < 1e−3 px; conferir
  que os testes de `render` rodam em menos de 1 s — cobre: N03, N04

## Fase 2 — Renderização

- [ ] **T16** Criar os shaders da grade (versões mínimas que compilam) e ligar
  `qt_add_shaders` ao módulo; criar o esqueleto de `SceneRenderer`
  (`initialize`, `render` que só limpa a tela, `release`) e o teste que cria um
  `QRhi` `Null`, um alvo offscreen e desenha um quadro —
  arquivos: `src/render/shaders/grid.vert/.frag`, `scene_renderer.hpp/.cpp`,
  `tests/unit/render/test_scene_renderer.cpp` — cobre: 29, 30
- [ ] **T17** Criar `Viewport` (`QRhiWidget`): aplica o plano de backends antes
  de ser mostrado, delega a `SceneRenderer`, conta quadros, emite
  `frameRendered` e `backendChanged`, `setSampleCount(4)`; remover
  `module.hpp/.cpp` e o teste antigo — arquivos: `viewport.hpp/.cpp` —
  cobre: 01, 02, 04, 08, 09
- [ ] **T18** Tratar falha de inicialização no `Viewport`: avançar para a
  próxima tentativa do plano; lista esgotada ou falha de pipeline ⇒
  `backendUnavailable` com o motivo; aviso de variável inválida no log —
  cobre: 03, 05, 06
- [ ] **T19** Colocar o `Viewport` como widget central do `MainWindow`, com a
  mensagem de falha no lugar dele quando `backendUnavailable` chegar; mostrar o
  backend no "Sobre" — arquivos: `main_window.hpp/.cpp` — cobre: 01, 06, 07
- [ ] **T20** Alterar o `--smoke-test`: sair com 0 ao primeiro `frameRendered`
  ou `backendUnavailable`, com 1 após 10 s; registrar no log qual dos dois
  ocorreu; configurar `femapp_smoke` com `FEM_RHI_BACKEND=null` e criar
  `femapp_smoke_no_gpu` com `FEM_RHI_BACKEND=fail` — arquivos: `main.cpp`,
  `tests/CMakeLists.txt` — cobre: 06, 29
  - **Ponto de parada:** se `femapp_smoke` não obtiver um quadro na plataforma
    `offscreen`, parar e propor ajuste no REQ-002-29 (risco 1 do design).
- [ ] **T21** Criar `cmake/CheckRhiConfined.cmake` e o teste
  `check_rhi_confined`; conferir que ele falha com um `#include <rhi/qrhi.h>`
  posto de propósito em `src/app` — cobre: 10
- [ ] **T22** Implementar a grade completa: quadrilátero sob o foco, linhas
  secundárias e principais com antisserrilhamento, espaçamento e `minorFade`
  vindos de `gridSpacing`, foco reduzido módulo `major` em `double`, eixos X e Y
  coloridos, esmaecimento por distância e por ângulo rasante —
  arquivos: `grid.vert/.frag`, `scene_renderer.cpp` — cobre: 20, 21, 22, 23
- [ ] **T23** Ligar o mouse à câmera: botão do meio = pan, Shift + meio =
  órbita, roda = zoom no cursor; `update()` apenas quando a câmera muda —
  arquivo: `viewport.cpp` — cobre: 09, 12, 13, 14
- [ ] **T24** Indicador de eixos: geometria das setas (teste unitário da
  geração: contagem de vértices, normais unitárias, cores), shaders `gizmo`,
  segundo passe em área de 96 px lógicos com profundidade própria —
  arquivos: `axis_gizmo_mesh.hpp/.cpp`, `gizmo.vert/.frag`, `scene_renderer.cpp`
  — cobre: 24, 25
- [ ] **T25** Rótulos X, Y, Z: atlas gerado com `QPainter`, quadriláteros
  voltados para a câmera, atlas refeito quando o `devicePixelRatio` muda —
  arquivos: `axis_labels.hpp/.cpp`, `label.vert/.frag` — cobre: 08, 24, 25

## Fase 3 — Interface

- [ ] **T26** Menu "Exibir": sete vistas padrão (Ctrl+1 … Ctrl+7), projeção
  marcável (Ctrl+Shift+O), "Enquadrar tudo" (Home), grade (F7) e eixos
  marcáveis; itens desabilitados quando o viewport não está disponível —
  arquivos: `main_window.hpp/.cpp`, `viewport.cpp` — cobre: 16, 17, 18, 26
- [ ] **T27** Traduzir os textos novos para pt_BR (menu, mensagem de falha,
  linha do backend no "Sobre") — arquivo: `i18n/femapp_pt_BR.ts` — cobre: 27
- [ ] **T28** `[M]` Roteiro manual no Windows, escrito nas notas desta tarefa:
  navegação (AC-5), vistas e projeção (AC-7), grade (AC-8), eixos e menu
  (AC-9), redimensionar e trocar de tela (AC-10), contador de quadros parado
  (AC-11), backend no "Sobre" e `FEM_RHI_BACKEND=opengl` e inválido (AC-2,
  AC-3), menus em português (AC-14) — cobre: 07, 08, 12–14, 16–18, 20–27
- [ ] **T29** `[M]` Medir quadros por segundo em build Release durante a
  navegação e o uso de CPU/GPU em repouso; registrar aqui — cobre: N01, N02

## Fase 4 — Fechamento

- [ ] **T90** Build e testes verdes nos quatro jobs do CI (push com sua
  confirmação) — cobre: 29, 30
- [ ] **T91** `clang-format` e `clang-tidy` sem pendências
- [ ] **T92** Conferir os catorze critérios de aceite de `requirements.md`,
  registrando as ressalvas de macOS e Linux
- [ ] **T93** Atualizar `specs/roadmap.md` para `concluída`, o `README.md` e o
  `CLAUDE.md` se os comandos ou pré-requisitos tiverem mudado

## Matriz de rastreabilidade

| Requisito | Tarefas | Testes |
|-----------|---------|--------|
| REQ-002-01 | T17, T19 | `femapp_smoke`; roteiro manual (AC-1) |
| REQ-002-02 | T03, T04, T17 | `test_backend`; roteiro manual (AC-2) |
| REQ-002-03 | T03, T04, T18 | `test_backend` |
| REQ-002-04 | T03, T04, T17 | `test_backend`; `femapp_smoke` (`null`); roteiro manual (AC-3) |
| REQ-002-05 | T03, T04, T18 | `test_backend`; roteiro manual (AC-3) |
| REQ-002-06 | T18, T19, T20 | `femapp_smoke_no_gpu` (AC-4) |
| REQ-002-07 | T19, T28 | Roteiro manual (AC-2) |
| REQ-002-08 | T09, T10, T17, T25, T28 | `test_camera`; roteiro manual (AC-10) |
| REQ-002-09 | T17, T23, T28 | Roteiro manual com contador de quadros (AC-11) |
| REQ-002-10 | T02, T21 | `check_rhi_confined` (AC-13) |
| REQ-002-11 | T07, T08 | `test_camera` (AC-6) |
| REQ-002-12 | T11, T12, T23 | `test_camera`; roteiro manual (AC-5) |
| REQ-002-13 | T07, T08, T23 | `test_camera`; roteiro manual (AC-5, AC-6) |
| REQ-002-14 | T11, T12, T23 | `test_camera`; roteiro manual (AC-5) |
| REQ-002-15 | T11, T12 | `test_camera` |
| REQ-002-16 | T09, T10, T26 | `test_camera`; roteiro manual (AC-7) |
| REQ-002-17 | T07, T08, T26 | `test_camera`; roteiro manual (AC-7) |
| REQ-002-18 | T13, T14, T26 | `test_camera`; roteiro manual (AC-7) |
| REQ-002-19 | T07, T13, T14 | `test_camera`; roteiro manual (AC-1) |
| REQ-002-20 | T22 | Roteiro manual (AC-8) |
| REQ-002-21 | T05, T06, T22 | `test_grid_spacing`; roteiro manual (AC-8) |
| REQ-002-22 | T22 | Roteiro manual (AC-8) |
| REQ-002-23 | T22 | Roteiro manual (AC-8) |
| REQ-002-24 | T24, T25 | Teste da geometria das setas; roteiro manual (AC-9) |
| REQ-002-25 | T24, T25 | Roteiro manual (AC-9) |
| REQ-002-26 | T26 | Roteiro manual (AC-9) |
| REQ-002-27 | T27 | Roteiro manual (AC-14) |
| REQ-002-28 | T07–T14 | `test_camera` (AC-6, AC-7) |
| REQ-002-29 | T16, T20, T90 | `test_scene_renderer`; `femapp_smoke` nos quatro jobs (AC-12) |
| REQ-002-30 | T01, T16, T90 | Build; `test_scene_renderer` carrega os shaders (AC-12) |
| NFR-002-01 | T29 | Medição manual |
| NFR-002-02 | T29 | Medição manual; contador de quadros (AC-11) |
| NFR-002-03 | T15 | `test_camera` |
| NFR-002-04 | T15 | Tempo no `ctest` |

## Notas de implementação

*(preenchidas durante a implementação)*
