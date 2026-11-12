# Design — 004 mesh-rendering

Status: rascunho · Data: 2026-11-12
Requisitos: [requirements.md](requirements.md)

## 1. Resumo da abordagem

A malha passa por três etapas, as duas primeiras fora da thread da interface:

1. **`core::extractSurface`** — faces visíveis (as dos elementos 2D e as da
   casca externa dos 3D, orientadas para fora) e suas arestas, sem Qt nem GPU.
2. **`render::buildMeshScene`** — a partir da malha e da superfície, monta uma
   `MeshScene` imutável: vértices em `float` relativos ao centro da malha,
   faixas por dimensão, tabelas de cor e as estruturas de seleção. Sem GPU.
3. **`SceneRenderer`** — envia a cena à GPU uma vez e desenha faces, arestas e
   pontos com três pipelines novos; seleção e cores só reescrevem um buffer de
   cor.

A **seleção é calculada na CPU** (raio contra os triângulos visíveis para o
clique; projeção dos vértices para janela e cruzamento), em vez do passe de
IDs na GPU previsto na arquitetura — ver [ADR 0003](../../decisions/0003-selecao-na-cpu.md).
A importação roda numa `QThread` e entrega a `MeshScene` pronta ao `Viewport`.

## 2. Verificação da constituição

| Princípio | Atende? | Observação |
|-----------|---------|------------|
| Multiplataforma (§2) | Sim | Só QRhi, Qt Core/Gui/Widgets e C++20; smoke test com malha nos quatro jobs |
| GPU somente via QRhi (§3) | Sim | Três pipelines novos em `SceneRenderer`; shaders GLSL 440 por `qsb`; QRhi segue confinado a `src/render` |
| Camadas / `core` sem Qt (§4) | Sim | Extração da superfície em `core`; cena e seleção em `render` sem tipos de GPU nas interfaces; importação em `app` |
| Correção numérica (§5) | Sim | Posições relativas ao centro da malha, deslocamento até o alvo calculado em `double` a cada quadro (REQ-004-18); orientação das faces conferida por teste de volume |
| Simplicidade / dependências (§7) | Sim | Nenhuma dependência nova (thread com `QThread`, de Qt Core); seleção na CPU dispensa leitura assíncrona da GPU |

## 3. Módulos e arquivos afetados

| Módulo | Arquivo | Novo/Alterado | Responsabilidade |
|--------|---------|---------------|------------------|
| core | `include/fem/core/mesh_surface.hpp`, `mesh_surface.cpp` | novo | Faces locais por família; `extractSurface` |
| render | `include/fem/render/mesh_scene.hpp`, `mesh_scene.cpp` | novo | `MeshScene`, `buildMeshScene`, cores por dimensão e grupo |
| render | `include/fem/render/mesh_picker.hpp`, `mesh_picker.cpp` | novo | Clique, janela e cruzamento, sem GPU |
| render | `include/fem/render/selection.hpp`, `selection.cpp` | novo | Conjunto selecionado e modo |
| render | `include/fem/render/display_options.hpp` | novo | Modos de exibição, cor e visibilidade |
| render | `mesh_gpu.hpp`, `mesh_gpu.cpp` (privados) | novo | Buffers e pipelines de faces, linhas e pontos |
| render | `selection_overlay.hpp/.cpp` (privados) | novo | Retângulo de janela/cruzamento (widget filho com `QPainter`) |
| render | `shaders/mesh_face.*`, `mesh_line.*`, `mesh_point.*` | novo | Shaders |
| render | `scene_renderer.hpp/.cpp`, `viewport.hpp/.cpp` | alterado | Malha no quadro; mouse para seleção; novos slots e sinais |
| app | `mesh_import.hpp/.cpp` | novo | Leitura e montagem da cena numa `QThread` |
| app | `main_window.hpp/.cpp`, `cli_options.hpp/.cpp`, `main.cpp` | alterado | Menus Arquivo/Editar/Exibir, arrastar arquivo, barra de status, linha de comando, smoke test |
| app | `i18n/femapp_pt_BR.ts` | alterado | Textos novos |
| tests | `unit/core/test_mesh_surface.cpp`, `unit/render/test_mesh_scene.cpp`, `test_mesh_picker.cpp`, `unit/app/test_cli_options.cpp` | novo | Testes sem GPU |
| tests | `CMakeLists.txt` | alterado | `femapp_smoke_mesh` |
| docs | `specs/architecture.md`, ADR 0003 | alterado/novo | Seleção na CPU |

## 4. Interfaces públicas

```cpp
// fem/core/mesh_surface.hpp — REQ-004-09, 10, 30
namespace fem::core {

// Local faces of a solid family, by vertex index, ordered so that the normal
// (right-hand rule) points out of the element. Gmsh vertex numbering.
struct LocalFace
{
    std::array<std::uint8_t, 4> vertices;
    std::uint8_t vertexCount; // 3 or 4
};
[[nodiscard]] std::span<const LocalFace> elementFaces(ElementFamily family) noexcept;

struct SurfaceFace
{
    ElementIndex element;
    std::array<NodeIndex, 4> nodes; // vertices only; nodes[3] unused for triangles
    std::uint8_t nodeCount;         // 3 or 4
};

struct SurfaceEdge
{
    NodeIndex a;
    NodeIndex b;
    ElementIndex element; // an element that owns the edge (for its color)
};

// What is visible of a mesh, by vertices only (REQ-004-10).
struct MeshSurface
{
    std::vector<SurfaceFace> faces2d; // faces of 2D elements, as given
    std::vector<SurfaceFace> faces3d; // faces owned by exactly one 3D element, outward
    std::vector<SurfaceEdge> edges2d; // unique edges of faces2d
    std::vector<SurfaceEdge> edges3d; // unique edges of faces3d
};

[[nodiscard]] MeshSurface extractSurface(const Mesh& mesh);

} // namespace fem::core
```

```cpp
// fem/render/display_options.hpp — REQ-004-11, 14, 16, 19
namespace fem::render {
enum class DisplayMode : std::uint8_t { ShadedWithEdges, Shaded, Wireframe };
enum class ColorMode : std::uint8_t { ByDimension, ByGroup };
enum class SelectionMode : std::uint8_t { Elements, Nodes };

struct DisplayOptions
{
    DisplayMode mode = DisplayMode::ShadedWithEdges;
    ColorMode color = ColorMode::ByDimension;
    std::array<bool, 4> dimensionVisible = {true, true, true, true}; // 0D..3D
};
} // namespace fem::render
```

```cpp
// fem/render/mesh_scene.hpp — REQ-004-09, 14, 15, 18
namespace fem::render {

// Everything needed to draw and pick a mesh, built once per import and
// immutable afterwards. Safe to build on a worker thread and share.
class MeshScene
{
public:
    [[nodiscard]] const core::Mesh& mesh() const noexcept;
    [[nodiscard]] const core::MeshSurface& surface() const noexcept;
    // Positions are stored relative to this point (the centre of the mesh).
    [[nodiscard]] const Eigen::Vector3d& origin() const noexcept;
    [[nodiscard]] core::BoundingBox bounds() const noexcept;

    // Vertex streams, in draw order (see section 5.2).
    [[nodiscard]] std::span<const Eigen::Vector3f> triangleVertices() const noexcept;
    [[nodiscard]] std::span<const core::ElementIndex> triangleElements() const noexcept; // per triangle
    [[nodiscard]] Range triangles(int dimension) const noexcept;     // 2 or 3
    [[nodiscard]] std::span<const Eigen::Vector3f> lineVertices() const noexcept;
    [[nodiscard]] std::span<const core::ElementIndex> lineElements() const noexcept;    // per segment
    [[nodiscard]] Range lines(LineSet set) const noexcept;           // Elements1d, Edges2d, Edges3d
    [[nodiscard]] std::span<const core::NodeIndex> pointNodes() const noexcept;          // 0D elements

    // Nodes reachable by a click and by a rectangle, per dimension.
    [[nodiscard]] std::span<const core::NodeIndex> surfaceNodes(int dimension) const noexcept;
    [[nodiscard]] std::span<const core::NodeIndex> allNodes(int dimension) const noexcept;

    // RGBA8 color of each element for a color mode (REQ-004-14, 15).
    [[nodiscard]] std::vector<std::uint32_t> elementColors(ColorMode mode) const;
};

[[nodiscard]] std::shared_ptr<const MeshScene> buildMeshScene(std::shared_ptr<const core::Mesh> mesh);

} // namespace fem::render
```

```cpp
// fem/render/selection.hpp — REQ-004-19 a 28
namespace fem::render {

class Selection
{
public:
    void reset(SelectionMode mode, int elementCount, int nodeCount);
    [[nodiscard]] SelectionMode mode() const noexcept;
    [[nodiscard]] int count() const noexcept;
    [[nodiscard]] bool contains(int index) const noexcept;
    void add(std::span<const int> indices);
    void remove(std::span<const int> indices);
    void clear();
    void removeElementsOfDimension(const core::Mesh& mesh, int dimension); // REQ-004-28
};

} // namespace fem::render
```

```cpp
// fem/render/mesh_picker.hpp — REQ-004-20 a 22, NFR-004-03
namespace fem::render {

struct PickRect { Vec2 from; Vec2 to; }; // pixels; to.x < from.x means crossing

// Clicks and rectangles against a MeshScene, on the CPU (ADR 0003).
class MeshPicker
{
public:
    explicit MeshPicker(std::shared_ptr<const MeshScene> scene);

    // Element or node under `pixel`, closest to the camera; nodes and 1D/0D
    // elements within `tolerancePx`.
    [[nodiscard]] std::optional<int> pick(const Camera& camera, const Vec2& pixel,
                                          SelectionMode mode, const DisplayOptions& display,
                                          double tolerancePx) const;

    // Window (inside) or crossing (inside or crossing), occluded items included.
    [[nodiscard]] std::vector<int> pickRect(const Camera& camera, const PickRect& rect,
                                            SelectionMode mode,
                                            const DisplayOptions& display) const;
};

} // namespace fem::render
```

```cpp
// Viewport (alterações) — REQ-004-05, 11, 14, 16, 17, 19 a 28
class Viewport : public QRhiWidget
{
public:
    void setScene(std::shared_ptr<const MeshScene> scene); // nullptr: no mesh
    [[nodiscard]] const DisplayOptions& displayOptions() const;
    [[nodiscard]] const Selection& selection() const;

public slots:
    void setDisplayMode(fem::render::DisplayMode mode);
    void setColorMode(fem::render::ColorMode mode);
    void setDimensionVisible(int dimension, bool visible);
    void setSelectionMode(fem::render::SelectionMode mode);
    void clearSelection();

signals:
    void selectionChanged(int count, fem::render::SelectionMode mode);
    void sceneShown(); // first frame rendered with the current scene (smoke test)
};
```

```cpp
// app/mesh_import.hpp — REQ-004-01 a 07
namespace fem::app {

// Reads a .msh file and builds its MeshScene on a worker thread.
class MeshImport : public QObject
{
    Q_OBJECT
public:
    [[nodiscard]] bool isRunning() const;
    void start(const QString& path); // ignored while running

signals:
    void started(const QString& path);
    void finished(const QString& path, std::shared_ptr<const render::MeshScene> scene,
                  const QStringList& warnings);
    void failed(const QString& path, const QString& message);
};

} // namespace fem::app
```

`CliOptions` ganha `std::optional<std::string> meshFile` (primeiro argumento
que não é opção).

## 5. Dados e algoritmos

### 5.1 Casca externa (REQ-004-09, 10)

Faces locais por família, com a numeração de vértices do Gmsh, ordenadas para
a normal apontar para fora (conferidas à mão contra os elementos de referência
e por teste de volume):

| Família | Faces |
|---------|-------|
| Tetraedro | (0,2,1), (0,1,3), (0,3,2), (1,2,3) |
| Hexaedro | (0,3,2,1), (4,5,6,7), (0,1,5,4), (1,2,6,5), (2,3,7,6), (0,4,7,3) |
| Cunha | (0,2,1), (3,4,5), (0,1,4,3), (1,2,5,4), (0,3,5,2) |
| Pirâmide | (0,3,2,1), (0,1,4), (1,2,4), (2,3,4), (3,0,4) |

Algoritmo: para cada face de cada elemento 3D, uma chave com os índices dos
vértices ordenados (4 × 32 bits, o quarto −1 em triângulos); ordenar o vetor de
`{chave, elemento, face}` e manter as chaves que aparecem uma vez. Para 1
milhão de tetraedros são 4 milhões de registros de 24 bytes (≈ 96 MB
temporários, ≈ 0,5 s em Release). Arestas: pares `(min, max)` de cada face,
ordenados e únicos, separadamente para `faces2d` e `faces3d`.

Os elementos de ordem alta usam só os vértices (REQ-004-10): a casca é a da
forma de lados retos.

### 5.2 Cena e buffers (REQ-004-09, 11 a 16, 18)

- **Triângulos:** cada face vira 1 (triângulo) ou 2 (quadrilátero, pela
  diagonal 0-2) triângulos, com vértices próprios (sem índice), primeiro os de
  `faces2d`, depois os de `faces3d`. Por vértice: posição `float` relativa ao
  centro da malha (12 bytes) e cor RGBA8 (4 bytes). Normais não são guardadas:
  o fragment shader calcula a normal da face por derivadas de tela
  (`dFdx`/`dFdy`), o que dá sombreamento chapado e serve aos dois lados.
- **Linhas:** segmentos `Lines`, primeiro os elementos 1D (pelos vértices),
  depois `edges2d`, depois `edges3d`; mesmo formato de vértice.
- **Pontos:** quadriláteros de 6 px desenhados por instância, a partir da
  posição de cada elemento 0D e, num buffer à parte, de cada nó selecionado.
- **Faixas por dimensão:** ocultar uma dimensão só muda as faixas desenhadas,
  sem reenviar nada (REQ-004-16). Arestas de 2D e de 3D seguem a visibilidade
  da sua dimensão.
- **Cor:** o buffer de cor é reescrito quando muda o modo de cor ou a seleção.
  O canal alfa marca "selecionado"; o shader troca a cor pela de realce
  (REQ-004-26). Nas arestas, um uniforme escolhe entre a cor do elemento
  (aramado) e uma cor escura fixa (sólido com arestas).
- **Modos:** sólido com arestas = faces + arestas; sólido = faces + linhas 1D;
  aramado = linhas 1D + arestas.
- **Sem disputa de profundidade (REQ-004-13):** o pipeline de faces usa
  `setDepthBias` e `setSlopeScaledDepthBias` para empurrar as faces para trás.
- **Coordenadas grandes (REQ-004-18):** os vértices ficam relativos ao centro
  da malha; a cada quadro o deslocamento `centro − alvo da câmera` é calculado
  em `double` e passa ao shader como `float`, junto da matriz relativa ao alvo
  da feature 002.

Cores por dimensão: 0D preto, 1D azul, 2D verde-água, 3D cinza-azulado. Cores
por grupo: paleta fixa de 12 cores bem distintas, repetida depois da 12ª;
elementos sem grupo em cinza claro; elemento em vários grupos pega a cor do
primeiro grupo da sua dimensão na ordem da malha (REQ-004-15).

Memória da cena para 1 milhão de tetraedros (casca de ≈ 100 mil triângulos):
alguns MB. Pior caso, 1 milhão de triângulos 2D: ≈ 48 MB de faces e ≈ 48 MB de
arestas, na CPU e na GPU.

### 5.3 Seleção (REQ-004-19 a 28)

- **Clique, modo elementos:** raio pelo pixel contra os triângulos das
  dimensões visíveis (Möller–Trumbore, em `float`, força bruta) e distância do
  cursor aos segmentos 1D e pontos 0D projetados (até a tolerância, 5 px). Vence
  o mais próximo da câmera. ≈ 20 ns por triângulo: < 30 ms para 1 milhão.
- **Clique, modo nós:** nós em `surfaceNodes` das dimensões visíveis projetados
  na tela, dentro da tolerância, do mais próximo da câmera para o mais longe;
  vence o primeiro que não está atrás de um triângulo (raio até o nó não acerta
  outra face antes).
- **Janela e cruzamento:** projeta uma vez todos os nós envolvidos. Janela:
  todos os vértices do elemento dentro do retângulo e à frente da câmera.
  Cruzamento: algum vértice dentro, ou alguma aresta do elemento projetado
  cruza o retângulo, ou o retângulo está dentro do elemento projetado. Nós:
  dentro do retângulo nos dois casos. Inclui itens escondidos (REQ-004-21, 22).
- **Gesto:** botão esquerdo pressionado e movimento acima de 4 px vira
  retângulo; senão é clique ao soltar. Shift no momento de soltar retira em vez
  de acrescentar. Esc e Editar ▸ Limpar seleção esvaziam.
- **Retângulo na tela (REQ-004-23):** widget filho transparente sobre o
  `Viewport`, desenhado com `QPainter` (azul contínuo / verde tracejado), sem
  receber eventos de mouse.
- **Armazenamento:** `std::vector<bool>` do tamanho da malha (elementos ou
  nós) mais a contagem.

### 5.4 Importação (REQ-004-01 a 08)

`MeshImport::start` cria uma `QThread` (`QThread::create`) que executa
`io::readGmsh` e, se der certo, `buildMeshScene`; o resultado volta à thread da
interface por sinal com conexão enfileirada. Caminho: `QString` →
`std::filesystem::path` por `toStdU16String()`, para acentos em todas as
plataformas. Durante a importação: barra de progresso indeterminada
(`QProgressBar` com intervalo 0–0) na barra de status e ação de importar
desabilitada.

Sucesso: `Viewport::setScene`, seleção limpa, "Enquadrar tudo" sobre a caixa
da malha, barra de status com nome, nós, elementos e grupos; depois, se houver
avisos, uma `QMessageBox` com eles. Falha: `QMessageBox` com a mensagem do
leitor; nada muda. No smoke test, avisos e erros vão para a saída padrão em vez
de caixas de diálogo.

Arrastar arquivo: `MainWindow` aceita `QDropEvent` com URLs locais terminadas
em `.msh` (sem diferenciar maiúsculas); mais de um arquivo ⇒ só o primeiro.

## 6. Fluxo

```
Arquivo ▸ Importar / soltar arquivo / linha de comando
  └─ MeshImport::start(path)                      [interface]
       └─ QThread: readGmsh → extractSurface → buildMeshScene   [trabalho]
            └─ finished(scene) ─(fila)→ MainWindow             [interface]
                 └─ Viewport::setScene(scene); fit; status; avisos
                      └─ próximo render(): SceneRenderer envia buffers à GPU
                           └─ sceneShown()  (smoke test encerra aqui)

mouse esquerdo ─→ Viewport: clique ou retângulo ─→ MeshPicker (CPU)
  └─ Selection add/remove ─→ buffer de cor marcado sujo ─→ update()
  └─ selectionChanged(count) ─→ barra de status
```

## 7. Tratamento de erros

| Situação | Comportamento | Requisito |
|----------|---------------|-----------|
| Arquivo inválido ou ilegível | Caixa com a mensagem do leitor; malha atual mantida | REQ-004-07 |
| Avisos do leitor | Malha exibida, depois caixa com os avisos | REQ-004-06 |
| Importação já em andamento | Nova importação ignorada; ação e soltar arquivo desabilitados | REQ-004-04 |
| Arquivo solto que não termina em `.msh` | Recusado no `dragEnterEvent` (cursor de proibido) | REQ-004-02 |
| Falha ao criar buffers da malha na GPU | Mesmo caminho de `backendUnavailable` da feature 002, com o motivo | — |
| Malha sem elementos visíveis (todas as dimensões ocultas) | Nada desenhado; clique e retângulo não selecionam | REQ-004-16 |

## 8. Estratégia de testes

| Teste | Tipo | Requisito | Referência/tolerância |
|-------|------|-----------|-----------------------|
| `elementFaces`: normal de cada face do elemento de referência aponta para fora | unitário | REQ-004-09 | sinal |
| Casca de 1 hexaedro: 6 faces, 12 arestas; de n³ hexaedros: 6n² faces | unitário | REQ-004-09, 30 | exato |
| Casca de `box_tet4`, `box_wedge6`, `box_hex20` (exemplos da 003): área 22, volume pelo teorema da divergência 6 (prova a orientação), cada aresta em exatamente 2 faces | unitário | REQ-004-09, 10, 30 | 1e−9 relativo |
| Malha mista à mão (hexaedro + pirâmide + tetraedro colados): nenhuma face interna | unitário | REQ-004-09 | exato |
| `buildMeshScene`: faixas por dimensão, contagens de triângulos e linhas, posições relativas ao centro | unitário | REQ-004-09, 16, 18 | exato / 1e−6 |
| Cores: por dimensão; por grupo com elemento em dois grupos e sem grupo | unitário | REQ-004-14, 15 | exato |
| `MeshPicker::pick`: elemento da frente num cubo de hexaedros visto de cada vista padrão; linha 1D dentro e fora da tolerância; nó escondido atrás de uma face não é escolhido | unitário | REQ-004-20, 30 | exato |
| `MeshPicker::pickRect`: janela e cruzamento sobre malha 2D conhecida em vista superior; itens escondidos incluídos; dimensões ocultas excluídas | unitário | REQ-004-21, 22, 30 | exato |
| `Selection`: acrescentar, retirar, limpar, ocultar dimensão | unitário | REQ-004-24, 25, 28 | exato |
| `parseCliOptions` com arquivo | unitário | REQ-004-03 | exato |
| `femapp_smoke_mesh`: `--smoke-test tests/data/gmsh/box_tet4.msh` com backend `null` | CTest | REQ-004-31 | "mesh rendered" |
| Roteiro manual: importar pelos três caminhos, modos, cores, visibilidade, seleção, Shift, Esc, realce, traduções, malha deslocada 1e6 | manual | REQ-004-01 a 29 | AC-1 a AC-10 |
| Medições com 1 milhão de Tet4: FPS, tempo até aparecer, tempo de seleção, repouso | manual, Release | NFR-004-01 a 04 | metas |

## 9. Alternativas consideradas

| Alternativa | Por que foi descartada |
|-------------|------------------------|
| Passe de IDs na GPU para o clique (arquitetura) | Leitura da textura é assíncrona no QRhi (resultado um ou dois quadros depois), difícil de testar sem GPU, e não serve para janela/cruzamento com itens escondidos; a CPU atende às metas (ADR 0003) |
| Tabela de dispersão para achar faces repetidas | ≈ 160 MB para 1 milhão de tetraedros; ordenar um vetor usa menos memória e é previsível |
| Vértices indexados com normais por vértice | Sombreamento chapado pede normal por face; derivadas no shader evitam o buffer de normais e a duplicação por normal |
| Pontos com `gl_PointSize` | Tamanho de ponto maior que 1 não existe no Direct3D 11 |
| Linhas largas | Largura de linha maior que 1 não é portável no QRhi |
| `QRubberBand` para o retângulo | Não permite escolher cor e tracejado |
| Qt Concurrent para a importação | Módulo novo do Qt; `QThread::create` basta |
| Selecionar só itens visíveis na janela | Contraria a decisão de seguir o AutoCAD (janela pega o que está dentro, visível ou não) |

## 10. Riscos e mitigação

- **Widget filho sobre `QRhiWidget` no macOS/Wayland** pode não compor como no
  Windows → fallback: desenhar o retângulo num passe de sobreposição do
  próprio `SceneRenderer`.
- **`setDepthBias` com efeito diferente entre backends** → valores ajustados no
  roteiro manual em Windows; macOS e Linux só pelo CI (renderização sem
  inspeção visual) — ressalva.
- **Extração da casca acima de 0,5 s em 1 milhão de tetraedros** → ordenar em
  paralelo (`std::sort` com política de execução) se a medição pedir; a meta
  total é 3 s.
- **Seleção em malhas 2D com milhões de triângulos** passar de 100 ms → grade
  uniforme ou BVH sobre os triângulos, só se a medição pedir.
- **Caixas de diálogo bloqueando o smoke test** → no modo `--smoke-test`
  mensagens vão para a saída padrão.

## 11. Novas dependências

Nenhuma. `QThread`, `QDropEvent`, `QProgressBar` e `QPainter` já vêm de Qt
Core, Gui e Widgets.
