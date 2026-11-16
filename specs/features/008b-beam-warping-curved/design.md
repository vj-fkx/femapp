# Design — 008b beam-warping-curved

Status: aprovado · Data: 2026-11-16
Requisitos: [requirements.md](requirements.md)

## 1. Resumo da abordagem

Tudo em `core`, sem Qt, sobre o `Model`, a análise e a viga da 008:

1. **Seções** (`section.hpp`, `section_thin_walled.cpp`, `section_solid.cpp`,
   `section_mesh.cpp`): duas formas novas no `SectionShape`:
   - `ThinWalled`: linha média com espessuras, pela teoria de paredes finas
     (coordenada setorial, fluxos de Bredt de várias células);
   - `SolidPolygon`: polígonos com furos, triangulados em `core` (ADR 0005)
     com triângulos de 6 nós, onde se resolvem a função de empenamento de
     Saint-Venant e as duas funções de cisalhamento (Pilkey, 2002).

   A `SectionProperties` ganha Iw, e as oito formas ganham Iw e ω por
   fórmulas. Como a seção poligonal custa uma solução numérica, a análise
   resolve cada seção uma vez e guarda o resultado (`SectionSolution`), que
   também serve às tensões.
2. **Um motor exato de viga por espaço de estados** (`beam_state.hpp`): para
   um trecho reto ou em arco de circunferência com propriedades constantes,
   as equações lineares da viga (equilíbrio, cinemática e lei constitutiva,
   com Timoshenko, centro de cisalhamento, empenamento de Vlasov, curvatura e
   base elástica) são um sistema s' = A·s + b(x) com A constante no
   referencial local que acompanha a barra. A matriz de transferência
   e^{A·L}, calculada pelo módulo `MatrixFunctions` do Eigen (ADR 0006), dá a
   rigidez exata, as cargas equivalentes exatas (polinômios e cargas globais
   pela matriz aumentada de Van Loan) e o estado em qualquer ponto. Trechos
   longos em relação ao comprimento característico são subdivididos e
   condensados, para não perder precisão.
3. **Três formulações de viga**:
   - a **viga reta da 008** (`BeamElement`), sem mudança, para vigas `Line2`
     sem empenamento;
   - a **viga exata** (`ExactBeamElement`), pelo motor do item 2, para a viga
     reta com empenamento e para a viga curva em arco (`Line3`, formulação
     arco);
   - a **viga isoparamétrica** de 3 nós (`IsoBeamElement`), com integração
     reduzida seletiva, para `Line3` na formulação isoparamétrica.
4. **Empenamento**: novo `DofType::Warp`. A condição de cada ponta (livre,
   impedido, contínuo) decide se ela usa o GDL do nó ou um GDL interno
   condensado (livre) ou eliminado (impedido). Os elementos são agrupados em
   conjuntos por combinação de pontas contínuas, e assim a interface
   `ElementFormulation` não muda.
5. **Modelo, validação e resultados**: atribuição com `warping` e
   `curvedFormulation`; item `WarpingEnd`; bimomento nodal e concentrado;
   torque distribuído; `BeamForces` com B, Tsv e Tw; `BeamResult::at` para
   os três tipos de viga, pelo mesmo `at(x)` (x ao longo do arco).

**Ajustes nos requisitos** (aprovados junto com este design e já aplicados em `requirements.md`):

- **REQ-008b-11 e AC-4:** como a viga com empenamento passa pelo motor
  exato, a base elástica nela é **exata**, e não só convergente. O texto passa
  a "exatos, como no REQ-008b-08", e o AC-4 troca "convergindo na taxa
  documentada" por "contra a solução analítica" (viga de Hetényi com torção).
- **AC-2:** as áreas de cisalhamento da seção poligonal saem da solução de
  elasticidade com ν = 0 (§5.1.3). A referência passa de "5/6 e ~0,9" para
  "5/6 no retângulo e 6/7 no círculo (fórmulas de Cowper com ν = 0)".

## 2. Verificação da constituição

| Princípio | Atende? | Observação |
|-----------|---------|------------|
| Multiplataforma (§2) | Sim | C++20 e Eigen; nada específico de sistema |
| GPU somente via QRhi (§3) | Sim | Nada de GPU |
| Camadas / `core` sem Qt (§4) | Sim | Tudo em `core`, inclusive a triangulação da seção (REQ-008b-24, ADR 0005) |
| Correção numérica (§5) | Sim | Patch test (reta e isoparamétrica); convergência (isoparamétrica, seção poligonal); soluções analíticas (empenamento em senh/cosh, anéis e arcos de Roark, Hetényi); equivalência entre as formulações; tolerâncias explícitas |
| Qualidade (§6) | Sim | Cobertura de `core` ≥ 80%; `compute` sem alocação |
| Simplicidade / dependências (§7) | Sim | Nenhuma dependência nova. O módulo `MatrixFunctions` já vem no pacote do Eigen (MPL-2.0), usado atrás de uma função (ADR 0006). Uma só máquina exata para empenamento e arco, em vez de fórmulas fechadas por caso |

## 3. Módulos e arquivos afetados

| Módulo | Arquivo | Novo/Alterado | Responsabilidade |
|--------|---------|---------------|------------------|
| core | `include/fem/core/dof_map.hpp`, `dof_map.cpp` | alterado | `DofType::Warp` (8 tipos, máscara de 8 bits) e nome "warp" |
| core | `include/fem/core/section.hpp`, `section.cpp` | alterado | `ThinWalled`, `SolidPolygon`, Iw, ω, `SectionSolution` (REQ-008b-01 a 06) |
| core | `section_thin_walled.cpp` | novo | Paredes finas: grafo, células, setorial, Bredt, fluxo de cisalhamento (REQ-008b-03) |
| core | `detail/section_mesh.hpp`, `section_mesh.cpp` | novo | Triangulação da seção: orelhas, Delaunay restrito, refinamento de Rivara, T6 (REQ-008b-02, ADR 0005) |
| core | `section_solid.cpp` | novo | Empenamento e cisalhamento na malha da seção; tensões (REQ-008b-02, 06) |
| core | `include/fem/core/beam_state.hpp`, `beam_state.cpp` | novo | Motor de espaço de estados: A, transferência, rigidez, cargas, estado em x (§5.3) |
| core | `include/fem/core/beam_element.hpp`, `beam_element.cpp` | alterado | `BeamData` com empenamento, curvatura e formulação; `ExactBeamElement`, `IsoBeamElement`; eixos ao longo da curva |
| core | `include/fem/core/beam_loads.hpp`, `beam_loads.cpp` | alterado | Torque distribuído, bimomento concentrado, posições por comprimento de arco |
| core | `include/fem/core/beam_result.hpp`, `beam_result.cpp` | alterado | B, Tsv, Tw, θ'; `at` nos três tipos; tensões de empenamento |
| core | `include/fem/core/model.hpp` | alterado | Campos e itens novos (§4) |
| core | `model_validation.cpp`, `detail/model_validation.hpp` | alterado | Verificações do REQ-008b-21 e condições de empenamento por nó |
| core | `include/fem/core/static_analysis.hpp`, `static_analysis.cpp` | alterado | Conjuntos por formulação e pontas contínuas; empenamento e bimomentos no resultado |
| core | `CMakeLists.txt` | alterado | Novos fontes |
| tests | `unit/core/test_section_thin_walled.cpp`, `test_section_solid.cpp` | novo | AC-1, AC-2, AC-9 |
| tests | `unit/core/test_beam_state.cpp` | novo | Motor exato contra a viga da 008 e contra senh/cosh |
| tests | `unit/core/test_beam_warping.cpp` | novo | AC-3, AC-4 |
| tests | `unit/core/test_beam_curved.cpp` | novo | AC-5 a AC-8 |
| tests | `unit/core/test_beam_validation.cpp`, `test_allocations.cpp` | alterado | AC-10; `compute` das novas formulações sem alocação |
| tests | `perf/bench_frame.cpp`, `perf/bench_section.cpp` | alterado/novo | NFR-008b-02, 03 |
| specs | `architecture.md`, `decisions/0005-…`, `decisions/0006-…` | alterado/novo | Seções, motor exato, empenamento; ADRs |

## 4. Interfaces públicas

```cpp
// dof_map.hpp — altered (REQ-008b-07, 10).
enum class DofType : std::uint8_t { U, V, W, RX, RY, RZ, T, Warp };
inline constexpr int kDofTypeCount = 8;   // DofMask stays std::uint8_t
```

```cpp
// section.hpp — altered (REQ-008b-01 to 06). Coordinates of the drawing (y, z).
struct ThinWalled {                        // REQ-008b-03: mid-line
    std::vector<Eigen::Vector2d> points;
    struct Wall { int from = 0, to = 0; double t = 0; };
    std::vector<Wall> walls;               // straight segments between points
};
struct SolidPolygon {                      // REQ-008b-02
    struct Region {
        std::vector<Eigen::Vector2d> outline;              // any orientation
        std::vector<std::vector<Eigen::Vector2d>> holes;
    };
    std::vector<Region> regions;
    double meshSize = 0;                   // largest edge; 0: default (§5.1.3)
};
using SectionShape = std::variant<Rectangle, Circle, CircularTube, RectangularTube,
                                  ISection, Angle, Channel, Tee, ThinWalled, SolidPolygon>;

struct Section {
    ... // 008 fields
    std::optional<double> warpingConstant = std::nullopt;  // Iw, generic section
};

struct SectionProperties {
    ... // 008 fields
    double warpingConstant = 0;            // Iw about the shear centre
};

struct StressPoint {                       // 008, extended
    ... // name, position, firstMoment1/2, thickness, torsionFactor
    double sectorial = 0;                  // omega about the shear centre (REQ-008b-20)
    double warpingShearFactor = 0;         // tau_w = Tw * factor
};

// The section solved once: properties, stress points and, for a solid
// polygon, the mesh and the warping and shear functions.
class SectionSolution {
public:
    [[nodiscard]] const SectionProperties& properties() const;
    [[nodiscard]] std::span<const StressPoint> points() const;
    // Solid polygon only: stresses at any (y, z) of the drawing, and the
    // point of largest von Mises over the mesh (REQ-008b-06).
    [[nodiscard]] PointStress stressAt(const BeamForces&, const Eigen::Vector2d&) const;
    [[nodiscard]] PointStress largestVonMises(const BeamForces&) const;
};
[[nodiscard]] Expected<std::shared_ptr<const SectionSolution>> solveSection(const Section&);
// 008: still there; it calls solveSection and keeps the properties.
[[nodiscard]] Expected<SectionProperties> sectionProperties(const Section&);
```

```cpp
// model.hpp — altered.
enum class CurvedFormulation : std::uint8_t { Arc, Isoparametric };   // REQ-008b-12
enum class WarpingCondition : std::uint8_t { Free, Restrained, Continuous }; // REQ-008b-09

struct Assignment {
    ... // 008 fields
    bool warping = false;                                // REQ-008b-07, 3D beams
    CurvedFormulation curved = CurvedFormulation::Arc;   // Line3 beams
};

struct WarpingEnd {                       // REQ-008b-09: overrides the default
    ElementTarget elements;
    BarEnd end = BarEnd::Both;
    WarpingCondition condition = WarpingCondition::Restrained;
};

// Restraint::dof may be DofType::Warp (REQ-008b-10).
struct NodalForce { ...; double bimoment = 0.0; };                      // REQ-008b-10
struct DistributedLoad { ...; double startTorque = 0.0, endTorque = 0.0; }; // about local x
struct ConcentratedLoad { ...; double bimoment = 0.0; };                // REQ-008b-17

struct Model { ...; std::vector<WarpingEnd> warpingEnds; };
```

```cpp
// beam_result.hpp — altered (REQ-008b-18 to 20).
struct BeamForces { ...; double b = 0, tsv = 0, tw = 0; };  // t = tsv + tw
struct BeamPoint  { ...; double warping = 0; };            // theta'
struct PointStress { ...; double warpingNormal = 0; };      // B omega / Iw, included in normal
// BeamResult keeps its interface; x runs along the arc length.
```

```cpp
// static_analysis.hpp — altered (REQ-008b-18).
struct StaticResult {
    ... // 008 fields
    std::vector<double> warping;            // per node: theta' (0 without dof)
    std::vector<double> reactionBimoments;  // per node: 0 where free
};
```

```cpp
// beam_state.hpp — REQ-008b-08, 11, 13, 19 (design §5.3).
// A bar of constant section and curvature in its moving local frame.
struct StateBar {
    double length = 0;
    Eigen::Vector3d curvature = Eigen::Vector3d::Zero(); // turn rate of the frame, local
    // Section stiffnesses about the centroid and the shear centre, the
    // foundation and the warping (EIw = 0: no warping state).
    ...
};
// Local stiffness and fixed-end loads of the bar (7 or 6 dofs per end),
// exact, with the internal subdivision of §5.3.
struct StateBarMatrices { BeamMatrix14 stiffness; BeamVector14 loads; };
[[nodiscard]] StateBarMatrices stateBarMatrices(const StateBar&, const SpanLoads&);
// The state (displacements, rotations, warping, forces, bimoment) at s.
[[nodiscard]] StateVector stateAt(const StateBar&, const SpanLoads&, const StateVector& start,
                                  double s, Side side);
```

```cpp
// beam_element.hpp — altered.
struct BeamData {
    ... // 008 fields
    double warpingStiffness = 0.0;                 // E Iw; 0: no warping
    std::array<WarpingCondition, 2> warpingEnds{}; // resolved (§5.4)
    Eigen::Vector3d curvature = Eigen::Vector3d::Zero(); // arc, local principal (§5.2)
    double arcLength = 0.0;                        // flexible length along the arc
};

class ExactBeamElement final : public ElementFormulation {      // Line2 + warping, Line3 arc
public:
    // `continuous`: whether each end uses the node's warping dof.
    ExactBeamElement(ElementKind, std::array<bool, 2> continuous, std::span<const BeamData>);
    ...
};
class IsoBeamElement final : public ElementFormulation { ... }; // Line3 isoparametric
```

## 5. Dados e algoritmos

### 5.1 Seções

#### 5.1.1 Iw e ω das oito formas (REQ-008b-01)

Fórmulas de paredes finas, nas linhas médias (h₀ = h − tf, b' = bf − tw/2 no
U), com a referência no código (Pilkey, 2002, tab. 6; Galambos, 1968):

| Forma | Iw | ω nos pontos |
|-------|----|--------------|
| Perfil I | tf·bf³·h₀²/24 | ±bf·h₀/4 nas pontas das mesas, 0 na alma |
| Perfil U | tf·b'³·h₀²/12 · (3b'·tf + 2h₀·tw)/(6b'·tf + h₀·tw) | fórmula setorial do U, nas pontas e nos cantos |
| Tubo retangular | t·b₀²·h₀²·(b₀ − h₀)²/(24·(b₀ + h₀)) | setorial da célula, nos cantos |
| Retângulo | b³·h³/144 (placa de Vlasov; pequeno, documentado) | 0 |
| Círculo, tubo circular, cantoneira, T | 0 | 0 |

Com Iw = 0, a atribuição com empenamento é recusada (REQ-008b-21), e a
mensagem diz que a forma não empena.

#### 5.1.2 Paredes finas (REQ-008b-03)

A linha média é um grafo: pontos e paredes. A validação recusa paredes
nulas, espessuras ≤ 0, pontos repetidos e grafo desconexo (REQ-008b-04).

1. **A, centroide, Iy, Iz, Iyz:** soma das paredes como retângulos finos
   (L·t, inércia de segmento t·L³/12 projetada); exatos para a idealização
   de linha média (cantos ignorados).
2. **Células:** os ciclos independentes do grafo (árvore geradora; cada aresta
   fora dela fecha uma célula). Com n células, os fluxos circulatórios
   saem de um sistema n×n (Bredt generalizado, compatibilidade de giro), e
   J = Σ b·t³/3 + Σ 2·Aᵢ·qᵢ por unidade de giro.
3. **Coordenada setorial ω:** integrada ao longo da árvore geradora a partir
   de um polo, com a correção das células pelos fluxos de torção (setorial
   de seção fechada, Vlasov). Com ω no centroide, o centro de cisalhamento
   sai de Iyω e Izω, e ω é recalculado em relação a ele e normalizado
   (∫ω dA = 0). Iw = ∫ω² dA.
4. **Fluxo de cisalhamento por cortante:** aberto pelo momento estático ao
   longo da árvore; células fechadas pela compatibilidade. Asy e Asz pela
   energia: As = V² / ∫(q²/t) ds.
5. **Pontos de tensão** (REQ-008b-06): as pontas e os nós da linha média,
   em cada face da parede (±t/2 na normal), com Q, t, ω e o fator de
   cisalhamento da torção (t nos ramos abertos, q/t nas células).

Custo O(paredes + células³), desprezível.

#### 5.1.3 Polígono maciço (REQ-008b-02)

1. **Geometria exata:** A, centroide, Iy, Iz e Iyz pelas fórmulas de
   polígono (teorema de Green), contornos somados e furos subtraídos.
2. **Validação:** menos de três vértices, área nula, autointerseção,
   furo fora do contorno, sobreposição entre regiões ou furos
   (REQ-008b-04), em O(n²) por segmentos (seções têm poucas centenas de
   vértices).
3. **Malha (ADR 0005):** triangulação por orelhas com os furos ligados ao
   contorno por pontes; trocas de aresta de Lawson até o Delaunay restrito;
   refinamento por bissecção da aresta mais longa (Rivara) até a maior
   aresta ≤ `meshSize`, que preserva os ângulos; nós de meio de aresta (T6).
   O `meshSize` padrão é √(A/1500), ou seja, cerca de 3 mil triângulos.
4. **Problemas na malha T6** (Pilkey, 2002, cap. 6; 6 pontos de Gauss por
   triângulo; Cholesky esparso do Eigen):
   - **empenamento:** ∇²ω = 0 com ∂ω/∂n = z·n_y − y·n_z no contorno
     (Neumann; um nó fixado e ω normalizado). J = Iy + Iz − ∫(z·∂ω/∂y −
     y·∂ω/∂z) dA;
   - **centro de cisalhamento e Iw:** de Iyω, Izω (Trefftz), ω levado ao
     centro de cisalhamento e normalizado, Iw = ∫ω² dA;
   - **cisalhamento:** as funções Ψ e Φ de Pilkey para Vy e Vz, com ν = 0.
     As áreas saem da energia de cisalhamento: As = V² / ∫(τ_xy² + τ_xz²) dA,
     com τ a tensão da cortante V aplicada só. Com ν = 0 elas dão 5/6 no retângulo e 6/7 no círculo
     (Cowper), daí o ajuste do AC-2. Com ν ≠ 0 o efeito é de poucos
     porcento e fica documentado.
5. **Tensões:** em qualquer ponto, por interpolação das derivadas dos campos
   no triângulo que o contém. O máximo de von Mises é procurado nos pontos
   de Gauss e nos nós de todos os triângulos.
6. **Convergência:** em T6, J converge com h⁴, a taxa que o AC-2 verifica
   (4 ± 0,5).

A `SectionSolution` guarda a malha e os campos, compartilhada
(`shared_ptr<const>`) entre a análise e os `BeamResult`.

### 5.2 Geometria e eixos das vigas curvas (REQ-008b-12 a 16)

- **Arco:** o círculo pelos três nós (o do meio entre as pontas), com
  centro, raio R, plano e abertura α. Comprimento de arco R·α. Nós
  colineares (R > 1e8·corda) dão a viga reta. α ≥ 2π − 1e−6, ou nó do meio
  coincidente com uma ponta (distância < 1e−9·corda), é recusado.
- **Eixos locais do arco:** em s = 0, x é a tangente, e y, z vêm da regra
  da 008 (§5.2 da 008) com o vetor de referência, o ângulo β e o ângulo
  principal. O referencial gira ao longo do arco em torno da binormal b do
  plano, com taxa 1/R; em eixos locais principais, isso é o vetor de
  curvatura constante Ω = (Λᵀ·b)/R, que tem componentes em y' e z' (a
  curvatura "em qualquer direção" em relação à seção) e nenhuma em x. O
  vetor de referência dado é usado só em s = 0 e transportado com o
  referencial; o padrão (Z global) é testado nos dois extremos e no meio, e
  a recusa do REQ-008b-16 vale se ele ficar paralelo à tangente em algum
  ponto (verificado no ponto do arco mais próximo da direção dele).
- **Isoparamétrica:** x(ξ) quadrática de Lagrange pelos três nós, ξ ∈
  [−1, 1]; jacobiano J(ξ) = |x'(ξ)|, recusado se ≤ 1e−9·corda em algum dos
  pontos de integração e nas pontas. Os eixos locais em cada ponto vêm da
  regra da 008 com a tangente local; o comprimento de arco é integrado por
  Gauss de 5 pontos.
- **Excentricidades:** as pontas do trecho flexível são os nós deslocados;
  o arco passa por elas e pelo nó do meio.

### 5.3 Motor exato de espaço de estados (REQ-008b-08, 11, 13, 19)

**Estado**, em eixos locais principais no referencial móvel, para 3D com
empenamento (14 componentes; sem empenamento, 12; em 2D, 6):

```
s = ( u, v, w, θx, θy, θz, ψ  |  N, Vy, Vz, T, My, Mz, B )
```

com ψ = taxa de giro de torção (o GDL de empenamento). Na derivada ao longo
do arco, um vetor em eixos locais vale d/ds + Ω×.

**Equações** (viga curva de Timoshenko com Vlasov, linear, referida ao
centroide, com o centro de cisalhamento por translação dos esforços, como
no §5.4 da 008; Vlasov, 1961; Yang e Kuo, 1987):

- equilíbrio: F' + Ω×F + q = 0; M' + Ω×M + e₁×F + m = 0;
- cinemática: u' + Ω×u − e₁×θ = (N/EA, Vy/GAsy, Vz/GAsz) + ε*;
  θ' + Ω×θ = (κx, My/EIy, Mz/EIz) + κ*;
- torção mista: κx = ψ; T = Tsv + Tw, Tsv = GJ·ψ, B = −EIw·ψ' e
  Tw = −B' (com a forma completa no referencial móvel do código);
- base elástica: q += −K_f·u_S, m_x += −k_θ·θx (em u no centro de
  cisalhamento);
- Euler-Bernoulli: as linhas de Vy/GAs e Vz/GAs somem.

Assim s' = A·s + b(s), com A constante (14×14). Com Ω = 0 e EIw = 0, a
torção é a uniforme e a flexão é a da 008: o teste do motor compara as duas
(1e−12).

**Cargas no vão:** trechos polinomiais de grau ≤ 1 em eixos locais (008) e
**cargas em eixos globais** sobre arcos, que não são polinômios no
referencial móvel: um vetor global constante g satisfaz g' = −Ω×g em eixos
locais. As duas entram como estados aumentados c = (g, s, 1), com a matriz
aumentada de Van Loan

```
d/ds [s; c] = [A  B; 0  C] [s; c]
```

em que B leva c às cargas q(s) e m(s) e C é a dinâmica de c (−Ω× em g,
deslocamento de potências no polinômio). A exponencial da
aumentada dá, no mesmo bloco, a matriz de transferência T = e^{A·ℓ} e a
resposta particular p, exatas. Concentradas são saltos do estado.

**Rigidez e cargas equivalentes:** com s(ℓ) = T·s(0) + p, partindo em
deslocamentos d e forças f, f(0) = T_df⁻¹·(d(ℓ) − T_dd·d(0) − p_d), e
assim K (14×14, simétrica) e as cargas de engaste. **Subdivisão:** quando
‖A‖·ℓ > 4 (empenamento com k·ℓ grande, base elástica rígida, arcos longos),
o trecho é dividido em m partes iguais com ‖A‖·ℓ/m ≤ 4; a rigidez de cada
parte é calculada como acima e as m − 1 junções internas são condensadas
(sistema bloco-tridiagonal). Tudo continua exato, e e^{A·ℓ} nunca cresce
além de e⁴ por parte.

**Exponencial:** `matrixExponential` em `beam_state.cpp`, por
`Eigen::MatrixExponential` (Padé 13 com escala e quadratura; ADR 0006),
em matrizes de tamanho máximo fixo, sem alocação.

**No elemento:** o resultado (14×14 ou 12×12 local) recebe, pela mesma
cadeia da 008 (§5.4 da 008), centro de cisalhamento, ligações (molas e
liberações por condensação, até 28 GDL), excentricidades e a rotação para
os eixos globais em cada ponta (a tangente da ponta final do arco é
diferente da inicial).

**Estado em x** (REQ-008b-19): os estados das junções das partes são
guardados; at(x) parte da junção anterior e aplica a transferência e os
saltos do trecho, exatos.

### 5.4 Empenamento nas pontas e nos nós (REQ-008b-09 a 11)

1. **Condição resolvida por ponta**: a do `WarpingEnd`, se houver; senão o
   padrão: entre as pontas de vigas com empenamento que chegam ao nó, as de
   tangentes colineares (|t₁ × t₂| < 1e−6) são `Continuous`, as demais
   `Free`.
2. **GDL do nó:** um nó tem `DofType::Warp` se ao menos uma ponta nele for
   `Continuous`.
3. **No elemento:** a ponta `Continuous` usa o GDL do nó; a `Free` vira um
   GDL interno condensado (como a liberação, k = 0); a `Restrained` é
   eliminada (ψ = 0). As pontas `Free` e `Restrained` não aparecem em
   `dofsAt`.
4. **Conjuntos:** como `dofsAt` depende da ponta, a análise cria um
   `ElementSet` por (formulação, ponta inicial contínua, ponta final
   contínua): até 4 conjuntos de vigas exatas, mais os isoparamétricos.
   A interface `ElementFormulation` não muda.
5. **Sinal:** ψ = dθx/ds no referencial da barra. Para duas barras
   colineares de sentidos opostos, θx e s trocam de sinal juntos, e ψ
   coincide; o GDL do nó é ψ sem conversão. B é o conjugado de ψ;
   bimomento nodal e reação de bimomento seguem essa convenção.
6. **Liberações e excentricidades:** a condição de empenamento vale na
   ponta do trecho flexível e não depende das liberações de esforços
   (REQ-008b-11); a excentricidade não transforma ψ.

### 5.5 Viga isoparamétrica (REQ-008b-14)

- **Campos**, em componentes globais: u e θ quadráticos de Lagrange nos três
  nós; com empenamento, ψ também quadrático, independente.
- **Deformações generalizadas** em cada ponto, lineares, com t a tangente
  e Λ os eixos locais principais: Γ = Λᵀ(u' − θ×t) (axial e cortantes) e
  κ = Λᵀθ' (torção e flexão); com empenamento, a restrição ψ = κx e
  B = −EIw·ψ'. São as mesmas relações do §5.3, escritas em componentes
  globais.
- **Integração reduzida seletiva:** 3 pontos de Gauss para flexão, torção e
  empenamento; 2 pontos para os termos de membrana, de cortante e da
  restrição ψ = κx (livre de travamento: Stolarski e Belytschko, 1983;
  Prathap, 1993). Euler-Bernoulli e a restrição de Vlasov usam penalidade:
  G·As = 1e6·EI/ℓ² e k_ψ = 1e6·EIw/ℓ², ℓ o comprimento do elemento,
  integradas com 2 pontos (erro relativo da ordem de 1e−6, documentado).
- **Cargas:** ∫Nᵀq com 4 pontos de Gauss, concentradas pelas funções de
  forma no ponto; base elástica com 3 pontos.
- **at(x):** deslocamentos pelas funções de forma; esforços pela lei
  constitutiva nos pontos de integração reduzida, extrapolados; aproximados
  (REQ-008b-19).
- **Taxas esperadas** (verificadas no AC-6): deslocamentos com h⁴ nos nós
  (quadrático com integração reduzida, Prathap), tolerância 4 ± 0,5; quando
  a observação der outra taxa, ela volta à spec, como na 008.

### 5.6 Cargas (REQ-008b-15, 17)

- Posições e intervalos medidos ao longo do comprimento de arco do trecho
  flexível (absolutos ou em fração).
- Torque distribuído: mx linear no intervalo, somado ao torque da
  excentricidade do centro de cisalhamento.
- Bimomento concentrado: salto de B no estado (motor exato) ou Nᵀ(a) no
  GDL ψ (isoparamétrica).
- Peso próprio e cargas globais em arcos: estados aumentados (§5.3); na
  reta, como na 008.

### 5.7 Fluxo da análise

```
runLinearStatic(model)
 ├─ validate(model)                     → falha: todos os problemas
 ├─ seções: solveSection uma vez por seção usada (cache por nome)
 ├─ BeamData: eixos, arco ou curva, empenamento, ligações, cargas
 ├─ condições de empenamento por ponta e GDL Warp por nó (§5.4)
 ├─ conjuntos: treliças, BeamElement (008), ExactBeamElement por
 │  (kind, pontas contínuas), IsoBeamElement
 ├─ DofMap::build; prescribe (inclusive Warp); cargas nodais (inclusive B)
 ├─ assemble → solve → reactions  (como na 008)
 └─ StaticResult: ... + warping e reactionBimoments; BeamResult por viga
```

### 5.8 Validação (REQ-008b-21)

Além da 008, com o item, o elemento ou o nó citados:

1. **Seção:** as recusas do REQ-008b-04, com polígono, vértice ou parede.
2. **Empenamento:** em viga 2D; com Iw ausente ou ≤ 0 (a mensagem diz quando
   a forma não empena); `WarpingEnd`, bimomento concentrado ou nodal, ou
   restrição `Warp` em elemento ou nó sem empenamento.
3. **Elemento:** `Line3` com viga sem formulação curva válida ou `Line2`
   pedindo a formulação curva; os casos do REQ-008b-16 (§5.2); no arco,
   apoio, carga, mola ou outro elemento no nó do meio.
4. **Mecanismo** (REQ-008b-22): o nome "warp" na direção sem rigidez.

### 5.9 Desempenho (NFR-008b-02, 03)

- Seção de 200 vértices com o tamanho padrão: cerca de 3 mil T6 e 6 mil
  nós; montagem e duas fatorações esparsas em menos de 0,1 s. A triangulação
  é O(n²) nos vértices e O(t·log t) no refinamento.
- Viga exata: uma exponencial de até 19×19 (14 estados, 3 da carga global e 2 do polinômio; ≈ 15 µs) por parte; feita no `compute`,
  dentro da montagem paralela.
- Pórtico com empenamento: 7 GDL por nó em vez de 6 aumenta o separador em
  7/6, e a fatoração em (7/6)³ ≈ 1,6. Partindo dos 3,2 a 3,5 s da 008, a
  estimativa é de 5 a 5,6 s, perto dos 6 s da meta. O benchmark vem cedo
  nas tarefas; se passar, a meta volta à spec antes de seguir.

## 6. Fluxo

Síncrono, numa thread que chama `runLinearStatic`; montagem paralela (006).
O `Model` é lido como `const`. Cada `BeamResult` guarda cópias dos seus
dados e a `SectionSolution` por `shared_ptr<const>`, e não depende do
`Model` depois da análise.

## 7. Tratamento de erros

| Situação | Comportamento | Requisito |
|----------|---------------|-----------|
| Seção inválida (polígono, parede) | `solveSection` falha citando a seção e o polígono, vértice ou parede; o problema aparece no `validate` | REQ-008b-04 |
| Empenamento sem Iw ou em 2D | Problema citando a atribuição | REQ-008b-21 |
| Itens de empenamento sem empenamento | Problema citando o item e o elemento ou nó | REQ-008b-21 |
| Arco degenerado, jacobiano, referência paralela à tangente, nó do meio usado | Problema citando o elemento e o motivo | REQ-008b-16 |
| GDL de empenamento sem rigidez | Falha com nó e direção "warp"; se todas as pontas no nó forem livres, a mensagem sugere impedir ou restringir | REQ-008b-22 |
| Exponencial com entrada não finita | Falha do elemento citando o elemento (não deve ocorrer com o modelo válido) | — |

## 8. Estratégia de testes

| Teste | Tipo | Requisito | Referência/tolerância |
|-------|------|-----------|-----------------------|
| `test_section_thin_walled`: I, U, Z, cartola e caixão de 1 e 2 células contra fórmulas de paredes finas; Iw e ω das oito formas; recusas | unitário | REQ-008b-01, 03, 04 | 1e−10 contra a fórmula |
| `test_section_solid`: geometria exata de polígonos e furos; J do retângulo (série), tubo (exato), I e U contra paredes finas; centro de cisalhamento do U; As 5/6 e 6/7; taxa de J; recusas | unitário/convergência | REQ-008b-02, 04, NFR-008b-02 | 1e−10 na geometria; 0,5% no padrão; taxa 4 ± 0,5 |
| `test_beam_state`: motor com Ω = 0 e EIw = 0 igual à `flexibleStiffness` da 008; simetria; modos rígidos de arco (6 com energia nula); subdivisão igual sem subdivisão onde ambas valem | unitário | REQ-008b-08, 13 | 1e−12 |
| `test_beam_warping`: balanço de I com torque na ponta; biapoiada com torque distribuído e concentrado; condições livre, impedido, contínuo; GJ → 0 e EIw → 0; bimomento nodal; viga de dois vãos igual a uma barra; T de três barras; base elástica de torção (Hetényi) | verificação | REQ-008b-07 a 11, 18, 19 | 1e−8 |
| `test_beam_curved`, arco: quarto de anel no plano e fora dele; anel com carga diametral; arco semicircular biarticulado com carga uniforme e com temperatura; viga curva em planta biengastada; R → ∞ igual à reta; at(s) | verificação | REQ-008b-12, 13, 15, 19 | 1e−8 contra Roark |
| `test_beam_curved`, isoparamétrica: patch test reto; convergência nos casos do arco; L/h = 1000 sem travamento; arco parabólico; nó do meio com apoio e carga | verificação/convergência | REQ-008b-14 | patch 1e−10; taxa 4 ± 0,5 |
| `test_beam_curved`, empenamento no arco: R grande igual à reta; arco e isoparamétrica concordando; caso de literatura (viga curva em planta de perfil I, Yang e Kuo, 1987) | verificação | REQ-008b-07, 13 | 1e−6; 1% contra a literatura |
| `test_beam_curved`, recursos da 008 no arco: liberações, molas, excentricidades, base elástica, cargas parciais e concentradas, globais e locais, peso, gradiente | verificação | REQ-008b-15, 17 | 1e−8 |
| Tensões: σw = B·ω/Iw no I; retângulo poligonal sob torção; paredes finas; von Mises | verificação | REQ-008b-06, 20 | 1e−8; 0,5% na poligonal |
| `test_beam_validation`: cada problema do §5.8 e vários juntos; mecanismo "warp" | unitário | REQ-008b-16, 21, 22 | mensagem cita o item |
| Suítes da 007 e da 008 inalteradas | regressão | REQ-008b-23 | as mesmas tolerâncias |
| `test_allocations`: `compute` de `ExactBeamElement` e `IsoBeamElement` | unitário | constituição §6 | zero alocações |
| `bench_section` (200 vértices) e `bench_frame` com e sem empenamento | desempenho | NFR-008b-02, 03 | < 0,5 s; < 5 s e < 6 s |
| Job de cobertura | CI | NFR-008b-04 | ≥ 80% |

## 9. Alternativas consideradas

| Alternativa | Por que foi descartada |
|-------------|------------------------|
| Fórmulas fechadas em senh/cosh para a torção com empenamento e de Roark para arcos | Uma fórmula por caso (pontas, cargas, base elástica, curvatura em cada direção); o espaço de estados cobre todos com uma só máquina exata e se testa contra a 008 |
| GDL de empenamento por nó sempre compartilhado | Errado em nós de pórtico com barras em ângulo (decisão da spec) |
| `dofsAt` com o índice do elemento | Mudaria a interface da 006 para todos; conjuntos por combinação de pontas resolvem sem isso |
| Twist em Hermite quíntico na isoparamétrica (C1 sem penalidade) | Mistura graus de interpolação com os giros quadráticos em componentes globais; a penalidade com integração reduzida é a mesma técnica do cisalhamento e não trava |
| Triangulação da seção pelo `mesher` | `core` não depende de `mesher` (§4); o gerador 2D só vem na 022 (ADR 0005) |
| Exponencial de matriz própria | O módulo do Eigen é maduro e já está no pacote (ADR 0006) |
| Áreas de cisalhamento com o ν do material | `sectionProperties` não conhece o material; a diferença é de poucos porcento e a seção fica independente dele |

## 10. Riscos e mitigação

- **Teorias de viga curva com empenamento divergem na literatura** (termos
  de acoplamento). → As duas formulações saem das mesmas deformações
  generalizadas (§5.3 e §5.5), e o AC-7 compara uma com a outra e com um
  caso publicado; a escolha de termos fica documentada.
- **Mau condicionamento da matriz de transferência** (e^{kL} grande). →
  Subdivisão com ‖A‖·ℓ ≤ 4 por parte e condensação; teste com kL = 50.
- **Pequenos kL e Ω** (o limite reto e o de torção uniforme). → A
  exponencial não tem cancelamento nesse limite (diferente das fórmulas em
  senh/cosh); teste explícito.
- **`MatrixFunctions` é do diretório `unsupported` do Eigen.** → Uso atrás
  de uma função; se a API mudar, troca-se por Padé próprio (ADR 0006).
- **Triangulação robusta** (polígonos quase degenerados, furos tocando o
  contorno). → Predicados com tolerância relativa ao tamanho da seção;
  testes de casos degenerados (§7.4 da constituição); recusa clara em vez
  de malha ruim.
- **NFR-008b-03 com margem estreita** (estimativa de 5 a 5,6 s). →
  Benchmark cedo; uma falha volta à spec.
- **Penalidades da isoparamétrica** limitam a precisão a cerca de 1e−6. →
  É uma formulação convergente (REQ-008b-14); as tolerâncias dos testes
  ficam acima disso e a exata é a recomendada na documentação.

## 11. Novas dependências

Nenhuma. O módulo `unsupported/Eigen/MatrixFunctions` faz parte do pacote
do Eigen já usado (MPL-2.0), e o seu uso fica registrado no ADR 0006.
