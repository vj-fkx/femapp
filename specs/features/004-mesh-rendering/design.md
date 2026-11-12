# Design — 004 mesh-rendering

Status: aprovado (ampliação da §12 aprovada em 2026-11-12) · Data: 2026-11-12

> As interfaces de seleção da §4 (`SelectionMode`, `Selection`, `MeshPicker`)
> e o desenho dos nós selecionados da §5.2 foram substituídos pela §12.
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
  de acrescentar. Esc, Editar ▸ Limpar seleção e um clique sem Shift que não
  escolhe nenhum item (fora da malha) esvaziam; arrastar um retângulo vazio
  não limpa.
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

## 12. Ampliação — faces e arestas de sólidos, tipos combináveis e nós (2026-11-12)

Atende aos REQ-004-19 a 22, 26 a 28 alterados e aos novos REQ-004-32 a 35.

### 12.1 Resumo

- **Itens novos sem estrutura nova:** a face de sólido é um índice em
  `surface().faces3d` e a aresta de sólido, um índice em `surface().edges3d`,
  que já existem, são únicos e estão na ordem dos buffers (REQ-004-32).
- **Seleção por tipo:** `SelectionMode` sai; entram `SelectionKind` (elemento,
  face, aresta, nó) e `SelectionFilter` com os tipos ligados. `Selection`
  guarda um conjunto por tipo.
- **Clique** em ordem de prioridade nó → aresta → face → elemento; **janela e
  cruzamento** devolvem os itens de todos os tipos ligados.
- **GPU:** faces selecionadas usam o mesmo bit de realce dos triângulos;
  arestas selecionadas são quadriláteros instanciados de 3 px (linhas largas
  não são portáveis); "Mostrar nós" reutiliza o pipeline de pontos com um
  buffer de nós à parte, com teste de profundidade.

### 12.2 Interfaces

```cpp
// fem/render/display_options.hpp — REQ-004-19, 34
enum class SelectionKind : std::uint8_t { Element, Face, Edge, Node };
inline constexpr std::size_t kSelectionKindCount = 4;

struct SelectionFilter
{
    // All on by default; at least one stays on (enforced by the Viewport).
    std::array<bool, kSelectionKindCount> enabled = {true, true, true, true};
    [[nodiscard]] bool isEnabled(SelectionKind kind) const noexcept;
};

struct DisplayOptions
{
    // ... (unchanged)
    bool showNodes = true; // REQ-004-34
};
```

```cpp
// fem/render/selection.hpp — REQ-004-19, 24 a 28, 33
// Items by kind; indices are elements, faces3d, edges3d or nodes.
struct SelectionItems
{
    std::array<std::vector<int>, kSelectionKindCount> byKind;
    [[nodiscard]] std::vector<int>& operator[](SelectionKind kind);
    [[nodiscard]] const std::vector<int>& operator[](SelectionKind kind) const;
    [[nodiscard]] bool empty() const noexcept;
};

class Selection
{
public:
    // Empties the selection and sizes it for a scene (nullptr: nothing).
    void reset(const MeshScene* scene);

    [[nodiscard]] int count(SelectionKind kind) const noexcept;
    [[nodiscard]] int total() const noexcept;
    [[nodiscard]] bool contains(SelectionKind kind, int index) const noexcept;
    [[nodiscard]] std::vector<int> items(SelectionKind kind) const;

    void add(const SelectionItems& items);
    void remove(const SelectionItems& items);
    void clear();
    void clear(SelectionKind kind);                                        // REQ-004-33
    void removeElementsOfDimension(const core::Mesh& mesh, int dimension); // 3: faces, edges too
};
```

```cpp
// fem/render/mesh_scene.hpp — acréscimo
// Per triangle, the index of its face in surface().faces2d (triangles(2))
// or surface().faces3d (triangles(3)). Segment s of lines(Edges3d) is
// surface().edges3d[s - lines(Edges3d).first].
[[nodiscard]] std::span<const std::uint32_t> triangleFaces() const noexcept;
```

```cpp
// fem/render/mesh_picker.hpp — REQ-004-20 a 22, 32
struct PickedItem
{
    SelectionKind kind;
    int index;
};

[[nodiscard]] std::optional<PickedItem> pick(const Camera& camera, const Vec2& pixel,
                                             const SelectionFilter& filter,
                                             const DisplayOptions& display,
                                             double tolerancePx) const;
[[nodiscard]] SelectionItems pickRect(const Camera& camera, const PickRect& rect,
                                      const SelectionFilter& filter,
                                      const DisplayOptions& display) const;
```

```cpp
// Viewport (alterações) — REQ-004-19, 27, 33, 34
public slots:
    void setSelectionKindEnabled(fem::render::SelectionKind kind, bool enabled); // replaces setSelectionMode
    void setNodesShown(bool shown);
signals:
    void selectionChanged(); // counts read from selection(); replaces (int, SelectionMode)
public:
    [[nodiscard]] const SelectionFilter& selectionFilter() const;
```

### 12.3 Clique (REQ-004-20)

*(Ordem alterada em 2026-11-12, a pedido do mantenedor.)* Na ordem, o
primeiro tipo ligado que achar algo vence:

1. **Elemento:** triângulos, linhas 1D e pontos 0D, o mais próximo da câmera.
2. **Face de sólido:** o triângulo mais próximo sob o cursor
   (`closestTriangle` devolve também o índice do triângulo); se for da faixa
   3D, vence a face `triangleFaces()[t]`.
3. **Aresta** (2D ou de sólido, com a dimensão visível): segmentos projetados
   até a tolerância, ordenados pela profundidade do ponto mais próximo; vence o
   primeiro não escondido por um triângulo, com a folga usada para os nós.
4. **Nó:** `surfaceNodes` das dimensões visíveis até a tolerância, do mais
   próximo para o mais longe; vence o primeiro não escondido.

Com elementos ligados, o clique sobre a malha sempre pega o elemento; faces,
arestas e nós por clique pedem desligar os tipos anteriores no menu, ou usar
janela e cruzamento.

### 12.4 Janela e cruzamento (REQ-004-21, 22)

Os nós são projetados uma vez, como hoje, e cada tipo ligado é testado:

- **Elementos e nós:** sem mudança.
- **Faces de sólido** (3D visível): polígono dos vértices da face; janela =
  todos os vértices dentro; cruzamento = algum vértice dentro, algum lado
  cruza o retângulo ou o centro do retângulo cai dentro do polígono (mesmos
  auxiliares `segmentTouchesBox` e `pointInTriangle`).
- **Arestas de sólido** (3D visível): janela = as duas pontas dentro;
  cruzamento = `segmentTouchesBox`.

Todos exigem os vértices à frente da câmera e incluem itens escondidos.

### 12.5 Desenho (REQ-004-26, 34, 35)

- **Faces selecionadas:** o triângulo 3D recebe o bit de realce se o elemento
  ou a face estiver selecionado; nada muda no shader de faces.
- **Arestas selecionadas:** novo shader `mesh_segment.vert` (com o
  `mesh_point.frag` existente): por instância, as duas pontas e a cor; o
  quadrilátero de canto [−1, 1]² já existente é esticado entre as pontas na
  tela, com 3 px × razão de pixels de largura. Desenhado no passe de
  sobreposição, sem teste de profundidade, como os nós selecionados, para que
  arestas escondidas escolhidas por janela continuem visíveis.
- **Mostrar nós:** buffer de instâncias com a união ordenada e sem repetição
  de `surfaceNodes` das dimensões visíveis, cor fixa `kNodeColor` (preto, a
  pedido do mantenedor; os nós selecionados ficam com o realce laranja por
  cima), desenhado
  com o pipeline de pontos existente, com teste de profundidade. Nos modos
  sólidos as faces, empurradas para trás pelo viés de profundidade, escondem
  os nós de trás; no aramado não há faces e todos aparecem (REQ-004-35). Os
  nós selecionados continuam por cima, no passe de sobreposição. O buffer é
  refeito quando mudam `showNodes` ou a visibilidade das dimensões.
- **Ordem alta:** *(substituído pela §13.)* Os nós intermediários passam a ser
  mostrados e clicáveis.

Memória extra para 1 milhão de tetraedros (casca de ≈ 50 mil nós e ≈ 150 mil
arestas): menos de 1 MB para os nós; `triangleFaces` soma 4 bytes por
triângulo (≈ 0,4 MB); os vetores de seleção de faces e arestas, poucos kB.

### 12.6 Interface (REQ-004-19, 27, 29, 33, 34)

- **Editar ▸ Selecionar**, submenu com quatro ações marcáveis: Elementos,
  Faces de sólidos, Arestas de sólidos e Nós. Desmarcar a última ligada é
  desfeito na hora (a ação volta marcada). Desligar um tipo chama
  `Selection::clear(kind)` (REQ-004-33); ligar não mexe na seleção.
- **Exibir ▸ Mostrar nós**, ação marcável, ligada por padrão.
- **Barra de status:** "2 elementos, 5 faces, 1 nó selecionados" — uma parte
  por tipo com itens, cada uma com `tr("%n element(s)", nullptr, n)` para o
  plural em pt_BR, unidas por vírgula; vazia quando nada está selecionado.
- **Ocultar a dimensão 3D** retira elementos 3D, faces e arestas
  (REQ-004-28); ocultar 2D não afeta faces de sólido.

### 12.7 Arquivos afetados

| Módulo | Arquivo | Alteração |
|--------|---------|-----------|
| render | `display_options.hpp` | `SelectionKind`, `SelectionFilter`, `showNodes`; sai `SelectionMode` |
| render | `selection.hpp/.cpp` | Conjuntos por tipo, `SelectionItems` |
| render | `mesh_scene.hpp/.cpp` | `triangleFaces()` |
| render | `mesh_picker.hpp/.cpp` | `PickedItem`, prioridade do clique, faces e arestas no retângulo |
| render | `mesh_gpu.hpp/.cpp`, `shaders/mesh_segment.vert` (novo) | Realce de faces, arestas selecionadas, nós mostrados |
| render | `viewport.hpp/.cpp` | Filtro, `setNodesShown`, novo sinal |
| app | `main_window.hpp/.cpp`, `i18n/femapp_pt_BR.ts` | Submenu Selecionar, Mostrar nós, barra de status |
| tests | `test_selection.cpp`, `test_mesh_picker.cpp`, `test_mesh_scene.cpp` | Casos novos |

### 12.8 Testes

| Teste | Tipo | Requisito | Referência |
|-------|------|-----------|------------|
| `triangleFaces`: bloco 3×3×3 de hexaedros, 54 faces da casca, 108 triângulos, cada face com 2 triângulos | unitário | REQ-004-32 | exato |
| `Selection`: acrescentar e retirar itens de tipos misturados; `count` por tipo; `clear(kind)`; ocultar 3D retira elementos 3D, faces e arestas | unitário | REQ-004-24, 25, 28, 33 | exato |
| `pick`, bloco 3×3×3 em vista frontal: só faces → a face central da frente (centro (1,5; 0; 1,5)); faces e elementos → a face; só elementos → o elemento (1,0,1); arestas → clique perto do meio de uma aresta da frente pega essa aresta; nós e arestas → clique num vértice pega o nó | unitário | REQ-004-20, 32 | exato |
| `pick`: aresta da face de trás não é escolhida (escondida) | unitário | REQ-004-20 | exato |
| `pickRect`, bloco 1×1×1 em vista frontal: janela em volta de tudo com faces e arestas → 6 faces e 12 arestas; cruzamento pequeno no meio da frente → só a face da frente; com 3D oculto → nada | unitário | REQ-004-21, 22, 32 | exato |
| Roteiro manual: submenu Selecionar com tipos combinados; realce de faces, arestas e nós nos três modos; contagem por tipo; "Mostrar nós" no sólido e no aramado; textos em pt_BR | manual | REQ-004-19 a 29, 33 a 35 | AC-13 a AC-15 |
| Medição: clique e janela com os quatro tipos ligados na malha de 1 milhão de Tet4 | manual, Release | NFR-004-03 | metas |

### 12.9 Alternativas consideradas

| Alternativa | Por que foi descartada |
|-------------|------------------------|
| Arestas selecionadas como linhas de 1 px com outra cor | Pouco visíveis sobre as arestas normais; o requisito pede linhas mais grossas |
| `setLineWidth` no pipeline | Largura maior que 1 não é garantida no QRhi (D3D11, Metal, núcleo do OpenGL) |
| Arestas selecionadas com teste de profundidade | Uma aresta escondida escolhida por janela ficaria invisível, ao contrário dos nós selecionados |
| Clique escolhendo o item mais próximo da câmera entre todos os tipos | Nó, aresta e face estão à mesma profundidade; a prioridade por tipo é previsível e segue os snaps de CAD |
| Um modo exclusivo por vez | Recusado pelo mantenedor: os tipos são combináveis |

### 12.10 Riscos

- **Quadriláteros das arestas com uma ponta atrás da câmera** (perspectiva
  muito próxima) → o shader descarta a instância se alguma ponta tiver
  `w ≤ 0`, o mesmo critério de "à frente da câmera" da seleção.
- **Nós mostrados cintilando sobre as faces** em algum backend → aumentar o
  viés de profundidade das faces, ajustado no roteiro manual no Windows; macOS
  e Linux só pelo CI (ressalva).
- **Clique em aresta lento** com muitos candidatos escondidos (cada um percorre
  os triângulos) → limitar a 16 candidatos; medir no NFR-004-03.

### 12.11 Verificação da constituição e dependências

Sem mudança em relação à §2: tudo em `render` e `app`, `core` intocado, só
QRhi; nenhuma dependência nova.

## 13. Arestas 2D, arestas dos elementos selecionados e nós intermediários (2026-11-12)

Pedido do mantenedor, sobre a §12. Atende aos REQ-004-19, 20, 26, 28, 32 e 34
alterados.

- **Arestas 2D no tipo "Arestas":** o índice de aresta passa a percorrer
  `edges2d` e depois `edges3d`, na mesma ordem dos segmentos
  `lines(Edges2d)` e `lines(Edges3d)`, que são contíguos no buffer de linhas.
  `MeshScene` ganha `edgeCount()`, `edge(i)` (a `SurfaceEdge`) e
  `edgeDimension(i)` (2 ou 3). Uma aresta comum a uma face 2D e a uma face de
  sólido aparece nos dois grupos, como dois itens. O clique e o retângulo
  testam as arestas 2D com a dimensão 2 visível e as de sólido com a 3D.
  `Selection::removeElementsOfDimension` passa a receber a cena e retira as
  arestas do grupo da dimensão ocultada. No menu, "Arestas de sólidos" vira
  "Arestas".
- **Arestas dos elementos selecionados (REQ-004-26):** ao refazer as cores,
  cada face de `faces2d` e `faces3d` cujo elemento está selecionado gera os
  segmentos grossos das suas arestas, desenhados pelo mesmo pipeline das
  arestas selecionadas, no passe de sobreposição. Assim todas as arestas
  visíveis do elemento ficam realçadas, também as que no buffer de linhas
  pertencem ao vizinho.
- **Nós intermediários (REQ-004-34):** `surfaceNodes(d)` passa a conter, para
  0D, 1D e 2D, todos os nós dos elementos; para 3D, os vértices das faces da
  casca e os nós de ordem alta sobre elas. Um nó local fica sobre uma face
  local quando suas coordenadas no elemento de referência
  (`core::referenceNodeCoordinates`) estão no plano dos vértices daquela face
  (tolerância 1e−9) — teste exato, sem depender da geometria (vale também para
  malhas curvas). A face local de cada face da casca é achada pelos seus
  vértices; a lista de nós por (tipo, face local) é guardada em cache durante a
  montagem da cena. Como o clique e "Mostrar nós" usam `surfaceNodes`, os
  intermediários ficam visíveis e clicáveis sem outra mudança.

Testes: arestas 2D e de sólido no clique e no retângulo (uma malha com um
hexaedro e um quadrilátero ao lado); ocultar 2D retira só as arestas 2D;
`surfaceNodes(3)` de um Tet10 isolado tem 10 nós e de um Hex27 isolado tem 26
(todos menos o central); de dois Hex27 colados (45 nós), 42: ficam de fora os
dois centrais e o centro da face comum.

## 14. Padrões de exibição e vista de malhas planas (2026-11-12)

Pedido do mantenedor. Atende ao REQ-004-36 e aos REQ-002-19, 22 e 26
alterados.

- **Projeção ortográfica por padrão:** `Camera` nasce em ortográfica; a ação
  Exibir ▸ Projeção ortográfica começa marcada.
- **Grade desligada, eixos visíveis:** o passe da grade passa a ser desenhado
  sempre; o quarto componente de `spacing` (antes 0) diz se as linhas da grade
  entram (1) ou não (0). As linhas dos eixos X e Y continuam, com o mesmo
  esmaecimento. `Viewport` começa com a grade desligada.
- **Vista de malha plana (REQ-004-36):** função sem GPU
  `planarView(const MeshScene&) -> std::optional<StandardView>`: sem elementos
  3D e com a extensão da caixa envolvente num eixo até 1e−9 × a diagonal, a
  vista Superior (Z constante), Frontal (Y constante) ou Direita (X constante),
  nessa ordem de preferência. `Viewport::setScene` aplica a vista antes de
  enquadrar. Testes: quadrado em XY → Superior; em XZ → Frontal; em YZ →
  Direita; com um hexaedro → nenhuma; placa inclinada → nenhuma.

