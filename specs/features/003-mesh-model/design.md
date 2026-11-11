# Design — 003 mesh-model

Status: rascunho · Data: 2026-11-11
Requisitos: [requirements.md](requirements.md)

## 1. Resumo da abordagem

- **`core`** ganha os tipos de elemento (tabela `constexpr` com dimensão, nós,
  vértices, ordem e posição dos nós de meio de aresta), a malha imutável
  `Mesh` e o construtor `MeshBuilder`, que valida tudo e devolve a malha ou um
  erro. Os elementos ficam em **blocos por tipo**, com conectividade em
  inteiros de 32 bits, e os identificadores externos ficam comprimidos em
  **faixas contíguas**, o que mantém a memória perto dos dados brutos.
- **A ordem local dos nós é a do Gmsh** para os catorze tipos. O leitor copia a
  conectividade sem permutar, e os testes com arquivos gerados pelo Gmsh
  confirmam a tabela.
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
| Correção numérica (§5) | Sim | Coordenadas em `double`; medidas com sinal para detectar elementos invertidos; ordem dos nós conferida contra arquivos do próprio Gmsh |
| Simplicidade / dependências (§7) | Sim | Uma dependência nova, `tl-expected` (header-only, CC0), já prevista na arquitetura; Gmsh só como ferramenta para gerar arquivos de teste, nunca ligado ao projeto |

## 3. Módulos e arquivos afetados

| Módulo | Arquivo | Novo/Alterado | Responsabilidade |
|--------|---------|---------------|------------------|
| core | `include/fem/core/expected.hpp` | novo | `Error`, `Expected<T>` sobre `tl::expected` |
| core | `include/fem/core/element_type.hpp`, `element_type.cpp` | novo | Enum `ElementType`, tabela de propriedades, arestas dos nós de meio de aresta |
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
| tests | `data/gmsh/*.geo`, `*.msh`, `Generate.cmake`, `README.md` | novo | Arquivos de exemplo e o script que os gera |
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

enum class ElementType : std::uint8_t {
    Point1, Line2, Line3, Tri3, Tri6, Quad4, Quad8,
    Tet4, Tet10, Hex8, Hex20, Wedge6, Wedge15, Pyramid5,
};
inline constexpr int kElementTypeCount = 14;

enum class ElementOrder : std::uint8_t { Linear, Quadratic };

struct ElementTypeInfo
{
    std::string_view name; // "Tet10"
    int dimension;         // 0..3
    int nodeCount;
    int vertexCount;
    ElementOrder order;
};

[[nodiscard]] const ElementTypeInfo& elementTypeInfo(ElementType type) noexcept;
[[nodiscard]] std::span<const ElementType> allElementTypes() noexcept;

// For quadratic types: local node vertexCount + i lies on the edge between
// the two vertices midNodeEdges(type)[i]. Empty for linear types.
[[nodiscard]] std::span<const std::array<int, 2>> midNodeEdges(ElementType type) noexcept;

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

### 5.1 Ordem local dos nós (REQ-003-03)

Igual à do Gmsh ("node ordering" da documentação do Gmsh). Vértices primeiro,
depois os nós de meio de aresta, na ordem das arestas abaixo:

| Tipo | Vértices | Arestas dos nós de meio de aresta (nó: vértices) |
|------|----------|------------------------------------------------|
| Line3 | 0–1 | 2: 0-1 |
| Tri3 / Tri6 | 0–2, anti-horário | 3: 0-1, 4: 1-2, 5: 2-0 |
| Quad4 / Quad8 | 0–3, anti-horário | 4: 0-1, 5: 1-2, 6: 2-3, 7: 3-0 |
| Tet4 / Tet10 | 0–2 base, 3 topo | 4: 0-1, 5: 1-2, 6: 2-0, 7: 3-0, 8: 3-2, 9: 3-1 |
| Hex8 / Hex20 | 0–3 base, 4–7 topo | 8: 0-1, 9: 0-3, 10: 0-4, 11: 1-2, 12: 1-5, 13: 2-3, 14: 2-6, 15: 3-7, 16: 4-5, 17: 4-7, 18: 5-6, 19: 6-7 |
| Wedge6 / Wedge15 | 0–2 base, 3–5 topo | 6: 0-1, 7: 0-2, 8: 0-3, 9: 1-2, 10: 1-4, 11: 2-5, 12: 3-4, 13: 3-5, 14: 4-5 |
| Pyramid5 | 0–3 base, 4 ápice | — |

A tabela foi escrita de memória da documentação do Gmsh; quem a confirma são os
testes de AC-7 (cada nó de meio de aresta de um arquivo gerado pelo Gmsh, com
arestas retas, tem de estar no ponto médio da aresta da tabela). Se algum
teste discordar, a tabela é corrigida e o design atualizado.

Exportações futuras (VTK na feature 014) terão de permutar Hex20 e Wedge15,
cuja ordem no VTK é diferente.

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

Tipos do Gmsh suportados: 15 Point1, 1 Line2, 8 Line3, 2 Tri3, 9 Tri6, 3 Quad4,
16 Quad8, 4 Tet4, 11 Tet10, 5 Hex8, 17 Hex20, 6 Wedge6, 18 Wedge15, 7
Pyramid5. Para os não suportados (REQ-003-19) o leitor precisa do número de
nós para pular o bloco no binário; a tabela privada cobre os tipos 1 a 31 do
Gmsh (lineares a quárticos). Um tipo fora dela é erro: "Unknown Gmsh element
type 92 at byte 1234."

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
| Tabela de tipos: dimensão, nós, vértices, ordem dos 14 tipos | unitário | REQ-003-01, 02 | exato |
| Elemento de referência de cada tipo com medida positiva e conhecida | unitário | REQ-003-03, 08 | 1e−12 |
| Nós de meio de aresta do elemento de referência no ponto médio das arestas da tabela | unitário | REQ-003-03 | 1e−12 |
| Malha mista montada em teste: consultas, ids não contíguos, caixa envolvente | unitário | REQ-003-04 a 07 | exato |
| Elemento invertido tem medida negativa; superfície fora do plano XY tem área positiva | unitário | REQ-003-08 | — |
| Cada erro de §5.4 com a mensagem esperada | unitário | REQ-003-09, 10, 14 | texto contém o id |
| Grupos: elemento em dois grupos, elemento sem grupo, `groupNodes`, busca por nome | unitário | REQ-003-11, 12, 13 | exato |
| Memória de malha sintética de 200 mil Tet4 com um grupo ≤ 1,5× os dados brutos | unitário | NFR-003-01 | razão |
| Um par de arquivos (ASCII e binário) por tipo, gerado pelo Gmsh: soma das medidas igual à da geometria, todas as medidas positivas, nós de meio de aresta no ponto médio | unitário | REQ-003-15, AC-7 | 1e−9 relativo |
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

**Arquivos de exemplo** (REQ-003-26): scripts `.geo` em `tests/data/gmsh/`,
um por tipo e alguns para grupos e casos especiais, com geometrias de medida
conhecida (segmento de comprimento 2, quadrado 2×3, caixa 1×2×3). O script
`Generate.cmake` roda o Gmsh 4.15.2 em cada `.geo` e grava as versões ASCII e
binária (`-format msh41`, com e sem `-bin`); a pirâmide, que o Gmsh não gera
sozinha num caso simples, é um `.msh` ASCII escrito à mão e convertido para
binário pelo próprio Gmsh. Os arquivos gerados ficam versionados (pequenos,
dezenas de kB no total); o CI não precisa do Gmsh. O README da pasta explica
como regenerá-los. Os arquivos de erro são pequenos `.msh` escritos à mão ou
derivados em memória pelo próprio teste.

## 9. Alternativas consideradas

| Alternativa | Por que foi descartada |
|-------------|------------------------|
| Conectividade única em CSR (offsets por elemento) para todos os tipos | +4 a 8 bytes por elemento e um nível de indireção; os blocos por tipo também favorecem os laços de montagem da feature 006 |
| `std::unordered_map` para os ids externos | Cerca de 40 bytes por entrada; estoura NFR-003-01 |
| Ordem de nós própria (ou do VTK) com permutação no leitor | Mais código e mais chance de erro; a ordem do Gmsh é tão boa quanto qualquer outra e documentada |
| Exceções para arquivo inválido | Contraria a arquitetura (exceções só para defeitos) |
| `iostream >>` para ler números | Lento e dependente do locale (vírgula decimal em pt_BR) |
| SDK do Gmsh para ler `.msh` | GPL — proibido pela constituição |
| Leitura em fluxo, sem carregar o arquivo inteiro | Complica a contagem de linhas e o salto de seções; a memória extra é temporária e aceitável até ~10 milhões de elementos |

## 10. Riscos e mitigação

- **Ordem dos nós escrita de memória (§5.1) pode ter erro** → os testes com
  arquivos do Gmsh conferem cada nó de meio de aresta; divergência corrige a
  tabela antes de a feature fechar.
- **`std::from_chars` para `double` pode não existir na libc++ do macOS do CI**
  → detecção por `__cpp_lib_to_chars`; na falta, analisador próprio de números
  reais, testado contra `from_chars` onde ele existe. Se a precisão do
  analisador próprio não chegar a arredondamento correto, o teste de
  igualdade ASCII × binário acusa.
- **Gerar uma pirâmide com o Gmsh** pode não ser simples → arquivo escrito à
  mão (previsto acima).
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

Ferramenta, não dependência: Gmsh 4.15.2 (GPL), usado só para gerar os arquivos
de teste, na máquina do mantenedor; não é ligado, distribuído nem exigido pelo
build ou pelo CI.

`io` deixa de depender de `fem::geom`, que não usa nesta feature (a arquitetura
continua prevendo a dependência para STEP/IGES/DXF, que a recolocam).
