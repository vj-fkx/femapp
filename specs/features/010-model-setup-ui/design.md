# Design — 010 model-setup-ui

Status: aprovado · Data: 2026-11-23
Requisitos: [requirements.md](requirements.md)

## 1. Resumo da abordagem

Um módulo novo, **`setup`** (`fem::setup`, sem Qt; ADR 0010), guarda o
documento do modelo, as edições reversíveis e a montagem do `core::Model`:

- **Documento:** um tipo valor, `setup::Document`. Os dados de cada item
  são os próprios tipos do `core` (`Material`, `Section`, `EndConnection`,
  `DistributedLoad`…), com os alvos trocados por `setup::Targets`:
  entidades do desenho, grupos da malha, ou elementos e nós de uma
  geração de malha.
- **Edições:** cada mudança é uma `DocumentEdit` (os itens antes e depois,
  pelo id do item), como o `geom::DrawingEdit`. O `app` a embrulha num
  `QUndoCommand` na mesma pilha do desenho.
- **Unidades:** os valores ficam no sistema ativo. Cada campo numérico tem
  uma grandeza (expoentes de força, comprimento, tempo e temperatura), e a
  troca de sistema converte tudo numa edição só.
- **Montagem:** `buildModel(document, drawing, mesh, load)` faz o
  `core::Model` de um caso ou de uma combinação sobre a malha atual. Os
  alvos por entidade viram `ElementIds` e `NodeIds` diretamente, pelo mapa
  da 009, invertido uma vez. Assim, 10⁵ entidades com o mesmo dado são uma
  atribuição só, não 10⁵ grupos. A montagem devolve a lista de problemas,
  cada um com o item envolvido.

No `app`:

- um `ModelController` é o dono do documento e da malha corrente (cena,
  mapa, geração e revisão do desenho);
- a árvore é um `QAbstractItemModel` num dock, e o painel de propriedades
  é um formulário montado a partir de descritores de campo;
- há menus para criar e atribuir à seleção, a conferência num dock, a
  unidade na barra de status, e a seção desenhada com `QPainter`.

No `render`, um `SymbolLayer` desenha apoios, cargas e eixos locais por
instâncias de glifos com tamanho constante na tela (shader próprio).

No `core`, `Mesh` ganha um índice dos grupos por nome (REQ-010-25).

## 2. Verificação da constituição

| Princípio | Atende? | Observação |
|-----------|---------|------------|
| Multiplataforma (§2) | Sim | C++20 e Qt Widgets; nada de plataforma. |
| GPU somente via QRhi (§3) | Sim | Os símbolos são uma camada RHI do `render` com shaders GLSL 440 compilados por `qsb`. A seção desenhada no painel é um `QPainter` num widget 2D, não 3D. |
| Camadas / `core` sem Qt (§4) | Sim | `setup` sem Qt (ADR 0010), dependendo de `core`, `geom` e `mesher`; `check_no_qt_includes` passa a cobri-lo. `render` recebe os símbolos como dados simples, sem depender de `setup`. A montagem roda na thread da interface (≤ 2 s; a 011 a leva para a de trabalho). |
| Correção numérica (§5) | Sim | §5.2: o sistema de unidades é escolhido e sempre visível; a conversão por expoentes é testada em ida e volta em 1e−12. Os testes da montagem comparam a análise do `core::Model` montado com a de modelos feitos à mão (o pórtico da T10 da 009, todas as ações, combinação na segunda ordem). |
| Simplicidade / dependências (§7) | Sim | Nenhuma dependência nova. O documento reusa os tipos do `core`; a árvore e os formulários são Qt Widgets puros (sem QtPropertyBrowser, que não vem no Qt 6). |

## 3. Módulos e arquivos afetados

| Módulo | Arquivo | Novo/Alterado | Responsabilidade |
|--------|---------|---------------|------------------|
| setup | `include/fem/setup/units.hpp`, `units.cpp` | novo | Sistemas, grandezas, fatores e conversão (REQ-010-02, 03) |
| setup | `include/fem/setup/document.hpp`, `document.cpp` | novo | Itens, alvos, `Document`, `DocumentEdit`, aplicar e reverter, ids (REQ-010-01, 04 a 18) |
| setup | `include/fem/setup/editing.hpp`, `editing.cpp` | novo | As edições prontas: criar, alterar, apagar com usos, atribuir com substituição, copiar entre casos, trocar unidades (REQ-010-03, 04, 07, 18, 23) |
| setup | `include/fem/setup/convert.hpp`, `convert.cpp` | novo | A conversão de cada tipo do `core` por grandeza (materiais, as oito formas e as seções genéricas, paredes finas, polígono, cargas, ligações, excentricidades, base, molas de empenamento) |
| setup | `include/fem/setup/build.hpp`, `build.cpp` | novo | `MeshContext`, `LoadSelection`, `buildModel`, `checkModel`, `targetState` (REQ-010-10, 13, 14, 17, 20, 21) |
| setup | `include/fem/setup/library.hpp`, `library.cpp` | novo | A biblioteca de materiais por norma, uma tabela de dados em SI, convertida ao sistema ativo (REQ-010-04, §5.9) |
| setup | `CMakeLists.txt` | novo | Módulo `fem::setup` |
| core | `include/fem/core/mesh.hpp`, `mesh.cpp`, `mesh_builder.cpp` | alterado | Índice dos grupos por (nome, dimensão), feito em `build()`; `findGroup` e `findGroups` por busca binária (REQ-010-25) |
| render | `include/fem/render/symbols.hpp` | novo | `SymbolSet`: apoios, forças, momentos, distribuídas e eixos, em coordenadas globais (REQ-010-09, 22) |
| render | `symbol_layer.hpp`, `symbol_layer.cpp`, `shaders/symbol.vert`, `shaders/symbol.frag` | novo | Glifos instanciados com tamanho constante na tela |
| render | `viewport.cpp`, `include/fem/render/viewport.hpp`, `scene_renderer.cpp` | alterado | `setSymbols`, `setSymbolsVisible`, o passe dos símbolos depois das arestas |
| app | `model_controller.hpp/.cpp` | novo | O documento, a malha corrente, as edições na pilha de desfazer, as atribuições à seleção, a conferência, os símbolos |
| app | `model_tree.hpp/.cpp` | novo | `QAbstractItemModel` da árvore, com os estados de alvo (inválido, aviso) |
| app | `property_panel.hpp/.cpp`, `fields.hpp/.cpp` | novo | O painel montado a partir de descritores de campo (rótulo, grandeza, leitura e escrita) |
| app | `section_view.hpp/.cpp`, `thin_walled_table.hpp/.cpp` | novo | A seção desenhada e a tabela de paredes finas (REQ-010-05, 06) |
| app | `model_dialogs.hpp/.cpp` | novo | Os diálogos de unidades, material da biblioteca, nova seção, atribuição, detalhes de viga, apoio, carga, casos e combinações |
| app | `check_panel.hpp/.cpp` | novo | A lista da conferência, clicável (REQ-010-21) |
| app | `line_mesh_job.hpp/.cpp`, `drafting_controller.*`, `main_window.*` | alterado | O MESH devolve também o mapa; a janela monta os docks, os menus e a unidade na barra de status, e liga a geração da malha ao `ModelController` |
| tests | `tests/unit/setup/test_units.cpp`, `test_document.cpp`, `test_editing.cpp`, `test_build.cpp` | novo | AC-1 a AC-5 |
| tests | `tests/unit/core/test_mesh.cpp` | alterado | Grupos pelo índice |
| tests | `tests/perf/bench_setup.cpp` | novo | NFR-010-01 |
| ci | `.github/workflows/ci.yml` | alterado | Cobertura de `src/setup/` ≥ 80% (NFR-010-03) |
| cmake | `cmake/CheckNoQtIncludes.cmake` | alterado | Inclui `setup` |
| specs | `decisions/0010-documento-do-modelo-em-setup.md` | novo | ADR do módulo |

## 4. Interfaces públicas

```cpp
// units.hpp — REQ-010-02, 03
namespace fem::setup {

enum class UnitSystem : std::uint8_t { NewtonMetre, KilonewtonMetre, NewtonMillimetre,
                                       KilogramForceCentimetre, PoundInch };

// A quantity as exponents of force, length, time and temperature (mass is
// F T^2 / L in a coherent system).
struct Quantity
{
    std::int8_t force = 0, length = 0, time = 0, temperature = 0;
};
namespace quantity { // the ones the document uses
inline constexpr Quantity kNone{}, kLength{0, 1, 0, 0}, kArea{0, 2, 0, 0},
    kInertia{0, 4, 0, 0}, kWarping{0, 6, 0, 0}, kForce{1, 0, 0, 0},
    kMoment{1, 1, 0, 0}, kBimoment{1, 2, 0, 0}, kStress{1, -2, 0, 0},
    kForcePerLength{1, -1, 0, 0}, kDensity{1, -4, 2, 0}, kAcceleration{0, 1, -2, 0},
    kTemperature{0, 0, 0, 1}, kPerTemperature{0, 0, 0, -1},
    kGradient{0, -1, 0, 1}, kConductivity{1, 0, -1, -1}, kSpecificHeat{0, 2, -2, -1},
    kSpringPerLength{1, -2, 0, 0}, kRotationalSpring{1, 1, 0, 0};
}

struct UnitNames { std::string force, length, mass, time, temperature; };
[[nodiscard]] UnitNames unitNames(UnitSystem system);
// "kN/m²", "N·mm", ...
[[nodiscard]] std::string unitLabel(UnitSystem system, Quantity q);
// The factor that turns a value of `q` in `from` into `to`.
[[nodiscard]] double conversionFactor(UnitSystem from, UnitSystem to, Quantity q);
// Temperature differences only (ΔT, α, gradients): °C and K, or °F.
}
```

```cpp
// document.hpp — REQ-010-01, 04 to 18
namespace fem::setup {

enum class ItemId : std::uint64_t {};

// REQ-010-12: what an item acts on.
struct GroupTarget { std::string name; int dimension = 1; };
struct Targets
{
    std::vector<geom::EntityId> entities;
    std::vector<GroupTarget> groups;
    // Picked in the mesh of `meshGeneration` (REQ-010-13).
    std::vector<core::ExternalId> elements;
    std::vector<core::ExternalId> nodes;
    std::uint64_t meshGeneration = 0;
};

// REQ-010-04: an extra property of a material, informative (fy, fu, fck,
// G...): a key, its quantity for the unit conversion, and the value.
struct MaterialProperty { std::string key; Quantity quantity; double value = 0.0; };
struct MaterialItem
{
    ItemId id{};
    core::Material data;
    std::string standard;                    // "EN 1993-1-1"; empty for one of the user's
    std::string grade;                       // "S355"
    std::vector<MaterialProperty> properties;
};
struct SectionItem { ItemId id{}; core::Section data; };
struct BarAssignment { ItemId id{}; Targets targets; core::Assignment data; }; // data.elements unused
struct ConnectionItem { ItemId id{}; Targets targets; core::EndConnection data; };
struct OffsetItem { ItemId id{}; Targets targets; core::RigidOffset data; };
struct FoundationItem { ItemId id{}; Targets targets; core::ElasticFoundation data; };
struct WarpingEndItem { ItemId id{}; Targets targets; core::WarpingEnd data; };
// REQ-010-15: the dofs held, value 0; settlements are loads.
struct SupportItem { ItemId id{}; Targets targets; std::array<bool, core::kDofTypeCount> held{}; };

// REQ-010-16: one action of a load case.
struct Settlement { core::DofType dof = core::DofType::U; double value = 0.0; };
struct SelfWeight { Eigen::Vector3d acceleration = Eigen::Vector3d::Zero(); };
using Action = std::variant<core::NodalForce, core::DistributedLoad, core::ConcentratedLoad,
                            SelfWeight, core::TemperatureChange, core::TemperatureGradient,
                            core::InitialStrain, Settlement>;
struct LoadItem { ItemId id{}; ItemId loadCase{}; Targets targets; Action action; };
// REQ-010-26: the standard and the category of the action of the case
// (keys of actionCategories(), design §5.10); empty for none.
struct LoadCase { ItemId id{}; std::string name; std::string standard; std::string category; };
struct Combination { ItemId id{}; std::string name; std::vector<std::pair<ItemId, double>> terms; };

using Item = std::variant<MaterialItem, SectionItem, BarAssignment, ConnectionItem, OffsetItem,
                          FoundationItem, WarpingEndItem, SupportItem, LoadCase, LoadItem,
                          Combination>;

// REQ-010-01: the model document. A value type; items keep their id for ever.
class Document
{
public:
    [[nodiscard]] UnitSystem units() const noexcept;
    [[nodiscard]] ItemId nextId() noexcept;
    [[nodiscard]] const Item* find(ItemId id) const;
    [[nodiscard]] std::span<const Item> items() const noexcept;  // in creation order
    [[nodiscard]] std::optional<ItemId> activeCase() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    void apply(const DocumentEdit& edit);
    void revert(const DocumentEdit& edit);
    // ...
};

// One reversible change (REQ-010-23): items out and in, by id, plus the
// unit system and the active case before and after.
struct DocumentEdit
{
    std::vector<Item> removed;
    std::vector<Item> added;
    std::optional<std::pair<UnitSystem, UnitSystem>> units;
    std::optional<std::pair<std::optional<ItemId>, std::optional<ItemId>>> activeCase;
};

}
```

```cpp
// editing.hpp — REQ-010-03, 04, 07, 18, 23: every edit as one DocumentEdit.
namespace fem::setup {
[[nodiscard]] DocumentEdit addItem(Document&, Item item);   // gives it an id
[[nodiscard]] DocumentEdit changeItem(const Document&, Item item);
// REQ-010-04, 05: refused with the items that use it.
[[nodiscard]] tl::expected<DocumentEdit, std::vector<ItemId>> removeItem(const Document&, ItemId);
// REQ-010-07: assigns and takes the targets out of the other assignments of
// the same kind (a bar assignment, a connection, ...), in one edit.
[[nodiscard]] DocumentEdit assign(Document&, Item item);
[[nodiscard]] DocumentEdit copyLoads(Document&, std::span<const ItemId> loads, ItemId toCase);
// REQ-010-03: every value converted, as one edit.
[[nodiscard]] DocumentEdit changeUnits(const Document&, UnitSystem to);
}
```

```cpp
// build.hpp — REQ-010-10, 13, 14, 17, 20, 21
namespace fem::setup {

// The mesh the model is built on.
struct MeshContext
{
    std::shared_ptr<const core::Mesh> mesh;
    std::shared_ptr<const mesher::LineMeshMap> map; // null for an imported mesh
    std::uint64_t generation = 0;                   // bumped on every new mesh
    std::optional<std::uint64_t> drawingRevision;   // of the drawing meshed (MESH)
};

struct LoadSelection { ItemId item{}; }; // a load case or a combination

enum class Severity : std::uint8_t { Error, Warning };
enum class IssueKind : std::uint8_t {
    NoMesh, MeshOutOfDate, MissingEntity, EntityNotMeshed, MissingGroup,
    StaleSelection, MissingNode, MissingElement, UnknownMaterial, UnknownSection,
    TrussBuckling, NoLoadSelected, Core
};
struct BuildIssue { Severity severity; IssueKind kind; std::optional<ItemId> item; std::string detail; };

struct BuildResult { core::Model model; std::vector<BuildIssue> issues; };

[[nodiscard]] BuildResult buildModel(const Document&, const geom::Drawing&, const MeshContext&,
                                     LoadSelection);
// REQ-010-21: the build, then core::validate() as issues of kind Core.
[[nodiscard]] std::vector<BuildIssue> checkModel(const Document&, const geom::Drawing&,
                                                 const MeshContext&, LoadSelection);
// REQ-010-13, 14: for the tree.
enum class TargetState : std::uint8_t { Valid, MissingEntity, StaleSelection, MissingGroup };
[[nodiscard]] TargetState targetState(const Targets&, const geom::Drawing&, const MeshContext&);
}
```

```cpp
// render/symbols.hpp — REQ-010-09, 22
namespace fem::render {
struct SupportSymbol { Vec3 at; std::uint8_t held; bool planar; };     // held: bit per dof
struct ArrowSymbol { Vec3 at; Vec3 vector; };                          // force, its direction
struct MomentSymbol { Vec3 at; Vec3 axis; };
struct DistributedSymbol { Vec3 from; Vec3 to; Vec3 startValue; Vec3 endValue; };
struct AxesSymbol { Vec3 at; Eigen::Matrix3d axes; };
struct SymbolSet
{
    std::vector<SupportSymbol> supports;
    std::vector<ArrowSymbol> forces;
    std::vector<MomentSymbol> moments;
    std::vector<DistributedSymbol> distributed;
    std::vector<AxesSymbol> axes;
};
// Viewport:
//   void setSymbols(std::shared_ptr<const SymbolSet> symbols);
//   void setSymbolsVisible(bool visible);
}
```

## 5. Dados e algoritmos

### 5.1 Unidades (REQ-010-02, 03; NFR-010-02)

| Sistema | Força | Comprimento | Massa (coerente) | Tempo | Temperatura |
|---------|-------|-------------|------------------|-------|-------------|
| N-m-kg-s | N | m | kg | s | °C |
| kN-m-t-s | kN | m | t | s | °C |
| N-mm-t-s | N | mm | t | s | °C |
| kgf-cm | kgf | cm | kgf·s²/cm (980,665 kg) | s | °C |
| lb-in | lbf | in | lbf·s²/in (175,127 kg) | s | °F |

- **Fatores para SI:**
  - força: 1, 10³, 1, 9,80665 e 4,4482216152605 N;
  - comprimento: 1, 1, 10⁻³, 10⁻² e 0,0254 m;
  - temperatura (diferença): 1 e 5/9.
- **Conversão:** o fator de uma grandeza é o produto dos fatores elevados
  aos expoentes, do sistema de origem para SI e de SI para o de destino.
  Ida e volta dá 1 a menos de alguns ulp (1e−12 no teste).
- **Ângulos e frações** (ν, ângulo de giro, frações de posição, βy e βz
  como comprimento) seguem a grandeza do campo, listada no `convert.cpp`
  por tipo.
- **Troca de sistema** (`changeUnits`): uma edição com todos os itens
  numéricos antes e depois, mais o par de sistemas. Desfazer restaura os
  valores guardados, sem converter de volta. O `app` pergunta antes
  ("the drawing is not converted…").

### 5.2 Documento e edições (REQ-010-01, 04 a 18, 23)

- **Itens:** um vetor em ordem de criação, com índice por id (como o
  `geom::Drawing`). Os ids não voltam a ser usados.
- **Edições:** `apply` tira os `removed` e põe os `added` (mesmo id =
  substituição), e `revert` faz o inverso. A revisão muda a cada edição.
- **Usos** (`removeItem`): um material ou seção referido por nome numa
  atribuição; um caso numa combinação ou com cargas (apagar um caso apaga
  as cargas dele na mesma edição, e a combinação que o usa recusa).
- **Atribuição com substituição** (`assign`): para os alvos do item novo,
  cada item da mesma espécie perde esses alvos, por um conjunto de hash.
  Um item que fica sem alvos sai. Tudo é uma edição: 10⁵ entidades são
  uma lista de ids e um passo de desfazer.
  - **Espécies:** atribuição de barra, ligação (por ponta: a nova
    substitui só a mesma ponta), excentricidade, base e ponta de
    empenamento.
  - **Apoios:** a atribuição substitui os GDL apoiados nos alvos.
  - **Cargas:** somam, não substituem.
- **Nomes:** únicos por espécie; o `app` sugere "Material 1, 2…".

### 5.3 Alvos e validade (REQ-010-11 a 14)

- **Por entidade:** válido se a entidade existe no desenho. Apagada, a
  entidade deixa o item inválido, que continua no documento; desfazer o
  apagamento a traz de volta e o item volta a ser válido sem mudar nada
  nele.
- **Por grupo:** válido se a malha corrente tem o grupo, com o nome e a
  dimensão.
- **Por seleção:** válido se `meshGeneration` é a geração corrente. Toda
  malha nova (MESH ou importação) incrementa a geração, e os itens
  antigos ficam `StaleSelection`.
- **Na árvore:** os itens inválidos têm um ícone de aviso e a dica do
  motivo.

### 5.4 Montagem (REQ-010-17, 20; NFR-010-01)

1. **Sem malha:** erro `NoMesh`. Se `drawingRevision` difere da revisão do
   desenho: erro `MeshOutOfDate` ("run MESH").
2. **Índice do mapa, uma vez:** entidade → elementos (de
   `map.elements`) e entidade → nós (de `map.nodes`, as entidades de cada
   nó). Para uma malha importada não há mapa, e alvos por entidade dão
   `EntityNotMeshed`.
3. **Cada item vira o seu tipo do `core`:**
   - os elementos vão como `ElementIds` com os IDs externos, juntando
     entidades, grupos (pelos elementos do grupo) e a seleção;
   - os nós vão como `NodeIds`, juntando os nós das entidades (pontos e
     todos os nós de linhas e polilinhas), os dos grupos e os da seleção.
   - Um item inválido dá o seu erro e é deixado de fora.
4. **Apoios e recalques:** os GDL apoiados por (nó, GDL) e os recalques
   do caso ou da combinação, somados por (nó, GDL). Cada (nó, GDL) dá um
   `Restraint` com o valor somado. Um recalque num GDL não apoiado é erro.
5. **Combinação:** a soma das ações dos casos com os fatores, aplicados a
   cada ação: forças, momentos, bimomentos, valores das distribuídas,
   torques, concentradas, ΔT, gradientes, deformações e recalques. O peso
   próprio é a soma dos vetores. O resultado é um só caso de carga no
   `core::Model`, por isso a segunda ordem e a flambagem veem a ação
   combinada.
6. **Materiais e seções:** copiados por nome. Um nome inexistente é erro.
7. **Treliças:** um aviso `TrussBuckling` por atribuição de treliça
   (REQ-010-10).
8. **`checkModel`:** a montagem, depois `core::validate(model)` como
   issues `Core` (sem item, com o texto do `core`).
   - **Ajuste de spec proposto (REQ-010-21):** clicar num problema
     seleciona o item quando ele é conhecido (os da montagem). Os do
     `core::validate` nomeiam elementos e nós, e realçam esses no viewport
     em vez de um item.

**Custo:** os índices do passo 2 são O(nós + elementos); os itens são
O(alvos). Com 10⁵ entidades e uma atribuição só, a montagem é linear (a
meta de 2 s). O índice dos grupos no `core` (§5.7) também tira a busca
linear para quem montar por `GroupRef`.

### 5.5 Interface (REQ-010-19, 21)

- **Árvore** (`ModelTree`, `QAbstractItemModel`):
  - Unidades;
  - Materiais;
  - Seções;
  - Barras (atribuições);
  - Detalhes (ligações, excentricidades, bases, pontas de empenamento);
  - Apoios;
  - Casos de carga (cada um com as suas cargas; o ativo em negrito);
  - Combinações;
  - Malha (origem, geração, nós e elementos, desatualizada ou não).
  - Duplo clique ativa um caso. Menu de contexto: novo, duplicar,
    renomear, apagar, copiar cargas para…
- **Painel de propriedades:** um formulário por tipo de item, montado de
  descritores (`Field`: rótulo, grandeza, tipo de editor — número com
  unidade, inteiro, caixa, lista, vetor 3, texto —, leitura e escrita no
  item). Cada edição de campo vira `changeItem`, um passo de desfazer.
  Valores inválidos não saem do campo, que fica com a borda vermelha e a
  dica.
- **Seção:**
  - a tabela das propriedades do `sectionProperties`;
  - o `SectionView` desenhado com `QPainter`: contorno pela forma
    (`stressPoints` e a geometria da forma), eixos principais, centroide,
    centro de cisalhamento e pontos de tensão;
  - o editor de paredes finas é uma `QTableWidget` de nós e outra de
    segmentos.
  - Polígono maciço: o comando pede as polilinhas fechadas selecionadas,
    a de maior área como contorno e as outras como furos.
- **Menu Model:**
  - Units…;
  - New material (from library)…;
  - New section…;
  - Assign bars…;
  - End connections…, Offsets…, Foundation…, Warping ends…;
  - Supports ▸ Fixed / Pinned / Roller / Custom…;
  - Load cases…;
  - Add load ▸ Nodal / Distributed / Concentrated / Self-weight /
    Temperature / Gradient / Initial strain / Settlement…;
  - Combinations…;
  - Check model;
  - Show symbols;
  - Show local axes.

  Os comandos de atribuição usam a seleção do viewport: entidades do
  desenho, ou elementos e nós da malha, conforme o alvo da seleção. Sem
  seleção, o diálogo oferece os grupos da malha.
- **Realce:** selecionar um item na árvore põe os alvos dele na seleção do
  viewport (desenho ou malha).
- **Conferência:** um dock com a lista (gravidade, texto, item). Clicar
  seleciona o item na árvore, ou realça os elementos e nós de um problema
  do `core`.
- **Barra de status:** "Units: kN, m, t, s, °C".

### 5.6 Símbolos (REQ-010-09, 22; NFR-010-01)

- **Montagem:** o `ModelController` faz o `SymbolSet` do caso ou da
  combinação ativa a partir da montagem (as posições dos nós da malha). O
  conjunto é refeito quando o documento, a malha ou o caso ativo mudam.
  Sem malha, os alvos por entidade usam a geometria do desenho (pontos e
  vértices), para que os apoios e as cargas apareçam antes do MESH.
- **Desenho** (`SymbolLayer`): os glifos são linhas, um molde por tipo,
  em tamanho unitário:
  - engaste, articulação, rolete e genérico;
  - seta;
  - arco com seta;
  - tríade.
  Cada símbolo é uma instância (posição, direção ou orientação, escala). O
  vertex shader escala o molde por `w` do clip para um tamanho constante
  em pixels.
- **Distribuídas:** setas instanciadas ao longo da barra (8 por barra,
  proporcionais aos valores) e uma linha unindo as pontas.
- **Custo:** os buffers de instância só mudam com o conjunto, e a câmera
  só muda uniformes. Com 10⁴ símbolos são ~10⁴ instâncias por passe, um
  draw por molde.

### 5.7 Índice dos grupos no `core` (REQ-010-25)

O `MeshBuilder::build()` faz `m_groupOrder`, os índices dos grupos
ordenados por (nome, dimensão). `findGroup` passa a ser uma busca binária
e `findGroups` um `equal_range` por nome. A ordem de `groups()` não muda.
O `resolve` do `model.cpp` já usa `findGroup`/`findGroups` e herda o
ganho.

### 5.8 A malha corrente no `app`

- **O `LineMeshJob` devolve também o mapa** (`shared_ptr<const
  LineMeshMap>`), e o `ModelController` guarda o `MeshContext`:
  - a cena;
  - o mapa;
  - a geração, incrementada a cada MESH e a cada importação;
  - a revisão do desenho no MESH; para uma importação, nenhuma.
- **Malha importada:** os alvos por entidade dão `EntityNotMeshed`; os
  por grupo e por seleção valem.

### 5.9 Biblioteca de materiais por norma (REQ-010-04)

*Ampliada na implementação (2026-11-23), por decisão do mantenedor.*

- **Dados, não código:** a biblioteca é uma lista de `LibraryMaterial`, com
  a norma, a família (aço, concreto, alumínio, madeira), a classe ou grau,
  o `core::Material` em SI e as propriedades extras em SI. Cada norma é uma
  função que devolve as suas linhas, e `materialLibrary()` junta todas.
  - **Uma norma nova** é uma função a mais.
  - **Uma classe nova** é uma linha a mais.
  - **Uma propriedade nova** é uma chave a mais (`fy`, `fu`, `fck`, `fmk`,
    `G`), com a sua grandeza. A conversão de unidades percorre a lista de
    propriedades por grandeza, sem conhecer as chaves, e o painel mostra
    o rótulo da chave (com a própria chave quando ela é desconhecida).
- **Copiar para o documento:** `fromLibrary(material, sistema)` faz o
  `MaterialItem` com o nome "grau (norma)", convertido ao sistema ativo.
- **Valores** (SI):

| Norma | Classes | E | ν | ρ | α | Extras |
|-------|---------|---|---|---|---|--------|
| EN 1993-1-1 | S235, S275, S355, S460 | 210 GPa | 0,3 | 7850 kg/m³ | 12·10⁻⁶/°C | fy/fu: 235/360, 275/430, 355/490, 460/540 MPa; G = 81 GPa |
| AISC 360 | A36, A572 Gr 50, A992 | 200 GPa (29 000 ksi) | 0,3 | 7850 | 11,7·10⁻⁶/°C | fy/fu: 250/400, 345/450, 345/450 MPa; G = 77,2 GPa |
| ABNT NBR 8800 | MR250, A572 Gr 50 | 200 GPa | 0,3 | 7850 | 12·10⁻⁶/°C | fy/fu: 250/400, 345/450 MPa; G = 77 GPa |
| EN 1992-1-1 | C20/25 a C50/60 | Ecm = 22·[(fck + 8)/10]^0,3 GPa | 0,2 | 2500 (armado) | 10·10⁻⁶/°C | fck |
| ACI 318 | f′c 3000, 4000, 5000, 6000 psi | Ec = 4700·√f′c MPa | 0,2 | 2400 | 9,9·10⁻⁶/°C | fck = f′c |
| ABNT NBR 6118 | C20 a C50 | Ecs = αi·5600·√fck MPa, αE = 1 (granito), αi = 0,8 + 0,2·fck/80 ≤ 1 | 0,2 | 2500 (armado) | 10·10⁻⁶/°C | fck |
| EN 1999-1-1 | alumínio | 70 GPa | 0,3 | 2700 | 23·10⁻⁶/°C | — |
| EN 338 | C24 | E₀,mean = 11 GPa | — | 420 (ρmean) | — | fmk = 24 MPa; G = 0,69 GPa |

- **Limites:**
  - k e c ficam vazios, porque estas normas não os dão à temperatura
    ambiente.
  - A madeira não tem ν isotrópico: fica vazio, e uma viga de C24 é
    recusada pela validação do `core`, que pede ν, até haver material
    ortotrópico.
  - Os valores vêm do conhecimento das normas, não dos textos, que são
    pagos. O mantenedor os conferiu na aprovação (2026-11-23).

### 5.10 Categorias de ação por norma (REQ-010-26)

*Ampliada na implementação (2026-11-23), por decisão do mantenedor.*

- **Dados, como a biblioteca de materiais (§5.9):** `actionCategories()`
  devolve uma lista de `ActionCategory` (norma, código, nome, natureza, ψ0,
  ψ1, ψ2 opcionais), uma função por norma. `findCategory(norma, código)`
  acha uma categoria.
  - **Uma norma nova** é uma função a mais.
  - **Uma categoria nova** é uma linha a mais.
- **No caso de carga:** ele guarda `standard` e `category` (o código). A
  classificação não entra na montagem; os γ e a geração das combinações
  ficam para uma feature futura.
- **Tabela:**

| Norma | Código | Ação | Natureza | ψ0 | ψ1 | ψ2 |
|-------|--------|------|----------|----|----|----|
| ABNT NBR 8681 | g1 | Peso próprio da estrutura | permanente | — | — | — |
| ABNT NBR 8681 | g2 | Elementos construtivos e instalações permanentes | permanente | — | — | — |
| ABNT NBR 8681 | ε | Ações permanentes indiretas (recalques, retração, protensão) | permanente | — | — | — |
| ABNT NBR 8681 | q1 | Uso, locais sem predominância de equipamentos fixos nem altas concentrações de pessoas (residenciais) | variável | 0,5 | 0,4 | 0,3 |
| ABNT NBR 8681 | q2 | Uso, locais com predominância de equipamentos fixos ou altas concentrações de pessoas (comerciais, escritórios) | variável | 0,7 | 0,6 | 0,4 |
| ABNT NBR 8681 | q3 | Uso, bibliotecas, arquivos, oficinas e garagens | variável | 0,8 | 0,7 | 0,6 |
| ABNT NBR 8681 | v | Vento | variável | 0,6 | 0,3 | 0 |
| ABNT NBR 8681 | t | Temperatura (variações uniformes) | variável | 0,6 | 0,5 | 0,3 |
| ABNT NBR 8681 | e | Excepcional | excepcional | — | — | — |
| ABNT NBR 8681 | s | Sísmica (ABNT NBR 15421) | sísmica | — | — | — |
| EN 1990 | G | Permanent | permanente | — | — | — |
| EN 1990 | P | Prestress | permanente | — | — | — |
| EN 1990 | Q-A | Imposed, category A: domestic, residential | variável | 0,7 | 0,5 | 0,3 |
| EN 1990 | Q-B | Imposed, category B: offices | variável | 0,7 | 0,5 | 0,3 |
| EN 1990 | Q-C | Imposed, category C: congregation areas | variável | 0,7 | 0,7 | 0,6 |
| EN 1990 | Q-D | Imposed, category D: shopping areas | variável | 0,7 | 0,7 | 0,6 |
| EN 1990 | Q-E | Imposed, category E: storage areas | variável | 1,0 | 0,9 | 0,8 |
| EN 1990 | Q-F | Imposed, category F: traffic, vehicles ≤ 30 kN | variável | 0,7 | 0,7 | 0,6 |
| EN 1990 | Q-G | Imposed, category G: traffic, 30 to 160 kN | variável | 0,7 | 0,5 | 0,3 |
| EN 1990 | Q-H | Imposed, category H: roofs | variável | 0 | 0 | 0 |
| EN 1990 | Q-S | Snow (altitude ≤ 1000 m, outside Finland, Iceland, Norway, Sweden) | variável | 0,5 | 0,2 | 0 |
| EN 1990 | Q-W | Wind | variável | 0,6 | 0,2 | 0 |
| EN 1990 | Q-T | Temperature (non-fire) | variável | 0,6 | 0,5 | 0 |
| EN 1990 | A | Accidental | excepcional | — | — | — |
| EN 1990 | AE | Seismic | sísmica | — | — | — |
| ASCE 7 | D | Dead | permanente | — | — | — |
| ASCE 7 | L | Live | variável | — | — | — |
| ASCE 7 | Lr | Roof live | variável | — | — | — |
| ASCE 7 | S | Snow | variável | — | — | — |
| ASCE 7 | R | Rain | variável | — | — | — |
| ASCE 7 | W | Wind | variável | — | — | — |
| ASCE 7 | E | Earthquake | sísmica | — | — | — |
| ASCE 7 | H | Lateral earth pressure | permanente | — | — | — |
| ASCE 7 | F | Fluids | permanente | — | — | — |
| ASCE 7 | T | Self-straining | variável | — | — | — |

- **Fontes:**
  - os ψ da NBR são os da tabela 11.2 da ABNT NBR 6118, que reproduz os
    da ABNT NBR 8681;
  - os do EN 1990 são os da tabela A1.1;
  - o ASCE 7 combina por fatores de carga, sem ψ.
- **Ressalva:** como na §5.9, os valores vêm do conhecimento das normas, e
  o mantenedor os confere.

## 6. Fluxo

```
seleção no viewport ─► Model ▸ Assign bars… (diálogo) ─► ModelController
   ─► setup::assign(document, item) ─► DocumentEdit ─► QUndoStack (mesma do desenho)
   ─► document.apply ─► árvore, painel, símbolos atualizados

MESH ─► LineMeshJob ─► (cena, relatório, mapa, revisão) ─► ModelController::meshChanged
   ─► geração++ ─► seleções antigas marcadas ─► símbolos refeitos

Check model ─► setup::checkModel(document, drawing, meshContext, caso ativo)
   ─► dock da conferência ─► clique ─► item na árvore / realce no viewport
```

## 7. Tratamento de erros

| Situação | Comportamento | Requisito |
|----------|---------------|-----------|
| Apagar material, seção ou caso em uso | Recusado; mensagem com os itens que usam | REQ-010-04, 05, 18 |
| Campo inválido no painel (negativo onde não pode, texto) | O valor não é aceito; borda vermelha e dica; nada vai para o documento | REQ-010-19 |
| Seção inválida | O painel mostra a mensagem do `core` no lugar das propriedades | REQ-010-06 |
| Polilinha aberta para polígono maciço | Recusado com a mensagem | REQ-010-05 |
| Atribuir sem seleção e sem grupos | O diálogo avisa e não cria nada | REQ-010-12 |
| Montagem: sem malha, malha desatualizada, alvo inválido, nome inexistente, recalque sem apoio | `BuildIssue` com a gravidade e o item; o item é deixado de fora | REQ-010-20 |
| Trocar unidades | Pergunta antes, com o aviso do desenho | REQ-010-03 |

## 8. Estratégia de testes

| Teste | Tipo | Requisito | Referência/tolerância |
|-------|------|-----------|-----------------------|
| Fatores dos sistemas e ida e volta de todas as grandezas | unitário | REQ-010-02, NFR-010-02 | 1e−12; E·A/L coerente em cada sistema |
| Troca de unidades de um documento com todos os tipos; desfazer | unitário | REQ-010-03 | valores exatos do antes |
| Documento: aplicar e reverter, ids, usos, substituição de atribuição, cargas que somam, copiar cargas | unitário | REQ-010-01, 04, 07, 15 a 18, 23 | exatos |
| Biblioteca por norma: cada material contra a tabela da §5.9, o E dos concretos pela fórmula da norma em cada classe, nas cinco unidades; as propriedades extras convertidas por grandeza | unitário | REQ-010-04 | 1e−12 |
| Seções: o documento devolve o `Section` do `core` com as propriedades iguais às calculadas direto | unitário | REQ-010-05, 06 | 1e−14 |
| Pórtico 2×2 da T10 da 009 montado pelo documento = o modelo do teste da 009 (análise linear) | verificação | REQ-010-07, 11, 12, 20, AC-3 | 1e−12 |
| Viga 3D com ligações, excentricidades, base e empenamento montada = feita à mão | verificação | REQ-010-08, AC-3 | 1e−12 |
| Um caso com cada ação do `core` = feito à mão; combinação 1,4G + 1,5Q na segunda ordem = ações somadas à mão | verificação | REQ-010-16, 17, AC-5 | 1e−12 |
| Alvos: entidade apagada e devolvida, malha nova invalidando seleções, malha importada com grupos e seleções, entidade sem malha | unitário | REQ-010-13, 14, AC-4 | estados e issues |
| Conferência: issues da montagem com o item, do `core` sem item, aviso de treliça | unitário | REQ-010-10, 21 | exatos |
| `Mesh::findGroup`/`findGroups` pelo índice; suítes 003 a 009 | unitário/regressão | REQ-010-25 | iguais |
| `bench_setup`: atribuir a 10⁵ entidades, desfazer, montar uma combinação | benchmark | NFR-010-01 | ≤ 1 s, ≤ 1 s, ≤ 2 s |
| Roteiro manual (árvore, painel, realce, conferência, símbolos, eixos, desfazer misto, FPS com 10⁴ símbolos) | manual | REQ-010-09, 19, 21, 22, 23, AC-6 | o roteiro |

## 9. Alternativas consideradas

| Alternativa | Por que foi descartada |
|-------------|------------------------|
| O documento no `app` | Não seria testável sem Qt, e a 011 e a 014 também o usam (REQ-010-01). |
| O documento no `core` | O `core` não conhece `geom` (IDs de entidade) nem `mesher` (o mapa): inverteria o grafo de dependências. |
| O documento no `mesher` | Mistura geração de malha com preparação do modelo; o módulo novo é mais claro (ADR 0010). |
| Um grupo `entity <id>` por entidade como alvo do `core::Model` | 10⁵ `GroupRef` com a busca linear seriam O(G²); com o índice, ainda são 10⁵ atribuições. `ElementIds` diretos pelo mapa fazem uma atribuição por item. |
| Itens próprios do `setup` em vez dos tipos do `core` | Duplicaria cada estrutura e cada conversão; os tipos do `core` com o alvo trocado reusam tudo. |
| Guardar em SI e converter só na exibição | Os valores digitados mudariam por arredondamento a cada exibição. Guardar no sistema ativo mantém o que o usuário digitou, e a troca de sistema é explícita e desfazível. |
| QtPropertyBrowser | Não faz parte do Qt 6 (dependência solta, LGPL); o formulário por descritores é pequeno. |
| Símbolos por QPainter sobre o viewport | Seriam CPU por quadro: 10⁴ símbolos ficariam abaixo de 30 FPS. A constituição §3 pede o RHI no 3D. |
| Combinação por superposição de resultados | Errada na segunda ordem e na flambagem; somar as ações serve às três análises (REQ-010-17). |

## 10. Riscos e mitigação

- **Tamanho da feature** (módulo novo, sete docks e diálogos, render). As
  tarefas vão em fases que se sustentam sozinhas:
  1. índice do `core`;
  2. unidades e documento;
  3. montagem;
  4. árvore e painel;
  5. diálogos de atribuição e cargas;
  6. símbolos;
  7. conferência e fechamento.
  Cada fase com as suítes verdes.
- **Conversão de unidades esquecida num campo** → `convert.cpp` tem uma
  função por tipo do `core` com todos os campos. Um teste converte um
  documento com cada campo não nulo e volta, e falha se algum valor não
  mudar quando devia mudar.
- **Desfazer de atribuições grandes** (memória de antes e depois) → os
  alvos são listas de ids (8 bytes cada): 10⁵ entidades são ~1 MB por
  passo.
- **Símbolos de distribuídas em barras curvas** → as setas seguem a corda
  dos elementos da malha. Antes do MESH, a corda da entidade.
- **Itens que referem entidades que nunca tiveram malha** (desenhadas
  depois do MESH) → a montagem dá `MeshOutOfDate`, porque a revisão
  difere; a mensagem pede o MESH.

## 11. Novas dependências

Nenhuma. O módulo `setup` usa `core`, `geom`, `mesher`, Eigen e
`tl::expected`, já no projeto (ADR 0010 para o módulo).
