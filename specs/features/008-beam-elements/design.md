# Design — 008 beam-elements

Status: aprovado · Data: 2026-11-16
Requisitos: [requirements.md](requirements.md)

## 1. Resumo da abordagem

Tudo em `core`, sem Qt, sobre o `Model` e a análise da 007:

1. **Seções** (`section.hpp`): a `Section` ganha as propriedades da seção
   genérica (todas opcionais) ou uma forma paramétrica (`SectionShape`, um
   `std::variant` das oito formas). `sectionProperties(Section)` devolve as
   propriedades num só formato: centroide, eixos principais, centro de
   cisalhamento, J e áreas de cisalhamento.
2. **Elemento** (`beam_element.hpp`): uma `ElementFormulation` por variante
   (2D, 3D) com uma tabela por elemento (`BeamData`) já resolvida. A rigidez
   do trecho flexível é a fórmula fechada de Timoshenko de 2 nós
   (Przemieniecki). Com φ = 0 ela é a de Euler-Bernoulli, e por isso não
   trava (REQ-008-07). Sobre ela se aplicam, nesta ordem: a base elástica;
   as molas e liberações de extremidade, por condensação estática de GDL
   internos; a excentricidade do centro de cisalhamento; a rotação para os
   eixos globais; e as excentricidades rígidas.
3. **Cargas no vão** (`beam_loads.hpp`): cada carga distribuída, concentrada
   ou térmica vira, na preparação da análise, uma lista de trechos
   polinomiais em eixos locais principais. As cargas nodais equivalentes são
   ∫Nᵀq com as funções de forma exatas da teoria. Isso dá deslocamentos
   nodais exatos sem base elástica (REQ-008-08).
4. **Resultados** (`beam_result.hpp`): por viga, os esforços nas
   extremidades do trecho flexível e um avaliador `BeamResult::at(x)`. Ele
   integra as cargas a partir da extremidade inicial, exatamente, por
   polinômios por trechos: esforços por equilíbrio e rotações e
   deslocamentos pelas relações da teoria. `sectionStresses` dá as tensões
   nos pontos característicos de cada forma (REQ-008-23).
5. **Modelo**: novos `ElementKind` (`Beam2D`, `Beam3D`) e campos de
   atribuição (teoria, orientação); novos itens (liberações e molas de
   ligação, excentricidades, base elástica, cargas no vão, gradiente);
   momentos em `NodalForce`; rotações em `Restraint`.
6. **Validação e análise**: o `Validator` da 007 ganha as verificações do
   REQ-008-24 e vai para um arquivo próprio. `runLinearStatic` monta treliças
   e vigas juntas: a `DofMap` faz a união dos GDL, e só nós de viga recebem
   rotações (REQ-008-20).

## 2. Verificação da constituição

| Princípio | Atende? | Observação |
|-----------|---------|------------|
| Multiplataforma (§2) | Sim | C++20 e Eigen; nada específico de sistema |
| GPU somente via QRhi (§3) | Sim | Nada de GPU |
| Camadas / `core` sem Qt (§4) | Sim | Tudo em `core` (REQ-008-27) |
| Correção numérica (§5) | Sim | Patch test (momento, normal e torção constantes), convergência (quarto de anel e base elástica), soluções analíticas de vigas, pórticos e grelhas; tolerâncias explícitas por teste; fórmulas aproximadas de seção documentadas |
| Qualidade (§6) | Sim | Cobertura de `core` ≥ 80% mantida no CI; sem alocação no `compute` |
| Simplicidade / dependências (§7) | Sim | Nenhuma dependência nova. Matrizes fechadas de tamanho fixo; sem quadratura genérica, exceto 4 pontos de Gauss para a base elástica e para ∫Nᵀq |

## 3. Módulos e arquivos afetados

| Módulo | Arquivo | Novo/Alterado | Responsabilidade |
|--------|---------|---------------|------------------|
| core | `include/fem/core/section.hpp`, `section.cpp` | novo | `Section` (movida de `material.hpp`), formas, `SectionProperties`, `sectionProperties`, pontos de tensão (REQ-008-02 a 05) |
| core | `include/fem/core/material.hpp` | alterado | `Section` sai daqui; o header inclui `section.hpp` para manter os includes da 007 |
| core | `include/fem/core/model.hpp`, `model.cpp` | alterado | Novos tipos, campos e itens do modelo (REQ-008-09, 11 a 18) |
| core | `include/fem/core/beam_element.hpp`, `beam_element.cpp` | novo | `BeamData`, eixos locais, `BeamElement` 2D/3D (REQ-008-05 a 15, 19) |
| core | `include/fem/core/beam_loads.hpp`, `beam_loads.cpp` | novo | Polinômios por trechos, cargas no vão em eixos locais, cargas equivalentes (REQ-008-16 a 19) |
| core | `include/fem/core/beam_result.hpp`, `beam_result.cpp` | novo | `BeamForces`, `BeamResult::at`, `sectionStresses` (REQ-008-21 a 23) |
| core | `include/fem/core/static_analysis.hpp`, `static_analysis.cpp` | alterado | Rotações, momentos de reação e resultados de viga; montagem mista (REQ-008-20, 21, 25, 26) |
| core | `model_validation.cpp`, `detail/model_validation.hpp` | novo | O `Validator`, movido de `static_analysis.cpp` e ampliado (REQ-008-10, 13, 14, 17, 24) |
| core | `CMakeLists.txt` | alterado | Novos fontes |
| tests | `unit/core/truss_meshes.hpp` | alterado | Renomeado para `bar_meshes.hpp`, mesma interface (serve a treliças e vigas) |
| tests | `unit/core/test_section.cpp` | novo | AC-1 |
| tests | `unit/core/test_beam_element.cpp` | novo | AC-2 |
| tests | `unit/core/test_beam_verification.cpp`, `test_beam_connections.cpp`, `test_beam_loads.cpp` | novo | AC-3 a AC-10 |
| tests | `unit/core/test_beam_validation.cpp` | novo | AC-11 |
| tests | `unit/core/test_allocations.cpp` | alterado | `BeamElement::compute` sem alocação |
| tests | `perf/bench_frame.cpp`, `CMakeLists.txt` | novo/alterado | NFR-008-02 |
| specs | `architecture.md` | alterado | O que existe de seções, vigas e resultados de viga |

## 4. Interfaces públicas

```cpp
// section.hpp — REQ-008-02 to 05. Shapes are drawn in the local (y, z)
// plane of the section: the height h along y, the width b along z.
struct Rectangle { double b = 0, h = 0; };
struct Circle { double d = 0; };
struct CircularTube { double d = 0, t = 0; };            // outer diameter, wall
struct RectangularTube { double b = 0, h = 0, t = 0; };
struct ISection { double h = 0, bf = 0, tf = 0, tw = 0; }; // doubly symmetric
struct Angle { double b = 0, h = 0, t = 0; };              // legs along z and y
struct Channel { double h = 0, bf = 0, tf = 0, tw = 0; };  // flanges toward +z
struct Tee { double h = 0, bf = 0, tf = 0, tw = 0; };      // flange at +y
using SectionShape = std::variant<Rectangle, Circle, CircularTube, RectangularTube,
                                  ISection, Angle, Channel, Tee>;

struct Section {
    std::string name;
    // Generic section (007 used only the area). About axes through the
    // centroid, parallel to the local y and z of the bar.
    std::optional<double> area;                         // A
    std::optional<double> inertiaY, inertiaZ;           // Iy, Iz
    std::optional<double> productOfInertia;             // Iyz (default 0)
    std::optional<double> torsionConstant;              // J
    std::optional<Eigen::Vector2d> shearCentre;         // from the centroid (default 0)
    std::optional<double> shearAreaY, shearAreaZ;       // Asy, Asz
    // Or a shape, from which all of the above are computed. Giving both is
    // a problem of the model.
    std::optional<SectionShape> shape;
};

// Everything the beam needs, in one format.
struct SectionProperties {
    double area = 0;
    Eigen::Vector2d centroid = Eigen::Vector2d::Zero();  // in the drawing (y, z)
    double principalAngle = 0;     // from local y to the principal axis 1
    double inertiaY = 0, inertiaZ = 0;  // principal: about axis 1 (y') and axis 2 (z')
    double torsionConstant = 0;
    Eigen::Vector2d shearCentre = Eigen::Vector2d::Zero(); // principal axes, from the centroid
    std::optional<double> shearArea1, shearArea2;
};

// REQ-008-03, 04: computed for a shape, checked and rotated to the principal
// axes for a generic section. Fails naming the dimension or the property.
[[nodiscard]] Expected<SectionProperties> sectionProperties(const Section& section);

// REQ-008-23: the characteristic points of a shape, for the stresses.
struct StressPoint {
    std::string name;                // "top flange tip", "web centre", ...
    Eigen::Vector2d position;        // principal axes, from the centroid
    double firstMoment1 = 0, firstMoment2 = 0; // Q for the shear of V1, V2
    double thickness = 0;            // where the shear flows (0: no shear)
    double torsionFactor = 0;        // tau = T * factor
};
[[nodiscard]] std::vector<StressPoint> stressPoints(const SectionShape& shape);
```

```cpp
// model.hpp — altered.
enum class ElementKind : std::uint8_t { Truss2D, Truss3D, Beam2D, Beam3D };
enum class BeamTheory : std::uint8_t { EulerBernoulli, Timoshenko };
enum class Axes : std::uint8_t { Global, Local };

struct Assignment {                    // REQ-007-04, 008-07, 09
    ElementTarget elements;
    ElementKind kind = ElementKind::Truss3D;
    std::string material;
    std::string section;
    BeamTheory theory = BeamTheory::EulerBernoulli;     // beams only
    std::optional<Eigen::Vector3d> reference;           // 3D beams; default §5.2
    double rollAngle = 0.0;                             // about the bar axis, radians
};

// Restraint::dof may now be RX, RY, RZ (REQ-008-16).
struct NodalForce { NodeTarget nodes; Eigen::Vector3d force, moment; }; // moment: new

enum class BarEnd : std::uint8_t { Start, End, Both };

// REQ-008-11, 12: per local force (N, Vy, Vz, T, My, Mz): none = rigid,
// 0 = released, k > 0 = spring. In 2D only N, Vy, Mz count.
struct EndConnection {
    ElementTarget elements;
    BarEnd end = BarEnd::Both;
    std::array<std::optional<double>, 6> stiffness;
};

struct RigidOffset {                   // REQ-008-14, global axes
    ElementTarget elements;
    Eigen::Vector3d start = Eigen::Vector3d::Zero(), end = Eigen::Vector3d::Zero();
};

struct ElasticFoundation {             // REQ-008-15, per length, local axes
    ElementTarget elements;
    Eigen::Vector3d translational = Eigen::Vector3d::Zero(); // kx, ky, kz
    double torsional = 0.0;                                   // k_theta_x
};

struct BarPosition { double value = 0.0; bool fraction = true; };  // REQ-008-17

struct DistributedLoad {               // REQ-008-16, per length of the bar
    ElementTarget elements;
    Axes axes = Axes::Global;
    Eigen::Vector3d startValue = Eigen::Vector3d::Zero(), endValue = Eigen::Vector3d::Zero();
    BarPosition from{0.0, true}, to{1.0, true};
};

struct ConcentratedLoad {              // REQ-008-16
    ElementTarget elements;
    Axes axes = Axes::Global;
    Eigen::Vector3d force = Eigen::Vector3d::Zero(), moment = Eigen::Vector3d::Zero();
    BarPosition at{0.5, true};
};

struct TemperatureGradient {           // REQ-008-16: dT/dy, dT/dz, local axes
    ElementTarget elements;
    double alongY = 0.0, alongZ = 0.0;
};

struct Model {
    ... // 007 fields
    std::vector<EndConnection> endConnections;
    std::vector<RigidOffset> rigidOffsets;
    std::vector<ElasticFoundation> foundations;
    std::vector<DistributedLoad> distributedLoads;
    std::vector<ConcentratedLoad> concentratedLoads;
    std::vector<TemperatureGradient> temperatureGradients;
};
```

```cpp
// beam_result.hpp — REQ-008-21 to 23.
struct BeamForces { double n = 0, vy = 0, vz = 0, t = 0, my = 0, mz = 0; }; // local (§5.7)

struct BeamPoint {
    BeamForces forces;
    Eigen::Vector3d displacement, rotation;   // local axes of the bar
};

enum class Side : std::uint8_t { Before, After };   // at a concentrated load

class BeamResult {
public:
    [[nodiscard]] double length() const;                 // flexible length
    [[nodiscard]] const Eigen::Matrix3d& axes() const;   // columns: local x, y, z (global)
    [[nodiscard]] BeamForces start() const, end() const;
    // x in [0, length()], from the start of the flexible length. At a
    // concentrated load `side` picks the value before or after the jump.
    [[nodiscard]] BeamPoint at(double x, Side side = Side::After) const;
private:
    ... // end state, piecewise polynomials of the loads, section stiffnesses
};

struct PointStress { std::string name; double normal = 0, shear = 0, vonMises = 0; };
// Stresses at the stress points of the shape (REQ-008-23).
[[nodiscard]] std::vector<PointStress> sectionStresses(const Section&, const BeamForces&);
// Generic section: normal stress at (y, z) given in the local axes.
[[nodiscard]] double normalStress(const SectionProperties&, const BeamForces&,
                                  const Eigen::Vector2d& point);
```

```cpp
// static_analysis.hpp — altered (REQ-008-21).
struct StaticResult {
    ... // 007 fields, unchanged
    std::vector<Eigen::Vector3d> rotations;        // per node: rx, ry, rz (0 without dof)
    std::vector<Eigen::Vector3d> reactionMoments;  // per node: 0 where free
    std::vector<std::optional<BeamResult>> beams;  // per element: only beams
};
```

```cpp
// beam_element.hpp — REQ-008-05 to 15, 19.
struct BeamData {                       // per element, resolved by the analysis
    // Flexible length, section and theory.
    double ea = 0, eiy = 0, eiz = 0, gj = 0;   // principal axes
    double phiY = 0, phiZ = 0;                  // 12 EI / (G As L^2); 0: Euler-Bernoulli
    Eigen::Matrix3d axes;                       // local principal axes, columns, global
    Eigen::Vector2d shearCentre;                // principal, from the centroid
    std::array<Eigen::Vector3d, 2> offsets;     // global, node to flexible end
    std::array<std::array<double, 6>, 2> springs; // +inf rigid, 0 released
    Eigen::Vector4d foundation;                 // kx, ky, kz, k_theta
    Eigen::Matrix<double, 12, 1> fixedEndLoads; // flexible, local: from beam_loads
};

// Local principal axes (§5.2): default reference, given reference and roll.
[[nodiscard]] Expected<Eigen::Matrix3d> beamAxes(const Eigen::Vector3d& x1, const Eigen::Vector3d& x2,
                                                 const std::optional<Eigen::Vector3d>& reference,
                                                 double roll, ElementKind kind);

class BeamElement final : public ElementFormulation {
public:
    BeamElement(ElementKind kind, std::span<const BeamData> beams);
    DofMask dofsAt(ElementType, int) const override;   // u v rz | u v w rx ry rz
    Expected<void> compute(const ElementNodes&, ElementIndex, Eigen::Ref<Eigen::MatrixXd>,
                           Eigen::Ref<Eigen::VectorXd>) const override;
};
```

## 5. Dados e algoritmos

### 5.1 Seções

Coordenadas do desenho (y, z), com h ao longo de y. Todas as formas são
compostas de retângulos (as circulares, de fórmulas exatas), e A, centroide,
Iy, Iz e Iyz saem da soma por Steiner, exatos. Os eixos principais ficam em
θp = ½·atan2(−2Iyz, Iz − Iy). Os eixos principais y' e z' são y e z
girados de θp. I'y e I'z são as inércias em torno deles: I'z resiste à
flexão no plano x–y' (momento Mz), I'y à flexão no plano x–z' (momento My),
a mesma convenção da 2D. Numa seção simétrica, θp = 0 e os eixos principais
são os do desenho.

| Forma | J | Centro de cisalhamento | Asy / Asz |
|-------|---|------------------------|-----------|
| Retângulo | Série de Saint-Venant truncada em 5 termos (erro < 1e−6) | centroide | 5/6·A, 5/6·A |
| Círculo | π d⁴/32 (exato) | centroide | 0,9·A, 0,9·A |
| Tubo circular | π (D⁴ − d⁴)/32 (exato) | centroide | 0,5·A, 0,5·A |
| Tubo retangular | Bredt: 4 Am² t / pm (linha média) | centroide | 2·(h − t)·t, 2·(b − t)·t |
| Perfil I | Σ b t³/3 (paredes finas, sem raios) | centroide | (h − tf)·tw, 5/6·2·bf·tf |
| Cantoneira | Σ b t³/3 | encontro das linhas médias das abas | 5/6 da aba paralela a cada eixo |
| Perfil U | Σ b t³/3 | e = 3 bf'² tf / (6 bf' tf + h' tw) atrás da alma (linhas médias) | (h − tf)·tw, 5/6·2·bf·tf |
| Perfil T | Σ b t³/3 | encontro das linhas médias da mesa e da alma | (h − tf)·tw, 5/6·bf·tf |

As fórmulas de paredes finas são documentadas no código com a referência
(Young e Budynas, *Roark's Formulas*, cap. 10). Os testes as comparam com a
própria fórmula (1e−12) e com valores tabelados, numa tolerância por forma:
retângulo, círculo e tubos ≤ 1e−6 contra os exatos; perfis abertos ≤ 15%
contra J de tabelas de perfis laminados, que incluem raios de concordância.
As áreas de cisalhamento de seções assimétricas são aproximações de
engenharia e ficam projetadas nos eixos principais.

Pontos de tensão (REQ-008-23): para cada forma, as extremidades das fibras
(as quatro quinas, ou oito pontos no perímetro dos circulares) e os pontos
de cisalhamento máximo (centro da alma, linha neutra). Em cada um:

- σ = N/A − Mz·y'/I'z + My·z'/I'y (flexão composta nos eixos principais,
  convenção do §5.7);
- τ = V·Q/(I·t), de Jourawski ou do fluxo de paredes finas;
- τ_T: T·r/J nos circulares, T/(2·Am·t) no tubo retangular (Bredt) e
  T·t/J nos abertos e no retângulo, este último com o fator α(b/h) da série;
- von Mises = √(σ² + 3(|τ| + |τ_T|)²). A soma dos módulos é conservadora e
  fica documentada.

A seção genérica não tem geometria: só a tensão normal em pontos dados.

### 5.2 Eixos locais (REQ-008-09, 10)

- **x local:** x = (x₂ − x₁)/L.
- **Vetor de referência:** r é o dado ou, por padrão, o Z global. Quando
  |x × Z| < 1e−6, o padrão passa a ser o X global.
- **Vetor de referência paralelo à barra:** se o r dado tiver |x × r̂| < 1e−6,
  é um problema do modelo.
- **Eixos y e z:** y = normalizar(r − (r·x)x) e z = x × y. Numa viga
  horizontal com r = Z, o y local fica vertical, como na 2D. Assim a seção
  desenhada com h ao longo de y trabalha de pé.
- **Ângulo β:** gira y e z em torno de x pela regra da mão direita. Em
  seguida o ângulo principal θp da seção gira os eixos até os principais.
  `BeamData::axes` guarda esses eixos principais.
- **2D:** y = Z × x e z = Z; β e r não se aplicam.

### 5.3 Trecho flexível (REQ-008-05 a 08)

Em eixos principais, com GDL locais na ordem
(u, v, w, θx, θy, θz) do nó a e depois do nó b, a matriz 12×12 é a fórmula
fechada de Timoshenko de 2 nós (Przemieniecki, 1968):

- axial: EA/L [1 −1; −1 1];
- torção: GJ/L [1 −1; −1 1];
- flexão no plano x–y (v, θz), com φy = 12·EIz/(G·Asy·L²):

```
EIz/((1+φy)L³) [ 12     6L          −12    6L
                 6L    (4+φy)L²    −6L    (2−φy)L²
                 …  simétrica ]
```

- flexão no plano x–z (w, θy): igual, com φz e os sinais de θy.

Euler-Bernoulli é φ = 0. Timoshenko com As → ∞ dá φ → 0, a mesma matriz,
sem travamento (REQ-008-07). As funções de forma exatas, dependentes de φ
(Friedman e Kosmatka, 1993), são as usadas para ∫Nᵀq (§5.5) e para a base
elástica. Elas resolvem a equação homogênea da teoria, e por isso os
deslocamentos nodais são exatos sob qualquer carga no vão (REQ-008-08).

**Base elástica (REQ-008-15):** K_f = ∫ Nᵀ diag(kx, ky, kz, kθ) N dx, com 4
pontos de Gauss e as mesmas funções de forma. Ela não é exata, porque a
solução com base elástica é hiperbólico-trigonométrica. O erro nodal cai
com h⁴ (superconvergência das cúbicas de Hermite), e essa é a taxa que o
AC-10 verifica: 4 ± 0,3.

### 5.4 Ligações, centro de cisalhamento e excentricidades (REQ-008-05, 11 a 14)

A cadeia de transformações vai do nó global até o trecho flexível local:

```
nó (global, 12 GDL)
  └─ excentricidade rígida: u_p = u_n + θ_n × r, θ_p = θ_n         (T_off, global)
      └─ rotação para os eixos principais: R = blkdiag(Λᵀ ×4)       (T_rot)
          └─ centro de cisalhamento: v_S = v − e₂·θx, w_S = w + e₁·θx  (T_sc)
              └─ ligações: GDL internos i, molas k entre p e i       (condensação)
                  └─ trecho flexível K_b (§5.3) + base elástica
```

**Ligações:** cada GDL local de extremidade com mola finita ou liberado
(k = 0) ganha um GDL interno, até 12. O sistema local é

```
[ K_ee + S   K_ei ] [u_e]   [ f_e ]
[ K_ie       K_ii ] [u_i] = [ f_i ]
```

em que S liga o GDL externo ao interno pela mola k. A condensação estática
dá K* = K_ee − K_ei K_ii⁻¹ K_ie e f* = f_e − K_ei K_ii⁻¹ f_i, numa matriz de
até 24×24 de tamanho máximo fixo
(`Eigen::Matrix<double, Dynamic, Dynamic, 0, 24, 24>`, sem alocação), com
LDLᵀ denso.

**Liberações instáveis (REQ-008-13):** a validação fatora K_ii de cada
viga com ligações. Um pivô ≤ 1e−12 × max|diag| significa que a barra é
instável por si só, e a mensagem lista as liberações. A base elástica entra
em K_ii, e assim uma barra "solta" sobre base elástica é aceita.

### 5.5 Cargas no vão (REQ-008-16 a 19)

Na preparação, cada carga sobre cada viga vira um conjunto de **trechos
polinomiais** q(x), de grau ≤ 1, em eixos locais principais:

- **distribuída global:** q_local = Λᵀ q_global, por comprimento da barra;
- **distribuída em parte do vão:** o intervalo [a, b] é convertido de
  fração para comprimento, sobre o trecho flexível;
- **concentrada:** um impulso em x = a, com força e momento;
- **peso próprio:** distribuída uniforme ρ·A·g (REQ-008-18);
- **carga no centroide de seção assimétrica:** o momento de torção
  m_x = −e₂·q₁ + e₁·q₂, somado à carga distribuída de torção (REQ-008-05);
- **temperatura:** ΔT e ε₀ dão a deformação axial imposta, como na 007; o
  gradiente dá a curvatura imposta κ₁ = α·∂T/∂y e κ₂ = α·∂T/∂z, rotacionada
  para os eixos principais.

As cargas equivalentes do trecho flexível engastado nas duas pontas são
f = ∫ Nᵀ q dx, mais Nᵀ(a)·P para cada concentrada. A integral usa Gauss de
3 pontos por trecho, exata para os polinômios de grau ≤ 5 envolvidos. A
parcela térmica é EA·ε* no axial e EI·κ nos momentos de extremidade. O
resultado é `BeamData::fixedEndLoads`, de 12 posições. Ligações e
excentricidades atuam sobre ele pela mesma cadeia do §5.4 (REQ-008-19).

### 5.6 Fluxo da análise

```
runLinearStatic(model)
 ├─ validate(model)                     → falha: todos os problemas
 ├─ BarData (treliças, 007) e BeamData (vigas): eixos, seção, ligações,
 │  cargas equivalentes; e as cargas no vão em trechos, guardadas para os
 │  resultados
 ├─ conjuntos: um ElementSet por ElementKind presente (até 4)
 ├─ DofMap::build (união: só nós de viga têm rotação); prescribe
 ├─ NodalLoad de forças e momentos nodais
 ├─ assemble → diagonal → fatoração → solve → reactions  (como na 007)
 └─ StaticResult: u, θ e reações por nó; N, ε, σ por barra (vigas também);
    BeamResult por viga (§5.7)
```

### 5.7 Esforços ao longo da barra (REQ-008-21, 22)

- **Convenção de sinais:** os esforços internos estão em eixos principais
  locais, na face de normal +x. N > 0 é tração. Vy, Vz, T, My e Mz seguem o
  sentido positivo dos eixos locais nessa face. Daí Mz > 0 traciona a
  fibra de y' < 0, com σ = N/A − Mz·y'/I'z + My·z'/I'y. Os nomes da API são y e z
  mesmo em eixos principais, e numa seção simétrica eles coincidem com os
  eixos do desenho.
- **Extremidades:** depois da solução, os GDL do nó são levados pela cadeia
  do §5.4 até o trecho flexível. Os GDL internos de ligação são recuperados
  por u_i = K_ii⁻¹(f_i − K_ie u_e). Os esforços nas pontas saem de
  F = K_b d − f_b.
- **Ao longo da barra:** `BeamResult::at(x)` parte de F_a e integra o
  equilíbrio: N(x), V(x) e T(x) com as cargas, e M(x) com V e com os
  momentos concentrados. As rotações saem de θ' = M/EI + κ e os
  deslocamentos de v' = θ + V/(G·As), a partir de d_a. Tudo é feito em
  polinômios por trechos, exatos, com saltos nas concentradas, escolhidos
  por `Side`.
- **Com base elástica:** a reação −k·v(x) entra como carga, com v(x)
  interpolado pelas funções de forma. O resultado é aproximado e converge
  (REQ-008-15).
- **Seções assimétricas:** a torção vem só do equilíbrio em relação ao
  centro de cisalhamento.
- **Esforço normal nos campos da 007:** `axialForce`, `strain` e `stress`
  continuam por elemento e passam a valer para vigas também, com N na
  extremidade inicial e σ = N/A.

### 5.8 Validação (REQ-008-24)

O `Validator` da 007 vai para `model_validation.cpp`. Ele soma as
verificações abaixo, sempre citando o item, o elemento ou o nó:

1. **Seção:** dimensões das formas (REQ-008-04); forma e propriedades ao
   mesmo tempo; Iy, Iz ou J ausente ou ≤ 0 conforme a viga (Iz em 2D; Iy,
   Iz e J em 3D); Iy·Iz − Iyz² ≤ 0; Asy ou Asz ausente ou ≤ 0 em
   Timoshenko; seção assimétrica em viga 2D (Iyz ≠ 0 ou centro de
   cisalhamento com z ≠ 0).
2. **Material:** ν ausente ou fora de (−1; 0,5) quando G é necessário.
3. **Elemento:** viga 2D fora do plano XY e viga que não é `Line2` (a mesma
   regra da treliça); vetor de referência paralelo (§5.2); trecho flexível
   ≤ 1e−9 × L; liberações instáveis (§5.4); duas ligações na mesma ponta,
   duas excentricidades ou duas bases elásticas no mesmo elemento.
4. **Itens só de viga em outro elemento:** ligação, excentricidade, base
   elástica, carga no vão ou gradiente num elemento que não é viga.
5. **Cargas no vão:** posição fora de [0, trecho flexível]; intervalo
   invertido (REQ-008-17).
6. **Valores:** rigidez de mola negativa.
7. **Nós:** momento ou rotação prescrita em nó sem o GDL; momento Mx ou My,
   ou rotação RX ou RY, em nó só de viga 2D.

### 5.9 Complexidade e desempenho (NFR-008-02)

Validação e preparação são O(elementos + itens); o `compute` de uma viga é
O(24³) no pior caso de ligações, constante. O banco é um edifício de 11 × 11
pilares (10 × 10 vãos) e 41 níveis, com 4 961 nós e cerca de 29,8 mil GDL
livres, mais vigas nas duas direções em cada nível.

O separador típico é um pavimento: 121 nós × 6 GDL ≈ 730 equações. Isso dá
uma fatoração da ordem de n·s² ≈ 1,6·10¹⁰ operações. A treliça de Kuhn
18³ da 007 tinha um custo da mesma ordem (16,5 mil GDL e separador de cerca
de 970) e levou 3,7 s. A estimativa é de 2 a 4 s. A tarefa do benchmark
confirma o número; se ele passar de 5 s, a meta volta à spec, como na 007.

## 6. Fluxo

Síncrono, numa thread que chama `runLinearStatic`; montagem paralela (006).
O `Model` é lido como `const`. Cada `BeamResult` carrega cópias dos seus
dados (trechos e rigidezes) e não depende do `Model` depois da análise.

## 7. Tratamento de erros

| Situação | Comportamento | Requisito |
|----------|---------------|-----------|
| Problemas de modelo (§5.8 e 007) | `validate` devolve todos; a análise falha com eles, um por linha | REQ-008-24 |
| Dimensão de forma inválida | Problema citando a seção e a dimensão; `sectionProperties` falha com a mesma frase | REQ-008-04 |
| Vetor de referência paralelo | Problema citando o elemento | REQ-008-10 |
| Liberações instáveis | Problema citando o elemento e as liberações | REQ-008-13 |
| Trecho flexível nulo ou negativo | Problema citando o elemento | REQ-008-14 |
| Carga fora do trecho | Problema citando a carga e o elemento | REQ-008-17 |
| GDL sem rigidez, inclusive rotação | Falha com nó e direção (rx, ry, rz); quando todas as vigas do nó liberam aquele momento, a mensagem sugere restringir a rotação | REQ-008-25 |
| Mecanismo global | Como na 007, com as rotações nos nomes | REQ-008-25 |
| `BeamResult::at` fora de [0, L] | Pré-condição, verificada por `assert` | — |

## 8. Estratégia de testes

| Teste | Tipo | Requisito | Referência/tolerância |
|-------|------|-----------|-----------------------|
| `test_section`: oito formas contra a própria fórmula e contra valores exatos e tabelados; eixos principais da cantoneira; centro de cisalhamento do U e do T; seção genérica girada para os eixos principais; dimensões inválidas; pontos de tensão | unitário | REQ-008-02 a 04, 23 | 1e−12 contra a fórmula; §5.1 contra tabelas |
| `test_beam_element`: K de Euler-Bernoulli e de Timoshenko igual à fórmula em orientação qualquer; invariância sob rotação; 6 (3D) e 3 (2D) modos rígidos com energia nula, inclusive com excentricidades e centro de cisalhamento; Timoshenko com As = 1e12·A igual a Euler-Bernoulli; eixos locais (padrão, vertical, vetor dado, β); condensação com mola 0 igual à liberação e com mola 1e14 igual ao rígido; sem alocação | unitário | REQ-008-05 a 07, 09, 11, 12, 14 | 1e−12 relativo |
| `test_beam_verification`, patch: barra em 4 trechos desiguais com momento, normal e torção constantes | verificação | REQ-008-08 | 1e−10 |
| `test_beam_verification`, analíticos: balanço (força e momento), biapoiada, biengastada, contínua de dois vãos, recalque e rotação prescrita; pórtico plano; pórtico espacial em L; grelha; viga com tirante; nas duas teorias | verificação | REQ-008-08, 16, 20, 21, NFR-008-01 | 1e−10 relativo |
| `test_beam_loads`: uniforme, trapezoidal, parcial e concentrada (com o salto de V) em biapoiada e biengastada, com M(x), V(x), v(x) e θ(x) em 11 pontos; gradiente (biengastada e biapoiada); ΔT, ε₀ e peso de viga inclinada; carga no centroide do U; cantoneira em flexão oblíqua, comparada com o modelo girado | verificação | REQ-008-05, 16 a 19, 22, NFR-008-01 | 1e−10 relativo |
| `test_beam_connections`: rótula interna (Gerber); barra rotulada igual à treliça; liberação de torção; mola de rotação contra a ligação semirrígida analítica e os limites 0 e ∞; excentricidade igual à barra rígida explícita (EA, EI 1e8 vezes maiores, tolerância 1e−6); cargas com rótula, mola e excentricidade | verificação | REQ-008-11 a 14, 19 | 1e−10 (1e−6 na barra rígida) |
| Timoshenko: balanço curto, com flecha PL³/3EI + PL/(G·As); esbelta (L/h = 1000) contra Euler-Bernoulli | verificação | REQ-008-07 | 1e−10; 1e−6 |
| Tensões: retângulo, I, tubo e U sob N, M, V e T contra resistência dos materiais | verificação | REQ-008-23 | 1e−10 |
| Convergência: quarto de anel (n = 8, 16, 32), ponta contra Castigliano; viga sobre base elástica (Hetényi), n = 4, 8, 16 | verificação | REQ-008-08, 15 | taxa 2 ± 0,2; 4 ± 0,3 |
| `test_beam_validation`: cada problema do §5.8 isolado e vários juntos; mecanismo de rotação (nó com todas as vigas rotuladas); 007 inalterada (a suíte de treliça passa sem mudança) | unitário | REQ-008-10, 13, 14, 17, 24 a 26 | mensagem cita o item |
| `bench_frame`: edifício de 11 × 11 pilares e 41 níveis (~30 mil GDL) | desempenho | NFR-008-02 | < 5 s em Release |
| Job de cobertura | CI | NFR-008-03 | ≥ 80% |

## 9. Alternativas consideradas

| Alternativa | Por que foi descartada |
|-------------|------------------------|
| Timoshenko de 2 nós com interpolação linear e integração reduzida | Trava com pouca integração ou exige muitos elementos; a fórmula fechada com φ é exata e não trava |
| Elemento exato com base elástica (funções hiperbólicas) | Exato, mas complica a formulação para as duas teorias e para as ligações; a convergência h⁴ das cúbicas atende ao REQ-008-15, que já pede convergência |
| Liberações por GDL duplicados na malha (nós coincidentes) | Muda a malha e a `DofMap` e expõe nós falsos ao usuário; a condensação no elemento é local e transparente |
| Ligações e excentricidades na `Assignment` | Uma rótula costuma valer para algumas barras de um conjunto; itens separados com alvo próprio servem melhor à 010 |
| Esforços ao longo da barra por pontos fixos amostrados no resultado | Precisão limitada pela amostragem e diagramas da 013 presos a ela; o avaliador exato custa pouco e não guarda pontos |
| Malha 2D da seção para J e áreas de cisalhamento já na 008 | O gerador 2D só vem na 022, e as fórmulas fechadas e de paredes finas atendem às oito formas; a seção poligonal fica para a 008b |

## 10. Riscos e mitigação

- **Convenções de sinal entre eixos principais, eixos do desenho, liberações
  e excentricidades.** → Um único caminho de transformações (§5.4) e testes
  de equivalência: o modelo girado contra o original, a barra rígida
  explícita contra a excentricidade, a mola 0 contra a liberação.
- **A taxa h⁴ da base elástica pode não aparecer nos tamanhos testados**,
  por estar na região pré-assintótica ou ser limitada pelo arredondamento.
  → Escolher a rigidez de modo que βL fique entre 2 e 8 nos tamanhos
  testados. Se a taxa observada for outra, a spec é atualizada (o AC-10 diz
  "taxa documentada no design").
- **Nós em que todas as vigas liberam o momento viram mecanismo.** → É o
  comportamento do REQ-008-25. A mensagem sugere restringir a rotação ou
  manter uma barra ligada. A restrição automática fica como ideia para a
  010, onde a interface pode oferecê-la.
- **J de perfis abertos sem raios de concordância difere das tabelas de
  perfis laminados.** → A tolerância por forma fica documentada (§5.1), e a
  seção genérica permite digitar o J da tabela.
- **NFR-008-02 com margem estreita (estimativa de 2 a 4 s).** → O benchmark
  vem cedo nas tarefas, e uma falha volta à spec antes do fechamento.
- **`static_analysis.cpp` cresce demais.** → A validação vai para
  `model_validation.cpp` e a preparação das vigas para
  `beam_element.cpp` e `beam_loads.cpp`.

## 11. Novas dependências

Nenhuma.
