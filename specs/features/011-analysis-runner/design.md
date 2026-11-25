# Design — 011 analysis-runner

Status: aprovado · Data: 2026-11-24
Requisitos: [requirements.md](requirements.md)

## 1. Resumo da abordagem

A feature se divide em três camadas, como a 010.

**`core` (sem Qt).** Ganha um controle de execução, o `RunControl`, que vai
nas opções de todas as análises (ADR 0011):
- uma bandeira atômica de cancelamento, lida entre as etapas e por bloco de
  elementos na montagem e na recuperação;
- um retorno de progresso, chamado de qualquer thread, que só grava
  números.

Cancelar devolve um `Error` marcado como cancelado. Sem controle, nada muda.
O `StaticResult` ganha a resultante das cargas aplicadas, para o resumo de
equilíbrio.

**`setup` (sem Qt).** Ganha:
- as opções da análise no documento, editáveis e desfazíveis;
- a função que roda um caso: montar, conferir e analisar;
- o conjunto de resultados por caso, com a identidade do estado analisado;
- o resumo de um resultado, como dados.

As revisões do documento e do desenho passam a voltar com o desfazer. Assim,
um resultado volta a ser atual quando o estado analisado volta.

**`app`.** Ganha:
- o `AnalysisJob`, que roda numa thread de trabalho sobre um instantâneo
  (cópia do documento e do desenho, mais a malha compartilhada), caso a caso;
- o progresso lido por um timer da interface;
- o menu Analysis, o diálogo de opções, o progresso na barra de status, o
  resumo no console, o estado na árvore e o dock Results, com tabelas
  virtuais.

## 2. Verificação da constituição

| Princípio | Atende? | Observação |
|-----------|---------|------------|
| Multiplataforma (§2) | Sim | `std::atomic`, `std::function` e `QThread`/`QtConcurrent`; nada de plataforma |
| GPU somente via QRhi (§3) | Sim | Nada de render novo; o realce usa a seleção do viewport que já existe |
| Camadas / `core` sem Qt (§4) | Sim | O controle de execução é C++ puro; a análise roda fora da thread da interface (§4.3) |
| Correção numérica (§5) | Sim | Sem controle, os resultados são idênticos bit a bit (teste); comparações com tolerância explícita |
| Simplicidade / dependências (§7) | Sim | Nenhuma dependência nova; o progresso é lido por um timer, sem fila de mensagens |

## 3. Módulos e arquivos afetados

| Módulo | Arquivo | Novo/Alterado | Responsabilidade |
|--------|---------|---------------|------------------|
| core | `include/fem/core/run_control.hpp` | novo | `RunControl`, `RunStage`, `RunProgress` (REQ-011-09) |
| core | `include/fem/core/expected.hpp` | alterado | `Error::cancelled` e `makeCancelled()` |
| core | `include/fem/core/static_analysis.hpp`, `static_analysis.cpp` | alterado | `StaticOptions::control`; verificações e progresso na montagem, fatoração, solução e recuperação; `StaticResult::appliedForce` (REQ-011-13) |
| core | `assembler.hpp/.cpp` | alterado | A montagem recebe o controle: cancelamento e fração por bloco |
| core | `second_order.cpp`, `buckling.cpp` | alterado | Verificação e progresso a cada iteração (das duas análises) |
| setup | `include/fem/setup/analysis.hpp`, `analysis.cpp` | novo | `AnalysisSettings`, `runCase`, `AnalysisResult`, `ResultSet`, `summarize` (REQ-011-01, 02, 05, 11, 12, 13) |
| setup | `document.hpp/.cpp` | alterado | As opções no documento e no `DocumentEdit`; a revisão que volta com o `revert` |
| setup | `convert.cpp` | alterado | Nada a converter (opções sem unidade): só o teste diz isso |
| geom | `drawing.hpp/.cpp` | alterado | A revisão que volta com o `revert` (como no documento) |
| app | `analysis_job.hpp/.cpp` | novo | Thread de trabalho, instantâneo, Run e Run all, cancelamento, progresso (REQ-011-03 a 08) |
| app | `analysis_text.hpp/.cpp` | novo | O resumo e as etapas em texto, por `tr()`, sem widgets (REQ-011-13) |
| app | `results_model.hpp/.cpp` | novo | `QAbstractTableModel` virtual das tabelas (REQ-011-14, NFR-011-04) |
| app | `results_panel.hpp/.cpp` | novo | O dock Results: caso, tabela, realce (REQ-011-14, 15) |
| app | `analysis_settings_dialog.hpp/.cpp` | novo | Tipo e opções (REQ-011-01) |
| app | `model_controller.*` | alterado | Dono do `ResultSet`; `resultsChanged()`; pushes das opções |
| app | `model_tree.cpp` | alterado | O estado do resultado em cada caso e combinação (REQ-011-16) |
| app | `main_window.*` | alterado | Menu Analysis, F5, Esc, progresso e botão na barra de status, falhas no console e na conferência (REQ-011-10, 17) |
| tests | `unit/core/test_run_control.cpp`, `unit/setup/test_analysis.cpp`, `unit/app/test_analysis_job.cpp`, `unit/app/test_results_model.cpp`, `unit/app/test_analysis_text.cpp`, `perf/bench_run_control.cpp` | novo | §8 |

## 4. Interfaces públicas

```cpp
// ---- core: include/fem/core/run_control.hpp (REQ-011-09) ----
namespace fem::core {

enum class RunStage : std::uint8_t { Assembly, Factorization, Solution, Recovery, Iteration };

struct RunProgress
{
    RunStage stage = RunStage::Assembly;
    double fraction = -1.0; // in [0, 1] within the stage; -1: unknown
    int iteration = 0;      // second order and buckling: 1, 2, ...; 0 outside them
};

// Optional control of a running analysis. The flag is set by another thread;
// `progress` may be called from any worker thread, often: it must be cheap
// and thread-safe (the app stores the numbers in atomics).
struct RunControl
{
    const std::atomic<bool>* cancel = nullptr;
    std::function<void(const RunProgress&)> progress;

    [[nodiscard]] bool cancelled() const noexcept;
    void report(const RunProgress& p) const; // no-op without `progress`
};

} // namespace fem::core

// ---- core: expected.hpp ----
struct Error
{
    std::string message;
    bool cancelled = false; // REQ-011-08: stopped by RunControl, not a failure
};
[[nodiscard]] tl::unexpected<Error> makeCancelled(); // "The analysis was cancelled."

// ---- core: static_analysis.hpp ----
struct StaticResult
{
    // ... as today ...
    // REQ-011-13: the resultant of the applied loads (the assembled load
    // vector summed over the u, v, w dofs, settlements apart), so that the
    // reactions can be checked against it.
    Eigen::Vector3d appliedForce = Eigen::Vector3d::Zero();
};
struct StaticOptions
{
    int threads = 0;
    RunControl control{}; // REQ-011-09; SecondOrderOptions and BucklingOptions carry it in `base`
};

// ---- setup: include/fem/setup/analysis.hpp ----
namespace fem::setup {

enum class AnalysisType : std::uint8_t { Linear, SecondOrder, Buckling };

// REQ-011-01: in the document, the defaults of the core.
struct AnalysisSettings
{
    AnalysisType type = AnalysisType::Linear;
    double tolerance = 1e-8; // second order
    int maxIterations = 50;  // second order
    int modes = 1;           // buckling
    friend bool operator==(const AnalysisSettings&, const AnalysisSettings&) = default;
};

// What was analysed: the result is current while these are the state's
// (REQ-011-12).
struct AnalysedState
{
    std::uint64_t documentRevision = 0;
    std::uint64_t drawingRevision = 0;
    std::uint64_t meshGeneration = 0;
};

using CoreResult = std::variant<core::StaticResult, core::SecondOrderResult, core::BucklingResult>;

struct AnalysisResult
{
    ItemId item{};
    AnalysisSettings settings;
    AnalysedState state;
    std::shared_ptr<const core::Mesh> mesh;
    CoreResult data;
    std::vector<BuildIssue> warnings; // REQ-011-05
};

// REQ-011-02, 05, 10: one case or combination: build (010), check, run.
// Errors of the check refuse it (the issues come back, nothing runs); a
// failure or a cancellation of the core comes back as its Error.
struct CaseOutcome
{
    std::optional<AnalysisResult> result;
    std::vector<BuildIssue> issues; // the check: errors, or the warnings of a result
    std::optional<core::Error> error;
};
[[nodiscard]] CaseOutcome runCase(const Document& document, const geom::Drawing& drawing,
                                  const MeshContext& mesh, ItemId item,
                                  const core::RunControl& control);

// REQ-011-11, 12: the results by item, and whether each is current.
class ResultSet
{
public:
    void put(AnalysisResult result); // replaces the item's
    void erase(ItemId item);         // the item left the document
    [[nodiscard]] const AnalysisResult* find(ItemId item) const;
    [[nodiscard]] std::span<const AnalysisResult> all() const;
    [[nodiscard]] static bool isCurrent(const AnalysisResult& r, const AnalysedState& now);
};

// REQ-011-13: the numbers of the summary; the app writes them.
struct Extreme
{
    double value = 0.0;
    core::ExternalId id = 0; // node or element
    int direction = 0;       // 0..2: x, y, z (or the force: N, Vy, Vz, T, My, Mz: 0..5)
};
struct Summary
{
    std::optional<Extreme> displacement, rotation; // largest magnitude, per component
    Eigen::Vector3d reactions = Eigen::Vector3d::Zero();
    Eigen::Vector3d applied = Eigen::Vector3d::Zero();
    std::array<std::optional<Extreme>, 6> barForces; // N, Vy, Vz, T, My, Mz over the bar ends
    int iterations = 0;                              // second order
    double change = 0.0;
    std::vector<double> factors; // buckling
    std::string note;
};
[[nodiscard]] Summary summarize(const AnalysisResult& result);

} // namespace fem::setup

// ---- setup: document.hpp ----
//   Document::analysis() / DocumentEdit::analysis (before, after);
//   changeAnalysis(document, settings) -> DocumentEdit, empty when equal.
//   Document::revision() and geom::Drawing::revision(): unique per state;
//   revert() restores the revision before the edit it reverts (LIFO).

// ---- app ----
class AnalysisJob : public QObject
{
    Q_OBJECT
public:
    // Copies the document and the drawing; refused (false) while running.
    bool start(const setup::Document& document, const geom::Drawing& drawing,
               const setup::MeshContext& mesh, std::vector<setup::ItemId> items);
    void cancel();
    [[nodiscard]] bool isRunning() const noexcept;
    // Read by a timer of the interface (atomics written by the worker).
    struct Progress { int index; int count; setup::ItemId item; core::RunProgress core; bool building; };
    [[nodiscard]] Progress progress() const;
signals:
    void caseFinished(const fem::setup::CaseOutcome& outcome); // one per item, in order
    void finished(bool cancelled);
};
```

## 5. Dados e algoritmos

### 5.1 Controle no `core` (REQ-011-09, NFR-011-02, 03)

- **Montagem** (`assemble`): o `parallel_for` por blocos de elementos testa
  `cancelled()` no início de cada bloco e pula o bloco se estiver cancelado.
  Um contador atômico de elementos feitos dá a fração, informada ao fim de
  cada bloco. Depois do laço, se cancelado, devolve `makeCancelled()`.
- **Fatoração e solução:** a verificação vem antes de cada uma. A fração é
  −1 (desconhecida), pois a fatoração não informa o andamento.
- **Recuperação dos esforços:** o mesmo esquema da montagem.
- **Segunda ordem:** a cada iteração, testa o cancelamento e informa
  `Iteration` com `iteration = i` e `fraction = i / maxIterations`. As
  montagens e fatorações internas da iteração também verificam.
- **Flambagem:** verifica em cada iteração de subespaço, na contagem de
  Sturm e na bisseção, com `iteration` = contagem de passos.
- **Custo:** uma leitura atômica relaxada por bloco (≈ 10³ elementos) e uma
  chamada de `std::function` por bloco. É desprezível diante da montagem
  (NFR-011-03).
- **Sem controle:** `cancel` nulo e `progress` vazio pulam as chamadas. As
  operações numéricas são as mesmas e na mesma ordem, então o resultado é
  idêntico bit a bit (AC-4).

### 5.2 Resultante das cargas (REQ-011-13)

`appliedForce` é a soma do vetor de cargas montado nos GDL u, v e w de todos
os nós, antes de impor os apoios. Inclui as cargas nodais, as nodais
equivalentes das cargas no vão e o peso próprio, que preservam a resultante.
Os recalques ficam de fora, porque são deslocamentos impostos. Em equilíbrio,
a soma das reações mais `appliedForce` dá zero.

### 5.3 Opções e revisões no documento (REQ-011-01, 12)

- As opções ficam no `Document`. O `DocumentEdit` ganha
  `std::optional<std::pair<AnalysisSettings, AnalysisSettings>> analysis`,
  aplicado e revertido como as unidades. Mudar as opções não converte nada.
- **Revisão que volta.** `Document` e `Drawing` guardam uma pilha das
  revisões anteriores:
  - `apply` empilha a revisão atual e dá uma nova, única (de um contador
    que só cresce);
  - `revert` desempilha e restaura a revisão anterior. O desfazer é LIFO,
    então a revisão volta ao estado anterior à edição desfeita.
- **Efeito na 011:** um resultado volta a ser atual quando se desfaz até o
  estado analisado.
- **Efeito na 009 e na 010:** desfazer a edição que deixou a malha
  desatualizada também volta a deixá-la atual, o que é mais correto.
- **Limite:** refazer dá uma revisão nova. Um resultado feito *depois* de
  uma edição, desfeita e refeita, fica desatualizado, embora o conteúdo seja
  o mesmo. Fica registrado em §10.

### 5.4 Execução (REQ-011-03 a 08, NFR-011-01, 02)

- **`start`** copia o `Document` (tipo valor), o `Drawing` e o
  `MeshContext` (a malha é `shared_ptr<const>`). Recebe a lista de itens:
  o ativo, ou todos os casos e combinações em ordem de id. A cópia do
  desenho de 10⁵ entidades leva poucos ms, abaixo dos 100 ms do NFR.
- **A thread de trabalho** (`QThread::create`, como na 009) faz, para cada
  item:
  1. publica `index`, `item` e `building = true`;
  2. chama `runCase` (a montagem e a conferência da 010, depois a análise
     do `core` com o `RunControl` do job);
  3. emite `caseFinished` com o resultado, os problemas ou o erro, por
     conexão enfileirada;
  4. com o cancelamento ligado, para.
- **Ao fim**, emite `finished(cancelled)`.
- **O progresso** é gravado em atômicos pelo retorno do `core`. Um `QTimer`
  de 100 ms na interface lê `progress()` e atualiza a barra de status. Não
  há sinal por chamada, então a fila não enche.
- **Um pedido durante a execução** é recusado com aviso na barra de status
  (REQ-011-07).
- **`cancel()`** liga a bandeira. O `core` para na próxima verificação, e o
  item em andamento volta como cancelado, sem resultado. Os itens já
  concluídos já chegaram pelo `caseFinished`.

### 5.5 Resultados e desatualização (REQ-011-11, 12, 16)

- O `ModelController` guarda o `ResultSet`. Para cada `caseFinished`:
  - com resultado, chama `put`;
  - com falha ou recusa, mantém o resultado anterior, que fica
    desatualizado como qualquer outro (REQ-011-10);
  - em todos os casos, emite `resultsChanged`.
- **"Atual"** quer dizer que o `AnalysedState` do resultado é igual ao
  estado de agora: as revisões do documento e do desenho, mais a geração da
  malha. Não há cópia nem comparação de conteúdo.
- **A árvore** mostra, ao lado de cada caso ou combinação com resultado, o
  tipo e o estado, por exemplo "linear · current" ou "buckling ·
  out of date". O texto sai de `ModelTree::label`, refeito em
  `resultsChanged`, `documentChanged` e `meshChanged`.
- **Item apagado:** o resultado de um caso ou combinação que sai do
  documento some com ele (`erase` no `documentChanged`).

### 5.6 Resumo (REQ-011-13)

`summarize` percorre o resultado:
- o maior |componente| dos deslocamentos e das rotações por nó;
- a soma das reações e o `appliedForce`;
- os maiores |N|, |Vy|, |Vz|, |T|, |My|, |Mz| entre as pontas de cada barra
  (as vigas pelo `BeamResult::start()`/`end()`, as treliças pelo
  `axialForce`);
- as iterações e a última variação da segunda ordem;
- os fatores e a nota da flambagem.

O `analysis_text` monta as linhas com `tr()` e as unidades do documento
(`unitLabel`), com os ids externos.

### 5.7 Tabelas (REQ-011-14, 15, NFR-011-04)

- **O `ResultsModel`** (`QAbstractTableModel`) lê o `AnalysisResult` em
  `data()`, sem copiar. As linhas são calculadas na hora. São quatro
  tabelas:
  1. Displacements: id do nó, u, v, w, θx, θy, θz e, se houver, o
     empenamento;
  2. Reactions: só os nós com algum GDL apoiado;
  3. Bar forces: id do elemento, ponta (start ou end) e N, Vy, Vz, T, My,
     Mz;
  4. Buckling: uma linha por modo e nó, mais um seletor de modo no painel.
- **O cabeçalho** de cada coluna traz a unidade da sua grandeza.
- **O `ResultsPanel`** (dock à direita, junto com Properties) tem a lista
  de casos e combinações com resultado, a tabela e um aviso quando o
  resultado está desatualizado.
- **A seleção de linhas** dá os ids. O painel emite
  `idsRequested(NamedIds)`, que a `MainWindow` liga ao realce da 010
  (`highlightIds`).

### 5.8 Interface (REQ-011-17, 10)

- **Menu Analysis:**
  - Settings… (diálogo com o tipo e as opções; OK gera `changeAnalysis`
    no desfazer);
  - Run analysis (F5);
  - Run all;
  - Cancel (Esc). A ação só fica habilitada enquanto a análise roda, e
    nesse tempo o Esc cancela a análise antes de chegar ao desenho.
- **Barra de status:** a barra de progresso da 009 (determinada quando a
  fração é conhecida), um texto ("Analysing ULS (2 of 5): iteration 3,
  assembly 40%") e um botão Cancel.
- **Falhas:** a mensagem do `core` vai para o console e para a conferência
  como um problema de núcleo. Os ids dela vão para o `highlightIds`.
- **Recusa pela conferência:** a conferência abre com os problemas.

## 6. Fluxo

```
UI thread                                worker thread
---------                                -------------
F5 / Run all
  job.start(doc copy, drawing copy, mesh, items)
  timer 100 ms ──► job.progress() ──► status bar
                                         for item in items:
                                           progress.building = true
                                           runCase(...)                (setup)
                                             buildModel / checkModel  (010)
                                             run* (core, RunControl)
                                               assemble: per block → atomics, cancel?
                                               factorize / solve      → cancel?
                                               iterate               → atomics, cancel?
  ◄── caseFinished(outcome) ─────────────── emit (queued)
  ResultSet.put / console summary / tree
  Esc ──► job.cancel() ──► atomic flag ──► core returns makeCancelled()
  ◄── finished(cancelled) ───────────────── emit
  timer stops, status bar clears
```

## 7. Tratamento de erros

| Situação | Comportamento | Requisito |
|----------|---------------|-----------|
| Erros da conferência (sem material, alvos inválidos, sem malha) | O item não roda, a conferência abre com os problemas e o resultado anterior fica | REQ-011-05 |
| Mecanismo, carga crítica, sem convergência, flambagem sem compressão | A mensagem do `core` vai ao console e à conferência, os ids são realçados e o resultado anterior fica (desatualizado) | REQ-011-10 |
| Cancelamento | Volta `Error{cancelled = true}`: no console "Cancelled", sem resultado novo, e o Run all para | REQ-011-08 |
| Pedido durante uma execução | Recusado, com aviso na barra de status | REQ-011-07 |
| Item apagado durante a execução | O resultado que chega é guardado só se o item ainda existe | REQ-011-11 |
| Edição durante a execução | O resultado chega com o estado analisado (o do instantâneo) e já aparece desatualizado | REQ-011-06, 12 |
| Falha de um item no Run all | O próximo item roda, e o fim lista as falhas | REQ-011-04 |

## 8. Estratégia de testes

| Teste | Tipo | Requisito | Referência/tolerância |
|-------|------|-----------|-----------------------|
| Com um controle que não cancela, as três análises dão os mesmos números que sem controle | unitário (core) | REQ-011-09, AC-4 | igualdade bit a bit nos vetores de resultado, no mesmo binário |
| O cancelamento ligado antes da montagem, durante (pela primeira chamada de progresso) e na iteração k da segunda ordem e da flambagem devolve `cancelled` | unitário (core) | REQ-011-08, 09 | — |
| O progresso passa por Assembly → Factorization → Solution → Recovery, com frações crescentes; Iteration com i = 1, 2… | unitário (core) | REQ-011-07, 09 | — |
| `appliedForce` + Σ reações = 0 no pórtico, em cargas nodais, no vão e no peso próprio | unitário (core) | REQ-011-13 | 1e−9 relativo |
| `runCase` no pórtico 2×2 da 010: as três análises iguais às chamadas diretas do `core` sobre o modelo montado | unitário (setup) | REQ-011-02, AC-1 | 1e−12 |
| Recusa pela conferência, falha do `core` e cancelamento em `CaseOutcome` | unitário (setup) | REQ-011-05, 10, AC-3 | — |
| `ResultSet`: substituir, apagar, `isCurrent` com as revisões; desfazer volta a revisão e torna atual; refazer não | unitário (setup, geom) | REQ-011-11, 12, AC-5 | — |
| `summarize` com valores conhecidos (o pórtico, um balanço) | unitário (setup) | REQ-011-13 | 1e−12 |
| `AnalysisJob`: Run all de 2 casos, 1 combinação e 1 inválido, na ordem; recusa de um segundo `start`; cancelamento com a segunda ordem em andamento libera a thread em ≤ 1 s | unitário (app, laço de eventos) | REQ-011-04, 06, 07, 08, AC-2, AC-4, NFR-011-02 | — |
| Laço de eventos durante a análise de 10⁵ barras: maior intervalo entre timers ≤ 100 ms | perf (Release) | NFR-011-01 | — |
| `bench_run_control`: com e sem controle | perf (Release) | NFR-011-03 | ≤ 2% |
| `ResultsModel`: contagem de linhas, valores e cabeçalhos com unidades em cada tabela; a seleção dá os ids | unitário (app) | REQ-011-14, 15 | — |
| Textos do resumo e das etapas | unitário (app) | REQ-011-13, 17 | — |
| Roteiro manual: menu, progresso, Esc, dock, realce, 10⁵ linhas, árvore | manual | REQ-011-14 a 17, AC-6, NFR-011-04 | — |

## 9. Alternativas consideradas

| Alternativa | Por que foi descartada |
|-------------|------------------------|
| Cancelar só descartando o resultado | Recusada pelo mantenedor: a CPU continua ocupada e o Run all demora tanto quanto sem cancelar |
| Um sinal Qt por chamada de progresso | A montagem chama o progresso de várias threads, muitas vezes, e encheria a fila da interface; atômicos lidos por um timer custam nada |
| `std::stop_token` no lugar do atômico | Exige `std::jthread`/`stop_source` de ponta a ponta; um `const std::atomic<bool>*` é mais simples e basta |
| Interromper a fatoração (forçar a parada do solver esparso) | O solver não tem ponto de parada; matar a thread vaza recursos. A fatoração termina, e a próxima verificação para (fora de escopo nos requisitos) |
| Saber se o resultado é atual comparando o conteúdo (o modelo montado) | Remontar o modelo a cada edição custa até 0,1 s em 10⁵ barras; as revisões que voltam com o desfazer resolvem em O(1) |
| Guardar os estados pelo índice da pilha de desfazer | Um mesmo índice pode ser outro estado depois de desfazer e editar; a pilha não dá identidade aos estados |
| Resultados no `app` | Ficariam testáveis só com Qt; no `setup`, junto da montagem, são testados sem interface, como o resto da 010 |

## 10. Riscos e mitigação

- **Esc já cancela o comando do desenho e limpa a seleção.** → A ação Cancel
  da análise só fica habilitada durante a execução e tem prioridade nesse
  tempo. Sem análise, o Esc segue como hoje. Isso entra no roteiro manual.
- **O progresso da fatoração é desconhecido** e pode parecer parado em
  modelos grandes. → A barra de status mostra "factorization" com a barra
  indeterminada.
- **Revisão nova no refazer** (§5.3). → Fica documentada. Cobre o caso pedido
  (desfazer até o estado analisado). Para refazer, seria preciso guardar a
  revisão no comando de desfazer, o que fica para depois se fizer falta.
- **Mudar o comportamento da revisão do desenho afeta a 009 e a 010** (malha
  desatualizada). → Os testes de `geom` e `setup` cobrem o novo
  comportamento. O efeito é a malha voltar a ser atual ao desfazer a edição
  que a desatualizou, o que é correto.
- **Copiar o desenho a cada análise** custa memória em modelos grandes. →
  Uma cópia por execução, liberada ao fim. Para 10⁵ entidades, alguns MB.
- **A ordem de `caseFinished` e `finished`** entre threads. → Conexões
  enfileiradas para o mesmo objeto preservam a ordem. O teste do job confere.

## 11. Novas dependências

Nenhuma.
