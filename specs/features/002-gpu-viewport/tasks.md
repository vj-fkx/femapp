# Tarefas — 002 gpu-viewport

Status: concluído · 2026-11-11
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

- [x] **T16** Criar os shaders da grade (versões mínimas que compilam) e ligar
  `qt_add_shaders` ao módulo; criar o esqueleto de `SceneRenderer`
  (`initialize`, `render` que só limpa a tela, `release`) e o teste que cria um
  `QRhi` `Null`, um alvo offscreen e desenha um quadro —
  arquivos: `src/render/shaders/grid.vert/.frag`, `scene_renderer.hpp/.cpp`,
  `tests/unit/render/test_scene_renderer.cpp` — cobre: 29, 30
- [x] **T17** Criar `Viewport` (`QRhiWidget`): aplica o plano de backends antes
  de ser mostrado, delega a `SceneRenderer`, conta quadros, emite
  `frameRendered` e `backendChanged`, `setSampleCount(4)`; remover
  `module.hpp/.cpp` e o teste antigo — arquivos: `viewport.hpp/.cpp` —
  cobre: 01, 02, 04, 08, 09
- [x] **T18** Tratar falha de inicialização no `Viewport`: avançar para a
  próxima tentativa do plano; lista esgotada ou falha de pipeline ⇒
  `backendUnavailable` com o motivo; aviso de variável inválida no log —
  cobre: 03, 05, 06
- [x] **T19** Colocar o `Viewport` como widget central do `MainWindow`, com a
  mensagem de falha no lugar dele quando `backendUnavailable` chegar; mostrar o
  backend no "Sobre" — arquivos: `main_window.hpp/.cpp` — cobre: 01, 06, 07
- [x] **T20** Alterar o `--smoke-test`: sair com 0 ao primeiro `frameRendered`
  ou `backendUnavailable`, com 1 após 10 s; registrar no log qual dos dois
  ocorreu; configurar `femapp_smoke` com `FEM_RHI_BACKEND=null` e criar
  `femapp_smoke_no_gpu` com `FEM_RHI_BACKEND=fail` — arquivos: `main.cpp`,
  `tests/CMakeLists.txt` — cobre: 06, 29
  - **Ponto de parada:** se `femapp_smoke` não obtiver um quadro na plataforma
    `offscreen`, parar e propor ajuste no REQ-002-29 (risco 1 do design).
- [x] **T21** Criar `cmake/CheckRhiConfined.cmake` e o teste
  `check_rhi_confined`; conferir que ele falha com um `#include <rhi/qrhi.h>`
  posto de propósito em `src/app` — cobre: 10
- [x] **T22** Implementar a grade completa: quadrilátero sob o foco, linhas
  secundárias e principais com antisserrilhamento, espaçamento e `minorFade`
  vindos de `gridSpacing`, foco reduzido módulo `major` em `double`, eixos X e Y
  coloridos, esmaecimento por distância e por ângulo rasante —
  arquivos: `grid.vert/.frag`, `scene_renderer.cpp` — cobre: 20, 21, 22, 23
- [x] **T23** Ligar o mouse à câmera: botão do meio = pan, Shift + meio =
  órbita, roda = zoom no cursor; `update()` apenas quando a câmera muda —
  arquivo: `viewport.cpp` — cobre: 09, 12, 13, 14
- [x] **T24** Indicador de eixos: geometria das setas (teste unitário da
  geração: contagem de vértices, normais unitárias, cores), shaders `gizmo`,
  segundo passe em área de 96 px lógicos com profundidade própria —
  arquivos: `axis_gizmo_mesh.hpp/.cpp`, `gizmo.vert/.frag`, `scene_renderer.cpp`
  — cobre: 24, 25
- [x] **T25** Rótulos X, Y, Z: atlas gerado com `QPainter`, quadriláteros
  voltados para a câmera, atlas refeito quando o `devicePixelRatio` muda —
  arquivos: `axis_labels.hpp/.cpp`, `label.vert/.frag` — cobre: 08, 24, 25

## Fase 3 — Interface

- [x] **T26** Menu "Exibir": sete vistas padrão (Ctrl+1 … Ctrl+7), projeção
  marcável (Ctrl+Shift+O), "Enquadrar tudo" (Home), grade (F7) e eixos
  marcáveis; itens desabilitados quando o viewport não está disponível —
  arquivos: `main_window.hpp/.cpp`, `viewport.cpp` — cobre: 16, 17, 18, 26
- [x] **T27** Traduzir os textos novos para pt_BR (menu, mensagem de falha,
  linha do backend no "Sobre") — arquivo: `i18n/femapp_pt_BR.ts` — cobre: 27
- [x] **T28** `[M]` Roteiro manual no Windows, escrito nas notas desta tarefa:
  navegação (AC-5), vistas e projeção (AC-7), grade (AC-8), eixos e menu
  (AC-9), redimensionar e trocar de tela (AC-10), contador de quadros parado
  (AC-11), backend no "Sobre" e `FEM_RHI_BACKEND=opengl` e inválido (AC-2,
  AC-3), menus em português (AC-14) — cobre: 07, 08, 12–14, 16–18, 20–27
- [x] **T29** `[M]` Medir quadros por segundo em build Release durante a
  navegação e o uso de CPU/GPU em repouso; registrar aqui — cobre: N01, N02

## Fase 4 — Fechamento

- [x] **T90** Build e testes verdes nos quatro jobs do CI (push com sua
  confirmação) — cobre: 29, 30
- [x] **T91** `clang-format` e `clang-tidy` sem pendências
- [x] **T92** Conferir os catorze critérios de aceite de `requirements.md`,
  registrando as ressalvas de macOS e Linux
- [x] **T93** Atualizar `specs/roadmap.md` para `concluída`, o `README.md` e o
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

**2026-11-10 — Fases 0 e 1 (T00–T15):**

- T00: Qt Shader Tools 6.8.3 instalado com `aqtinstall` (autorizado pelo
  mantenedor).
- Testes e implementação foram compilados juntos, e não em dois passos
  separados (teste falhando, depois implementação). Mesmo desvio da 001.
- Desvio do design §5.1 (enquadrar): em vez da esfera envolvente, `fit` projeta
  os oito cantos da caixa nos eixos da câmera. A esfera deixava mais de 50 % de
  folga em vistas ortogonais de caixas achatadas; com os cantos, o canto
  limitante fica sempre a 1/1,1 da borda. Em ortográfica a profundidade é
  ignorada.
- 35 testes passando após a fase 1; zero avisos com `/W4`.

**2026-11-10 — Fases 2 e 3 (T16–T19, T21–T27), T20 parada:**

- Build sem avisos, `clang-format` e `clang-tidy` limpos, 39 de 40 testes
  passando. O que falha é o `femapp_smoke` (T20), ver abaixo.
- Verificado por execução no Windows: o aplicativo renderiza um quadro com
  Direct3D 11 (padrão), com `FEM_RHI_BACKEND=opengl`, com `null` e com um valor
  inválido (cai no padrão). `femapp_smoke_no_gpu` (`fail`) passa.
- **Não verificado visualmente:** a aparência da grade, dos eixos e dos
  rótulos, a navegação com o mouse e os menus. Fica para o roteiro manual
  (T28). O teste de `SceneRenderer` com o backend `Null` só prova que shaders,
  pipelines e comandos são aceitos, não que a imagem está certa.
- T21: `check_rhi_confined` conferido com quatro includes proibidos postos de
  propósito em `src/app` (todos reprovados) e com `<QRhiWidget>` (aceito).
- Acréscimos à interface do design §4: `Viewport::isAvailable()`,
  `unavailableReason()` e `camera()`; `SceneRenderer::initialize` devolve
  `bool` com `errorString()`. Necessários para tratar a falha de backend de
  forma síncrona na construção da janela.
- Desvio do design §5.3: a profundidade não é limpa na área do indicador de
  eixos (o QRhi não limpa parte de um passe). O indicador é desenhado em uma
  fatia fina na frente do intervalo de profundidade (`maxDepth = 0,01`).
  Suficiente enquanto nada mais escreve profundidade; rever na feature 004.
- Ajustes no `.clang-tidy`: desligados
  `clang-analyzer-optin.core.EnumCastOutOfRange` (dispara dentro das macros do
  Catch2) e `readability-redundant-access-specifiers` (conflita com as seções
  `signals:` / `public slots:` do Qt).
- `aqtinstall.log` foi parar em um commit por engano; removido e acrescentado
  ao `.gitignore`.

**T20 — ponto de parada atingido (risco 1 do design):**

- Com `QT_QPA_PLATFORM=offscreen`, o `QRhiWidget` não inicializa nem com o
  backend `null`: o aplicativo cai no caminho "viewport indisponível". Na
  plataforma nativa do Windows o mesmo comando renderiza um quadro.
- O texto do REQ-002-29 não menciona `offscreen`; quem fixou isso foi o design
  (§8) e a configuração do teste herdada da 001.
- Proposta, aguardando aprovação: rodar `femapp_smoke` na plataforma nativa
  (sem `QT_QPA_PLATFORM=offscreen`), mantendo `FEM_RHI_BACKEND=null`; no CI de
  Linux, rodar o `ctest` sob `xvfb-run`. `femapp_smoke_no_gpu` continua em
  `offscreen`.

**2026-11-10 — T20 concluída (ajuste aprovado pelo mantenedor):**

- `femapp_smoke` passou a rodar na plataforma nativa com
  `FEM_RHI_BACKEND=null`; o CI de Linux roda o `ctest` sob `xvfb-run`, com os
  pacotes `xvfb` e `libxcb-*` acrescentados. Design §8 e §10 e README
  atualizados.
- Local (Windows): 40 de 40 testes passando, build sem avisos.
- Não verificado: o smoke test nos runners de macOS e Linux (depende do CI,
  T90) e a lista de pacotes `libxcb-*` no Ubuntu 24.04.

**2026-11-10 — T28 e T91:**

- T28: roteiro manual da janela no Windows confirmado pelo mantenedor
  (resposta "está ok" ao pedido de conferência de navegação, menu "Exibir",
  grade, indicador de eixos e backend no "Sobre"). Os itens não foram
  relatados um a um; redimensionar entre telas de escalas diferentes (AC-10),
  `FEM_RHI_BACKEND=opengl` no "Sobre" (AC-3) e o contador de quadros parado
  (AC-11) não foram confirmados explicitamente.
- T91: `clang-format --dry-run --Werror` sem diferenças e `clang-tidy` com zero
  apontamentos em todos os `.cpp` de `src` e `tests`; 40 de 40 testes.
- Pendentes: T29 (medição de quadros por segundo e de repouso, do
  mantenedor), T90 (CI), T92 e T93.

**2026-11-11 — Execução nº 9 do CI, commit `88efb16` (T90, em andamento):**

- <https://github.com/vj-fkx/femapp/actions/runs/43014696951>
- `windows`, `linux-gcc` e `format`: passaram. O smoke test sob `xvfb-run`
  funcionou no `linux-gcc`.
- `linux-clang`: build e testes passaram; falhou no passo "Run clang-tidy".
- `macos`: falhou no passo "Test".
- Os logs dos dois jobs ainda não foram lidos (o GitHub exige login para
  abri-los); causa desconhecida até lá.

**2026-11-11 — Logs da execução nº 9 (T90, em andamento):**

- `linux-clang`: `clang-tidy` acusou `cppcoreguidelines-pro-type-member-init`
  em `camera.cpp` (um `std::array<Vec3, 8>` declarado sem inicializar). O
  `clang-tidy` do Windows não acusa o mesmo. Corrigido: a função passou a
  devolver o array já construído.
- `macos`: `femapp_smoke` terminou em "viewport unavailable". O log mostra
  "QRhiWidget: No QRhi" para `Null`, depois para Metal e para OpenGL. Não dá
  para saber, só por essa execução, se Metal falha por si no runner ou se
  falhou por ter sido tentado depois do `Null`, com a janela já aberta.
- Proposta, aguardando aprovação: no macOS, rodar `femapp_smoke` com o backend
  padrão (Metal) em vez de `null`, com o log do QRhi ligado, para descobrir se
  Metal funciona no runner. Se funcionar, o REQ-002-29 precisa de uma emenda
  para o macOS; se não, a decisão volta para o mantenedor.

**2026-11-11 — Execução nº 10 do CI, commit `c2def87` (T90, em andamento):**

- <https://github.com/vj-fkx/femapp/actions/runs/43018282248>
- `windows`, `linux-gcc`, `linux-clang` (com `clang-tidy`) e `format`:
  passaram.
- `macos`: `femapp_smoke` continua em "viewport unavailable", agora com Metal
  como primeira tentativa. O log mostra só "QRhiWidget: No QRhi" para Metal e
  para OpenGL; nenhuma linha das categorias `qt.rhi.*` apareceu, o que sugere
  que o Qt nem chegou a tentar criar o QRhi da janela.
- Hipóteses, nenhuma confirmada: (a) o runner de macOS do GitHub não oferece
  composição por GPU para janelas Qt; (b) o `QRhiWidget` é pintado uma vez
  antes de a janela estar pronta no macOS, e o `Viewport` desiste do backend
  cedo demais, na primeira falha. A hipótese (b) seria um defeito real no
  macOS, não só no CI.
- Os outros 39 testes passam no macOS, incluindo o `SceneRenderer` com o
  backend `Null` e `femapp_smoke_no_gpu`.

**2026-11-11 — Execução nº 11 do CI, commit `e59bfcb` (T90, em andamento):**

- <https://github.com/vj-fkx/femapp/actions/runs/43028271839>
- `Viewport` passou a repetir o quadro até cinco vezes antes de desistir de um
  backend, registrando plataforma e estado da janela a cada falha.
- `macos`: continua falhando, mas o diagnóstico descartou a hipótese de
  "pintura cedo demais": as falhas persistem com a janela já exposta, e o tipo
  de superfície da janela fica em 0 (raster) o tempo todo. Ou seja, a janela
  principal nunca foi trocada para composição por GPU.
- Causa provável (não confirmada): o `Viewport` era construído já como filho
  da janela, depois de `menuBar()`. O Qt só troca a janela para composição por
  GPU quando um `QRhiWidget` é reparentado para dentro dela; se o identificador
  nativo da janela já existe (o que a barra de menus pode provocar no macOS),
  a troca não acontece. No Windows a janela nativa só é criada no `show()`,
  por isso o problema não aparecia.
- Correção candidata, validada só no Windows (40 de 40 testes): criar o
  `Viewport` primeiro e sem pai, deixando `setCentralWidget` fazer o
  reparentamento. Se a causa for essa, é um defeito real no macOS, não só do
  CI.
- A recompilação local do OpenCASCADE pelo vcpkg (cerca de 40 min) aconteceu
  nesta sessão sem mudança no projeto; causa não investigada.

**2026-11-11 — Execução nº 12 do CI, commit `7126f36` (T90):**

- <https://github.com/vj-fkx/femapp/actions/runs/43029686897> — os cinco jobs
  passaram. No macOS o `femapp_smoke` renderizou um quadro com Metal.
- Causa confirmada: o `Viewport` construído como filho de uma janela cuja
  janela nativa já existia ficava sem QRhi no macOS. Criá-lo primeiro e sem pai
  resolveu. Era um defeito real do aplicativo no macOS, não do CI.
- Limpeza feita depois, ainda não verificada no CI:
  - o `femapp_smoke` do macOS voltou a usar `FEM_RHI_BACKEND=null`, como nas
    outras plataformas e como pede o REQ-002-29. A falha anterior com `null`
    no macOS tinha, ao que tudo indica, a mesma causa; só a próxima execução
    confirma;
  - as novas tentativas antes de trocar de backend foram removidas (a hipótese
    que as motivou foi descartada); ficou só a linha de diagnóstico no log.
  - Local (Windows): 40 de 40 testes, sem avisos, `clang-tidy` limpo.
- A T90 só fecha com a próxima execução verde.

**2026-11-11 — Execução nº 13 do CI, commit `9c90973` (T90, em andamento):**

- <https://github.com/vj-fkx/femapp/actions/runs/43032618946>
- `macos`, `windows` e `format`: passaram. No macOS o `femapp_smoke` passou com
  `FEM_RHI_BACKEND=null`, o que confirma que a falha anterior com `null` tinha
  a mesma causa (viewport sem QRhi) e dispensa a emenda ao REQ-002-29.
- `linux-gcc` e `linux-clang`: mais de 15 minutos parados no
  `sudo apt-get update` (espelho de pacotes do Ubuntu sem resposta). Não
  chegaram a compilar; resultado ainda desconhecido quando esta nota foi
  escrita.
- Os dois jobs de Linux falharam com código 100 no `apt-get` ("Temporary
  failure resolving 'azure.archive.ubuntu.com'"). Reexecutados uma vez; a
  segunda tentativa encontrou o mesmo espelho sem resposta. Problema da
  infraestrutura do GitHub; o código da limpeza ainda não foi compilado nem
  testado em Linux.
- A reexecução também falhou com código 100 no `apt-get` nos dois jobs de
  Linux. A página de status do GitHub não registrava incidente. Acrescentado
  `Acquire::Retries=5` às chamadas do `apt-get` no workflow; isso só ajuda em
  falhas passageiras, não se o espelho ficar fora do ar por muito tempo.

**2026-11-11 — T29 (medição do mantenedor, build Release, Windows, Direct3D 11):**

- Navegação: média de 120 quadros por segundo. Meta NFR-002-01: ≥ 60.
- Repouso: sem consumo de CPU nem de GPU pelo femapp (Gerenciador de
  Tarefas). Atende NFR-002-02 e, por consequência, o AC-11.
- Ressalvas: o modelo da GPU e a taxa de atualização do monitor não foram
  informados, então não dá para afirmar que a medição foi feita em GPU
  integrada, como o NFR-002-01 especifica. A medição foi com a cena desta
  feature (grade e eixos), no Windows apenas.

**2026-11-11 — Execução nº 14 do CI, commit `c2892ea` (T90 concluída):**

- <https://github.com/vj-fkx/femapp/actions/runs/43109270385> — os cinco jobs
  passaram, incluindo `linux-gcc` e `linux-clang` (com `clang-tidy`). É a
  primeira execução verde com a limpeza do `Viewport` em todas as plataformas.

**2026-11-11 — T92, conferência dos critérios de aceite (em andamento):**

- Atendidos (11 de 14):
  - AC-1, AC-5, AC-7 (menu e atalhos), AC-8, AC-9, AC-14: roteiro manual do
    mantenedor (T28).
  - AC-4: `femapp_smoke_no_gpu`. AC-6, AC-7 (vistas e enquadramento):
    `test_camera`. AC-12: execução nº 14. AC-13: `check_rhi_confined`.
  - AC-11: medição do mantenedor (T29), sem consumo de CPU/GPU em repouso. O
    contador de quadros em si não é exibido; a evidência é indireta.
- Pendentes:
  - AC-2: Direct3D 11 no "Sobre" confirmado no Windows (T28). No macOS o CI
    renderizou com Metal na execução nº 12, mas o "Sobre" não foi visto; no
    Linux, Vulkan nunca foi exercitado (o CI usa `null`).
  - AC-3: verificado em 2026-11-11 que um valor inválido (`bogus`) e um
    backend de outra plataforma (`metal`) registram aviso e caem no padrão, e
    que `opengl` renderiza um quadro (com `QT_FORCE_STDERR_LOGGING=1`, já que
    no Windows o log do Qt vai para o depurador). Falta ver "OpenGL" na caixa
    "Sobre".
  - AC-10: redimensionar e mover entre telas com escalas diferentes não foi
    confirmado explicitamente.

**2026-11-11 — Conferência do mantenedor e defeito encontrado (T92):**

- AC-3: "Sobre" mostra OpenGL com `FEM_RHI_BACKEND=opengl`. AC-10:
  redimensionar sem distorção nem borrão. Ambos confirmados pelo mantenedor.
- **Defeito:** com zoom muito grande, as linhas da grade somem (inclusive os
  eixos X e Y). AC-8 reaberto.
- Causa provável, pelo design §5.1: o zoom ancora no ponto sob o cursor no
  *plano de foco* (perpendicular à visada, passando pelo alvo), e não no plano
  da grade. Fora do centro da tela esse ponto fica acima ou abaixo de Z = 0,
  então cada passo de zoom tira o alvo do plano da grade. A altura do alvo
  converge para uma fração da distância inicial enquanto a distância tende a
  zero; o plano da grade acaba além do plano de corte distante (100 × d) e, antes
  disso, as linhas ficam densas demais (o espaçamento é calculado no alvo) e
  esmaecem.
- Proposta de alteração no design, aguardando aprovação: zoom e pan ancoram no
  ponto onde o raio do cursor encontra o plano de trabalho Z = 0, quando esse
  ponto existe à frente da câmera e dentro do intervalo de profundidade; caso
  contrário, continuam usando o plano de foco.

**2026-11-11 — Correção dos dois defeitos da grade (alteração do design aprovada):**

- Zoom e pan passam a ancorar no plano de trabalho Z = 0 quando o raio do
  cursor o encontra à frente da câmera e dentro do intervalo de profundidade;
  senão, no plano de foco (design §5.1 atualizado). Novos métodos
  `Camera::workPlanePoint` e `Camera::anchorPoint`.
- Eixos X e Y da grade passam a esmaecer no horizonte junto com as linhas
  principais (`grid.frag`), em vez de seguir até a borda do quadrilátero.
- Testes novos: zoom repetido fora do centro mantém o alvo em Z = 0 e a grade
  dentro do intervalo de profundidade; zoom e pan acima do horizonte usam o
  plano de foco; pan no plano mantém a altura do alvo.
- Local (Windows): 43 de 43 testes, sem avisos, `clang-format` e `clang-tidy`
  limpos. Falta a conferência visual do mantenedor (AC-8) e o CI.

**2026-11-11 — Fechamento (T92, T93):**

- AC-8: conferido pelo mantenedor depois da correção (grade visível com zoom
  grande, eixos esmaecendo no horizonte, pan sobre a grade).
- T92: os 14 critérios de aceite marcados. Ressalvas que ficam:
  - AC-2: backend nativo no "Sobre" conferido só no Windows (Direct3D 11). No
    macOS o CI renderizou com Metal (execução nº 12), sem que ninguém visse a
    janela; no Linux o Vulkan nunca foi exercitado, só o backend `null` sob
    `xvfb`.
  - NFR-002-01: medido em uma GPU de modelo não informado; não está provado
    para GPU integrada.
  - AC-11: evidência indireta (consumo zero em repouso); o contador de quadros
    não é exibido.
- T93: roadmap atualizado para `concluída`. README e `CLAUDE.md` já descrevem
  o Qt Shader Tools e o `xvfb-run`; os comandos de build não mudaram.
- O CI do commit de fechamento ainda não tinha terminado quando esta nota foi
  escrita.
