# Design — 003 mesh-model

Status: rascunho · Data: 2026-11-11 · *Revisado no mesmo dia: todos os tipos do Gmsh*
Requisitos: [requirements.md](requirements.md)

## 1. Resumo da abordagem

- **`core`** ganha os 103 tipos de elemento do Gmsh (tabela `constexpr` com
  família, dimensão, ordem, nós, vértices e coordenadas de referência, **gerada
  a partir do próprio Gmsh** por um script versionado), a malha imutável
  `Mesh` e o construtor `MeshBuilder`, que valida tudo e devolve a malha ou um
  erro. Os elementos ficam em **blocos por tipo**, com conectividade em
  inteiros de 32 bits, e os identificadores externos ficam comprimidos em
  **faixas contíguas**, o que mantém a memória perto dos dados brutos.
- **A ordem local dos nós é a do Gmsh** para todos os tipos. O leitor copia a
  conectividade sem permutar, e a tabela não é escrita à mão: vem da API do
  Gmsh, então a ordem fica certa por construção.
- **`io`** ganha `readGmsh`, que lê o arquivo inteiro para a memória e o
  percorre com um cursor que conhece a linha (ASCII) ou a posição em bytes
  (binário), para as mensagens de erro.
- **Erros esperados** (arquivo inválido, malha inconsistente) voltam como
  `core::Expected<T>`, sobre `tl::expected`, como a arquitetura já prevê.
  Exceções só para defeitos de programação.

## 2. Verificação da constituição

| Princípio | Atende? | Observação |
|-----------|---------|------------|
| Multiplataforma (§2) | Sim | C++20 puro; caminhos por `std::filesystem::path`; números lidos sem depender do locale; testes nos quatro jobs do CI |
| GPU somente via QRhi (§3) | N/A | Sem renderização nesta feature |
| Camadas / `core` sem Qt (§4) | Sim | `Mesh` em `core`, leitor em `io`; nenhum dos dois inclui Qt. `io` entra na verificação `check_no_qt_includes` |
| Correção numérica (§5) | Sim | Coordenadas em `double`; medidas com sinal para detectar elementos invertidos; tabela de tipos e ordem dos nós extraídas do próprio Gmsh e conferidas por arquivos gravados por ele |
| Simplicidade / dependências (§7) | Sim | Uma dependência nova, `tl-expected` (header-only, CC0), já prevista na arquitetura; Gmsh só como ferramenta para gerar a tabela de tipos e os arquivos de teste, nunca ligado ao projeto |

## 3. Módulos e arquivos afetados

| Módulo | Arquivo | Novo/Alterado | Responsabilidade |
|--------|---------|---------------|------------------|
| core | `include/fem/core/expected.hpp` | novo | `Error`, `Expected<T>` sobre `tl::expected` |
| core | `include/fem/core/element_type.hpp`, `element_type.cpp` | novo | Enum `ElementType` e consultas sobre a tabela |
| core | `element_type_table.inc` (privado, gerado) | novo | Tabela dos 103 tipos: dados do Gmsh, gravados pelo script abaixo |
| tools | `generate_element_types.py` | novo | Lê a tabela de tipos da API do Gmsh e grava `element_type_table.inc` e o enum |
| core | `include/fem/core/mesh.hpp`, `mesh.cpp` | novo | `Mesh`, `Group`, `BoundingBox` |
| core | `include/fem/core/mesh_builder.hpp`, `mesh_builder.cpp` | novo | `MeshBuilder` e validação |
| core | `include/fem/core/mesh_measure.hpp`, `mesh_measure.cpp` | novo | Comprimento, área e volume com sinal |
| core | `id_ranges.hpp`, `id_ranges.cpp` (privados) | novo | Identificadores externos comprimidos em faixas |
| core | `CMakeLists.txt` | alterado | Fontes; dependência `tl::expected` |
| io | `include/fem/io/gmsh_reader.hpp`, `gmsh_reader.cpp` | novo | `readGmsh` |
| io | `gmsh_cursor.hpp/.cpp`, `gmsh_types.hpp/.cpp` (privados) | novo | Leitura ASCII/binária com posição; tabela de tipos do Gmsh |
| io | `module.hpp/.cpp` | removido | Substituto provisório da 001 |
| io | `CMakeLists.txt` | alterado | Fontes; deixa de depender de `fem::geom` (não usa) |
| tests | `unit/core/test_element_type.cpp`, `test_mesh.cpp`, `test_mesh_measure.cpp` | novo | Testes de `core` |
| tests | `unit/io/test_gmsh_reader.cpp` | novo | Testes do leitor (substitui `test_io.cpp`) |
| tests | `data/gmsh/*.geo`, `*.msh`, `generate_test_meshes.py`, `README.md` | novo | Arquivos de exemplo e o script que os gera |
| tests | `perf/bench_gmsh_read.cpp` | novo | Medição de NFR-003-02, fora do `ctest` |
| cmake | `CheckNoQtIncludes.cmake` | alterado | Inclui o módulo `io` |
| raiz | `vcpkg.json` | alterado | `tl-expected` |
| docs | `specs/architecture.md` | alterado | Dependências de `core` e `io`; ordem dos nós |

## 4. Interfaces públicas

```cpp
// fem/core/expected.hpp — erros esperados, REQ-003-09, 10, 14, 21, 22, 23
namespace fem::core {

struct Error
{
    std::string message; // English; complete sentence for the user
};

template <typename T>
using Expected = tl::expected<T, Error>;

[[nodiscard]] tl::unexpected<Error> makeError(std::string message);

} // namespace fem::core
```

```cpp
// fem/core/element_type.hpp — REQ-003-01, 02, 03
namespace fem::core {

// Generated from Gmsh 4.15.2 by tools/generate_element_types.py; one value
// per Gmsh element type with a fixed number of nodes. Names follow Gmsh:
// family + number of nodes, with an "I" suffix for Gmsh's incomplete
// variants that share a node count (e.g. Tri15 and Tri15I).
enum class ElementType : std::uint8_t {
    Point1,
    Line2, Line3, Line4, /* ... */ Line11,
    Tri3, Tri6, Tri9, Tri10, /* ... */
    Quad4, Quad8, Quad9, /* ... */
    Tet4, Tet10, /* ... */
    Hex8, Hex20, Hex27, /* ... */ Hex1000,
    Wedge6, Wedge15, Wedge18,
    Pyramid5, Pyramid13, Pyramid14, /* ... */
};
inline constexpr int kElementTypeCount = 103;

enum class ElementFamily : std::uint8_t {
    Point, Line, Triangle, Quadrilateral, Tetrahedron, Hexahedron, Wedge, Pyramid,
};

struct ElementTypeInfo
{
    std::string_view name; // "Tet10"
    ElementFamily family;
    int dimension;   // 0..3
    int order;       // 1..10 (0 for the point)
    int nodeCount;
    int vertexCount; // the vertices are always the first nodes
    int gmshType;    // Gmsh element type number, e.g. 11 for Tet10
};

[[nodiscard]] const ElementTypeInfo& elementTypeInfo(ElementType type) noexcept;
[[nodiscard]] std::span<const ElementType> allElementTypes() noexcept;

// Coordinates of the nodes in Gmsh's reference element, in local node order:
// nodeCount triples (u, v, w). This is what documents the node ordering.
[[nodiscard]] std::span<const std::array<double, 3>>
referenceNodeCoordinates(ElementType type) noexcept;

} // namespace fem::core
```

```cpp
// fem/core/mesh.hpp — REQ-003-04 a 08, 11 a 13
namespace fem::core {

using NodeIndex = std::int32_t;    // internal, contiguous from 0
using ElementIndex = std::int32_t; // internal, contiguous from 0
using ExternalId = std::int64_t;   // number in the source file

struct BoundingBox
{
    Eigen::Vector3d min;
    Eigen::Vector3d max;
    [[nodiscard]] bool isEmpty() const noexcept; // mesh without nodes
};

// Named set of elements of one dimension, e.g. a Gmsh physical group.
struct Group
{
    std::string name;
    int dimension = 0;
    std::vector<ElementIndex> elements; // sorted, unique
};

// All elements of one type, stored contiguously.
struct ElementBlock
{
    ElementType type;
    ElementIndex firstElement;            // global index of the first element
    std::vector<NodeIndex> connectivity;  // nodeCount entries per element
};

class Mesh // immutable; built by MeshBuilder
{
public:
    [[nodiscard]] int nodeCount() const noexcept;
    [[nodiscard]] int elementCount() const noexcept;
    [[nodiscard]] int elementCount(ElementType type) const noexcept;
    [[nodiscard]] int dimension() const noexcept; // highest element dimension; 0 if empty

    [[nodiscard]] Eigen::Vector3d nodeCoordinates(NodeIndex node) const;
    [[nodiscard]] ExternalId nodeId(NodeIndex node) const;
    [[nodiscard]] std::optional<NodeIndex> findNode(ExternalId id) const;

    [[nodiscard]] ElementType elementType(ElementIndex element) const;
    [[nodiscard]] std::span<const NodeIndex> elementNodes(ElementIndex element) const;
    [[nodiscard]] ExternalId elementId(ElementIndex element) const;
    [[nodiscard]] std::optional<ElementIndex> findElement(ExternalId id) const;

    // One block per type present, in ElementType order; the global element
    // index follows this order.
    [[nodiscard]] std::span<const ElementBlock> blocks() const noexcept;

    [[nodiscard]] BoundingBox boundingBox() const;

    [[nodiscard]] std::span<const Group> groups() const noexcept;
    [[nodiscard]] const Group* findGroup(std::string_view name, int dimension) const;
    [[nodiscard]] std::vector<const Group*> findGroups(std::string_view name) const;
    [[nodiscard]] std::vector<NodeIndex> groupNodes(const Group& group) const; // sorted, unique

    // Bytes owned by the mesh (capacities of its containers). NFR-003-01.
    [[nodiscard]] std::size_t memoryUsage() const noexcept;
};

} // namespace fem::core
```

```cpp
// fem/core/mesh_builder.hpp — REQ-003-04, 05, 09, 10, 11, 14
namespace fem::core {

// Element as known while building: its index inside the block of its type.
struct ElementHandle
{
    ElementType type;
    std::int32_t local;
};

class MeshBuilder
{
public:
    void reserveNodes(std::size_t count);
    void reserveElements(ElementType type, std::size_t count);

    // Nodes first. The first addElement() (or finishNodes()) closes the node
    // list and checks for repeated ids.
    [[nodiscard]] Expected<NodeIndex> addNode(ExternalId id, const Eigen::Vector3d& position);
    [[nodiscard]] Expected<void> finishNodes();

    [[nodiscard]] Expected<ElementHandle> addElement(ElementType type, ExternalId id,
                                                     std::span<const ExternalId> nodeIds);

    [[nodiscard]] Expected<void> addGroup(std::string name, int dimension,
                                          std::vector<ElementHandle> elements);

    // Checks repeated element ids and group consistency; the builder is
    // left empty either way.
    [[nodiscard]] Expected<Mesh> build();
};

} // namespace fem::core
```

```cpp
// fem/core/mesh_measure.hpp — REQ-003-08
namespace fem::core {

// Length, area or volume from the vertices (straight edges, planar faces).
// Points measure 1. Volumes are signed. Areas are signed (by the Z component
// of the vector area) when the mesh lies in the XY plane, and positive
// otherwise. Lengths are positive.
[[nodiscard]] double elementMeasure(const Mesh& mesh, ElementIndex element);

} // namespace fem::core
```

```cpp
// fem/io/gmsh_reader.hpp — REQ-003-15 a 24
namespace fem::io {

struct GmshReadResult
{
    core::Mesh mesh;
    std::vector<std::string> warnings; // e.g. ignored element types
};

// Gmsh .msh 4.1, ASCII or binary.
[[nodiscard]] core::Expected<GmshReadResult> readGmsh(const std::filesystem::path& path);

// Same, from memory. `sourceName` only appears in messages. Used by tests.
[[nodiscard]] core::Expected<GmshReadResult> readGmsh(std::string_view contents,
                                                      std::string_view sourceName);

} // namespace fem::io
```

## 5. Dados e algoritmos

### 5.1 Tipos de elemento e ordem dos nós (REQ-003-01 a 03)

A tabela vem da API do Gmsh (`gmsh.model.mesh.getElementProperties`), lida
pelo script `tools/generate_element_types.py` para cada número de tipo de 1 a
999. Entram os 103 tipos com número fixo de nós:

| Família | Tipos | Ordens |
|---------|-------|--------|
| Ponto | 1 | — |
| Linha | 10 | 1 a 10 |
| Triângulo | 18 | 1 a 10, completos e incompletos |
| Quadrilátero | 19 | 1 a 10, completos e incompletos (serendipidade) |
| Tetraedro | 18 | 1 a 10, completos e incompletos |
| Hexaedro | 17 | 1 a 9, completos e incompletos |
| Cunha | 3 | 1 e 2 (6, 15 e 18 nós) |
| Pirâmide | 17 | 1 a 9, completos e incompletos |

Ficam fora os 14 tipos que o Gmsh descreve sem número fixo de nós ou sem
geometria pelos vértices: polígono, poliedro, borda de polígono, os quatro
XFEM e os sete de ordem 0 com um nó.

Para cada tipo o script grava nome, família, dimensão, ordem, número de nós e
de vértices, número do tipo no Gmsh e as coordenadas dos nós no elemento de
referência. A saída (`element_type_table.inc` e a lista do enum) é versionada;
o build não precisa do Gmsh. Os dados são fatos sobre o formato (contagens e
coordenadas), não código do Gmsh.

A ordem local dos nós é a do Gmsh: vértices primeiro, depois nós de aresta,
de face e internos. As coordenadas de referência documentam essa ordem.

A ligação entre o número do tipo no Gmsh e o `ElementType` fica na própria
tabela (`gmshType`), então o leitor não tem tabela separada.

Exportações futuras (VTK na feature 014) terão de permutar alguns tipos, cuja
ordem no VTK é diferente.

### 5.2 Armazenamento (NFR-003-01)

- **Nós:** três `std::vector<double>` (X, Y, Z), como a arquitetura pede (SoA).
- **Elementos:** um `ElementBlock` por tipo presente, em ordem de
  `ElementType`. O índice global é `firstElement` do bloco mais o índice local.
  Assim não há tabela por elemento para descobrir tipo ou posição: basta uma
  busca binária nos no máximo 14 blocos.
- **Identificadores externos** (`IdRanges`, privado): lista de faixas
  `{primeiro id, primeiro índice, quantidade}`. Ids consecutivos na ordem de
  inserção estendem a faixa atual, que é o caso normal nos arquivos do Gmsh;
  ids soltos viram faixas de um elemento (pior caso: 24 bytes por item). Depois
  de fechada, a lista é ordenada por id para busca binária, e ids repetidos
  aparecem como faixas sobrepostas.
- **Grupos:** vetor ordenado de `ElementIndex` (4 bytes por elemento do grupo).

Estimativa para 1 milhão de Tet4 (cerca de 170 mil nós) com um grupo cobrindo
tudo: dados brutos ≈ 16 MB de conectividade + 4 MB de coordenadas; malha ≈
20 MB + 4 MB do grupo + faixas desprezíveis ≈ 1,2× os dados brutos.

Limite: até 2³¹ − 1 nós e elementos (índices de 32 bits). Acima disso o
construtor devolve erro.

### 5.3 Medida dos elementos (REQ-003-08)

- Ponto: 1. Linha: distância entre os vértices 0 e 1.
- Triângulo: área vetorial `½ (v1 − v0) × (v2 − v0)`; quadrilátero:
  `½ (v2 − v0) × (v3 − v1)` (diagonais; exata para quadriláteros planos).
- Sólidos: soma de volumes com sinal de tetraedros dos vértices — hexaedro em
  6 tetraedros que compartilham a diagonal 0-6, cunha em 3, pirâmide em 2. Exata
  para faces planas.
- Em todos os casos só os vértices contam: a medida é a da forma de lados
  retos, também para elementos de ordem alta. A medida exata de elementos
  curvos precisa das funções de forma (feature 006).
- Sinal das áreas: se todos os nós têm |Z| ≤ 1e−12 × diagonal da caixa
  envolvente, a área tem o sinal da componente Z da área vetorial; senão é
  positiva (malhas de superfície em 3D não têm orientação definida).

### 5.4 Construção e validação (REQ-003-09, 10, 14)

| Verificação | Momento | Mensagem (exemplo) |
|-------------|---------|--------------------|
| Número de nós ≠ do tipo | `addElement` | "Element 12 (Tet10) has 9 nodes; Tet10 needs 10." |
| Nó inexistente | `addElement` | "Element 12 references node 999, which does not exist." |
| Nó repetido no elemento | `addElement` | "Element 12 uses node 7 more than once." |
| Id de nó repetido | `finishNodes` | "Node id 7 is used more than once." |
| Id de elemento repetido | `build` | "Element id 12 is used more than once." |
| Grupo com nome repetido na dimensão | `addGroup` | "There is already a 2D group named 'wall'." |
| Grupo com elemento de outra dimensão | `addGroup` | "Group 'wall' (2D) contains element 12, which is 3D." |
| Mais de 2³¹ − 1 nós ou elementos | `addNode` / `addElement` | "The mesh has more than 2147483647 elements." |

Mensagens em inglês, como o resto do código; a tradução, quando chegarem à
interface, é responsabilidade do `app` (feature 004).

### 5.5 Leitor `.msh` 4.1

O arquivo é lido inteiro para um `std::string` (cerca de 60 MB para 1 milhão de
tetraedros em ASCII) e percorrido por um cursor:

- **ASCII:** tokens separados por espaço; o cursor conta as linhas. Inteiros com
  `std::from_chars`; reais com `std::from_chars` quando a biblioteca padrão o
  oferece para `double`, senão com um analisador próprio que não depende do
  locale (ver riscos).
- **Binário:** lê `int` (4 bytes), `size_t` (8 bytes, exigido pelo cabeçalho)
  e `double` diretamente, por `memcpy`; o cursor guarda a posição em bytes.

Seções tratadas (as demais são puladas até o `$End…` correspondente, REQ-003-18):

| Seção | Tratamento |
|-------|------------|
| `$MeshFormat` | Exige versão `4.1` e tamanho de `size_t` 8. No binário, o inteiro `1` em seguida confere a ordem dos bytes (REQ-003-21). Versão diferente ⇒ "Gmsh format version 2.2 is not supported; save the mesh as version 4.1." |
| `$PhysicalNames` | Mapa (dimensão, número) → nome |
| `$Entities` | Mapa (dimensão, entidade) → grupos físicos. Pontos, curvas, superfícies e volumes, com caixas e fronteiras puladas |
| `$PartitionedEntities` | Recusa: "Partitioned meshes are not supported." (REQ-003-21) |
| `$Nodes` | Por bloco de entidade: ids, depois coordenadas; coordenadas paramétricas puladas (REQ-003-17) |
| `$Elements` | Por bloco: dimensão, entidade, tipo do Gmsh, quantidade, depois id e nós de cada elemento |

Tipos do Gmsh: os 103 da tabela de §5.1 são carregados. Os demais
(REQ-003-19) são pulados com aviso; no binário isso exige saber quantos nós eles
têm, e o único caso possível num `.msh` 4.1 são os de ordem 0 com um nó, já que
polígonos, poliedros e XFEM não podem ser gravados em blocos de tamanho fixo.
Um número de tipo desconhecido é erro: "Unknown Gmsh element type 250 at byte
1234."

Grupos (REQ-003-16): cada elemento entra nos grupos físicos da sua entidade.
Nome do `$PhysicalNames` ou, na falta, `group_<dim>d_<número>` (por exemplo,
`group_2d_7`). Ordem dos grupos: por dimensão, depois por número.

Avisos: um por tipo ignorado, com nome e quantidade, por exemplo
"Ignored 48 elements of unsupported Gmsh type 10 (9-node quadrangle)."

Erros (REQ-003-22): o cursor anexa a posição — "line 1532" ou "byte 20481" — e o
nome do arquivo. Qualquer erro descarta o construtor, então nada parcial sai
(REQ-003-23).

Caminhos (REQ-003-24): `std::ifstream` aberto com `std::filesystem::path`, que
no Windows usa a API de caminhos largos; a camada `app` converte de `QString`
quando chegar a hora.

## 6. Fluxo

```
readGmsh(path)
  └─ lê o arquivo inteiro → readGmsh(contents, name)
       ├─ $MeshFormat → modo ASCII/binário, checagens
       ├─ $PhysicalNames, $Entities → mapas
       ├─ $Nodes → builder.addNode(...) ; builder.finishNodes()
       ├─ $Elements → builder.addElement(...) por bloco suportado,
       │              guarda os ElementHandle por entidade; conta os ignorados
       ├─ grupos físicos → builder.addGroup(...)
       └─ builder.build() → GmshReadResult{mesh, warnings}
```

Tudo síncrono, numa thread. Executar em segundo plano com progresso fica para
o `app` (feature 004 ou 011).

## 7. Tratamento de erros

| Situação | Comportamento | Requisito |
|----------|---------------|-----------|
| Elemento inválido ou ids repetidos | `Expected` com erro; nada é construído | REQ-003-09, 10, 23 |
| Grupo inválido | Idem | REQ-003-14 |
| Arquivo inexistente ou ilegível | Erro com o caminho e o motivo do sistema | REQ-003-22 |
| Versão diferente de 4.1, particionado, ordem de bytes diferente, `size_t` ≠ 8 | Erro que nomeia a situação | REQ-003-21 |
| Arquivo malformado | Erro com linha ou byte | REQ-003-22 |
| Tipos não suportados | Elementos ignorados, aviso por tipo | REQ-003-19 |
| Nenhum elemento suportado | Erro: "The file has no elements of a supported type." | REQ-003-20 |
| Índice fora do intervalo nas consultas de `Mesh` | Defeito do chamador: `assert` em Debug | — |

## 8. Estratégia de testes

| Teste | Tipo | Requisito | Referência/tolerância |
|-------|------|-----------|-----------------------|
| Tabela de tipos: 103 tipos, contagem por família, nós, vértices, ordem, número do Gmsh únicos | unitário | REQ-003-01, 02 | exato |
| Elemento de referência de cada tipo com medida positiva e igual à do elemento de referência do Gmsh | unitário | REQ-003-03, 08 | 1e−12 |
| Vértices de referência de cada família iguais entre todos os tipos da família | unitário | REQ-003-03 | exato |
| Malha mista montada em teste: consultas, ids não contíguos, caixa envolvente | unitário | REQ-003-04 a 07 | exato |
| Elemento invertido tem medida negativa; superfície fora do plano XY tem área positiva | unitário | REQ-003-08 | — |
| Cada erro de §5.4 com a mensagem esperada | unitário | REQ-003-09, 10, 14 | texto contém o id |
| Grupos: elemento em dois grupos, elemento sem grupo, `groupNodes`, busca por nome | unitário | REQ-003-11, 12, 13 | exato |
| Memória de malha sintética de 200 mil Tet4 com um grupo ≤ 1,5× os dados brutos | unitário | NFR-003-01 | razão |
| Arquivo com um elemento de cada um dos 103 tipos, gravado pelo Gmsh (ASCII e binário): cada nó lido na posição dada pelas coordenadas de referência e medidas iguais às esperadas | unitário | REQ-003-15, AC-7 | 1e−12 relativo |
| Malhas reais geradas a partir de `.geo` (linhas, superfícies e volumes, de ordem 1 e 2): soma das medidas igual à da geometria, todas positivas | unitário | REQ-003-15 | 1e−9 relativo |
| ASCII e binário do mesmo modelo produzem malhas iguais (nós, elementos, grupos) | unitário | REQ-003-15, 16 | exato |
| Grupos físicos com nome e sem nome | unitário | REQ-003-16 | exato |
| Ids esparsos e fora de ordem; nós com coordenadas paramétricas | unitário | REQ-003-17 | exato |
| Arquivo com `$NodeData`/`$Periodic` e com quadriláteros de 9 nós: lido, com aviso | unitário | REQ-003-18, 19 | texto do aviso |
| Só tipos não suportados ⇒ recusado | unitário | REQ-003-20 | — |
| Versão 2.2, particionado, ordem de bytes trocada (gerada no teste a partir de um binário) | unitário | REQ-003-21 | texto da mensagem |
| Truncado, número inválido, nó inexistente, em ASCII (linha) e binário (byte) | unitário | REQ-003-22, 23 | posição na mensagem |
| Arquivo copiado no teste para um caminho com acentos e lido | unitário | REQ-003-24 | — |
| `check_no_qt_includes` cobrindo `io` | CTest | REQ-003-25 | — |
| `bench_gmsh_read` com 1 milhão de Tet4, ASCII e binário | manual, Release | NFR-003-02 | < 5 s / < 30 s |
| Tempo dos testes de `core` e `io` | CTest | NFR-003-03 | < 5 s |

**Arquivos de exemplo** (REQ-003-26), em `tests/data/gmsh/`, produzidos pelo
script `generate_test_meshes.py` com a API Python do Gmsh 4.15.2:

- **Todos os tipos:** um modelo com um elemento de cada um dos 103 tipos, cada
  um numa entidade própria e afastado dos demais; os nós ficam na imagem das
  coordenadas de referência por uma transformação afim conhecida (escala,
  rotação e translação diferentes por tipo). Gravado em ASCII e em binário.
- **Malhas reais:** scripts `.geo` com geometrias de medida conhecida (segmento
  de comprimento 2, quadrado 2×3, caixa 1×2×3), em ordem 1 e 2, com grupos
  físicos com e sem nome, gravados em ASCII e binário.
- **Casos especiais:** seções extras (`$NodeData`, `$Periodic`), ids esparsos,
  coordenadas paramétricas e um tipo não suportado, quando o Gmsh conseguir
  gravá-los; senão, pequenos `.msh` ASCII escritos à mão.
- **Erros:** pequenos `.msh` escritos à mão ou derivados em memória pelo
  próprio teste (por exemplo, a ordem de bytes trocada).

Os arquivos ficam versionados (dezenas a poucas centenas de kB no total); o CI
não precisa do Gmsh. O README da pasta explica como regenerá-los.

## 9. Alternativas consideradas

| Alternativa | Por que foi descartada |
|-------------|------------------------|
| Conectividade única em CSR (offsets por elemento) para todos os tipos | +4 a 8 bytes por elemento e um nível de indireção; os blocos por tipo também favorecem os laços de montagem da feature 006 |
| `std::unordered_map` para os ids externos | Cerca de 40 bytes por entrada; estoura NFR-003-01 |
| Ordem de nós própria (ou do VTK) com permutação no leitor | Mais código e mais chance de erro; a ordem do Gmsh é tão boa quanto qualquer outra e documentada |
| Tabela de tipos escrita à mão | 103 tipos com até 1000 nós: inviável de escrever e conferir; a API do Gmsh dá os dados exatos |
| Tipo de elemento genérico (número do Gmsh guardado como inteiro, sem enum) | Perde a verificação do compilador nos `switch` das features de elementos; o enum gerado custa o mesmo byte |
| Exceções para arquivo inválido | Contraria a arquitetura (exceções só para defeitos) |
| `iostream >>` para ler números | Lento e dependente do locale (vírgula decimal em pt_BR) |
| SDK do Gmsh para ler `.msh` | GPL — proibido pela constituição |
| Leitura em fluxo, sem carregar o arquivo inteiro | Complica a contagem de linhas e o salto de seções; a memória extra é temporária e aceitável até ~10 milhões de elementos |

## 10. Riscos e mitigação

- **Dados do Gmsh mudarem entre versões** → a tabela é gerada uma vez, com a
  versão registrada no arquivo gerado (4.15.2); regenerar com outra versão
  mostra a diferença no diff. O formato 4.1 e a numeração dos tipos são
  estáveis desde 2018.
- **Elementos de ordem alta são grandes** (Hex1000 tem 1000 nós) → a memória é
  proporcional aos nós de cada elemento, como para qualquer tipo; nenhum
  limite novo.
- **`std::from_chars` para `double` pode não existir na libc++ do macOS do CI**
  → detecção por `__cpp_lib_to_chars`; na falta, analisador próprio de números
  reais, testado contra `from_chars` onde ele existe. Se a precisão do
  analisador próprio não chegar a arredondamento correto, o teste de
  igualdade ASCII × binário acusa.
- **Arquivos muito grandes** (dezenas de milhões de elementos) → carregar o
  arquivo inteiro dobra a memória durante a leitura; aceitável para a meta de
  1 milhão do produto; leitura em fluxo fica para quando houver necessidade.
- **Gmsh 4.15.2 (de 2026) pode escrever algo que versões antigas não
  escrevem, ou vice-versa** → os arquivos versionados fixam o que é testado; o
  formato 4.1 é estável desde 2018.

## 11. Novas dependências

| Dependência | Versão | Licença | Justificativa |
|-------------|--------|---------|---------------|
| `tl-expected` (vcpkg) | a do baseline do vcpkg | CC0 1.0 | `Expected<T>` para erros esperados, já prevista em `specs/architecture.md` até C++23 ser o piso. Header-only |

Ferramenta, não dependência: Gmsh 4.15.2 (GPL), pela API Python, usado só na
máquina do mantenedor para gerar a tabela de tipos e os arquivos de teste; não é
ligado, distribuído nem exigido pelo build ou pelo CI.

`io` deixa de depender de `fem::geom`, que não usa nesta feature (a arquitetura
continua prevendo a dependência para STEP/IGES/DXF, que a recolocam).
