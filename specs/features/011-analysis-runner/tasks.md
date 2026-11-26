# Tarefas — 011 analysis-runner

Status: em andamento
Design: [design.md](design.md) · Requisitos: [requirements.md](requirements.md)

Regras:
- Uma tarefa = uma mudança pequena, compilável e testável.
- Testes vêm antes ou junto da implementação que verificam.
- `[P]` marca tarefas que podem ser feitas em paralelo com a anterior.
- `[M]` marca tarefas que dependem de ação do mantenedor.
- Marque `[x]` somente com build e testes passando.
- Os IDs abaixo omitem o prefixo: `01` significa `REQ-011-01`; `N01`
  significa `NFR-011-01`.
- Em toda tarefa, as suítes da 001 a 010 continuam passando.
- Textos da interface em inglês, por `tr()`.
- Antes de cada push: o g++ 13 com `-Wall -Wextra -Wpedantic -Werror` e
  `-O2` nos arquivos alterados, e o clang-tidy, como aprendido no
  fechamento da 010.

## Fase 1 — Controle de execução no `core` (ADR 0011)

- [x] **T01** O `RunControl` e o cancelamento no `Error`:
  - `run_control.hpp` (`RunStage`, `RunProgress`, `RunControl` com
    `cancelled()` e `report()`);
  - `Error::cancelled` e `makeCancelled()`;
  - `StaticOptions::control`;
  - testes de `RunControl` sozinho: sem bandeira e sem retorno não faz
    nada; com a bandeira, `cancelled()` a segue.
  Arquivos: `core/include/fem/core/run_control.hpp`, `expected.hpp`,
  `static_analysis.hpp`, `tests/unit/core/test_run_control.cpp`. Cobre: 09.
- [x] **T02** A estática linear com controle (design §5.1):
  - testes primeiro:
    - com um controle que nunca cancela, o pórtico, a treliça e uma viga
      com cargas no vão dão exatamente os mesmos vetores que sem controle
      (igualdade bit a bit no mesmo binário, com 1 thread e com todas;
      números gravados no teste mudariam entre compiladores e com FMA);
    - o progresso passa por Assembly, Factorization, Solution e Recovery,
      com frações em [0, 1] crescentes na montagem;
    - o cancelamento ligado antes, na primeira chamada de progresso da
      montagem e na recuperação devolve `cancelled`;
  - depois a montagem por blocos com o atômico e o contador, as
    verificações entre as etapas e a recuperação.
  Arquivos: `assembler.hpp/.cpp`, `static_analysis.cpp`,
  `test_run_control.cpp`. Cobre: 07, 08, 09.
- [x] **T03** A segunda ordem e a flambagem com controle:
  - testes primeiro: com controle, bit a bit iguais a sem controle; o progresso traz
    `Iteration` com i = 1, 2…; o cancelamento na iteração k da segunda
    ordem, numa iteração de subespaço e na bisseção de Sturm da
    flambagem devolve `cancelled`;
  - depois as verificações e o progresso em `second_order.cpp` e
    `buckling.cpp`.
  Arquivos: `second_order.cpp`, `buckling.cpp`, `test_run_control.cpp`.
  Cobre: 07, 08, 09.
- [x] **T04** A resultante das cargas (design §5.2):
  - testes primeiro: `appliedForce` + Σ reações = 0 em 1e−9 com cargas
    nodais, distribuídas, concentradas no vão e peso próprio; um recalque
    não entra;
  - depois o campo e a soma do vetor de cargas.
  Arquivos: `static_analysis.hpp/.cpp`,
  `tests/unit/core/test_static_validation.cpp`. Cobre: 13.
- [x] **T05** `bench_run_control`: o pórtico do `bench_frame` (ou o maior
  pórtico de barras da suíte de perf), com e sem controle, em Release;
  a diferença ≤ 2%. Arquivo: `tests/perf/bench_run_control.cpp`.
  Cobre: N03.
  - Medido em 2026-11-25, máquina do mantenedor: 4 961 nós, 13 640 barras,
    medianas de 7 rodadas intercaladas, cerca de 12 300 chamadas de
    progresso por análise; diferença de −0,6% e −0,8% em duas execuções
    (ruído). O prédio do `bench_frame` passou para
    `tests/perf/frame_building.hpp`, usado pelos dois.

## Fase 1b — Onde está a instabilidade (REQ-011-18, design §5.2a)

*Acrescentada em 2026-11-25.*

- [x] **T21** `elementEnergies` no `assembler`:
  - testes primeiro: numa barra, igual a uᵀKu da matriz montada; zero
    num movimento de corpo rígido; o mesmo com 1 thread e com todas;
  - depois a função.
  Arquivos: `assembler.hpp/.cpp`, `tests/unit/core/test_assembler.cpp`.
  Cobre: 18.
- [x] **T22** O modo e o sítio do mecanismo, e o GDL sem rigidez com os
  elementos do nó:
  - testes primeiro: quadrado articulado sem diagonal (exatamente as três
    barras que se movem); mecanismo num vão de um pórtico longo (só as
    barras do vão); GDL sem rigidez com os elementos do nó; a mesma
    mensagem com 1 thread e com todas;
  - depois `instability.hpp/.cpp` e as três mensagens de mecanismo (linear,
    segunda ordem, flambagem) pela mesma função.
  Arquivos: `src/core/detail/instability.hpp`, `src/core/instability.cpp`,
  `static_analysis.cpp`, `second_order.cpp`, `buckling.cpp`,
  `tests/unit/core/test_instability.cpp`; os testes de mensagem da 007 e
  da 008 ajustados ao texto novo. Cobre: 18.
- [x] **T23** A segunda ordem:
  - testes primeiro: pilar acima da crítica ao lado de um descarregado (a
    direção lateral, só os elementos comprimidos); instabilidade dentro
    de uma barra (o elemento e o plano y ou z);
  - depois `unstableSite` e a mensagem.
  Arquivos: `instability.cpp`, `second_order.cpp`,
  `tests/unit/core/test_instability.cpp`. Cobre: 18.
- [x] **T24** [P] `idsInMessage` lê as mensagens novas: os nós e os
  elementos citados, sem a contagem. Arquivo:
  `tests/unit/app/test_check_issues.cpp` (e o parser, se precisar).
  Cobre: 18.

## Fase 2 — Revisões que voltam com o desfazer

- [x] **T06** `Document` e `Drawing`: `apply` empilha a revisão e dá uma
  nova; `revert` restaura a anterior (design §5.3):
  - testes primeiro: aplicar, desfazer volta a revisão; aplicar de novo
    dá uma revisão nova, diferente de todas; a malha da 010 volta a ser
    atual ao desfazer a edição que a desatualizou; um comando de várias
    edições (LINE com três segmentos) desfeito volta à revisão de antes
    dele, e um comando que se anula (LINE desfeito com U) volta na hora;
  - depois as pilhas nos dois tipos e `Drawing::foldApplies`, chamada por
    `CommandSession::finish` *(acrescentado na T06, 2026-11-25)*.
  Arquivos: `geom/include/fem/geom/drawing.hpp`, `drawing.cpp`,
  `geom/commands.hpp`, `commands.cpp`, `tests/unit/geom/test_commands.cpp`,
  `setup/include/fem/setup/document.hpp`, `document.cpp`,
  `tests/unit/geom/test_drawing.cpp`, `tests/unit/setup/test_document.cpp`,
  `tests/unit/setup/test_build.cpp`. Cobre: 12.

## Fase 3 — `setup`: opções, execução, resultados e resumo

- [x] **T07** As opções no documento:
  - `AnalysisSettings` e `AnalysisType` em `analysis_settings.hpp` (à parte
    do `analysis.hpp`, que inclui o `build.hpp`, que inclui o documento);
  - `Document::analysis()`, `DocumentEdit::analysis` e
    `changeAnalysis`, aplicados e revertidos como as unidades;
  - testes: padrões do `core`, a troca desfeita e refeita, a edição vazia
    quando igual, a conversão de unidades sem efeito.
  Arquivos: `setup/include/fem/setup/analysis_settings.hpp`,
  `document.hpp/.cpp`, `editing.hpp/.cpp`,
  `tests/unit/setup/test_analysis.cpp` (o `convert.cpp` não precisou mudar:
  as opções não têm unidade). Cobre: 01.
- [x] **T08** `runCase` (design §4, §5.4):
  - testes primeiro: no pórtico 2×2 da 010 (o `Frame` de
    `test_build.cpp`), as três análises iguais às chamadas diretas do
    `core` sobre o modelo montado, em 1e−12; uma combinação; a recusa
    pela conferência (sem material) com os problemas e sem análise; a
    falha do `core` (mecanismo, flambagem sem compressão) no `error`; o
    cancelamento; os avisos (treliça) num resultado;
  - depois a função.
  Arquivos: `analysis.hpp/.cpp`, `test_analysis.cpp`; o pórtico da 010
  passou para `tests/unit/setup/frame_document.hpp`, e a conferência do
  `core` para `src/setup/detail/check.hpp`, para o `runCase` montar o
  modelo uma vez só. Cobre: 02, 05, 10.
- [x] **T09** `ResultSet` e `isCurrent`:
  - testes: `put` substitui, `erase`, `all` em ordem de item;
    `isCurrent` com as três partes do estado; desfazer até o estado
    analisado torna atual; refazer não (o limite do design §5.3);
  - depois a classe.
  Arquivos: `analysis.hpp/.cpp`, `test_analysis.cpp`. Cobre: 11, 12.
- [x] **T10** `summarize` (design §5.6):
  - testes com valores conhecidos: o balanço com carga na ponta
    (deslocamento e momento máximos à mão), o pórtico (equilíbrio), a
    segunda ordem (iterações), a flambagem (fatores e nota);
  - depois a função.
  Arquivos: `analysis.hpp/.cpp`, `test_analysis.cpp`. Cobre: 13.

## Fase 4 — App: execução

- [x] **T11** `AnalysisJob` (design §5.4):
  - testes primeiro, com laço de eventos:
    - o Run all de dois casos, uma combinação e um caso inválido emite
      `caseFinished` na ordem, com três resultados e uma recusa, e depois
      `finished(false)`;
    - um segundo `start` durante a execução é recusado;
    - o cancelamento com a segunda ordem em andamento libera a thread em
      ≤ 1 s, com `finished(true)` e sem os casos seguintes;
    - `progress()` mostra o índice, o caso e a etapa;
  - depois o job, sobre `QThread::create`, como a 009.
  Arquivos: `src/app/analysis_job.hpp/.cpp`,
  `tests/unit/app/test_analysis_job.cpp`; o `QCoreApplication` dos testes
  passou para `tests/unit/app/test_application.hpp`, um só para o
  executável. O caso em andamento volta pelo `caseFinished` como
  cancelado. Cobre: 03, 04, 06, 07, 08, N02.
- [x] **T12** `ModelController`: dono do `ResultSet`:
  - `put` no `caseFinished` (só se o item existe), `erase` dos itens
    apagados, `resultsChanged()`, `push` das opções;
  - testes: os resultados chegam, um item apagado não guarda, a troca
    das opções entra no desfazer.
  Arquivos: `model_controller.hpp/.cpp`,
  `tests/unit/app/test_model_controller.cpp`. Cobre: 01, 11, 12.
- [x] **T13** Textos do resumo e das etapas:
  - `analysis_text`: as linhas do resumo nas unidades do documento, com
    os ids externos; o texto do progresso ("Analysing ULS (2 of 5):
    iteration 3, assembly 40%"); as falhas e o cancelamento;
  - testes sem widgets.
  Arquivos: `src/app/analysis_text.hpp/.cpp`,
  `tests/unit/app/test_analysis_text.cpp`. Cobre: 07, 13.
- [ ] **T14** O NFR da interface livre: um teste de perf (Release) roda a
  análise linear do pórtico do `bench_line_mesh` pelo `AnalysisJob` e
  mede o maior intervalo entre os disparos de um timer de 10 ms na
  thread da interface: ≤ 100 ms. Também mede o tempo até liberar a
  thread depois do cancelamento na montagem. Arquivo:
  `tests/perf/bench_analysis_job.cpp`. Cobre: N01, N02.

## Fase 5 — App: interface

- [ ] **T15** Menu Analysis e barra de status:
  - Settings… (o diálogo do tipo e das opções), Run analysis (F5), Run
    all, Cancel (Esc só habilitado durante a execução);
  - o progresso pelo timer de 100 ms, a barra e o botão Cancel;
  - o resumo no console;
  - as falhas no console e na conferência, com o realce dos ids;
  - a recusa pela conferência abrindo o dock.
  Arquivos: `analysis_settings_dialog.hpp/.cpp`, `main_window.hpp/.cpp`.
  Cobre: 01, 05, 07, 08, 10, 13, 17.
- [ ] **T16** O estado na árvore: o texto do caso e da combinação com o
  tipo e "current"/"out of date", refeito em `resultsChanged`,
  `documentChanged` e `meshChanged`; testes no `ModelTree`. Arquivos:
  `model_tree.hpp/.cpp`, `tests/unit/app/test_model_tree.cpp`. Cobre: 16.
- [ ] **T17** `ResultsModel`:
  - testes primeiro: o número de linhas e os valores de cada tabela
    (deslocamentos, reações só nos nós apoiados, esforços nas duas
    pontas, modos), os cabeçalhos com as unidades, os ids das linhas
    selecionadas;
  - depois o modelo virtual.
  Arquivos: `src/app/results_model.hpp/.cpp`,
  `tests/unit/app/test_results_model.cpp`. Cobre: 14, 15, N04.
- [ ] **T18** O dock Results: a lista dos casos com resultado, a escolha
  da tabela e do modo, o aviso de desatualizado e a seleção levando ao
  `highlightIds`. Arquivos: `results_panel.hpp/.cpp`, `main_window.cpp`.
  Cobre: 14, 15, 16.

## Fase 6 — Fechamento

- [ ] **T19** [M] Roteiro manual com o mantenedor (AC-6; pode ser feito
  junto com os roteiros adiados da 009 e da 010):
  1. Pórtico 2×2 da 010; Analysis ▸ Settings: linear; F5: o progresso
     aparece, o resumo vai ao console, a árvore marca "linear · current".
  2. O dock Results: as quatro tabelas, as unidades, selecionar linhas
     realça nós e barras.
  3. Editar uma carga: a árvore e o dock dizem "out of date"; desfazer:
     "current".
  4. Segunda ordem e flambagem com Run all: dois casos e uma combinação.
  5. Um caso sem apoio: o mecanismo no console, com o nó realçado.
  6. O pórtico do `bench_line_mesh`: Esc durante a montagem cancela; a
     interface responde durante a análise; 10⁵ linhas rolam no dock.
  7. Esc sem análise rodando: cancela o comando do desenho, como antes.
  Cobre: 14 a 17, N01, N04.
- [ ] **T20** Documentação: `architecture.md` (o `RunControl` no `core`,
  a execução e os resultados no `setup` e no `app`, a revisão que volta
  com o desfazer) e o README (o menu Analysis). Cobre: 17.
- [ ] **T90** [M] Build e testes verdes nos jobs do CI, inclusive a
  cobertura de `core`, `mesher` e `setup`; push com sua confirmação.
  Cobre: N05.
- [ ] **T91** `clang-format` e `clang-tidy` sem pendências (também o do
  Linux); `core`, `geom`, `mesher` e `setup` sem headers Qt. Cobre: 09.
- [ ] **T92** Conferir os sete critérios de aceite.
- [ ] **T93** Atualizar `specs/roadmap.md` para `concluída`.

## Matriz de rastreabilidade

| Requisito | Tarefas | Testes |
|-----------|---------|--------|
| REQ-011-01 | T07, T12, T15 | `test_analysis.cpp`, `test_model_controller.cpp`, roteiro T19 |
| REQ-011-02 | T08 | `test_analysis.cpp` |
| REQ-011-03 | T11, T15 | `test_analysis_job.cpp`, roteiro T19 |
| REQ-011-04 | T11 | `test_analysis_job.cpp` |
| REQ-011-05 | T08, T15 | `test_analysis.cpp`, roteiro T19 |
| REQ-011-06 | T11, T14 | `test_analysis_job.cpp`, `bench_analysis_job.cpp` |
| REQ-011-07 | T02, T03, T11, T13, T15 | `test_run_control.cpp`, `test_analysis_job.cpp`, `test_analysis_text.cpp` |
| REQ-011-08 | T02, T03, T11, T15 | `test_run_control.cpp`, `test_analysis_job.cpp`, roteiro T19 |
| REQ-011-09 | T01, T02, T03, T91 | `test_run_control.cpp`, `check_no_qt_includes` |
| REQ-011-10 | T08, T15 | `test_analysis.cpp`, roteiro T19 |
| REQ-011-11 | T09, T12 | `test_analysis.cpp`, `test_model_controller.cpp` |
| REQ-011-12 | T06, T09, T12 | `test_drawing.cpp`, `test_document.cpp`, `test_analysis.cpp` |
| REQ-011-13 | T04, T10, T13, T15 | `test_static_validation.cpp`, `test_analysis.cpp`, `test_analysis_text.cpp` |
| REQ-011-14 | T17, T18 | `test_results_model.cpp`, roteiro T19 |
| REQ-011-15 | T17, T18 | `test_results_model.cpp`, roteiro T19 |
| REQ-011-16 | T16, T18 | `test_model_tree.cpp`, roteiro T19 |
| REQ-011-17 | T15, T20 | roteiro T19 |
| REQ-011-18 | T21, T22, T23, T24 | `test_assembler.cpp`, `test_instability.cpp`, `test_check_issues.cpp` |
| NFR-011-01 | T14, T19 | `bench_analysis_job.cpp`, roteiro T19 |
| NFR-011-02 | T11, T14 | `test_analysis_job.cpp`, `bench_analysis_job.cpp` |
| NFR-011-03 | T05 | `bench_run_control.cpp` |
| NFR-011-04 | T17, T19 | `test_results_model.cpp`, roteiro T19 |
| NFR-011-05 | T90 | job de cobertura do CI |

| Critério | Tarefas |
|----------|---------|
| AC-1 | T08, T10 |
| AC-2 | T09, T11 |
| AC-3 | T08, T15, T22, T23, T24 |
| AC-4 | T02, T03, T11, T14 |
| AC-5 | T06, T09, T16 |
| AC-6 | T15 a T19 |
| AC-7 | T05, T90 |
