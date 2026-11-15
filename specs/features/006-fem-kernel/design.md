# Design — 006 fem-kernel

Status: aprovado · Data: 2026-11-15
Requisitos: [requirements.md](requirements.md)

## 1. Resumo da abordagem

Tudo em `core`, sem Qt:

1. **Funções de forma** geradas, não escritas à mão: para cada tipo, um espaço
   polinomial (P1/P2 nos simplexos, Q1/Q2 e serendipity nos quadriláteros e
   hexaedros, P×Q na cunha) e uma matriz de coeficientes obtida uma vez,
   invertendo a matriz de Vandermonde nos nós de referência do Gmsh
   (`core::referenceNodeCoordinates`). Assim a ordem de nós é a do Gmsh por
   construção, e a avaliação é um produto de monômios por uma matriz fixa, sem
   alocação. A pirâmide de 5 nós, que não é polinomial, tem fórmula fechada.
2. **Quadraturas** tabeladas (simplexos) e por produto de Gauss (demais).
3. **Mapeamento isoparamétrico** com jacobiano, determinante ou medida de
   linha/superfície em 3D, e derivadas físicas.
4. **`DofMap`**: máscara de tipos de GDL por nó, vinda das formulações dos
   elementos; numeração determinística; GDL prescritos eliminados.
5. **Montagem** em duas fases: padrão esparso (simbólico) a partir da
   adjacência de nós e numérico em paralelo por **coloração de elementos**
   (oneTBB), escrevendo direto na matriz sem travas. Resultado igual para
   qualquer número de threads.
6. **Solver direto** atrás de uma interface: `SparseCholesky` sobre
   `Eigen::SimplicialLDLT` com reordenação AMD; falha de pivô vira erro.
7. **`solveLinear`**: monta, impõe prescrições, resolve e devolve valores e
   reações (estas numa segunda passada pelos elementos).

O núcleo é verificado com um elemento de Poisson que fica só nos testes.

## 2. Verificação da constituição

| Princípio | Atende? | Observação |
|-----------|---------|------------|
| Multiplataforma (§2) | Sim | C++20, Eigen e oneTBB (vcpkg nas três plataformas); nada específico de sistema |
| GPU somente via QRhi (§3) | Sim | Nada de GPU |
| Camadas / `core` sem Qt (§4) | Sim | Tudo em `core`; oneTBB dependência privada de `core` |
| Correção numérica (§5) | Sim | Patch test e convergência com o elemento de teste; tolerâncias explícitas nos testes; pivôs da fatoração verificados |
| Qualidade (§6) | Sim | Cobertura de `core` medida no CI (gcovr, job Linux), ≥ 80% |
| Simplicidade / dependências (§7) | Sim | Uma dependência nova, prevista na arquitetura: oneTBB ([ADR 0004](../../decisions/0004-onetbb-para-paralelismo.md)); ferramenta de cobertura só no CI |

## 3. Módulos e arquivos afetados

| Módulo | Arquivo | Novo/Alterado | Responsabilidade |
|--------|---------|---------------|------------------|
| core | `include/fem/core/shape_functions.hpp`, `shape_functions.cpp` | novo | Espaços, coeficientes, avaliação de N e ∂N/∂ξ |
| core | `include/fem/core/quadrature.hpp`, `quadrature.cpp` | novo | Regras por família e grau |
| core | `include/fem/core/isoparametric.hpp`, `isoparametric.cpp` | novo | Jacobiano, medida, derivadas físicas |
| core | `include/fem/core/dof_map.hpp`, `dof_map.cpp` | novo | Tipos de GDL, numeração, prescrições |
| core | `include/fem/core/element_formulation.hpp` | novo | Interface das formulações |
| core | `include/fem/core/assembler.hpp`, `assembler.cpp` | novo | Padrão esparso, coloração, montagem paralela |
| core | `include/fem/core/linear_solver.hpp`, `linear_solver.cpp` | novo | Interface e `SparseCholesky` |
| core | `include/fem/core/linear_static.hpp`, `linear_static.cpp` | novo | `solveLinear`: valores e reações |
| core | `CMakeLists.txt`; `vcpkg.json` | alterado | oneTBB (privado) |
| tests | `unit/core/test_shape_functions.cpp`, `test_quadrature.cpp`, `test_isoparametric.cpp`, `test_dof_map.cpp`, `test_assembler.cpp`, `test_poisson.cpp`, `poisson_element.hpp`, `unit_meshes.hpp`, `test_allocations.cpp` | novo | Testes e elemento de teste |
| tests | `perf/bench_kernel.cpp` | novo | NFR-006-01 a 03 |
| ci | `.github/workflows/ci.yml`, `CMakePresets.json` | alterado | Preset e job de cobertura |
| specs | `architecture.md`, ADR 0004 | alterado/novo | oneTBB; o que existe do núcleo |

## 4. Interfaces públicas

```cpp
// fem/core/shape_functions.hpp — REQ-006-01, 02, 05
namespace fem::core {

inline constexpr int kMaxNodes = 27; // Hex27

// Values and derivatives of the shape functions at one reference point.
// Fixed-size storage: no allocation (REQ-006-05).
struct ShapeValues
{
    int count = 0;                                    // nodes of the type
    std::array<double, kMaxNodes> n{};                // N_i
    std::array<std::array<double, 3>, kMaxNodes> dn{}; // dN_i/dxi_k, k < dimension
};

// Supported for the 17 types of REQ-006-01; other types give false.
[[nodiscard]] bool hasShapeFunctions(ElementType type) noexcept;
// `xi` in Gmsh's reference element of the type; unused coordinates ignored.
void evaluateShape(ElementType type, const std::array<double, 3>& xi, ShapeValues& out);

} // namespace fem::core
```

```cpp
// fem/core/quadrature.hpp — REQ-006-06, 07
namespace fem::core {

struct QuadraturePoint
{
    std::array<double, 3> xi;
    double weight; // the weights add up to the measure of the reference element
};

// A rule exact for polynomials up to `degree` (at least); the points live for
// the whole program (static tables), so callers keep a span.
[[nodiscard]] std::span<const QuadraturePoint> quadrature(ElementFamily family, int degree);
[[nodiscard]] int maxQuadratureDegree(ElementFamily family) noexcept; // 9 or 5

} // namespace fem::core
```

```cpp
// fem/core/isoparametric.hpp — REQ-006-03, 04
namespace fem::core {

// Geometry of an element at one reference point.
struct PointGeometry
{
    Eigen::Vector3d position;
    double measure;     // det J (3D), or |J| for lines and surfaces in space
    ShapeValues shape;  // with dn turned into physical derivatives dN/dx_k
};

// Nodes of the element, in local order (copied by the caller into fixed
// storage: no allocation).
struct ElementNodes
{
    ElementType type;
    int count;
    std::array<Eigen::Vector3d, kMaxNodes> x;
};

// Error when det J <= 0 for a solid, or a zero measure for a line or a
// surface (see 5.3).
[[nodiscard]] Expected<PointGeometry> evaluateGeometry(const ElementNodes& nodes,
                                                       const std::array<double, 3>& xi);

} // namespace fem::core
```

```cpp
// fem/core/dof_map.hpp — REQ-006-08 to 11
namespace fem::core {

enum class DofType : std::uint8_t { U, V, W, RX, RY, RZ, T };
using DofMask = std::uint8_t; // bit k = DofType k
[[nodiscard]] constexpr DofMask mask(DofType t) noexcept;

using DofIndex = std::int32_t; // index of a dof of the map, 0 .. size()-1

class DofMap
{
public:
    // Union of the masks asked for each node (from the formulations).
    static DofMap build(const Mesh& mesh, std::span<const ElementSet> sets);

    [[nodiscard]] std::int32_t size() const noexcept;           // all dofs
    [[nodiscard]] std::int32_t freeCount() const noexcept;      // equations
    [[nodiscard]] DofMask maskOf(NodeIndex node) const;
    [[nodiscard]] std::optional<DofIndex> find(NodeIndex node, DofType type) const;
    [[nodiscard]] NodeIndex nodeOf(DofIndex dof) const;
    [[nodiscard]] DofType typeOf(DofIndex dof) const;

    // REQ-006-10, 11. Error if the dof does not exist.
    [[nodiscard]] Expected<void> prescribe(NodeIndex node, DofType type, double value);
    // Equation of a free dof, or -1 for a prescribed one; valid after
    // finalize(), which numbers the free dofs.
    void finalize();
    [[nodiscard]] std::int32_t equation(DofIndex dof) const;
    [[nodiscard]] double prescribedValue(DofIndex dof) const;
    [[nodiscard]] bool isPrescribed(DofIndex dof) const;
};

} // namespace fem::core
```

```cpp
// fem/core/element_formulation.hpp — REQ-006-12
namespace fem::core {

// What an element contributes: its dofs per node and its local matrix and
// vector (local dof order: node by node, and within a node by DofType
// order). Must be thread safe (const) and must not allocate.
class ElementFormulation
{
public:
    virtual ~ElementFormulation() = default;
    [[nodiscard]] virtual DofMask dofsAt(ElementType type, int localNode) const = 0;
    // `matrix` is size x size and `vector` size, both zeroed by the caller,
    // with size = sum of the dofs of the nodes.
    [[nodiscard]] virtual Expected<void> compute(const ElementNodes& nodes, ElementIndex element,
                                                 Eigen::Ref<Eigen::MatrixXd> matrix,
                                                 Eigen::Ref<Eigen::VectorXd> vector) const = 0;
};

// Elements that share one formulation.
struct ElementSet
{
    std::span<const ElementIndex> elements;
    const ElementFormulation* formulation;
};

inline constexpr int kMaxElementDofs = kMaxNodes * 7;

} // namespace fem::core
```

```cpp
// fem/core/assembler.hpp — REQ-006-13, 14
namespace fem::core {

using SparseMatrix = Eigen::SparseMatrix<double, Eigen::ColMajor, std::int32_t>;

struct AssembledSystem
{
    SparseMatrix matrix;   // free x free, lower triangle (symmetric)
    Eigen::VectorXd rhs;   // free; element vectors, nodal loads, minus K_fp * u_p
};

struct NodalLoad { NodeIndex node; DofType type; double value; };

// `threads` = 0: all cores; 1: sequential (benchmarks and tests).
[[nodiscard]] Expected<AssembledSystem> assemble(const Mesh& mesh, const DofMap& dofs,
                                                 std::span<const ElementSet> sets,
                                                 std::span<const NodalLoad> loads,
                                                 int threads = 0);

} // namespace fem::core
```

```cpp
// fem/core/linear_solver.hpp — REQ-006-15, 16
namespace fem::core {

class LinearSolver
{
public:
    virtual ~LinearSolver() = default;
    [[nodiscard]] virtual Expected<void> factorize(const SparseMatrix& lowerTriangle) = 0;
    [[nodiscard]] virtual Expected<Eigen::VectorXd> solve(const Eigen::VectorXd& rhs) const = 0;
};

// Eigen::SimplicialLDLT with AMD ordering; refuses zero, negative or
// negligible pivots (see 5.6).
class SparseCholesky final : public LinearSolver { /* ... */ };

} // namespace fem::core
```

```cpp
// fem/core/linear_static.hpp — REQ-006-17
namespace fem::core {

struct LinearSolution
{
    Eigen::VectorXd values;    // per DofIndex: free and prescribed
    Eigen::VectorXd reactions; // per DofIndex: nonzero only at prescribed dofs
};

[[nodiscard]] Expected<LinearSolution> solveLinear(const Mesh& mesh, const DofMap& dofs,
                                                   std::span<const ElementSet> sets,
                                                   std::span<const NodalLoad> loads,
                                                   LinearSolver& solver, int threads = 0);

} // namespace fem::core
```

## 5. Dados e algoritmos

### 5.1 Funções de forma (REQ-006-01, 02, 05)

Elementos de referência do Gmsh (os de `referenceNodeCoordinates`): linha
[−1, 1]; triângulo e tetraedro unitários; quadrilátero [−1, 1]²; hexaedro
[−1, 1]³; cunha = triângulo × [−1, 1]; pirâmide com base [−1, 1]² em z = 0 e
ápice (0, 0, 1).

| Tipo | Espaço (monômios em ξ, η, ζ) |
|------|------------------------------|
| Line2, Line3 | 1, ξ (, ξ²) |
| Tri3, Tri6 | P1, P2 |
| Quad4, Quad9 | Q1, Q2 (produto tensorial) |
| Quad8 | serendipity: 1, ξ, η, ξ², ξη, η², ξ²η, ξη² |
| Tet4, Tet10 | P1, P2 |
| Hex8, Hex27 | Q1, Q2 |
| Hex20 | serendipity: os 20 monômios de grau total ≤ 3 com grau ≤ 2 em cada variável, menos ξ²η², η²ζ², ζ²ξ², ξ²ηζ… (lista fechada no código, 20 termos) |
| Wedge6 | P1(ξ, η) × {1, ζ} |
| Wedge15 | P2(ξ, η) × {1, ζ} ∪ P1(ξ, η) × {ζ²} |
| Wedge18 | P2(ξ, η) × {1, ζ, ζ²} |
| Pyr5 | fórmula racional: base `N = (1−ζ ± ξ)(1−ζ ± η)/(4(1−ζ))`, ápice `N₅ = ζ`, com o limite em ζ → 1 tratado à parte |

Para cada tipo polinomial, na primeira chamada (inicialização estática
segura), monta-se a Vandermonde `V(i, j) = monômio_j(nó_i)` e guarda-se
`C = V⁻¹` (`Eigen::FullPivLU`, com verificação de posto). Avaliar é calcular os
monômios e suas derivadas em ξ (arrays fixos) e multiplicar por `C`. Tudo em
memória fixa (`std::array`), sem alocação.

### 5.2 Quadratura (REQ-006-06, 07)

| Família | Regras | Grau máximo |
|---------|--------|-------------|
| Linha | Gauss–Legendre, 1 a 5 pontos (grau 2n − 1) | 9 |
| Quadrilátero, hexaedro | produto de Gauss com n por direção | 9 |
| Triângulo | simétricas de Dunavant com pesos positivos: grau 1 (1 ponto), 2 (3), 4 (6, também para grau 3), 5 (7) | 5 |
| Tetraedro | simétricas com pesos positivos: grau 1 (1 ponto), 2 (4), 5 (14 pontos, Walkington, também para graus 3 e 4) | 5 |
| Cunha | triângulo de grau p × Gauss de ⌈(p+1)/2⌉ pontos | 5 |
| Pirâmide | Gauss colapsada (Duffy): produto em [−1, 1]² × [0, 1] com o fator (1 − ζ)² incluído, n = ⌈(p + 3)/2⌉ | 5 |

As constantes são tabeladas no código com 16 algarismos e conferidas pelos
testes de exatidão (AC-3).

### 5.3 Mapeamento (REQ-006-03, 04)

`J(k, d) = Σ_i x_i(k) · ∂N_i/∂ξ_d` (3 × d, d = dimensão do tipo).

- d = 3: `measure = det J`; derivadas físicas `∂N/∂x = J⁻ᵀ ∂N/∂ξ`.
- d = 1 ou 2 (linhas e superfícies, também imersas em 3D): `G = JᵀJ`,
  `measure = √det G`, derivadas tangenciais `∂N/∂x = J G⁻¹ ∂N/∂ξ` (gradiente
  na variedade, o que o elemento de teste usa).
- Erro (REQ-006-04): sólido com `det J ≤ 0`, ou linha ou superfície com
  medida nula. A orientação de superfícies e linhas não é exigida (o sinal não
  existe para elas). Mensagem: "Element N has a non-positive Jacobian
  determinant.", com o ID externo, pelo chamador.

### 5.4 GDL (REQ-006-08 a 11)

- `DofMap::build` percorre os conjuntos e faz `máscara[nó] |= dofsAt(tipo, k)`
  para cada nó local; depois numera os GDL nó a nó (ordem do índice interno) e,
  dentro do nó, na ordem de `DofType`: determinístico.
- Armazenamento: `offset[nó]` (CSR, tamanho nós + 1) e, por GDL, nó e tipo.
- `prescribe` marca o GDL e guarda o valor; `finalize` numera as equações só
  dos livres, em ordem crescente de `DofIndex`.

### 5.5 Montagem (REQ-006-13, 14)

1. **Padrão simbólico:** adjacência de nós pelos elementos (pares, ordenados e
   únicos por nó) → para cada coluna livre, as linhas livres ≥ ela entre os GDL
   dos nós vizinhos; `SparseMatrix` com `reserve` exato, valores zero.
2. **Coloração de elementos:** gulosa sobre o grafo "dois elementos com nó
   em comum", em ordem de índice: elementos de uma cor não compartilham GDL.
   Para 1 milhão de Tet4 são tipicamente 20 a 30 cores.
3. **Numérico:** para cada cor, `tbb::parallel_for` sobre os elementos; cada
   thread tem buffers locais (`thread_local` ou `tbb::enumerable_thread_specific`)
   de `kMaxElementDofs²`, criados uma vez. A matriz local vai direto para o
   padrão (busca binária na coluna) e o vetor local para `rhs`; colunas
   prescritas viram `rhs -= K_fp ū_p`. Como cada entrada recebe as
   contribuições na mesma ordem (cor por cor, elemento por elemento), o
   resultado não depende do número de threads (REQ-006-13 com folga).
4. Cargas nodais somadas em `rhs` depois (sequencial).

O primeiro erro de elemento (por exemplo, jacobiano) interrompe a montagem e
volta com o ID externo do elemento.

### 5.6 Solver (REQ-006-15, 16)

`SparseCholesky` usa `Eigen::SimplicialLDLT<SparseMatrix, Eigen::Lower,
Eigen::AMDOrdering<int>>`. Além de `info() != Success`, verifica os pivôs
`D`: algum ≤ 0, ou `min|D| / max|D| < 1e−14`, vira o erro "The system matrix
is singular or not positive definite (a support may be missing)."

### 5.7 Solução e reações (REQ-006-17)

`solveLinear`: `assemble` → `factorize` → `solve` → `values` (livres da
solução, prescritos dos valores dados) → reações por uma segunda passada
paralela pelos elementos, acumulando `K_e u_e − f_e` só nas linhas de GDL
prescritos, menos as cargas nodais nesses GDL. Custo semelhante ao da
montagem numérica, sem guardar `K_pf`.

### 5.8 Elemento de teste e malhas (só em `tests/`)

- `PoissonElement`: GDL T em todos os nós; `K = ∫ k ∇N·∇N dΩ`,
  `f = ∫ N s(x) dΩ`, com `s` uma função dada; quadratura de grau 2p (rigidez)
  e 2p + 2 (carga). Serve a linhas, superfícies e sólidos (gradiente
  tangencial nos de dimensão menor).
- `unit_meshes.hpp`: malhas estruturadas de [0, 1]^d em segmentos, triângulos,
  quadriláteros, tetraedros (6 por cubo), hexaedros e cunhas, lineares e
  quadráticas (nós de ordem 2 posicionados pelo mapeamento do elemento linear
  nos pontos de `referenceNodeCoordinates`, unidos por coordenada).
- Patch test: u = 1 + 2x − y + 3z (s = 0) em malhas distorcidas; erro nos nós
  ≤ 1e−10.
- Convergência: u = sin(πx)·sin(πy)(·sin(πz)), s = dπ²·u; contorno prescrito
  exato; erro L2 por quadratura de grau alto em 3 refinamentos; taxa medida
  entre os dois últimos.

### 5.9 Cobertura (NFR-006-04)

Preset `coverage` (GCC, `--coverage`, Debug) e job no CI (Linux) que roda os
testes e o `gcovr --filter src/core --fail-under-line 80`. O `gcovr` é
instalado com `pip` no job (ferramenta de CI, não dependência do produto).

## 6. Fluxo

```
mesh + conjuntos (elementos, formulação) + cargas + prescrições
  └─ DofMap::build → prescribe(...) → finalize
       └─ solveLinear
            ├─ assemble: padrão → cores → parallel_for (buffers por thread)
            ├─ SparseCholesky::factorize (AMD + LDLᵀ) → verificação de pivôs
            ├─ solve → values
            └─ reações (segunda passada) → LinearSolution
```

## 7. Tratamento de erros

| Situação | Comportamento | Requisito |
|----------|---------------|-----------|
| Tipo sem funções de forma (ex.: ordem 3) | `hasShapeFunctions` falso; a formulação recusa o elemento com erro | REQ-006-01 |
| Jacobiano nulo ou negativo | Erro com o ID externo do elemento; montagem interrompida | REQ-006-04 |
| GDL prescrito inexistente | Erro "Node N has no dof T." | REQ-006-11 |
| Mecanismo / matriz singular | Erro da fatoração; nenhuma solução | REQ-006-16 |
| Nenhum GDL livre | `values` só com os prescritos; reações calculadas; sem fatoração | REQ-006-17 |
| Grau de quadratura acima do máximo | Pré-condição (assert): programação | REQ-006-06 |

## 8. Estratégia de testes

| Teste | Tipo | Requisito | Referência/tolerância |
|-------|------|-----------|-----------------------|
| Delta de Kronecker nos nós, partição da unidade e soma nula das derivadas em 50 pontos aleatórios, todos os 17 tipos | unitário | REQ-006-01, 02 | 1e−12 |
| Derivadas por diferenças finitas centradas | unitário | REQ-006-01 | 1e−7 |
| Monômios integrados exatamente até o grau declarado, cada família e grau; pesos positivos nos simplexos; soma dos pesos = medida | unitário | REQ-006-06, 07 | 1e−12 relativo |
| Mapeamento afim (jacobiano constante, volume = soma), distorcido, invertido (erro), linha e triângulo em 3D (comprimento, área) | unitário | REQ-006-03, 04 | 1e−12 |
| Avaliação de forma e geometria sem alocação (contador global de `new` no executável de teste) | unitário | REQ-006-05 | 0 alocações |
| `DofMap`: máscaras misturadas (U,V numa barra; T noutra; nó comum), numeração determinística, prescrições válidas e inválidas | unitário | REQ-006-08 a 11 | exato |
| Montagem: matriz de 2 elementos conhecida à mão; paralela (todas as threads) = sequencial em 1e−12 numa malha de 20 mil Tet4; cargas nodais; rhs com prescrição não nula | unitário | REQ-006-12 a 14 | 1e−12 |
| Solver: sistema SPD pequeno conhecido; mecanismo (sem apoio) recusado | unitário | REQ-006-15, 16 | 1e−12 |
| Poisson: patch test 1D, 2D, 3D, linear e quadrático; convergência linear (≈ 2) e quadrática (≈ 3) em Tri, Quad, Tet, Hex, Wedge; reações equilibram a fonte | verificação | REQ-006-12 a 17 | taxa ± 0,2; equilíbrio 1e−10 |
| `bench_kernel`: montagem de 1 milhão de Tet4 com 1 thread e todas; solução de ~100 mil GDL | manual, Release | NFR-006-01 a 03 | metas |
| Cobertura de `core` | CI (Linux) | NFR-006-04 | ≥ 80% |

## 9. Alternativas consideradas

| Alternativa | Por que foi descartada |
|-------------|------------------------|
| Funções de forma escritas à mão por tipo | 17 tipos × derivadas: muito código propenso a erro de ordem de nós; a Vandermonde nos nós do Gmsh acerta a ordem por construção e os testes a confirmam |
| Montagem por triplets por thread e soma final | 1 milhão de Tet4 com 1 GDL dá 16 milhões de triplets (256 MB); a coloração escreve direto no padrão |
| Montagem com operações atômicas | Não determinística na ordem das somas; mais lenta com muitos conflitos |
| Penalidade para os apoios | Recusada pelo mantenedor; a eliminação é exata |
| `std::thread` em vez de oneTBB | Recusado pelo mantenedor; o oneTBB equilibra a carga e será usado na malha |
| Regras de quadratura colapsadas (Duffy) nos simplexos | Não simétricas; o requisito pede regras simétricas com pesos positivos (ficam só na pirâmide) |
| Guardar `K_pf` para as reações | Memória extra; a segunda passada custa o mesmo que a montagem numérica |

## 10. Riscos e mitigação

- **Constantes das regras de quadratura copiadas com erro** → testes de
  exatidão por monômio em cada regra (AC-3) pegam qualquer dígito errado.
- **Vandermonde mal condicionada** (Hex27) → coordenadas de referência
  pequenas (≤ 1) e `FullPivLU` com verificação de posto; o delta de Kronecker
  confere a 1e−12.
- **Cores demais** (malhas muito irregulares) reduzem o paralelismo → medir no
  benchmark; se necessário, ordenar os elementos por localidade antes de
  colorir.
- **oneTBB no vcpkg do macOS e Linux** → coberto pelo CI; triplets dinâmicos
  já usados pelo OCCT.
- **Pivô "pequeno" mal calibrado** (1e−14 relativo) → testes com mecanismo e
  com problema bem posto mas mal escalado (coeficientes de 1e−6 a 1e6).

## 11. Novas dependências

- **oneTBB** (vcpkg `tbb`), Apache-2.0, ligação dinâmica, dependência privada
  de `core` — [ADR 0004](../../decisions/0004-onetbb-para-paralelismo.md).
- **gcovr** (BSD-3), só no job de cobertura do CI, instalado com `pip`; não
  vai para o produto.
