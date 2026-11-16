# Design — 007 truss-elements

Status: aprovado · Data: 2026-11-15
Requisitos: [requirements.md](requirements.md)

## 1. Resumo da abordagem

Tudo em `core`, sem Qt, sobre o núcleo da 006:

1. **`Material` e `Section`**: structs simples com nome. As propriedades que
   podem faltar são `std::optional`, porque a validação precisa distinguir
   "ausente" de "zero".
2. **`Model`**: um agregado de valor (structs e `std::vector` públicos). A
   malha fica num `std::shared_ptr<const Mesh>`, então copiar o modelo não
   copia a malha, o que serve de base para o instantâneo da 011. Materiais e
   seções são referenciados pelo nome. Conjuntos de elementos e de nós são
   referenciados por um grupo da malha ou por uma lista de ids externos (os
   números que o usuário vê). Edição significa mexer nos vetores. Na 010, a
   interface embrulha cada edição num `QUndoCommand`.
3. **`TrussElement`**: uma `ElementFormulation` por variante (2D, 3D) com uma
   tabela por elemento de dados já resolvidos (EA, deformação imposta e peso
   por comprimento). `compute()` lê só a tabela e os nós, sem alocar.
4. **`validate(Model)`**: percorre o modelo inteiro e devolve a lista de
   problemas, sem parar no primeiro.
5. **`runLinearStatic(Model)`**: valida, resolve os conjuntos, monta a tabela
   das barras, numera os GDL e prescreve apoios. Antes de fatorar, verifica a
   diagonal nula da matriz. Depois resolve com `SparseCholesky` e recupera
   N, ε e σ por barra. O resultado traz deslocamentos e reações por nó.
6. **Mecanismo**: duas camadas de diagnóstico. A primeira é a diagonal nula,
   em que um nó não tem rigidez numa direção e todos esses casos são
   listados. A segunda é o pivô nulo na fatoração, um mecanismo global.
   `SparseCholesky` passa a informar a primeira equação singular, já na
   numeração original.

## 2. Verificação da constituição

| Princípio | Atende? | Observação |
|-----------|---------|------------|
| Multiplataforma (§2) | Sim | C++20 e Eigen; nada específico de sistema |
| GPU somente via QRhi (§3) | Sim | Nada de GPU |
| Camadas / `core` sem Qt (§4) | Sim | Tudo em `core` (REQ-007-16) |
| Correção numérica (§5) | Sim | Patch test (corpo rígido e deformação uniforme), convergência (barra pendurada), soluções analíticas 2D e 3D; tolerâncias explícitas; núcleo agnóstico a unidades (REQ-007-07) |
| Qualidade (§6) | Sim | Cobertura de `core` ≥ 80% mantida no CI |
| Simplicidade / dependências (§7) | Sim | Nenhuma dependência nova; `Model` como agregado de valor, sem camada de comandos (fica na 010) |

## 3. Módulos e arquivos afetados

| Módulo | Arquivo | Novo/Alterado | Responsabilidade |
|--------|---------|---------------|------------------|
| core | `include/fem/core/material.hpp` | novo | `Material`, `Section` (REQ-007-01, 02) |
| core | `include/fem/core/model.hpp`, `model.cpp` | novo | `Model`, alvos, apoios, ações; resolução dos alvos na malha (REQ-007-03 a 07) |
| core | `include/fem/core/truss_element.hpp`, `truss_element.cpp` | novo | Formulação da treliça 2D/3D e recuperação de esforços (REQ-007-08, 11, 12) |
| core | `include/fem/core/static_analysis.hpp`, `static_analysis.cpp` | novo | `validate`, `runLinearStatic`, `StaticResult`, diagnóstico de mecanismo (REQ-007-09, 10, 12 a 15) |
| core | `include/fem/core/linear_solver.hpp`, `linear_solver.cpp` | alterado | `SparseCholesky::singularEquation()` (REQ-007-15) |
| core | `CMakeLists.txt` | alterado | Novos fontes |
| tests | `unit/core/truss_meshes.hpp` | novo | Malhas de treliça para os testes (`MeshBuilder`) |
| tests | `unit/core/test_model.cpp`, `test_truss_element.cpp`, `test_truss_verification.cpp`, `test_static_validation.cpp` | novo | AC-1 a AC-7 |
| tests | `perf/bench_truss.cpp`, `CMakeLists.txt` | novo/alterado | NFR-007-02 |
| specs | `architecture.md` | alterado | O que existe de `Material`, `Section`, `Model` e da análise estática |

## 4. Interfaces públicas

```cpp
// material.hpp — REQ-007-01, 02. Numbers without units (REQ-007-07).
struct Material {
    std::string name;
    std::optional<double> youngsModulus;      // E
    std::optional<double> poissonsRatio;      // nu
    std::optional<double> density;            // rho
    std::optional<double> thermalExpansion;   // alpha
    std::optional<double> conductivity;       // k
    std::optional<double> specificHeat;       // c
};

struct Section {                              // bar section; 008 adds inertias
    std::string name;
    std::optional<double> area;               // A
};
```

```cpp
// model.hpp — REQ-007-03 to 07.
struct GroupRef { std::string name; };        // a group of the mesh

struct ElementIds { std::vector<ExternalId> ids; };  // external ids
struct NodeIds { std::vector<ExternalId> ids; };
// Elements: a group of dimension 1 or a list of external element ids.
using ElementTarget = std::variant<GroupRef, ElementIds>;
// Nodes: the nodes of the groups with that name (any dimension) or a list
// of external node ids. (Distinct list types: with two identical variants
// the two resolve() overloads would collide — adjusted in T01.)
using NodeTarget = std::variant<GroupRef, NodeIds>;

enum class ElementKind : std::uint8_t { Truss2D, Truss3D };   // 008: beams

struct Assignment {                           // REQ-007-04
    ElementTarget elements;
    ElementKind kind = ElementKind::Truss3D;
    std::string material;
    std::string section;
};

struct Restraint {                            // REQ-007-05
    NodeTarget nodes;
    DofType dof = DofType::U;                 // u, v or w in 007
    double value = 0.0;                       // 0: support; else settlement
};

struct NodalForce { NodeTarget nodes; Eigen::Vector3d force; };     // REQ-007-06
struct TemperatureChange { ElementTarget elements; double deltaT; };
struct InitialStrain { ElementTarget elements; double strain; };    // eps0

// One load case. A value type: copies share the immutable mesh.
struct Model {
    std::shared_ptr<const Mesh> mesh;
    std::vector<Material> materials;
    std::vector<Section> sections;
    std::vector<Assignment> assignments;
    std::vector<Restraint> restraints;
    std::vector<NodalForce> nodalForces;
    std::optional<Eigen::Vector3d> gravity;   // acceleration, whole model
    std::vector<TemperatureChange> temperatureChanges;
    std::vector<InitialStrain> initialStrains;

    [[nodiscard]] const Material* findMaterial(std::string_view name) const;
    [[nodiscard]] const Section* findSection(std::string_view name) const;
};

// Internal indices of a target, sorted and unique, or why it does not resolve
// (missing group, unknown id).
[[nodiscard]] Expected<std::vector<ElementIndex>> resolve(const Mesh&, const ElementTarget&);
[[nodiscard]] Expected<std::vector<NodeIndex>> resolve(const Mesh&, const NodeTarget&);
```

```cpp
// truss_element.hpp — REQ-007-08, 11, 12.
struct BarData {                              // per element, resolved
    double axialStiffness = 0.0;              // EA
    double imposedStrain = 0.0;               // alpha*dT + eps0
    Eigen::Vector3d weight = Eigen::Vector3d::Zero(); // rho*A*g, per length
};

class TrussElement final : public ElementFormulation {
public:
    // `bars` indexed by ElementIndex; must outlive the formulation.
    TrussElement(ElementKind kind, std::span<const BarData> bars);
    DofMask dofsAt(ElementType, int) const override;          // u v (w)
    Expected<void> compute(const ElementNodes&, ElementIndex,
                           Eigen::Ref<Eigen::MatrixXd>,
                           Eigen::Ref<Eigen::VectorXd>) const override;
};

// N = EA ((u2 - u1).e / L - imposedStrain): tension positive.
[[nodiscard]] double axialForce(const BarData&, const Eigen::Vector3d& x1,
                                const Eigen::Vector3d& x2, const Eigen::Vector3d& u1,
                                const Eigen::Vector3d& u2);
```

```cpp
// static_analysis.hpp — REQ-007-09, 10, 12 to 15.
struct ModelIssue { std::string message; };   // full sentence, names the item

// Every problem found, in model order; empty when the model can be solved.
[[nodiscard]] std::vector<ModelIssue> validate(const Model&);

struct StaticResult {
    std::shared_ptr<const Mesh> mesh;
    std::vector<Eigen::Vector3d> displacements; // per node (u, v, w; 0 if no dof)
    std::vector<Eigen::Vector3d> reactions;     // per node (0 where free)
    // Per element (NaN for elements that are not bars):
    std::vector<double> axialForce;             // N
    std::vector<double> strain;                 // mechanical: N / EA
    std::vector<double> stress;                 // N / A
};

struct StaticOptions { int threads = 0; };      // 0: all cores

// REQ-007-12, 13: reads the model only. Fails with the issues of validate()
// (one per line) or with the node and direction of a mechanism.
[[nodiscard]] Expected<StaticResult> runLinearStatic(const Model&, const StaticOptions& = {});
```

```cpp
// linear_solver.hpp — altered (REQ-007-15).
class SparseCholesky {
    ...
    // After a failed factorize(): the first equation, in the numbering of
    // the input matrix, whose pivot was not clearly positive.
    [[nodiscard]] std::optional<std::int32_t> singularEquation() const;
};
```

## 5. Dados e algoritmos

### 5.1 Barra de treliça

Nós x₁ e x₂, comprimento L = |x₂ − x₁| e direção e = (x₂ − x₁)/L. Em 2D, e
tem só as componentes x e y (z = 0 garantido pela validação). Com
k = EA/L:

```
K = k [  e eᵀ  −e eᵀ ]        (6×6 em 3D, 4×4 em 2D)
      [ −e eᵀ   e eᵀ ]
```

Deformação imposta ε* = α·ΔT + ε₀, somando todas as ações sobre a barra:

```
f_imposta = EA ε* [ −e ; e ]            (afasta os nós quando ε* > 0)
f_peso    = (ρ A L / 2) [ g ; g ]       (componentes x, y em 2D)
```

Recuperação: N = EA((u₂ − u₁)·e / L − ε*); ε = N/EA (mecânica); σ = N/A.
Com a barra presa nas duas pontas, N = −EA·ε* (AC-5). Não há quadratura: as
fórmulas são exatas e cabem em 6×6 sem alocação.

### 5.2 Resolução dos alvos

- `GroupRef` de elementos: `mesh.findGroup(name, 1)`. Um grupo inexistente ou
  de outra dimensão é um problema.
- `GroupRef` de nós: a união de `groupNodes` de todos os grupos com o nome
  (pontos, linhas…), o que permite usar grupos físicos de ponto do Gmsh.
- Listas de ids usam `findElement` / `findNode`. Um id desconhecido é um
  problema que cita o id.

### 5.3 Validação (REQ-007-09, 10, 14)

Uma passada que coleta todos os problemas. Os itens são citados pela posição
e pelo nome, por exemplo "Assignment 2: material \"Steel\" does not exist.":

1. falta a malha, ou nenhum elemento atribuído;
2. alvos que não se resolvem: grupo ou id inexistente, em qualquer item;
3. atribuição com material ou seção inexistente; E ausente ou ≤ 0; A ausente
   ou ≤ 0; ρ ausente ou ≤ 0 com `gravity` definido; α ausente com ΔT em
   alguma barra do material;
4. elemento de dimensão ≥ 1 sem atribuição, ou com duas. Elementos de
   dimensão 0 (`Point1`) não precisam de atribuição: só servem para definir
   grupos de nós;
5. elemento atribuído que não é `Line2` (REQ-007-10), barra de comprimento
   nulo, treliça 2D com |z| > 1e−9 × diagonal da caixa envolvente
   (REQ-007-09);
6. apoio em direção que o nó não tem (w num nó só de treliça 2D), apoio ou
   força em nó sem barra ligada, força com componente w ≠ 0 em nó só 2D.

O item 6 usa a mesma máscara de GDL que a `DofMap` vai usar, calculada a
partir das atribuições válidas.

### 5.4 Fluxo da análise

```
runLinearStatic(model)
 ├─ validate(model)                     → falha: todos os problemas
 ├─ bars[elementCount]: BarData por elemento atribuído
 ├─ conjuntos: um ElementSet por ElementKind presente
 ├─ DofMap::build; prescribe(restrições)  (recalque: valor ≠ 0)
 ├─ NodalLoad das forças nodais (só GDL existentes)
 ├─ assemble → diagonal ≤ 1e−12 × maior diagonal?  → falha: nó e direção, todos
 ├─ SparseCholesky::factorize → falha? singularEquation → nó e direção
 ├─ solve; valores; reactions()         (núcleo da 006: solveLinear)
 └─ StaticResult: u e reações por nó; N, ε, σ por barra
```

Para não montar duas vezes, `runLinearStatic` usa `assemble`, a interface do
solver e `reactions` diretamente, e não `solveLinear`. A inversa
equação → GDL é um vetor montado uma vez.

### 5.5 Equação singular no `SparseCholesky`

`SimplicialLDLT` fatora P A Pᵀ. O primeiro k, na ordem permutada, com
D(k) ≤ 1e−14 × max|D| (o mesmo critério da 006) é levado à numeração
original pela permutação inversa. Quando o Eigen para num pivô exatamente
nulo, os D seguintes ficam zerados, e o primeiro k encontrado é o do pivô
nulo. A mensagem da 006 é mantida para quem não usa a análise.

### 5.6 Complexidade

Validação e resolução dos alvos são O(elementos + nós + itens), com conjuntos
ordenados. A montagem e a solução são as da 006. O banco da treliça espacial
(NFR-007-02) tem 18³ nós em células cúbicas com diagonais (arestas de Kuhn),
cerca de 16,5 mil GDL livres (meta revista em 2026-11-15; ver requisitos).

## 6. Fluxo

Síncrono, numa thread que chama `runLinearStatic`. A montagem é paralela
(oneTBB, 006). O `Model` é lido como `const` (REQ-007-13), e a 011 vai
rodar a análise sobre uma cópia.

## 7. Tratamento de erros

| Situação | Comportamento | Requisito |
|----------|---------------|-----------|
| Problemas de modelo (lista do §5.3) | `validate` devolve todos; `runLinearStatic` falha com eles, um por linha, e o número no título | REQ-007-14 |
| Treliça 2D fora do plano XY | Problema citando o elemento (id externo) | REQ-007-09 |
| Elemento atribuído que não é `Line2` | Problema citando o elemento e o tipo | REQ-007-10 |
| Nó sem rigidez numa direção (diagonal nula) | Falha listando cada nó (id externo) e direção | REQ-007-15 |
| Mecanismo global (pivô nulo) | Falha citando um nó e a direção: "the structure is a mechanism" | REQ-007-15 |
| Erros do núcleo (montagem) | Repassados com a mensagem da 006 | REQ-006-16 |

Nenhuma exceção em fluxo esperado (architecture.md §3).

## 8. Estratégia de testes

| Teste | Tipo | Requisito | Referência/tolerância |
|-------|------|-----------|-----------------------|
| `test_model`: editar e copiar materiais, seções, atribuições, apoios e ações; resolução por grupo (elementos e nós) e por lista; cópia compartilha a malha; a análise não altera o modelo | unitário | REQ-007-01 a 07, 13 | igualdade exata |
| `test_truss_element`: K igual à fórmula fechada em orientação aleatória; invariância por rotação; energia nula nos modos rígidos (2D: 3; 3D: 5, mais a rotação axial); vetores de ΔT, ε₀ e peso; sem alocação no `compute` | unitário | REQ-007-08, 11 | 1e−12 relativo |
| `test_truss_verification` patch: deslocamento rígido qualquer numa treliça distorcida dá N = 0; deformação uniforme numa treliça estruturada simétrica é reproduzida nos nós internos | verificação | REQ-007-12 | 1e−10 |
| `test_truss_verification` analíticos 2D e 3D: barra axial, V simétrica, treliça plana isostática (método dos nós), tripé, três barras hiperestática, recalque | verificação | REQ-007-12, NFR-007-01 | 1e−10 relativo |
| `test_truss_verification` ações: ΔT e ε₀ com a barra presa e com uma ponta livre; barra pendurada sob peso próprio | verificação | REQ-007-11, NFR-007-01 | 1e−10 relativo |
| `test_truss_verification` convergência: barra pendurada com n = 4, 8 e 16; u nodal exato; taxa do erro L2 de N ≈ 1 | verificação | REQ-007-12, NFR-007-01 | u: 1e−10; taxa: 1 ± 0,1 |
| `test_static_validation`: cada problema do §5.3 isolado e vários juntos; z ≠ 0 na 2D; `Tri3` atribuído; nó sem rigidez (diagonal); mecanismo global (quadrado articulado sem diagonal) | unitário | REQ-007-09, 10, 14, 15 | mensagem cita o item |
| `test_assembler` (ampliado): `singularEquation` aponta a equação correta numa matriz com linha nula | unitário | REQ-007-15 | exato |
| `bench_truss`: treliça espacial 18³ nós (~16,5 mil GDL) | desempenho | NFR-007-02 | < 5 s em Release |
| Job de cobertura | CI | NFR-007-03 | ≥ 80% |

## 9. Alternativas consideradas

| Alternativa | Por que foi descartada |
|-------------|------------------------|
| `Model` como classe com métodos de inclusão e remoção e ids estáveis por item | Mais código sem ganho agora: a 010 embrulha a edição em comandos de desfazer, e os ids estáveis devem vir das entidades de `geom` (009/010), não de cada item |
| Malha por valor no `Model` | Copiar o modelo (instantâneo da 011) copiaria a malha inteira; com `shared_ptr<const Mesh>` a cópia é barata e a malha já é imutável |
| Alvos por índices internos | Os índices não são o que o usuário vê; os ids externos são os números do arquivo e da interface |
| Uma formulação por barra, com E, A e ε* nela | Milhares de objetos e um `ElementSet` por barra; uma tabela indexada por `ElementIndex` é compacta e serve à montagem paralela |
| Detectar mecanismos por autovalores (modos de energia nula) | Custo e complexidade altos; diagonal nula mais pivô nulo atendem ao REQ-007-15 (pelo menos um nó e a direção) |
| `ResultSet` genérico já agora | O formato dos campos de resultado depende da visualização (012); `StaticResult` é simples e será adaptado lá |

## 10. Riscos e mitigação

- **O pivô nulo cai num nó distante da causa real do mecanismo**, por causa
  da ordem AMD. → O REQ pede "ao menos um nó". A diagonal nula, o caso mais
  comum (nó solto numa direção), é listada exatamente. A mensagem diz que é
  um mecanismo e sugere conferir os apoios.
- **Um pivô pequeno mas legítimo, em barras com rigidezes muito diferentes,
  é tomado como mecanismo.** → É o mesmo critério relativo de 1e−14 da 006,
  e o teste com razão de rigidez 1e6 não pode falhar.
- **A tolerância de z da treliça 2D recusa malhas com ruído numérico.** →
  Ela é relativa à diagonal da caixa envolvente (1e−9), como
  `Mesh::liesInXYPlane`.
- **O `Model` agregado expõe estado inconsistente**, como um nome de material
  removido. → É intencional: o `Model` é o documento em edição, e a
  validação antes da análise é que garante a consistência.

## 11. Novas dependências

Nenhuma.
