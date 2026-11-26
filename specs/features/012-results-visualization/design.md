# Design — 012 results-visualization

Status: aprovado · Data: 2026-11-26
Requisitos: [requirements.md](requirements.md)

## 1. Resumo da abordagem

A feature tem três camadas, como as anteriores.

1. **`core` amostra o resultado.** Ele percorre cada barra em pontos
   (estações), com a posição original, o deslocamento global e o valor de uma
   grandeza. Para isso ganha `result_sampling.hpp`, sem Qt e paralelo com o
   oneTBB. O `BeamResult` passa a dizer onde fica cada ponto da barra
   (`placementAt`), coisa que hoje não sabe.
2. **`render` desenha.** Os pontos viram segmentos grossos instanciados, como
   as arestas selecionadas da 004. O vertex shader soma fator × deslocamento,
   e o fragment shader colore por uma textura de escala de cores. Trocar a
   escala, a faixa ou as faixas de cor só muda uniformes. Trocar a grandeza só
   reenvia um buffer de valores. A animação só muda o fator a cada quadro. Os
   textos (legenda, cabeçalho, reações, extremos e consulta) são desenhados
   por um widget de sobreposição com `QPainter`, como o `SelectionOverlay` da
   004.
3. **`app` coordena.** O `ResultView` escolhe o resultado (o do dock Results),
   roda a amostragem numa `QThread`, monta os textos com as unidades do
   documento e liga a barra de opções no dock. Os símbolos da 010 ganham as
   reações e escondem as cargas.

## 2. Verificação da constituição

| Princípio | Atende? | Observação |
|-----------|---------|------------|
| Multiplataforma (§2) | Sim | Nada de plataforma. A escala de cores é uma textura 2D de 256×1, e não 1D, que o QRhi não oferece em todos os backends. |
| GPU somente via QRhi (§3) | Sim | Uma nova camada (`ResultLayer`) no módulo `render`, com os shaders em GLSL 440 compilados pelo `qsb`. |
| Camadas / `core` sem Qt (§4) | Sim | A amostragem e os extremos ficam no `core`. O `render` só lê o `core`, e as unidades e os textos ficam no `app`. A amostragem roda fora da thread da interface (§4.3). |
| Correção numérica (§5) | Sim | Os pontos são os do `BeamResult::at` em double, conferidos em 1e−12. A flecha e o momento da viga biapoiada são conferidos com a solução analítica em 1e−9. A consulta reavalia o `core` no x clicado. As unidades aparecem na legenda e nos rótulos (§5.2). |
| Simplicidade / dependências (§7) | Sim | Nenhuma dependência nova. O texto reusa o padrão `QPainter` sobre o viewport, que já existe, e não cria um atlas de fontes na GPU. |

## 3. Módulos e arquivos afetados

| Módulo | Arquivo | Novo/Alterado | Responsabilidade |
|--------|---------|---------------|------------------|
| core | `include/fem/core/beam_result.hpp`, `beam_result.cpp` | alterado | `Data::origin` e `placementAt(x)`: a posição e os eixos globais num ponto da barra (§5.1) |
| core | `static_analysis.cpp`, `stability_analysis.cpp`, `beam_iso.cpp` | alterado | Preenchem `origin` onde montam o `BeamResult` |
| core | `include/fem/core/result_sampling.hpp`, `result_sampling.cpp` | novo | Estações, amostragem da forma e da grandeza, extremos e fator automático (§5.2 a §5.4) |
| render | `include/fem/render/result_scene.hpp`, `result_scene.cpp` | novo | `ResultScene` (float, relativa à origem) a partir das amostras; os valores em float |
| render | `include/fem/render/colormap.hpp`, `colormap.cpp` | novo | Escala azul→vermelho, faixas, a imagem da textura |
| render | `include/fem/render/result_picker.hpp`, `result_picker.cpp` | novo | O clique sobre a forma desenhada → barra e x, ou nó |
| render | `include/fem/render/result_display.hpp` | novo | `ResultDisplay` (opções por quadro) e `ResultAnnotations` (textos) |
| render | `result_layer.hpp`, `result_layer.cpp` | novo | Buffers, pipelines e desenho dos segmentos, da forma original e dos nós realçados |
| render | `result_overlay.hpp`, `result_overlay.cpp` | novo | Widget com `QPainter`: legenda, cabeçalho e rótulos |
| render | `shaders/result_line.vert`, `result_line.frag`, `result_point.vert` | novo | Segmento grosso deformado e colorido; ponto do nó realçado |
| render | `scene_renderer.*`, `viewport.*`, `include/fem/render/viewport.hpp` | alterado | O passe de resultados, a animação, o clique de consulta |
| render | `include/fem/render/symbols.hpp`, `symbol_layer.cpp` | alterado | `reactionForces` e `reactionMoments`, numa cor própria |
| app | `result_display_settings.hpp`, `.cpp` | novo | Opções do usuário, grandezas por tipo de resultado, unidades, formatação e textos, sem widgets |
| app | `result_view.hpp`, `.cpp` | novo | Controlador: resultado escolhido, trabalho de amostragem, ligação com o viewport |
| app | `result_display_bar.hpp`, `.cpp` | novo | A barra de opções no dock Results |
| app | `results_panel.*`, `results_model.*` | alterado | O caso e o modo escolhidos saem por sinal; os números usam `formatResult` (REQ-012-19) |
| app | `symbol_builder.*`, `model_controller.*`, `main_window.*` | alterado | Cargas escondidas e reações; o menu View; o `ResultView` |
| tests | `unit/core/test_result_sampling.cpp`, `unit/render/test_colormap.cpp`, `unit/render/test_result_scene.cpp`, `unit/app/test_result_display.cpp`, `unit/app/test_result_view.cpp` | novo | §8 |
| tests | `unit/render/test_rhi_smoke.cpp` (ou o teste de fumaça atual) | alterado | Um quadro com a camada de resultados no backend `Null` |
| specs | `architecture.md` | alterado | §3 e §4, ao fim da implementação |

## 4. Interfaces públicas

```cpp
// ---- core/beam_result.hpp (alterado) -------------------------------------
namespace fem::core {

// Feature 012: where a point of the centroid axis of a bar is.
struct BeamPlacement
{
    Eigen::Vector3d position = Eigen::Vector3d::Zero(); // global
    Eigen::Matrix3d axes = Eigen::Matrix3d::Identity(); // principal, columns, global
};

class BeamResult
{
public:
    struct Data
    {
        // ... as today ...
        // Feature 012: the global position of the start of the flexible
        // length (the start node moved by offsets[0]).
        Eigen::Vector3d origin = Eigen::Vector3d::Zero();
    };
    // REQ-012-05: x as in at(). Straight bars: origin + x axes.col(0), axes
    // constant. Isoparametric bars: on the flexible curve, with the axes
    // that isoBeamAt uses at that arc length.
    [[nodiscard]] BeamPlacement placementAt(double x) const;
    [[nodiscard]] const Eigen::Vector3d& origin() const { return m_data.origin; }
    [[nodiscard]] std::span<const SpanPoint> concentrated() const;     // loads.points
    [[nodiscard]] std::span<const double> junctions() const;           // 008c parts
};

} // namespace fem::core

// ---- core/result_sampling.hpp (novo) -------------------------------------
namespace fem::core {

// REQ-012-09: what the bars can be colored by.
enum class ResultQuantity : std::uint8_t {
    Ux, Uy, Uz, UMagnitude,   // global
    Rx, Ry, Rz,               // global
    N, Vy, Vz, T, My, Mz,     // principal local axes (REQ-008-21)
};
inline constexpr int kResultQuantityCount = 13;
[[nodiscard]] bool isForce(ResultQuantity q) noexcept;

// The response to draw: a static result or a buckling mode.
struct ResultFields
{
    std::shared_ptr<const Mesh> mesh;
    std::span<const Eigen::Vector3d> displacements; // per node
    std::span<const Eigen::Vector3d> rotations;     // per node
    std::span<const std::optional<BeamResult>> beams;
    std::span<const double> axialForce;             // per element; empty for a mode
};
[[nodiscard]] ResultFields fieldsOf(const StaticResult& result);
[[nodiscard]] ResultFields fieldsOf(const StaticResult& base, const BucklingMode& mode);

// A point of a bar: x from the start of its flexible length, the side of a
// concentrated load, and whether it is the bar's node (rigid links, §5.2).
struct Station
{
    double x = 0.0;
    Side side = Side::After;
    NodeIndex node = -1; // the node itself, before or after a rigid link
};

struct SampledBar
{
    ElementIndex element = -1;
    std::uint32_t first = 0; // first sample
    std::uint32_t count = 0; // samples; consecutive ones make a segment
};

struct ShapeSamples
{
    std::vector<SampledBar> bars;
    std::vector<Station> stations;           // per sample
    std::vector<Eigen::Vector3d> positions;  // per sample, global, original
    std::vector<Eigen::Vector3d> displacements; // per sample, global
    BoundingBox bounds;                      // of the original positions
};

struct SampleOptions
{
    int perBeam = 0;             // 0: from kSampleBudget (§5.2); else odd >= 3
    int threads = 0;             // 0: all cores
};
inline constexpr std::size_t kSampleBudget = 1'000'000;

// REQ-012-05: the stations of every bar and the deformed shape there.
[[nodiscard]] ShapeSamples sampleShape(const ResultFields& fields, const SampleOptions& options = {});

struct Extreme
{
    double value = 0.0;
    std::uint32_t sample = 0;
};
struct FieldSamples
{
    ResultQuantity quantity = ResultQuantity::UMagnitude;
    std::vector<double> values; // per sample
    Extreme min;
    Extreme max;
};

// REQ-012-09, 10, 17: the quantity at every sample, from at() (each side of
// a load), and its extremes. Forces are per bar, never averaged.
[[nodiscard]] FieldSamples sampleField(const ResultFields& fields, const ShapeSamples& shape,
                                       ResultQuantity quantity, int threads = 0);

// REQ-012-18: the quantity at one point, straight from the core.
[[nodiscard]] double valueAt(const ResultFields& fields, ElementIndex element, const Station& at,
                             ResultQuantity quantity);
[[nodiscard]] Eigen::Vector3d displacementAt(const ResultFields& fields, ElementIndex element,
                                             const Station& at);

// REQ-012-06: the factor that makes the largest displacement 10% of the
// largest side of the bounds; 1 when nothing moves.
inline constexpr double kAutoScaleFraction = 0.1;
[[nodiscard]] double autoScale(const ShapeSamples& shape) noexcept;

} // namespace fem::core

// ---- render/colormap.hpp (novo) ------------------------------------------
namespace fem::render {
// REQ-012-12: blue (0) -> cyan -> green -> yellow -> red (1).
[[nodiscard]] Rgba colormapColor(double t) noexcept;
// The band of a value: floor((v - min) / (max - min) * bands), clamped to
// [0, bands - 1]; a value on a limit goes to the upper band, max to the last.
[[nodiscard]] int bandOf(double value, double min, double max, int bands) noexcept;
// The color drawn for a value: continuous (bands == 0) or the centre of its
// band; outside [min, max], the color of the nearest end (REQ-012-11).
[[nodiscard]] Rgba valueColor(double value, double min, double max, int bands) noexcept;
[[nodiscard]] QImage colormapImage(int width = 256);
}

// ---- render/result_scene.hpp (novo) --------------------------------------
namespace fem::render {
// Built on a worker thread from core::ShapeSamples; immutable (as MeshScene).
class ResultScene
{
public:
    const Eigen::Vector3d& origin() const;
    std::span<const Eigen::Vector3f> positions() const;     // per sample, from origin
    std::span<const Eigen::Vector3f> displacements() const; // per sample
    std::span<const std::uint32_t> segmentStarts() const;  // sample i -> i + 1
    std::span<const core::ElementIndex> segmentElements() const;
    std::span<const core::SampledBar> bars() const;
    std::span<const core::Station> stations() const;
    // Per node of a bar: one of its samples (selection and labels).
    std::span<const std::uint32_t> nodeSamples() const;     // -1 when none
    const std::shared_ptr<const core::Mesh>& mesh() const;
    std::size_t gpuBytes() const;                          // NFR-012-04
};
[[nodiscard]] std::shared_ptr<const ResultScene> buildResultScene(const core::ShapeSamples& shape,
                                                                  std::shared_ptr<const core::Mesh> mesh);
// The values of one quantity, two per segment (its two ends).
[[nodiscard]] std::shared_ptr<const std::vector<float>> segmentValues(const ResultScene& scene,
                                                                      const core::FieldSamples& field);
}

// ---- render/result_display.hpp (novo) ------------------------------------
namespace fem::render {
enum class Animation : std::uint8_t { Off, Static, Mode }; // 0..f, or -f..f
struct ResultDisplay
{
    bool deformed = true;   // REQ-012-07
    bool original = false;  // REQ-012-07: thin, neutral
    double scale = 1.0;     // REQ-012-06
    bool colored = true;    // REQ-012-09: false = the 1D color of the 004
    double min = 0.0, max = 1.0; // REQ-012-11
    int bands = 0;          // REQ-012-12: 0 = continuous
    Animation animation = Animation::Off; // REQ-012-15
    double period = 2.0;    // seconds per cycle
    bool playing = false;
};
struct ResultLabel
{
    enum class Kind : std::uint8_t { Query, Extreme, Reaction };
    Kind kind = Kind::Reaction;
    Eigen::Vector3d at = Eigen::Vector3d::Zero();           // original, global
    Eigen::Vector3d displacement = Eigen::Vector3d::Zero(); // scaled with the shape
    QString text;
};
struct ResultLegend
{
    QString title;                                // "Mz [kN·m]"
    double min = 0.0, max = 0.0;
    int bands = 0;
    std::vector<std::pair<double, QString>> ticks; // formatted by the app
};
struct ResultAnnotations
{
    std::optional<ResultLegend> legend;
    QStringList headline;   // scale, mode and factor, out of date
    bool warning = false;   // the last headline line is a warning (REQ-012-02)
    std::vector<ResultLabel> labels;
};
}

// ---- render/result_picker.hpp (novo) -------------------------------------
namespace fem::render {
struct ResultPick
{
    core::ElementIndex element = -1;
    core::Station station;    // x interpolated on the segment clicked
    core::NodeIndex node = -1; // a node within the tolerance wins
};
// REQ-012-18: the point of the shape drawn (scale given) nearest to the
// pixel, within tolerancePx; nothing otherwise.
[[nodiscard]] std::optional<ResultPick> pickResult(const ResultScene& scene, double scale,
                                                   const Camera& camera, Vec2 pixel,
                                                   double tolerancePx = 6.0);
}

// ---- render/viewport.hpp (alterado) --------------------------------------
class Viewport
{
public:
    // REQ-012-01, 02: the result shown (nullptr: none) and its values.
    void setResultScene(std::shared_ptr<const ResultScene> scene,
                        std::shared_ptr<const std::vector<float>> values);
    void setResultValues(std::shared_ptr<const std::vector<float>> values); // REQ-012-13
    void setResultDisplay(const ResultDisplay& display);                    // REQ-012-08, 15
    void setResultAnnotations(ResultAnnotations annotations);
    void setResultsShown(bool shown);
    [[nodiscard]] bool resultsShown() const;
    [[nodiscard]] double animatedScale() const; // the factor of this frame
signals:
    void resultPicked(const fem::render::ResultPick& pick); // REQ-012-18
};

// ---- render/symbols.hpp (alterado) ---------------------------------------
struct SymbolSet
{
    // ... as today ...
    // REQ-012-16: reactions, in the reaction color; forces end at the node.
    std::vector<ArrowSymbol> reactionForces;
    std::vector<MomentSymbol> reactionMoments;
};

// ---- app/result_display_settings.hpp (novo, sem widgets) ----------------
namespace fem::app {
enum class ScaleMode : std::uint8_t { Automatic, User, Real };
struct ResultDisplaySettings
{
    core::ResultQuantity quantity = core::ResultQuantity::UMagnitude;
    bool colored = true;
    ScaleMode scaleMode = ScaleMode::Automatic;
    double userScale = 1.0;
    bool deformed = true, original = false;
    bool rangeFixed = false; double rangeMin = 0.0, rangeMax = 0.0;
    int bands = 0;
    bool reactions = true, extremes = true;
    bool playing = false; double period = 2.0;
};
// REQ-012-09: what a result offers, and what is shown when the chosen one
// is not offered (buckling: |u|).
[[nodiscard]] std::vector<core::ResultQuantity> quantitiesOf(const setup::AnalysisResult& r);
[[nodiscard]] core::ResultQuantity shownQuantity(const setup::AnalysisResult& r,
                                                 core::ResultQuantity wanted);
[[nodiscard]] QString quantityName(core::ResultQuantity q);          // "Mz", "|u|"
[[nodiscard]] setup::Quantity quantityUnit(core::ResultQuantity q);  // length, force…
// REQ-012-19: one format for the tables and the viewport.
[[nodiscard]] QString formatResult(double value);
// REQ-012-11, 14, 16, 17, 18: the texts, from the samples and the result.
struct AnnotationInput { /* result, mode, fields, shape, field, settings, scale, units,
                            current, query */ };
[[nodiscard]] render::ResultAnnotations annotationsFor(const AnnotationInput& in);
[[nodiscard]] render::ResultDisplay displayFor(const ResultDisplaySettings& s,
                                               const core::FieldSamples& field,
                                               double autoScale, bool buckling);
}

// ---- app/result_view.hpp (novo) ------------------------------------------
namespace fem::app {
class ResultView : public QObject
{
    Q_OBJECT
public:
    ResultView(ModelController* controller, render::Viewport* viewport, QObject* parent = nullptr);
    void setShown(bool shown);                            // REQ-012-01
    void showResult(setup::ItemId item, int mode);        // from the Results dock
    void setSettings(const ResultDisplaySettings& s);
    [[nodiscard]] const ResultDisplaySettings& settings() const;
    [[nodiscard]] bool isAvailable() const;
    // For the tests (REQ-012-08, 13): scene and value builds started so far.
    [[nodiscard]] int sceneBuilds() const;
    [[nodiscard]] int valueBuilds() const;
signals:
    void availabilityChanged(bool available);
    void ready(); // the scene or the values of the last request are shown
};
}
```

O `ResultsPanel` ganha o sinal `resultChosen(ItemId, int mode)`, emitido
quando muda o caso ou o modo. A barra de opções (`ResultDisplayBar`) fica no
alto do dock, acima da lista de casos.

## 5. Dados e algoritmos

### 5.1 Onde fica cada ponto da barra

Hoje o `BeamResult` dá os esforços e o deslocamento em x, nos eixos locais,
mas não onde x fica no espaço. Ele ganha `Data::origin`, a posição global do
início do comprimento flexível: o nó inicial mais `offsets[0]`. Quem monta o
`BeamResult` já tem os dois.

`placementAt(x)` faz o seguinte:
- **barra reta** (exata ou não): `origin + x·axes.col(0)`, com os eixos
  constantes;
- **barra isoparamétrica** (`Line3`): o ponto da curva flexível no parâmetro
  `isoParameter(nós, x)`, com os eixos que o `pointAt` de `beam_iso.cpp` já
  calcula ali. São os mesmos que `isoBeamAt` usa para dar o deslocamento
  local. Assim, `axes · displacement` é o deslocamento global sem
  ambiguidade.

O `origin` padrão (zero) não muda nenhum resultado existente. Os testes de
hoje continuam valendo, e um teste novo confere `placementAt(0)` e
`placementAt(length)` contra os nós mais os `offsets`.

### 5.2 Estações e amostragem da forma

As estações de uma viga são, em ordem de x:
1. o nó inicial, se `offsets[0] ≠ 0` (ligação rígida, §5.2.1);
2. `perBeam` pontos uniformes de 0 a L, com `perBeam` ímpar, de modo que o
   meio é um deles (REQ-012-05);
3. em cada carga concentrada no vão (`loads.points`), dois pontos no mesmo
   x, com `Side::Before` e `Side::After` (REQ-012-10);
4. as junções das partes (008c, nos modos de flambagem);
5. o nó final, se `offsets[1] ≠ 0`.

Estações repetidas (mesmo x e mesmo lado) são fundidas.

`perBeam` vem do orçamento de amostras: `perBeam = clamp(ímpar(kSampleBudget / nBarras), 5, 17)`.
Isso dá 17 pontos até cerca de 6·10⁴ barras e 9 com 10⁵ barras. Um pórtico
modelado com uma barra por peça tem a curva lisa. A malha fina do
`bench_line_mesh` já tem barras curtas, e 9 pontos bastam. Os testes passam
`perBeam` explícito.

Por amostra:
- `positions = placementAt(x).position`;
- `displacements = placementAt(x).axes · at(x, side).displacement`.

Para as **treliças** (sem `BeamResult`, com `axialForce` finito), são duas
amostras: os nós, com os deslocamentos nodais. Elementos que não são barras
ficam de fora.

O laço é sobre as barras com `tbb::parallel_for`, em dois passos. O primeiro
conta as amostras de cada barra. Depois vem uma soma de prefixos, e o segundo
passo preenche. O resultado é o mesmo com qualquer número de threads.

#### 5.2.1 Ligações rígidas

Num nó com excentricidade, o segmento do nó ao início flexível é desenhado
reto. Ele vai do nó deslocado ao ponto flexível deslocado. Os valores desse
segmento são os da ponta flexível vizinha.

### 5.3 A grandeza e os extremos

`sampleField` avalia `at(x, side)` de novo em cada amostra. A outra opção
seria guardar o `BeamPoint` de todas: 128 bytes por amostra, cerca de 120 MB
com 10⁵ barras. Por amostra:
- **deslocamentos e rotações:** globais, `axes · local`, e o módulo
  `|u|`;
- **esforços:** os de `BeamForces`, nos eixos principais locais;
- **treliças:** só N (`axialForce`), e os outros esforços são 0;
- **ligações rígidas:** o valor da ponta flexível vizinha;
- **modos de flambagem:** só deslocamentos e rotações, porque o `app` não
  pede esforços.

Os extremos são o mínimo e o máximo dos valores em double, com a amostra.
Num empate, vale a primeira amostra. O id do nó ou da barra e o x saem da
amostra (§5.6).

`valueAt` e `displacementAt` fazem a mesma conta num ponto só. É o que a
consulta usa (REQ-012-18), e por isso o valor é o do `core` no x clicado, e
não o da amostra mais próxima.

### 5.4 Fator automático

```
autoScale = kAutoScaleFraction · max(lados de bounds) / max_i |displacements_i|
```

Se o maior deslocamento for zero (ou não finito), o fator é 1. O fator real
do REQ-012-06 é 1. O fator do usuário é guardado nas `ResultDisplaySettings`
e mantido ao trocar de caso (REQ-012-04). O automático é recalculado para
cada resultado.

### 5.5 GPU

**`ResultScene`.** Posições relativas a `origin` (o centro da caixa, como
na `MeshScene`, REQ-004-18) e deslocamentos, ambos em float. Um segmento por
par de amostras consecutivas de uma barra. Os segmentos de comprimento zero
(os dois lados de uma carga) são pulados.

**Buffers da `ResultLayer`:**

| Buffer | Conteúdo por segmento | Bytes |
|--------|------------------------|-------|
| geometria (instância) | posição e deslocamento das duas pontas, 4 × vec3 | 48 |
| valores (instância) | valor das duas pontas, 2 × float | 8 |
| bandeiras (instância) | elemento selecionado (realce) | 1, arredondado a 4 |

São 60 bytes por segmento, abaixo dos 64 do NFR-012-04. Há um segmento por
amostra, menos uma por barra. Trocar a grandeza reenvia só os valores
(REQ-012-13). Trocar a seleção reenvia só as bandeiras.

**`result_line.vert`.** É o quad de tela do `mesh_segment.vert` (3 px vezes
a razão de pixels), com `p = posição + escala·deslocamento` nas duas pontas
e `escala` num uniforme (REQ-012-08). O valor de cada ponta vai ao fragment
shader interpolado.

**`result_line.frag`.** Faz `t = (v − min)/(max − min)`, limitado a [0, 1]:
- com faixas: `t = (floor(t·N) + 0.5)/N`, e N − 1 quando `t = 1`, a mesma
  regra de `bandOf`;
- a cor vem da textura 256×1 de `colormapImage()`, com amostragem linear;
- sem cor: a cor 1D da 004 (`dimensionColor(1)`);
- selecionado: a cor de realce.

**Forma original** (REQ-012-07): o mesmo pipeline com escala 0, uma
bandeira de cor neutra num uniforme e 1 px de largura. É desenhada antes da
deformada.

**Nós realçados** (REQ-011-15 na exibição de resultados): pontos em
`posição + escala·deslocamento` das amostras de `nodeSamples`, com o shader
de pontos da 004 adaptado (`result_point.vert`).

**Animação** (REQ-012-15): o `Viewport` tem um `QElapsedTimer` e pede um
quadro novo a cada vsync enquanto toca. A escala do quadro é:
- `Static`: `f·(1 − cos(2πt/T))/2`, de 0 a f;
- `Mode`: `f·sin(2πt/T)`, de −f a +f.

Nada é reenviado, só o uniforme.

**Passe.** Com a exibição de resultados ligada e uma `ResultScene`, o
`SceneRenderer` desenha a `ResultLayer` no lugar da `MeshGpu`. Depois vêm a
grade, os símbolos e os eixos, como hoje. Os elementos de outras dimensões
não aparecem: as análises da 011 são só de barras.

### 5.6 Textos (sobreposição)

O `ResultOverlay` é um `QWidget` filho do viewport, transparente aos
eventos, como o `SelectionOverlay`. Ele desenha:
- **legenda** (REQ-012-11, 12): no canto inferior direito, com o título e a
  unidade, a barra de cores pintada por `valueColor` (as mesmas cores do
  shader) e os ticks formatados pelo `app`. São 6 ticks contínuos, ou um por
  limite de faixa até 11.
- **cabeçalho**: no canto superior esquerdo, com o caso, "Scale ×f
  (auto)", "Mode k: factor λ" e, por último, em laranja, o aviso de
  desatualizado (REQ-012-02, 14).
- **rótulos**: em `at + escala·displacement`, projetados com a `Camera` a
  cada quadro. A ordem de prioridade é consulta, depois extremos, depois
  reações. Um rótulo que cobriria outro já desenhado é pulado, e os que
  ficam fora da tela também.

O `app` monta os textos (`annotationsFor`):
- **reações**: os componentes com |valor| > 1e−9 · (o maior componente do
  mesmo tipo), por exemplo "Fy = 12.5 kN", um rótulo por nó com as linhas
  dos componentes. As setas e os arcos vão no `SymbolSet` (`reactionForces`
  e `reactionMoments`), no sentido da reação e em verde, para não confundir
  com as cargas.
- **extremos**: "max Mz = 45.2 kN·m · bar 17, x = 2.5 m", ou "node 12" quando
  a amostra é um nó (x = 0 ou L, sem ligação rígida, ou a estação do nó).
- **consulta**: o id, o x, o valor de `valueAt` e o `displacementAt`.

### 5.7 Grandezas, unidades e formato

| Grandeza | Nome | Unidade (`setup::Quantity`) |
|----------|------|-----------------------------|
| Ux, Uy, Uz, \|u\| | u, v, w, \|u\| | comprimento |
| Rx, Ry, Rz | θx, θy, θz | rad (adimensional) |
| N, Vy, Vz | N, Vy, Vz | força |
| T, My, Mz | T, My, Mz | momento |

Num modo de flambagem a legenda diz "(normalized)" e não leva unidade, como
a tabela da 011. `formatResult(v)` é `QLocale().toString(v, 'g', 6)`. O
dock Results passa a usá-lo num delegate, e assim o viewport e as tabelas
mostram o mesmo texto (REQ-012-19).

## 6. Fluxo

```
Dock Results (caso, modo) ──resultChosen──► ResultView::showResult
ModelController::resultsChanged / documentChanged / meshChanged ─► ResultView::refresh
                                   │
             (exibição ligada e resultado existe)
                                   ▼
   QThread::create ─► fieldsOf → sampleShape → sampleField(q) → buildResultScene,
                      segmentValues, autoScale            (geração n)
                                   │ finished (fila)
                                   ▼
   geração ainda é a última? ─► Viewport::setResultScene(scene, values)
                               Viewport::setResultDisplay(displayFor(...))
                               Viewport::setResultAnnotations(annotationsFor(...))
                               ModelController::setSymbolOptions(loads off, reactions)

Troca de grandeza ─► QThread: sampleField → segmentValues ─► setResultValues
Troca de escala, faixa, faixas, original, animação ─► setResultDisplay (só uniformes)
Clique ─► Viewport (pickResult com animatedScale) ─resultPicked─► ResultView:
          valueAt, displacementAt ─► setResultAnnotations (com a consulta)
```

- Cada pedido tem um número de geração. O resultado de um trabalho velho é
  descartado ao chegar. Só um trabalho roda por vez, e um pedido que chega
  durante ele espera o fim e roda o último.
- O trabalho usa um `shared_ptr` do `ResultSet` copiado ao iniciar (os
  resultados são imutáveis depois de guardados). Uma nova análise durante o
  trabalho não o atinge.
- **Seleção.** Na exibição de resultados, o clique esquerdo consulta e
  seleciona o nó ou o elemento clicado. A seleção por janela continua
  usando a malha original. O realce só aparece se a malha do resultado for
  a mesma da cena do viewport (como o REQ-011-15).
- **Desligar a exibição** devolve o passe da malha, os símbolos de cargas e o
  clique normal. A cena de resultados fica guardada para religar sem
  refazer.

## 7. Tratamento de erros

| Situação | Comportamento | Requisito |
|----------|---------------|-----------|
| Nenhum resultado escolhido, ou o resultado sumiu (item apagado) | A ação View ▸ Show Results fica desabilitada, a exibição é desligada e o viewport volta ao normal | REQ-012-01 |
| Resultado desatualizado | Desenha sobre a malha guardada, com a linha de aviso no cabeçalho | REQ-012-02 |
| A malha do resultado não é a da cena do viewport | Sem realce da seleção; a consulta continua | REQ-012-02 |
| Grandeza pedida que o resultado não tem (esforço num modo) | Mostra \|u\| e guarda a escolha para o próximo resultado estático | REQ-012-09 |
| Todos os valores iguais (min = max) | Faixa [v − 1, v + 1] se v ≠ 0, senão [−1, 1]; uma cor só | REQ-012-11 |
| Faixa fixada com min ≥ max | O campo recusa; a faixa anterior continua | REQ-012-11 |
| Nenhum deslocamento | Fator 1 e deformada igual à original | REQ-012-06 |
| Valor não finito de `at()` | A amostra entra sem cor (a do 1D) e fica fora dos extremos | REQ-012-09 |
| Clique fora da forma | Nada; a consulta anterior some | REQ-012-18 |
| Falha ao criar pipeline ou buffer | O mesmo tratamento dos outros passes: mensagem e tentativa do próximo backend | — |

## 8. Estratégia de testes

| Teste | Tipo | Requisito | Referência/tolerância |
|-------|------|-----------|-----------------------|
| `placementAt` nas pontas = nó + offset, reta e `Line3`; o meio da `Line3` na curva | unitário (core) | REQ-012-05 | 1e−12 |
| Viga biapoiada, carga q: as amostras são `placementAt` e `axes·at().displacement`; o meio tem a flecha 5qL⁴/384EI | verificação (core) | REQ-012-05, AC-1 | 1e−12 / 1e−9 |
| Estações: ímpares e com o meio; os dois lados de uma carga concentrada; junções; ligações rígidas; treliça com 2 amostras; `perBeam` pelo orçamento | unitário (core) | REQ-012-05, 10 | exato |
| `sampleField(Mz)` máximo qL²/8 no meio; Vy antes e depois da carga concentrada = `at(Before/After)`; um nó de pórtico com as barras sem média | verificação (core) | REQ-012-09, 10, AC-2 | 1e−9 |
| Grandezas globais numa barra girada 30° em torno de z | unitário (core) | REQ-012-09 | 1e−12 |
| `valueAt` em x fora das amostras = `at(x)`, e diferente da amostra vizinha | unitário (core) | REQ-012-18, AC-6 | 1e−12 |
| `autoScale` = 10% da maior dimensão / maior deslocamento; 1 sem deslocamento | unitário (core) | REQ-012-06, AC-1 | 1e−12 |
| Mesma amostragem com 1 e com todas as threads | unitário (core) | NFR-012-02 | bit a bit |
| `bandOf`, `valueColor`: N faixas dão N cores; limite cai na faixa de cima; fora da faixa, as pontas | unitário (render) | REQ-012-11, 12, AC-3 | exato |
| `buildResultScene`: segmentos, os de comprimento zero pulados, `nodeSamples`, `gpuBytes() ≤ 64 × amostras` | unitário (render) | NFR-012-04, AC-9 | exato |
| `pickResult`: o clique no meio de um segmento deformado dá a barra e o x; o nó ganha dentro da tolerância | unitário (render) | REQ-012-18 | 1e−6 relativo em x |
| Quadro com a `ResultLayer` (deformada, original, faixas) no backend `Null` | fumaça (render) | NFR-012-03 | sem erro |
| `quantitiesOf` / `shownQuantity`: flambagem sem esforços; volta a \|u\| | unitário (app) | REQ-012-09 | — |
| `annotationsFor`: reações do pórtico 2×2 com os valores, as unidades do documento e a soma = carga aplicada; extremos com o id; o cabeçalho do modo com o fator do `core` | unitário (app) | REQ-012-14, 16, 17, 19, AC-4, AC-5 | 1e−9 na soma |
| `formatResult` igual no delegate da tabela e nos rótulos | unitário (app) | REQ-012-19 | texto igual |
| `ResultView`: trocar de caso mantém a grandeza e a escala do usuário; trocar a escala ou as faixas não aumenta `sceneBuilds` nem `valueBuilds`; trocar a grandeza aumenta só `valueBuilds`; editar o modelo põe o aviso | unitário (app, offscreen) | REQ-012-02, 04, 08, 13, AC-7 | contadores |
| `ResultView` com o pórtico do `bench_line_mesh`: maior intervalo do laço de eventos ≤ 100 ms e preparação ≤ 2 s (Release) | benchmark (app) | NFR-012-02, AC-9 | como o NFR-011-01 |
| Roteiro manual: exibição, deformada sobre a original, cores e faixas, animação, reações, consulta, 30 FPS com o pórtico grande | manual | AC-8, NFR-012-01 | contador de FPS |

## 9. Alternativas consideradas

| Alternativa | Por que foi descartada |
|-------------|------------------------|
| Deformada reta entre os nós | Esconde a flexão no vão e a flambagem entre os nós. O mantenedor escolheu a curva exata. |
| Amostragem no `render` ou no `app` | Os dois não usam o oneTBB, e a amostragem é numérica, testável sem Qt e reutilizável na 013 (diagramas) e na 014 (`.vtu`). |
| Guardar o `BeamPoint` de todas as amostras | São 128 B por amostra, cerca de 120 MB com 10⁵ barras. Reavaliar `at()` ao trocar a grandeza custa menos de 1 s, fora da interface. |
| Texto na GPU com atlas de glifos | Mais código e um shader para algumas dezenas de rótulos. O `QPainter` sobre o viewport já é usado na 004. |
| Linhas de 1 px (`QRhiGraphicsPipeline::Lines`) | A largura de linha não é portável entre os backends, e 1 px não mostra a cor. O quad de tela da 004 resolve. |
| Textura 1D para a escala de cores | Não existe em todos os backends do QRhi. 256×1 em 2D dá o mesmo. |
| Recolorir na CPU (cor por vértice) | Mudar a faixa ou as faixas reenviaria tudo. Com o valor bruto no buffer e a faixa em uniformes, nada é reenviado. |
| Ordenar os rótulos por espaço livre (algoritmo de posicionamento) | YAGNI: pular os que se sobrepõem, por prioridade, basta para ler. |

## 10. Riscos e mitigação

- **`at()` caro em barras exatas ou isoparamétricas** (a isoparamétrica
  refaz a rigidez a cada chamada) → o orçamento de amostras limita o total,
  e o laço é paralelo. Se o NFR-012-02 falhar, o `BeamResult` ganha
  `at(span<const Station>)`, que prepara uma vez por barra. Isso fica medido
  no benchmark antes de otimizar.
- **30 FPS com cerca de 10⁶ segmentos instanciados e a forma original** →
  60 B por segmento, sem reenvio por quadro. Se não bastar, a forma original
  pode usar `perBeam = 2`. Isso é medido no roteiro manual.
- **Rótulos piscando durante a animação** → as posições são projetadas a
  cada quadro, com a escala do quadro (`animatedScale`), e a sobreposição é
  pintada depois do quadro (`frameRendered`).
- **Eixos da `Line3` diferentes dos de `isoBeamAt`** → `placementAt` usa o
  mesmo `pointAt`, e um teste compara o deslocamento global com a
  interpolação dos GDL nodais nos nós.
- **Extremos entre amostras** (um momento máximo fora das estações) → o meio
  e as cargas concentradas são estações. O máximo exato num ponto qualquer
  fica para a 013, nos diagramas.

## 11. Novas dependências

Nenhuma.
