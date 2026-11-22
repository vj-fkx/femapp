# Design — 008e beam-nonlinear-geometry

Status: rascunho, não aprovado · Data: 2026-11-22

> Feature adiada para depois da 1.0 pelo mantenedor (2026-11-22), com este
> design em rascunho. Na retomada, faltam a aprovação dele e do ADR 0009 e o
> ajuste proposto do REQ-008e-16 (§5.8).
Requisitos: [requirements.md](requirements.md)

## 1. Resumo da abordagem

A análise geometricamente não linear (`runNonlinearStatic`) segue o caminho
de equilíbrio de um `Model` de treliças e vigas. A cada passo, um Newton
completo resolve o resíduo r(u, λ) = f_int(u) − f_c − λ·f_v, onde f_c é a
parte constante e f_v a parte variável do caso de carga.

**Duas formulações, verificadas uma contra a outra** (ADR 0009):

- **Corrotacional** para os elementos de dois nós (treliça, viga reta) e
  para o arco exato da 008b. O referencial do elemento acompanha o
  movimento de corpo rígido (Battini e Pacoste, 2002, com o empenamento).
  No referencial local, o elemento é a **barra exata da 008c/008d**: o motor
  de espaço de estados com o perfil de referência dos seis esforços, tomado
  dos esforços locais correntes. Assim o P-δ e a flexo-torção já estão
  exatos dentro da barra, e as grandes rotações ficam no referencial. A
  tangente local é consistente: inclui a variação do perfil com os
  deslocamentos locais, por diferenças finitas nos seis esforços de início
  (§5.4).
- **Barra geometricamente exata** (Simo e Reissner) na isoparamétrica de
  três nós. É a extensão natural da energia de barra que a 008d já usa nela
  para a rigidez geométrica, com a interpolação objetiva das rotações de
  Crisfield e Jelenić e o empenamento de Vlasov por penalidade, como na
  008d (§5.5).

**Rotações finitas:** cada nó guarda sua orientação (quatérnio). As
incógnitas globais de rotação são os vetores de rotação totais, aditivos.
Os momentos nodais mortos são conjugados a eles: conservativos, iguais aos
semitangenciais da 008d até a segunda ordem e exatos em torno de eixo fixo.
A tangente fica simétrica sem cargas seguidoras (§5.2).

**Caminho:** controle de carga, de deslocamento e de comprimento de arco
(Crisfield, cilíndrico), com passo adaptativo e corte de passo (§5.6). A
carga em duas fases aplica primeiro f_c por controle de carga e depois segue
λ (§5.7).

**Pontos críticos:**

- detecção pela contagem de pivôs negativos da LDLᵀ da tangente mais as
  contagens internas das barras exatas (008c/008d); com cargas seguidoras,
  pelo sinal do determinante da LU (§5.8);
- localização por secante no menor autovalor, com bissecção de reserva;
- classificação pelo modo contra a carga;
- troca de ramo perturbando pelo modo (§5.9).

A **flambagem a partir de um estado** é a da 008c/008d (ADR 0007) sobre a
geometria e os esforços do estado (§5.10).

**Cargas seguidoras** ficam nos eixos do elemento ou do nó deformado, com
rigidez de carga não simétrica, resolvida por `SparseLU` do Eigen, que não é
uma dependência nova (§5.11).

O resultado é o caminho de estados `StaticResult` mais as orientações, os
pontos críticos e o motivo da parada (§4).

## 2. Verificação da constituição

| Princípio | Atende? | Observação |
|-----------|---------|------------|
| Multiplataforma (§2) | Sim | C++20 e Eigen, nada de plataforma; o CI das quatro plataformas roda as suítes novas. As tolerâncias de pontos críticos e taxas são medidas na faixa (lição da T90 da 008d), não no último refinamento só. |
| GPU somente via QRhi (§3) | N/A | Nada de interface nem de render. |
| Camadas / `core` sem Qt (§4) | Sim | Tudo em `core` (REQ-008e-21); `check_no_qt_includes` cobre. A análise aceita `std::stop_token` para a 011, sem thread própria. |
| Correção numérica (§5) | Sim | Formas fechadas, integrais elípticas, a barra exata não linear da T24b e um tiro independente nos testes (NFR-008e-01); duas formulações independentes (corrotacional e exata) concordando com o refinamento; o limite de carga pequena contra a 008c/008d; testes de patch de rotação rígida (forças internas nulas sob movimento de corpo rígido, a objetividade). Tolerâncias explícitas em cada teste. |
| Simplicidade / dependências (§7) | Sim | Nenhuma dependência nova: `Eigen::SparseLU` (MPL-2.0, já no Eigen) para as tangentes não simétricas. O motor exato, a validação, a montagem, a contagem de Sturm e a flambagem existentes são reaproveitados. |

## 3. Módulos e arquivos afetados

| Módulo | Arquivo | Novo/Alterado | Responsabilidade |
|--------|---------|---------------|------------------|
| core | `include/fem/core/nonlinear_analysis.hpp` | novo | Opções, estados, pontos críticos, resultado; `runNonlinearStatic` e `runStateBuckling` (REQ-008e-01, 06 a 12, 17, 18, 22) |
| core | `nonlinear_analysis.cpp` | novo | O caminho: fases, controles, passo adaptativo, Newton, corte de passo, pontos críticos, troca de ramo, estados guardados |
| core | `detail/nonlinear_problem.hpp`, `nonlinear_problem.cpp` | novo | `NonlinearProblem`: o estado (posições, orientações, empenamento), resíduo e tangente, atualização, recuperação dos resultados; sobre `StaticProblem::prepare` |
| core | `detail/rotation.hpp`, `rotation.cpp` | novo | SO(3): exponencial, logaritmo, Tₛ(θ) e sua inversa, quatérnios, reescala do vetor de rotação |
| core | `corotational.hpp`, `corotational.cpp` | novo | Referencial do elemento de dois nós e do arco, deslocamentos locais, matrizes de transformação e rigidez do referencial (com empenamento); treliça corrotacional |
| core | `beam_state.cpp`, `include/fem/core/beam_state.hpp` | alterado | `localResponse`: forças locais e tangente consistente da barra exata a partir dos deslocamentos locais (o perfil dos esforços locais; diferenças finitas nos esforços de início) |
| core | `beam_iso.cpp`, `include/fem/core/beam_iso.hpp` | alterado | A isoparamétrica geometricamente exata: deformações de Simo-Reissner, interpolação objetiva, Wagner e monossimetria, empenamento, cargas seguidoras |
| core | `include/fem/core/linear_solver.hpp`, `linear_solver.cpp` | alterado | `SparseLu` (Eigen `SparseLU`, COLAMD): fatoração não simétrica, solução e sinal do determinante |
| core | `include/fem/core/assembler.hpp`, `assembler.cpp` | alterado | Montagem completa (não simétrica) além do triângulo inferior, com o mesmo padrão por coloração |
| core | `include/fem/core/model.hpp` | alterado | `follower` em `DistributedLoad` e `NodalForce`; `constant` em todas as cargas e recalques; `gravityConstant` (REQ-008e-14, 15, 19, 22) |
| core | `include/fem/core/beam_result.hpp`, `beam_result.cpp` | alterado | `BeamPoint::position` (posição deformada, global) e o referencial corrente nos dados (REQ-008e-17) |
| core | `static_analysis.cpp`, `second_order.cpp`, `buckling.cpp` | alterado | As marcas `follower` e `constant` ignoradas (cargas mortas e comuns), documentado (REQ-008e-20) |
| core | `model_validation.cpp` | alterado | Nenhuma mensagem nova prevista (§7); a validação comum vale |
| tests | `tests/unit/core/test_rotation.cpp` | novo | SO(3) |
| tests | `tests/unit/core/test_corotational.cpp` | novo | Referencial, objetividade, patch de corpo rígido, tangente contra diferenças finitas |
| tests | `tests/unit/core/test_nonlinear_analysis.cpp` | novo | AC-1 a AC-7 |
| tests | `tests/unit/core/nonlinear_reference.hpp` | novo | Elástica por integrais elípticas, tiro da barra exata não linear (o da T24b, em C++ no teste) |
| tests | `tests/perf/bench_frame.cpp` | alterado | `--nonlinear n`: n passos do caminho (NFR-008e-02) |
| specs | `decisions/0009-nao-linearidade-geometrica-de-barras.md` | novo | ADR das formulações |

## 4. Interfaces públicas

```cpp
// model.hpp — REQ-008e-14, 15, 19, 22 (as mudanças; o resto continua)
struct NodalForce
{
    // ... (008d)
    bool follower = false; // REQ-008e-15: turns with the node (non-linear analysis)
    bool constant = false; // REQ-008e-22: the constant phase
};
struct DistributedLoad
{
    // ... (008d)
    // REQ-008e-14: in the axes of the deformed bar (the given components
    // turn with it); per length of the undeformed bar.
    bool follower = false;
    bool constant = false;
};
// constant também em ConcentratedLoad, TemperatureChange, InitialStrain,
// TemperatureGradient e Restraint (o recalque); e no Model:
//     bool gravityConstant = false;

// beam_result.hpp — REQ-008e-17
struct BeamPoint
{
    // ... (008d)
    // The position of the centroid axis, global; in the small-displacement
    // analyses the undeformed position plus the displacement.
    Eigen::Vector3d position = Eigen::Vector3d::Zero();
};
// BeamResult::Data ganha `origin` (a posição corrente do início do
// comprimento flexível); `axes` passa a ser o referencial corrente.

// linear_solver.hpp — REQ-008e-16
class SparseLu final
{
public:
    [[nodiscard]] Expected<void> factorize(const SparseMatrix& full);
    [[nodiscard]] Expected<Eigen::VectorXd> solve(const Eigen::VectorXd& rhs) const;
    [[nodiscard]] int determinantSign() const; // +1 or -1
    [[nodiscard]] std::optional<std::int32_t> singularEquation() const;
};

// nonlinear_analysis.hpp
namespace fem::core {

enum class PathControl : std::uint8_t { Load, Displacement, ArcLength };

// REQ-008e-08
enum class PathStop : std::uint8_t {
    TargetLoad, TargetDisplacement, MaxSteps, CriticalPoint, Failed, Cancelled
};

struct NonlinearOptions
{
    StaticOptions base{};
    PathControl control = PathControl::ArcLength;
    // Displacement control and target: the node (external id), the dof.
    std::optional<ExternalId> node;
    DofType dof = DofType::U;
    double firstStep = 0.1;   // Δλ (load, arc length: the first one) or Δu
    double minStep = 1e-6;    // relative to firstStep
    double maxStep = 10.0;    // relative to firstStep
    int desiredIterations = 4;
    double targetLoad = 1.0;  // λ
    std::optional<double> targetDisplacement;
    int maxSteps = 200;
    int maxIterations = 20;   // per step
    double residualTolerance = 1e-8;     // relative to the load vector
    double displacementTolerance = 1e-8; // relative to the step
    bool stopAtCritical = false;
    double criticalTolerance = 1e-8;     // relative, on λ
    bool switchBranch = false;            // REQ-008e-11
    int branchSide = +1;                  // +1 or -1 along the mode
    int constantSteps = 10;               // REQ-008e-22: the first phase
    int keepEvery = 1;                    // the states kept
    std::stop_token stop{};
};

// REQ-008e-17: a converged state of the path.
struct PathState
{
    int phase = 2;       // 1: the constant part being applied
    double factor = 0.0; // λ (in phase 1, the fraction of the constant part)
    StaticResult result; // rotations: rotation vectors, global (|θ| <= π);
                         // 2D: the total angle about z
    std::vector<Eigen::Quaterniond> orientations; // per node, 3D beams
    int iterations = 0;
};

enum class CriticalKind : std::uint8_t { Limit, Bifurcation, InsideBar };

// REQ-008e-10
struct CriticalPoint
{
    CriticalKind kind = CriticalKind::Limit;
    int phase = 2;
    double factor = 0.0;
    PathState state;
    BucklingMode mode; // REQ-008c-11 normalization
    ElementIndex element = -1; // InsideBar
    bool followed = false;     // REQ-008e-11: the path switched branch here
};

struct NonlinearResult
{
    std::vector<PathState> states;
    std::vector<CriticalPoint> critical;
    PathStop stop = PathStop::MaxSteps;
    std::string message; // REQ-008e-18; the flutter note of REQ-008e-16
};

// REQ-008e-01 to 11, 14 to 18, 22: fails only with the issues of validate()
// or a mechanism of the initial state; any later failure returns the path
// so far with stop = Failed and the message.
[[nodiscard]] Expected<NonlinearResult> runNonlinearStatic(const Model& model,
                                                           const NonlinearOptions& options = {});

// REQ-008e-12: the linearized buckling from a converged state of a path of
// the same model: the variable part times μ on top of the state.
[[nodiscard]] Expected<BucklingResult> runStateBuckling(const Model& model, const PathState& state,
                                                        const BucklingOptions& options = {});

} // namespace fem::core
```

Internamente:

```cpp
// detail/nonlinear_problem.hpp
class NonlinearProblem
{
public:
    static Expected<std::unique_ptr<NonlinearProblem>> prepare(const Model&, const StaticOptions&);
    // The state: free dofs as increments on top of the stored configuration.
    struct State { Eigen::VectorXd values; std::vector<Eigen::Quaterniond> orientations; };
    Expected<void> evaluate(const State&, double constantFactor, double variableFactor,
                            Residual& out) const;    // r, K_T (sym. or full), counts
    void update(State&, const Eigen::VectorXd& increment) const; // multiplicative rotations
    Expected<PathState> recover(const State&, int phase, double factor) const;
};
```

## 5. Dados e algoritmos

### 5.1 As formulações (ADR 0009; REQ-008e-02, 03)

| Elemento | Formulação | No referencial |
|----------|------------|----------------|
| Treliça 2D/3D | Corrotacional exata (Crisfield): N = EA·(l − l0)/l0 | — |
| Viga reta 2D/3D (EB, Timoshenko; ligações, molas, excentricidades, base elástica; empenamento) | Corrotacional (Battini e Pacoste, 2002) | A barra exata da 008c/008d com o perfil dos esforços locais |
| Arco exato (`Line3`, nó do meio sem GDL) | Corrotacional, o referencial pela rotação de corpo rígido do arco inicial | O arco exato com o perfil dos esforços locais |
| Isoparamétrica (`Line3`, três nós) | Barra geometricamente exata (Simo e Reissner; empenamento de Simo e Vu-Quoc por penalidade) | — |

Nas excentricidades rígidas, o braço gira com o nó: a ponta flexível fica
em x_nó + R_nó·e. Elas entram na transformação, e não mais como o termo de
segunda ordem da 008c. A base elástica fica nos eixos locais do elemento
(Winkler que acompanha a barra), na barra exata local.

### 5.2 Rotações finitas e momentos (REQ-008e-02, 15)

- Cada nó de viga 3D guarda a orientação Rₙ (quatérnio unitário). As
  incógnitas de rotação das iterações são vetores de rotação **totais**
  θₙ (Rₙ = exp(θₙ)·R₀ₙ), atualizados de forma aditiva na iteração e
  multiplicativa na orientação. A tangente em θ inclui Tₛ(θ) (Battini e
  Pacoste), o que a deixa simétrica com cargas conservativas.
- Quando |θₙ| passa de π ao fim de um passo convergido, θₙ é reescalado
  para θₙ·(1 − 2π/|θₙ|): a mesma rotação, sem a singularidade de Tₛ em 2π.
- **Momento morto:** um vetor fixo M conjugado de θₙ, com trabalho M·θₙ.
  É conservativo. Até a segunda ordem coincide com o semitangencial da
  008d, porque os parâmetros semitangenciais 2·tan(|θ|/2)·e ≈ θ + O(θ³);
  em torno de eixo fixo é exato. Não pede rigidez de carga. A reescala
  muda o potencial por uma constante em torno de eixo fixo; em geral, o
  teste do pórtico de Argyris e o caso 3D do AC-1 conferem.
- **2D:** a rotação é o ângulo total em torno de z, aditivo e sem limite
  (o círculo de 2π do AC-1 chega a 2π).
- **Empenamento:** o ψ nodal é aditivo (Battini e Pacoste).
- **Resultados:** `rotations` traz o vetor de rotação total (log de
  Rₙ·R₀ₙᵀ, |θ| ≤ π; 2D: o ângulo total) e `orientations` traz os
  quatérnios.

### 5.3 O elemento corrotacional (REQ-008e-03, 04)

- **Referencial** (Battini e Pacoste, 2002):
  - e₁ é a corda entre as pontas flexíveis correntes;
  - e₂ e e₃ vêm do eixo local y médio das duas pontas, q = ½(Rₐ·q₀ +
    R_b·q₀), projetado normal a e₁;
  - no arco, o referencial inicial é o do arco da 008b e a corda é a dele.
- **Deslocamentos locais:**
  - ū = l − l₀;
  - θ̄ₐ e θ̄_b = log(R_rᵀ·Rₙ·R₀ₙ_local);
  - ψ̄ = ψ;
  - na treliça, só ū.
- **Forças globais e tangente:** f = Bᵀ·f_l e K = Bᵀ·K_l·B + K_ref(f_l),
  com a rigidez do referencial K_ref das expressões de Battini e Pacoste,
  mais Tₛ(θ). O empenamento entra nas duas sem termo de referencial (o
  bimomento é escalar).
- **Testes de objetividade e de patch** (`test_corotational.cpp`):
  - forças internas nulas sob movimento de corpo rígido finito qualquer;
  - a tangente contra diferenças finitas de f, em 1e−6.

### 5.4 O elemento local: a barra exata (REQ-008e-03, 04, 05)

- **Forças locais:** f_l(d) = K(p)·d + f₀(p). K e f₀ são a matriz e as
  cargas da barra exata da 008c/008d com o perfil de referência p. O perfil
  vem da estática exata a partir dos seis esforços locais de início s, mais
  as cargas do vão nos eixos locais. Na barra reta é exato pela estática;
  no arco, amostrado como na 008d.
- **Ponto fixo:** s precisa ser o esforço de início que a própria barra
  devolve, s = S(d, s). A iteração global de Newton faz esse ponto fixo:
  cada avaliação toma s da iteração anterior, e o resíduo global só
  converge quando s se repete. Não há laço interno.
- **Tangente consistente:** K_l = K(p) + (∂f_l/∂s)·(I − ∂S/∂s)⁻¹·(∂S/∂d).
  As derivadas em s são diferenças finitas progressivas nos seis esforços
  (três no 2D), com passo relativo 1e−7: são seis avaliações do motor por
  elemento, além da principal.
  - **Por que é preciso:** sem esse termo, o Newton converge linearmente e
    a singularidade da tangente se desloca na ordem das rotações locais. A
    T24b mostrou que, no balanço, essa ordem é a do efeito procurado.
  - Na flambagem a partir de reto (Euler), d de flexão é nulo, o termo
    some e o ponto crítico coincide com a 008c.
- **Limite de carga pequena (REQ-008e-05):** com λ → 0, f_l → K(0)·d, que
  é a 008 a 008d. A segunda ordem da 008d é o primeiro passo do ponto fixo
  com o referencial fixo. O teste confere a concordância até a ordem que o
  P-δ representa: diferença O(λ²·rotação), medida com dois níveis de carga.
- **Contagem interna:** os pivôs negativos internos da barra exata (as
  partes da 008c/008d) entram na contagem de Sturm (§5.8), como antes.

### 5.5 A isoparamétrica geometricamente exata (REQ-008e-03, 04)

- **Cinemática e interpolação:**
  - posição x(ξ) quadrática pelos três nós;
  - rotações pela interpolação objetiva de Crisfield e Jelenić: um triad
    de referência no nó do meio e as rotações locais relativas a ele
    interpoladas;
  - ψ quadrático, como na 008d.
- **Deformações (Simo e Reissner):** Γ = Rᵀ·x' − e₁ e K = axial(Rᵀ·R').
  As deformações iniciais da geometria curva são descontadas.
- **Energia:**
  - ½·Γᵀ·C_Γ·Γ + ½·Kᵀ·C_K·K, com os termos da seção da 008d (centro de
    cisalhamento, áreas de cisalhamento);
  - o termo de Wagner e de monossimetria pela deformação axial da fibra
    com ½·r²·K₁² (Pi e Trahair; Battini e Pacoste): ½·EA·r₀²·Γ₁·K₁² mais
    ½·EI_r·K₁⁴, com βy e βz pelo mesmo caminho;
  - o empenamento de Vlasov com a penalidade da 008d.
- **Integração:** reduzida seletiva, como na 008b.
- **Verificações:** no limite de carga pequena, a segunda variação da
  energia é a rigidez geométrica da 008d (teste de igualdade em 1e−10);
  contra o arco corrotacional, convergindo com o refinamento.
- **Cargas seguidoras no vão:** nos eixos do triad interpolado.

### 5.6 O caminho (REQ-008e-06 a 09)

- **Preditor:** tangente na solução convergida, Δu = Δλ·K_T⁻¹·f_v.
- **Controle de carga:** Δλ fixo no passo.
- **Controle de deslocamento:** Δuᵢ fixo no GDL escolhido, pela partição
  de Batoz e Dhatt.
- **Comprimento de arco (Crisfield, cilíndrico):**
  - restrição ‖Δu‖² = Δs², só nos deslocamentos, adimensionalizados por
    ‖u‖ do primeiro passo; rotações e empenamento com o mesmo peso, pela
    escala do primeiro passo;
  - **sentido do preditor:** o sinal de Δu_prev·Δu_pred (Crisfield). Ele
    não muda no ponto limite de carga, e o caminho atravessa
    (REQ-008e-09);
  - **raiz do corretor:** a de menor ângulo com o incremento anterior;
    raízes complexas cortam o passo.
- **Newton:**
  - converge quando ‖r‖ ≤ tol_r·‖f_ref‖ e ‖δu‖ ≤ tol_u·‖Δu‖ (padrões 1e−8),
    com f_ref = f_c + max(1, |λ|)·f_v;
  - limite de iterações por passo (padrão 20);
  - a divergência é detectada quando ‖r‖ cresce três iterações seguidas.
- **Passo adaptativo:** Δs ← Δs·√(n_desejado/n_anterior), entre
  minStep·first e maxStep·first. Se o passo falha, Δs ← Δs/4, a partir do
  estado convergido. Abaixo do mínimo: falha (REQ-008e-18).
- **Parada:** λ alvo, deslocamento alvo, passos máximos, ponto crítico ou
  cancelamento. O último passo é encurtado para cair no alvo exato (por
  secante e corretor com o controle de carga).

### 5.7 Carga em duas fases (REQ-008e-22)

- Cada carga marcada `constant` vai para f_c, inclusive peso próprio, ΔT,
  pré-deformação, gradiente e recalque; o resto vai para f_v.
- **Fase 1:** controle de carga em η de 0 a 1, com r = f_int − η·f_c,
  constantSteps passos iniciais (adaptativo) e a detecção de pontos
  críticos.
  - Se não chega a η = 1: falha, com o caminho até ali e a mensagem "the
    constant part of the load alone cannot be carried (η = …)".
  - Um ponto limite de f_c sozinho é essa falha.
- **Fase 2:** o controle escolhido em λ, com f_c inteiro.
- **Sem marcas:** só a fase 2.

### 5.8 Pontos críticos (REQ-008e-10, 16)

- **Contagem:**
  - J(estado) = pivôs negativos da LDLᵀ indefinida da tangente simétrica
    (`factorizeIndefinite`) mais Σ contagens internas das barras exatas,
    como na 008c/008d;
  - com cargas seguidoras, a tangente é não simétrica: o sinal de det(K_T)
    da LU mais Σ contagens internas (mod 2) acusa a passagem de um número
    ímpar de autovalores reais por zero.
- **Detecção:** J (ou o sinal) muda entre dois estados convergidos.
- **Localização:** entre os dois estados, no parâmetro do controle (Δs ou
  Δλ), por secante sobre o menor autovalor em módulo de K_T, obtido por
  iteração inversa com a fatoração. A bissecção é a reserva, até
  |Δλ| ≤ criticalTolerance·|λ|.
- **Modo:** o vetor nulo φ, pela iteração inversa no ponto; normalizado
  como no REQ-008c-11.
- **Classificação:**
  - **limite** se |φᵀ·f_v| > 1e−3·‖φ‖·‖f_v‖;
  - **bifurcação** em caso contrário;
  - **dentro da barra** quando quem muda é só a contagem interna de um
    elemento (os nós parados; φ nodal nulo). Esse não troca de ramo: a
    mensagem sugere subdividir a barra.
- **Cargas seguidoras:** a análise acrescenta à mensagem "static analysis:
  flutter of the non-conservative loads is not checked" (REQ-008e-16).
- **Ajuste de spec proposto (REQ-008e-16):** "contar os pivôs negativos"
  passa a ser "detectar pelo sinal do determinante" na tangente não
  simétrica. A inércia não existe ali (a LU não dá a contagem), e um par
  de autovalores complexos que cruzam o eixo é o flutter, que fica fora.

### 5.9 Troca de ramo (REQ-008e-11)

- No ponto de bifurcação localizado (u_c, λ_c, φ), o primeiro passo do
  ramo secundário parte de u_c + σ·ξ·φ, com σ = branchSide e
  ξ = 0,1·Δs·‖Δu‖/‖φ‖.
- O corretor usa a restrição ortogonal à tangente primária (Wagner e
  Wriggers, 1988), e o caminho segue com o controle escolhido.
- Na bifurcação simétrica (Euler), os dois lados dão ramos espelhados; na
  assimétrica, lados diferentes.
- O ponto fica com `followed = true`.

### 5.10 Flambagem a partir de um estado (REQ-008e-12)

- **Pencil:** com a geometria, as orientações e os esforços do estado
  fixos, K(μ) é a tangente corrotacional e exata com os perfis
  p(μ) = p_estado + μ·Δp. Δp são os esforços da solução tangente
  K_T·Δu = f_v (os incrementos por unidade de carga variável), e
  K_ref(f_estado + μ·Δf) é linear em μ.
- **Busca:** a mesma do ADR 0007 (pencil quadrático, contagem de Sturm,
  refinamento, fatores perdidos), sobre esse K(μ).
- **Estado inicial:** p_estado = 0 e K_ref = 0, e o resultado é a
  `runLinearBuckling` da 008c/008d (teste em 1e−10).
- **Cargas seguidoras:** a busca recusa com a mensagem de que a flambagem
  linearizada pede uma tangente simétrica. O caminho continua sendo a
  referência para elas.

### 5.11 Cargas seguidoras (REQ-008e-14, 15, 16)

- **No vão:** as componentes dadas (locais, ou globais na configuração
  inicial) ficam fixas no referencial corrente do elemento. As cargas
  nodais equivalentes da barra exata local são transformadas por Bᵀ, e a
  rigidez de carga é a derivada de Bᵀ·f_eq em relação ao referencial: não
  simétrica.
  - **Por comprimento indeformado.** Com deformação pequena, a diferença
    para o deformado é O(ε). O anel do AC-5 é inextensível na prática, e a
    forma fechada 3EI/R³ vale para as duas medidas.
- **Nodais:** F = Rₙ·R₀ₙᵀ·F₀ e M = Rₙ·R₀ₙᵀ·M₀, com o trabalho em δθ e a
  rigidez de carga ∂(Tₛᵀ·M)/∂θ.
- **Tangente:** na presença de qualquer seguidora, a montagem é completa e
  a fatoração é a `SparseLu`.

### 5.12 Custo (NFR-008e-02)

Medido em 2026-11-22, Release, na máquina do mantenedor, no pórtico
espacial da 008 (4961 nós, 13640 barras, 29040 GDL livres), com
`bench_frame --cost`:

| Medida | Sem empenamento | Com empenamento |
|--------|-----------------|-----------------|
| Análise linear | 3,303 s | 5,345 s |
| Motor exato em todas as barras, sequencial | 0,226 s | 0,255 s |
| Montagem exata + LDLᵀ indefinida + solução | 3,09 s | 3,15 s |
| Uma solução com a fatoração | 0,021 s | 0,020 s |

- **Uma iteração de Newton** ≈ motor ×7 (a tangente consistente da §5.4)
  mais a referência corrotacional e a fatoração. Isso dá ≈ 1,6 s de
  montagem sequencial, ≈ 0,3 s em paralelo, mais ≈ 2,9 s de fatoração:
  1,0 a 1,4 vez a linear.
- **Um passo** de ~4 iterações ≈ 4 a 6 vezes a linear.

**Metas propostas** (a tarefa de custo confirma, como na 008d):

- **Um passo convergido**, com a contagem de pivôs: ≤ 8 vezes a análise
  linear do mesmo modelo, sem empenamento (3,3 s), e ≤ 8 vezes a linear
  sem empenamento com empenamento.
- **Um caminho de 20 passos** sem ponto crítico: ≤ 150 vezes a linear.
- **A localização de um ponto crítico:** ≤ 15 passos equivalentes.
- **As análises existentes:** as metas da 008b, 008c e 008d continuam, e a
  tarefa de custo as mede de novo.

## 6. Fluxo

```
runNonlinearStatic(model, options)
 ├─ NonlinearProblem::prepare        validate (008d) → issues → Expected error
 │    └─ f_c, f_v (marcas constant), seguidoras, contagens internas
 ├─ fase 1 (se f_c ≠ 0): controle de carga η 0→1
 └─ fase 2: controle escolhido em λ
      laço de passos
       ├─ preditor (sinal, Δs)
       ├─ Newton: evaluate → r, K_T (LDLᵀ | LU) → corretor → update
       │    └─ falhou: Δs/4, de novo; < mínimo → stop = Failed, mensagem
       ├─ convergido: contagem J; guarda o estado (keepEvery)
       ├─ J mudou: localizar → classificar → CriticalPoint
       │    ├─ stopAtCritical → stop = CriticalPoint
       │    └─ switchBranch e bifurcação → perturbação → ramo secundário
       ├─ alvo atingido → passo final no alvo → stop
       └─ stop_token → stop = Cancelled
runStateBuckling(model, state)
 └─ NonlinearProblem no estado → pencil K(μ) → busca do ADR 0007
```

## 7. Tratamento de erros

| Situação | Comportamento | Requisito |
|----------|---------------|-----------|
| Problemas de `validate()` | Erro com as mensagens (uma por linha), sem caminho | REQ-008e-19 |
| Mecanismo no estado inicial | Erro com o nó e a direção (como a 008c) | REQ-008e-18 |
| Passo mínimo sem convergência | Caminho até ali, `stop = Failed`, "the step from λ = … did not converge with the smallest step (… after … iterations)" | REQ-008e-18 |
| Tangente singular fora de ponto crítico (fatoração falha e o menor autovalor não troca de sinal) | Caminho até ali, `Failed`, com o nó e a direção da equação, ou o elemento | REQ-008e-18 |
| Fase 1 não chega a η = 1 | Caminho até ali, `Failed`, "the constant part of the load alone cannot be carried (η = …)" | REQ-008e-22 |
| Bifurcação dentro de uma barra com troca de ramo pedida | Ponto `InsideBar`, sem troca, nota "subdivide element … to follow the branch" | REQ-008e-11 |
| Cargas seguidoras | Nota do flutter na mensagem do resultado | REQ-008e-16 |
| `runStateBuckling` com seguidoras | Erro "the linearized buckling needs a symmetric tangent: follower loads are present" | REQ-008e-12 |
| `runStateBuckling` sem fator positivo | Como o REQ-008c-12 | REQ-008e-12 |
| `node` do controle de deslocamento inexistente ou sem o GDL | Erro antes do caminho, com o id e o GDL | REQ-008e-06 |
| Cancelamento | Caminho até ali, `Cancelled` | REQ-008e-08 |

A marcação seguidora não pede mensagem nova: uma seguidora numa treliça ou
um momento seguidor num nó sem rotações já caem nas mensagens da 008 (carga
de vão em treliça, momento sem rotação).

## 8. Estratégia de testes

| Teste | Tipo | Requisito | Referência/tolerância |
|-------|------|-----------|-----------------------|
| SO(3): exp/log, Tₛ, reescala, quatérnios | unitário | REQ-008e-02 | identidades em 1e−14; Tₛ contra diferenças finitas 1e−8 |
| Patch de corpo rígido e objetividade (2D, 3D, arco, isoparamétrica, com excentricidade e empenamento) | unitário | REQ-008e-02, 03 | forças internas ≤ 1e−10·EA |
| Tangente consistente contra diferenças finitas do resíduo (todos os elementos, com e sem seguidoras) | unitário | REQ-008e-03, 04, 16 | 1e−6 |
| Limite de carga pequena: linear e segunda ordem 008c/008d | verificação | REQ-008e-05 | 1e−8 na linear com λ = 1e−6 escalada; segunda ordem: diferença de ordem λ² medida em dois níveis |
| Balanço enrolado em círculo (M = 2πEI/L), 2D e 3D, corrotacional e isoparamétrica | verificação | REQ-008e-01, 02 | forma fechada; 1e−6 com malha documentada |
| Elástica do balanço com força na ponta (integrais elípticas, no teste), vários níveis | verificação | REQ-008e-01, 03 | 1e−5, taxa sobre a faixa |
| Curva de 45° de Bathe e Bolourchi | verificação | REQ-008e-01, 03 | valores publicados, 3 algarismos |
| Treliça de von Mises (snap-through, ponto limite analítico) | verificação | REQ-008e-06, 09, 10 | forma fechada 1e−6; controle de carga para e o de arco atravessa |
| Pórtico de Lee e de Williams | verificação | REQ-008e-09 | publicados (Battini 2002, Crisfield), 0,5% |
| Controle de deslocamento, alvos, motivo da parada, estados guardados | unitário | REQ-008e-06, 08, 17 | exatos |
| Pilar de Euler: bifurcação na carga da 008c, troca de ramo, pós-flambagem contra a elástica | verificação | REQ-008e-10, 11, 05 | 1e−8 no ponto; 1e−5 na elástica |
| Bifurcação dentro da barra (nós parados) | unitário | REQ-008e-10 | classificada `InsideBar` |
| Flambagem a partir do estado inicial = `runLinearBuckling`; num estado adiante, perto do ponto crítico do caminho | verificação | REQ-008e-12 | 1e−10; a tendência para o ponto crítico (estimativa melhora com o estado) |
| Balanço da T24b (EIy/EIz = 0,05) contra a barra exata não linear (tiro de Newton no teste, como na T24b) | verificação | REQ-008e-13, 04 | 1e−5 com refinamento (o efeito é 0,7% a 3,7%) |
| Viga I biapoiada com garfos, momento uniforme, com e sem empenamento; EIy/EIz → 0 tende à 008d | verificação | REQ-008e-04, 13 | 1e−4 no limite |
| Altura da carga com grandes rotações | verificação | REQ-008e-04 | contra a isoparamétrica e o tiro |
| Anel sob pressão hidrostática seguidora (3EI/R³) e com carga morta (diferente) | verificação | REQ-008e-14, 16 | 1e−5 |
| Arco sob pressão seguidora; balanço com força seguidora na ponta; momento nodal seguidor | verificação | REQ-008e-14, 15 | tiro independente no teste (forma fechada onde houver), 1e−5 |
| Arco circular profundo com carga concentrada (limite e bifurcação) | verificação | REQ-008e-03 | publicado (DaDeppo e Schmidt), 0,5%; arco e isoparamétrica concordando com taxa sobre a faixa |
| Carga em duas fases: viga-coluna, pórtico, fase 1 impossível, flambagem só na parte variável | verificação | REQ-008e-22 | viga-coluna 008c no limite pequeno 1e−6; elástica adiante 1e−5 |
| Falhas: passo mínimo, tangente singular, cancelamento | unitário | REQ-008e-18 | mensagens |
| Marcas ignoradas nas análises antigas | unitário | REQ-008e-20 | resultados iguais em 1e−14 |
| Suítes 007 a 008d | regressão | REQ-008e-20 | inalteradas |
| `bench_frame --nonlinear 20` | benchmark | NFR-008e-02 | metas da §5.12 |

## 9. Alternativas consideradas

| Alternativa | Por que foi descartada |
|-------------|------------------------|
| Barra geometricamente exata em todos os elementos | As vigas retas do modelo são `Line2`. Um elemento linear exato trava, e só a isoparamétrica tem nó do meio. Perderia a exatidão por barra do P-δ e da flexo-torção que a 008c/008d construíram, e pediria várias divisões por barra para o que hoje é exato com uma. |
| Corrotacional com elemento local linear (ou cúbico raso) | É o clássico, mas pede 4 a 8 elementos por barra para o P-δ e a flexo-torção (Battini e Pacoste). O elemento local exato já existe e custa pouco (§5.12). |
| Corrotacional em todos, inclusive a isoparamétrica | O elemento de três nós com GDL no meio não tem referencial natural de corda, e ficariam duas formulações parecidas. A exata na isoparamétrica dá uma verificação independente. |
| Tangente local sem a variação do perfil (quase Newton) | Converge para o mesmo equilíbrio, mas linearmente, e desloca os pontos críticos na ordem das rotações locais (§5.4). |
| Fréchet da exponencial de matriz (analítica) para ∂K/∂s | A matriz em blocos dobra o tamanho e custa cerca de 8 vezes por direção. As diferenças finitas custam 1 vez por direção e dão 1e−8 de precisão na tangente, suficiente para Newton e contagem. |
| Lagrangiana total | Mesma questão das rotações finitas, sem reaproveitar o elemento exato. |
| Momentos mortos pela rotação espacial (spin) | Não são conservativos e deixam a tangente não simétrica mesmo sem cargas seguidoras: perderia a contagem de Sturm. |
| Comprimento de arco esférico (com λ na restrição) | O cilíndrico é mais robusto quando ‖f_v‖ e ‖u‖ têm escalas diferentes (Crisfield); o esférico fica para depois, se um caso pedir. |
| Pivôs negativos por uma LDLᵀ da parte simétrica nas tangentes não simétricas | Não é a estabilidade do sistema não simétrico. O sinal do determinante é o critério certo de divergência. |

## 10. Riscos e mitigação

- **Custo da tangente consistente** (6 avaliações extras do motor por
  elemento). A §5.12 mede 1,0 a 1,4 vez a linear por iteração. Se a tarefa
  de custo passar da meta:
  - derivadas só em N nas barras sem momentos de referência relevantes
    (|M| e |T| abaixo de 1e−6 da escala);
  - reuso de ∂S/∂s entre iterações de um passo.
- **Reescala do vetor de rotação** e a conservatividade dos momentos em 3D
  geral. Mitigação: o caso 3D do AC-1, o pórtico de Argyris e um teste de
  trabalho em ciclo fechado (o trabalho do momento morto num caminho
  fechado de rotações deve ser nulo, em 1e−10).
- **Classificação limite × bifurcação** sensível ao limiar de |φᵀ·f_v|.
  Mitigação: o limiar é relativo, e os testes cobrem os casos de Euler, da
  treliça de von Mises e do arco profundo (que tem os dois).
- **Pontos críticos múltiplos ou próximos** (simetria). A contagem dá o
  salto certo; a localização trata um ponto de multiplicidade k como um
  só, com k modos (iteração em subespaço de k vetores).
- **Determinante com seguidoras** perde pares de autovalores reais que
  cruzam juntos. Mitigação: documentado; o passo adaptativo limita Δλ perto
  de mudanças rápidas do menor autovalor.
- **Diferenças de plataforma** nas taxas e nos pontos críticos (a lição da
  T90). Mitigação: as taxas são medidas sobre a faixa, e as tolerâncias dos
  testes ficam uma ordem acima do ruído medido no WSL antes do CI.
- **Tamanho da feature.** As tarefas vão em fases que se sustentam
  sozinhas:
  1. SO(3) e treliça;
  2. viga reta corrotacional;
  3. caminho e pontos críticos;
  4. arco e isoparamétrica;
  5. empenamento;
  6. seguidoras e duas fases;
  7. flambagem do estado.
  Cada uma com as suítes verdes.

## 11. Novas dependências

Nenhuma. `Eigen::SparseLU` (MPL-2.0) já faz parte do Eigen do projeto.
