# Design — 008d beam-warping

Status: aprovado · Data: 2026-11-18
Requisitos: [requirements.md](requirements.md)

## 1. Resumo da abordagem

Tudo em `core`, sem Qt, sobre o motor exato de espaço de estados (008b), a
segunda ordem e a flambagem (008c). A ordem interna das tarefas segue a dos
requisitos: primeiro o empenamento na estática linear, depois a
flexo-torção.

1. **Empenamento de volta ao `Model`.** O commit `b756147` (T21b da 008b)
   retirou do modelo o que já estava feito e testado. Esta feature parte do
   estado anterior a ele (`c19d42b`) para as interfaces e o caminho da
   análise:
   - `Assignment::warping`, `WarpingEnd`, `DofType::Warp`;
   - o bimomento nodal e a restrição de empenamento;
   - o empenamento e o bimomento nos resultados.
   O núcleo (motor e elemento exato com pontas livres ou impedidas) nunca
   saiu. Nesse caminho entram:
   - a mola de empenamento: a ponta livre já é um GDL interno condensado,
     e a mola é a rigidez dele (§5.2);
   - o empenamento na isoparamétrica, como a 008b planejava: ψ quadrático
     e independente, com a restrição de Vlasov por penalidade (§5.3).
2. **Estado de referência completo.** O `AxialProfile` da 008c (só N)
   passa a `ReferenceProfile`, com os seis esforços (N, Vy, Vz, T, My,
   Mz) nos pontos de Gauss das partes e o fator das cargas (§5.4). Ele
   fica fora do `BeamData`, num `shared_ptr<const>`, para não multiplicar a
   memória dos dados de barra (§5.4).
3. **Flexo-torção por segunda variação da energia** (ADR 0008). Os termos
   novos saem de um funcional quadrático de Vlasov-Trahair:
   - o acoplamento do giro com os momentos fletores;
   - o termo de Wagner N·r0² e os de monossimetria βy·My e βz·Mz;
   - a excentricidade do centro de cisalhamento;
   - a altura da carga.
   As equações de Euler-Lagrange desse funcional entram na matriz A do
   motor, e a rigidez da barra continua **simétrica**. Isso mantém a
   fatoração LDLᵀ, a contagem de Sturm e o gradiente conjugado da 008c.
   Os momentos nodais passam a ser semitangenciais (a convenção conservativa
   desse funcional), e isso fica documentado. Com N, momentos e cargas
   constantes na barra, A é constante e a barra é exata; quando variam, o
   Magnus de 4ª ordem da 008c integra.
4. **Isoparamétrica**: a rigidez geométrica é a segunda variação do mesmo
   funcional em componentes globais, integrada com 3 pontos (§5.3).
5. **Contagem de Sturm**: a regra de partes da 008c (cada parte longe da
   sua própria flambagem com as pontas presas) ganha os limites de torção,
   de flexo-torção e da altura da carga (§5.6).
6. **Segunda ordem e flambagem**: os mesmos laços da 008c, agora sobre o
   perfil completo. Na segunda ordem, o ponto fixo de Anderson itera os
   seis esforços. Na flambagem, todos os esforços e as cargas escalam com λ.
7. **Seções**: r0² e os coeficientes de monossimetria βy e βz, exatos por
   integrais de polígono nas formas e na poligonal e de linha média nas
   paredes finas, e opcionais na genérica (§5.7).

**Ajustes nos requisitos** (aprovados junto com este design e aplicados em `requirements.md`):

- **NFR-008d-02:** o requisito pede medir também a segunda ordem e a
  flambagem com empenamento, mas elas só existem depois da flexo-torção. A
  medida feita agora (§5.9) é a da análise linear com empenamento, no
  código da 008b que já a tinha. As metas propostas para a segunda ordem e
  a flambagem são razões sobre essa linear. Uma tarefa cedo mede o custo
  real e para, se passar, como a T03 da 008c. Texto proposto: "O design
  DEVE medir a análise linear com empenamento em todas as vigas e propor
  as metas da linear, da segunda ordem e da flambagem; a tarefa de custo
  as confirma antes da flexo-torção completa."
- **REQ-008d-19:** a flexo-torção sempre ativa muda também o custo da
  segunda ordem e da flambagem **sem** empenamento. Uma carga uniforme dá
  momento parabólico, e com isso as vigas do benchmark passam ao Magnus.
  A tarefa de custo mede esse efeito. Se as metas da 008c (4× e 12×)
  deixarem de valer, o mantenedor decide entre otimizar e rever a meta.
  Nada muda no texto do requisito; o risco fica registrado no §10.

## 2. Verificação da constituição

| Princípio | Atende? | Observação |
|-----------|---------|------------|
| Multiplataforma (§2) | Sim | C++20 e Eigen; nada específico de sistema |
| GPU somente via QRhi (§3) | Sim | Nada de GPU |
| Camadas / `core` sem Qt (§4) | Sim | Tudo em `core` (REQ-008d-20) |
| Correção numérica (§5) | Sim | Soluções analíticas: torção mista em senh/cosh, Mcr com momento uniforme (com e sem empenamento, I monossimétrico com βz), Pcr,T e flexo-torção de pilares monossimétricos, mola de empenamento. Convergência: isoparamétrica contra o arco exato, e arco contra a cadeia de barras retas exatas (Richardson). Patch: barra exata subdividida igual à inteira; K simétrica em 1e−12. Referências tabeladas (C1, C2, arcos) com tolerância explícita |
| Qualidade (§6) | Sim | Cobertura de `core` ≥ 80%; `compute` sem alocação (o perfil é montado antes da montagem e lido por ponteiro constante) |
| Simplicidade / dependências (§7) | Sim | Nenhuma dependência nova. Um funcional só gera os termos do motor exato e da isoparamétrica; o empenamento reaproveita o código da 008b que já estava testado |

## 3. Módulos e arquivos afetados

| Módulo | Arquivo | Novo/Alterado | Responsabilidade |
|--------|---------|---------------|------------------|
| core | `include/fem/core/dof_map.hpp`, `dof_map.cpp` | alterado | `DofType::Warp` (8 tipos) |
| core | `include/fem/core/model.hpp` | alterado | `Assignment::warping`, `WarpingEnd`, bimomento nodal, ponto de aplicação das cargas no vão |
| core | `include/fem/core/section.hpp`, `section.cpp`, `section_thin_walled.cpp`, `section_solid.cpp` | alterado | r0², βy e βz; βy e βz opcionais na genérica; momento setorial nos pontos (τw) |
| core | `include/fem/core/beam_element.hpp` | alterado | `ReferenceProfile` no lugar de `AxialProfile`; molas e continuidade de empenamento por ponta; propriedades de flexo-torção em `BeamData` |
| core | `src/core/beam_state.cpp`, `include/fem/core/beam_state.hpp` | alterado | termos de flexo-torção em A; altura da carga (distribuída em A, concentrada como salto); regra de partes ampliada |
| core | `src/core/beam_exact.cpp` | alterado | mola de empenamento condensada; empenamento contínuo pelo GDL do nó |
| core | `src/core/beam_iso.cpp`, `include/fem/core/beam_iso.hpp` | alterado | ψ quadrático com penalidade; rigidez geométrica completa |
| core | `src/core/beam_loads.cpp`, `include/fem/core/beam_loads.hpp` | alterado | ponto de aplicação: torque em relação ao centro de cisalhamento e termo de altura |
| core | `model_validation.cpp`, `detail/model_validation.hpp` | alterado | condições de empenamento por ponta e nó; verificações do REQ-008d-17 |
| core | `static_analysis.cpp`, `detail/static_stages.hpp`, `include/fem/core/static_analysis.hpp` | alterado | conjuntos por pontas contínuas; empenamento e bimomentos no resultado; perfis completos |
| core | `second_order.cpp`, `buckling.cpp`, `include/fem/core/stability_analysis.hpp` | alterado | iteração e λ sobre o perfil completo; empenamento nos modos |
| core | `beam_result.cpp`, `include/fem/core/beam_result.hpp` | alterado | B, Tsv, Tw e ψ nas pontas e em `at(x)`; σw e τw |
| tests | `test_beam_warping.cpp`, `test_flexural_torsional.cpp` (novos); `test_beam_curved.cpp`, `test_beam_validation.cpp`, `test_section_*.cpp`, `test_buckling.cpp`, `test_second_order_analysis.cpp` (ampliados); `tests/perf/bench_frame.cpp` (argumento `--warping`) | novo / alterado | verificação (§8) e benchmark |
| specs | `architecture.md`, ADR 0008 | alterado / novo | registro |

## 4. Interfaces públicas

```cpp
// dof_map.hpp — REQ-008d-01: the rate of twist of a beam with warping.
enum class DofType : std::uint8_t { U, V, W, RX, RY, RZ, T, Warp };
inline constexpr int kDofTypeCount = 8;

// model.hpp — REQ-008d-01, 03, 04, 13
struct Assignment
{
    // … as in 008b …
    bool warping = false; // REQ-008d-01: torsion with warping (Vlasov), 3D beams
};

// REQ-008d-03: the warping at one end, instead of the default (continuous
// between collinear ends with warping, free otherwise).
enum class WarpingCondition : std::uint8_t {
    Free,       // no bimoment
    Restrained, // no warping (end plate, stiffener)
    Elastic,    // B = stiffness * psi
    Continuous  // shares the warping dof of the node
};
struct WarpingEnd
{
    ElementTarget elements;
    BarEnd end = BarEnd::Both;
    WarpingCondition condition = WarpingCondition::Restrained;
    double stiffness = 0.0; // Elastic: kw >= 0 (bimoment per unit rate of twist)
};

struct NodalForce
{
    // … as in 008 …
    double bimoment = 0.0; // REQ-008d-04: on the warping dof
};
// Restraint: dof = DofType::Warp restrains the warping (REQ-008d-04).

struct DistributedLoad / ConcentratedLoad
{
    // … as in 008b …
    // REQ-008d-13: the point of the section where it acts, local (y, z) of
    // the bar as the user sees them, from the centroid; empty: the centroid.
    std::optional<Eigen::Vector2d> point = std::nullopt;
};

struct Model
{
    // … as in 008c …
    std::vector<WarpingEnd> warpingEnds;
};

// section.hpp — REQ-008d-12 (principal axes, from the centroid)
struct Section
{
    // … as in 008b …
    std::optional<double> monosymmetryY = std::nullopt; // beta_y, generic only (default 0)
    std::optional<double> monosymmetryZ = std::nullopt; // beta_z
};
struct SectionProperties
{
    // … as in 008b …
    double polarRadius2 = 0.0;  // r0^2 = (Iy + Iz) / A + ys^2 + zs^2, about the shear centre
    double monosymmetryY = 0.0; // beta_y = (1/Iy) integral z (y^2 + z^2) dA - 2 zs
    double monosymmetryZ = 0.0; // beta_z = (1/Iz) integral y (y^2 + z^2) dA - 2 ys
};
struct StressPoint
{
    // … as in 008b …
    double sectorialMoment = 0.0; // S_omega, for tau_w = Tw S_omega / (Iw t)
    double sectorialThickness = 0.0;
};

// beam_element.hpp — the reference state of feature 008c, completed
// (design §5.4): the internal forces at the two Gauss points of `parts`
// equal parts, linear within a part, and the factor of the loads (their
// load-height terms). Built before the assembly; compute() only reads it.
struct ReferencePoint
{
    double n = 0.0, vy = 0.0, vz = 0.0, t = 0.0, my = 0.0, mz = 0.0; // BeamForces convention
};
inline constexpr int kMaxReferenceParts = 32;
struct ReferenceProfile
{
    int parts = 0; // 0: first order
    std::array<ReferencePoint, 2 * kMaxReferenceParts> points{};
    double loadFactor = 0.0; // the factor of the span loads (1 in the second order, lambda in buckling)

    [[nodiscard]] ReferencePoint at(double s, double length) const;
    [[nodiscard]] ReferenceProfile scaled(double factor) const;
    [[nodiscard]] bool active() const { return parts > 0; }
};
struct BeamData
{
    // … as in 008c, without AxialProfile …
    double warpingStiffness = 0.0; // E Iw; > 0: the beam has warping
    // REQ-008d-03: per end, kRigid (restrained), 0 (free) or a spring; the
    // continuous ends use the node's dof instead.
    std::array<double, 2> warpingSprings{0.0, 0.0};
    std::array<bool, 2> warpingContinuous{false, false};
    // REQ-008d-10, 12: for the flexural-torsional terms.
    double polarRadius2 = 0.0;
    double monosymmetryY = 0.0;
    double monosymmetryZ = 0.0;
    std::shared_ptr<const ReferenceProfile> reference; // null: first order
};

// beam_loads.hpp — REQ-008d-13: SpanPiece and SpanPoint gain the point of
// the section, principal, from the shear centre (the lever of the
// torque, and the load-height term in the second order and buckling).
Eigen::Vector2d point = Eigen::Vector2d::Zero(); // set from the load or the centroid

// static_analysis.hpp — REQ-008d-07
struct StaticResult
{
    // … as in 008c …
    std::vector<double> warping;           // per node: psi (0 without the dof)
    std::vector<double> reactionBimoments; // per node: 0 where free
};

// beam_result.hpp — REQ-008d-07, 08, 09
struct BeamForces
{
    // … as in 008 …
    double b = 0.0;   // bimoment: sigma_w = B omega / Iw
    double tsv = 0.0; // t = tsv + tw (without warping, tsv = t)
    double tw = 0.0;
};
struct BeamPoint
{
    // … as in 008 …
    double warping = 0.0; // psi
};
struct PointStress
{
    // … as in 008 …
    double warpingNormal = 0.0; // sigma_w, included in `normal`
    double warpingShear = 0.0;  // tau_w, included in `shear`
};

// stability_analysis.hpp — REQ-008d-15
struct BucklingMode
{
    // … as in 008c …
    std::vector<double> warping; // per node
};
```

As funções de análise (`runLinearStatic`, `runSecondOrderStatic`,
`runLinearBuckling`) não mudam de assinatura nem de opções (REQ-008d-20).

## 5. Dados e algoritmos

### 5.1 Empenamento na viga reta e no arco (REQ-008d-01, 02, 05, 06)

O motor e o elemento exato já têm o empenamento verificado (008b §5.3 e
T16): estado de 14 componentes com ψ e Q = E·Iw·ψ' (B = −Q), torção mista
Q' = G·J·ψ − T_S, base elástica de torção e Ω do arco no referencial
móvel. Muda só o acesso, a partir de `c19d42b`:

- `Assignment::warping` em viga 3D, com Iw > 0, dá `warpingStiffness =
  E·Iw` à barra. Iw é o da seção resolvida (formas, paredes finas,
  poligonal) ou o da genérica.
- O elemento exato atende reta (`Line2`) e arco (`Line3`). A viga reta com
  empenamento sempre vai ao elemento exato. Sem empenamento, a reta segue no
  caminho fechado da 008 na análise linear (REQ-008d-19).

### 5.2 Condições de empenamento (REQ-008d-03, 04)

1. **Por ponta**, a do `WarpingEnd`, se houver. Senão, o padrão da 008b
   §5.4: entre as pontas de vigas com empenamento que chegam a um nó, as de
   tangentes colineares (|t₁ × t₂| < 1e−6) são `Continuous`; as demais,
   `Free`. Na isoparamétrica, a tangente é a da curva quadrática, que
   quebra levemente entre elementos que aproximam uma curva suave (3,7e−3
   com 4 elementos num quarto de anel): a tolerância continua 1e−6, e
   quem quer continuidade numa cadeia isoparamétrica declara
   `WarpingEnd Continuous` *(decidido pelo mantenedor em 2026-11-18, na
   T09)*.
2. **GDL do nó**: o nó tem `DofType::Warp` se ao menos uma ponta nele for
   `Continuous`. A restrição e o bimomento nodal agem nesse GDL.
3. **No elemento**:
   - a ponta `Continuous` usa o GDL do nó;
   - as outras são um GDL interno de ψ, condensado com a mola kw:
     - `Free`: kw = 0;
     - `Elastic`: kw dado;
     - `Restrained`: kw = kRigid, eliminado como ψ = 0.
   É o mesmo tratamento das molas de ligação da 008. A condensação entra na
   contagem de Sturm (008c §5.4), como as outras.
4. **Conjuntos**: `dofsAt` depende das pontas contínuas. A análise cria um
   `ElementSet` por (formulação, ponta inicial contínua, ponta final
   contínua), como no design da 008b.
5. **Sinal**: ψ = dθx/ds no referencial da barra; em barras colineares de
   sentidos opostos, θx e s trocam de sinal juntos e ψ coincide (008b §5.4).
   B é o conjugado de ψ. O bimomento nodal e a reação seguem essa convenção.
6. **Liberações e excentricidades**: a condição vale na ponta do trecho
   flexível e não depende das liberações de esforços. A excentricidade não
   transforma ψ.

### 5.3 Isoparamétrica (REQ-008d-06, 10, 14)

- **Empenamento** (008b §5.5): ψ quadrático, independente, nos três nós
  (`DofType::Warp` também no nó do meio). A restrição de Vlasov ψ = κx entra
  por penalidade k_ψ = 1e6·EIw/ℓ², integrada com 2 pontos; flexão, torção e
  empenamento com 3. Nas pontas, as condições do §5.2: o nó do meio tem
  sempre o GDL, e a mola entra como rigidez concentrada no GDL da ponta.
- **Rigidez geométrica**: a segunda variação do funcional do §5.5 em
  componentes globais, com os esforços de referência interpolados
  linearmente dos 2 pontos reduzidos e integrada com 3 pontos. A da 008c
  (N·B⊥ᵀB⊥) é o caso só com N, e o teste confere que fica igual.
- **Convergência**: contra o arco exato, com taxa ≥ 3,5, como na 008c.

### 5.4 Perfil de referência (REQ-008d-10, 13, 14)

`ReferenceProfile` generaliza o `AxialProfile` da 008c: os seis esforços
nos 2 pontos de Gauss de P partes iguais, lineares na parte, e o fator das
cargas. P vem da regra da 008c (§5.2 de lá), agora pelo esforço que mais
varia:

- **barra reta sem cargas no vão**: N, V e T constantes e M linear. P = 1
  é exato, e o Magnus de 4ª ordem integra A linear sem erro de quadratura
  além do comutador;
- **carga uniforme**: M parabólico. P = 4, e a calibração da tarefa ajusta
  P até o momento crítico ficar ≤ 1e−8 da barra subdividida;
- **cargas trapezoidais, parciais ou concentradas**: quebras nos pontos de
  carga, P até 32;
- **arcos**: P = ⌈ângulo/(π/16)⌉, até 32.

**Memória**: o perfil completo tem 64 pontos × 6 = 3 KB. Em `BeamData`
(copiado em `Beams`, nos `BeamResult` e nos modos), o pórtico de 13 640
barras passaria de 150 MB. Por isso o perfil fica num `shared_ptr<const>`,
montado antes de cada montagem e compartilhado pelas cópias. `compute` só o
lê, sem alocar.

**De onde vem**: a mesma recuperação da 008c (§5.2 de lá), agora com os
seis esforços:

- na reta, pela estática exata (esforços de ponta mais as integrais das
  cargas);
- no arco, por `stateAt` nos pontos de Gauss;
- na isoparamétrica, pelos 2 pontos reduzidos.

O `loadFactor` é 1 na segunda ordem e λ na flambagem, e multiplica os termos
de altura das cargas.

### 5.5 Flexo-torção no motor exato (REQ-008d-10, 11, 13, 14; ADR 0008)

**Funcional** (Vlasov, 1961; Trahair, 1993, cap. 3; Yang e McGuire,
1986). A segunda variação da energia, em eixos principais, com os
deslocamentos v, w do centro de cisalhamento, o giro φ e os esforços de
referência N, My, Mz (Vy e Vz entram pelas derivadas, ao integrar por
partes), é:

```
δ²Π = ½∫ [ EIz v''² + EIy w''² + GJ φ'² + EIw φ''²                 (elástica, 008b)
         + N (v'² + w'² + r0² φ'² + 2 zs v'φ' − 2 ys w'φ')          (Engesser, Wagner, excentricidade)
         + 2 Mz φ w'' − 2 My φ v''                                  (acoplamento flexão-torção)
         + (βy My + βz Mz) φ'²                                      (monossimetria)
         − Σ_cargas (q · d) φ² ] dx                                 (altura da carga)
```

- d é o vetor do centro de cisalhamento ao ponto de aplicação da carga.
- (q·d) < 0 desestabiliza: carga para baixo aplicada acima do centro de
  cisalhamento.
- Os sinais dos termos de momento e de β seguem a convenção de esforços
  da 008 (`BeamForces`). Eles são **fixados pelos testes de referência**
  (AC-5, AC-6), como o braço rígido da 008c.
- Em Timoshenko, as curvaturas v'' e w'' são as de flexão (θz' e −θy',
  sem a parcela do cortante), e o termo de N segue Engesser, como na 008c.

**No espaço de estados**: as equações de Euler-Lagrange do funcional,
escritas na forma de primeira ordem s' = A·s, com os mesmos estados da
008b (o estado não cresce). Ficam 12 sem empenamento e 14 com ele. Os termos
novos de A:

- **Wagner**: na torção, a rigidez efetiva de Saint-Venant passa de G·J a
  G·J + K, com K = N·r0² + βy·My + βz·Mz. Sem empenamento, a taxa de giro é
  κx = T_S/(G·J + K); com empenamento, Q' = (G·J + K)·ψ − T_S.
- **Excentricidade**: N acopla a inclinação do centroide ao giro, pela
  parcela do giro em torno do centro de cisalhamento. É o termo que a 008c
  deixou de fora na inclinação (008c §5.1).
- **Acoplamento**: os momentos fletores da seção girada ganham o giro vezes
  o momento de referência (My,loc ← My − φ·Mz e Mz,loc ← Mz + φ·My, sinais
  pelos testes). Com M variável, os cortantes de referência entram pelo
  termo integrado por partes.
- **Altura da carga distribuída**: k_q = λ·(q·d) no termo do giro em Mx',
  como uma base elástica de torção (negativa se desestabiliza).
  **Concentrada**: salto ΔMx = λ·(P·d)·θx no ponto, uma matriz de salto
  linear no estado, exata na transferência.

**Arcos** (REQ-008d-14): as mesmas equações no referencial móvel (Ω), com
as derivadas d/ds + Ω× da 008b. O funcional da barra curva é o de Yang e
Kuo (1987) restrito aos termos que esse referencial gera. A escolha fica
registrada no código e verificada no AC-8, contra a cadeia de barras retas
exatas e contra a literatura.

**Verificações de consistência** (testes, antes dos casos de aceite):

- K da barra simétrica em 1e−12, com N, momentos, cargas com altura e
  empenamento;
- só com N, os mesmos números da 008c;
- barra cortada em duas e condensada igual à inteira.

### 5.6 Partes e contagem de Sturm (REQ-008d-11, 16)

A regra da 008c escolhe partes tão curtas que nenhuma flamba sozinha com as
pontas presas, com margem 4 sobre o esforço da parte. Ela ganha três
limites, todos para a parte biengastada de comprimento l:

- **torção** (K < 0): G·J + 4π²·E·Iw/l² ≥ 4·|K|;
- **flexo-torção**: (2π/l)·√(E·I_min·(G·J + 4π²·E·Iw/l²)) ≥ 4·|M|_max;
- **altura da carga**: 4·|k_q|·l² ≤ 4π²·(G·J + 4π²·E·Iw/l²)/4,
  conservador.

Com isso, J₀ de cada parte é zero, e a contagem de Wittrick-Williams segue
a 008c §5.4: os pivôs negativos globais mais os das condensações internas
(junções, ligações e as molas de empenamento do §5.2). Uma barra que pede
mais de `kMaxParts` partes continua marcada como instável (`kUnstableBar`).
A regra tem um teste próprio: uma barra subdividida mais do que a regra
pede dá a mesma contagem.

### 5.7 Seções: r0², βy e βz (REQ-008d-12)

Em eixos principais, a partir do centroide, com (ys, zs) o centro de
cisalhamento:

- r0² = (Iy + Iz)/A + ys² + zs²;
- βy = (1/Iy)·∫ z·(y² + z²) dA − 2·zs;
- βz = (1/Iz)·∫ y·(y² + z²) dA − 2·ys.

Este é o βx de Trahair, que dá zero nas seções duplamente simétricas.

- **Formas paramétricas**:
  - retângulo, I, U, T e cantoneira são uniões de retângulos, com as
    integrais exatas pelas fórmulas de polígono (Green, monômios de grau 3);
  - círculo e tubos: β = 0 por simetria.
- **Paredes finas**: integrais de linha média (∫ t·(…) ds por parede, exatas
  para o polinômio no segmento), na mesma aproximação das outras
  propriedades dela.
- **Poligonal maciça**: fórmulas de polígono exatas (contornos menos
  furos), 1e−10.
- **Genérica**: `monosymmetryY` e `monosymmetryZ` opcionais, padrão 0
  (duplamente simétrica), com a hipótese escrita no comentário da
  interface. r0² sai das propriedades dela.

Para τw, os pontos das formas e das paredes finas ganham o momento setorial
Sω = ∫ ω·t ds acumulado desde a ponta livre, e a espessura no ponto.

### 5.8 Segunda ordem e flambagem (REQ-008d-10, 15, 16)

- **Segunda ordem** (008c §5.5): o ponto fixo de Anderson itera o vetor
  dos perfis completos (seis esforços por ponto). A variação mede
  deslocamentos, giros, empenamentos e esforços. A contagem final e as
  recusas não mudam. Sem carga lateral nem torque, um perfil duplamente
  simétrico sob momento fletor não gira, como na teoria: a segunda ordem
  amplifica um giro que já existe.
- **Flambagem** (008c §5.6): o caso de carga dá os perfis e as cargas de
  referência, e K(λ) usa `reference.scaled(λ)` com `loadFactor = λ`. O
  subespaço, o refinamento, a contagem e a bissecção não mudam. O modo traz
  ψ por nó (`BucklingMode::warping`) e, nas barras, ψ e B em `at(x)`. A
  normalização é a da 008c: maior translação 1 ou, sem translação, maior
  rotação 1, que no modo de torção é o giro (REQ-008d-15).
- **Sem compressão nem momento**: a recusa do REQ-008c-12 passa a olhar
  também os momentos fletores. Um caso só de tração e sem momento não tem
  fator.

### 5.9 Custo (NFR-008d-02)

**Medido em 2026-11-18**, no pórtico da 008 (10 × 40, 13 640 barras), em
Release, na máquina do mantenedor, duas medidas de cada:

| Caso | Código | GDL | Tempo |
|------|--------|-----|-------|
| Linear sem empenamento | 008c (`800860a`) | 29 040 | 3,60 e 3,69 s |
| Linear sem empenamento | 008b com empenamento (`c19d42b`) | 29 040 | 3,62 e 3,77 s |
| Linear com empenamento em todas as vigas | 008b (`c19d42b`, `bench_frame 10 40 warping`) | cerca de 33 880 | 6,06 e 6,22 s |

- Com empenamento, a linear custa 1,65 a 1,68 vez a sem empenamento.
- No modo `warping`, o benchmark da 008b troca os pilares-caixão por
  perfis abertos: o empenamento de Vlasov é para perfis abertos, e por
  isso o maior deslocamento muda (0,0967 para 0,1435). Este é o caso a
  medir, e o argumento `--warping` do benchmark atual o reproduz.
- O código da 008c não mudou a linear sem empenamento: 3,6 a 3,7 s nos
  dois.

**Metas propostas** (NFR-008d-02), em razões, para não depender da
máquina:

- **linear com empenamento** ≤ 2,2 vezes a linear sem empenamento
  (medido: 1,65 a 1,68; a folga cobre as molas, a continuidade e os
  resultados novos);
- **segunda ordem com empenamento** ≤ 4 vezes a linear com empenamento (a
  razão da 008c);
- **flambagem com 5 fatores, com empenamento** ≤ 12 vezes a linear com
  empenamento (a razão da 008c);
- **sem empenamento** continuam a meta da 008b (linear < 5 s) e as da
  008c (4× e 12×), agora com a flexo-torção ativa.

A tarefa de custo, logo depois do perfil completo e antes dos casos de
aceite, mede a segunda ordem e a flambagem com e sem empenamento. Se
alguma meta não passar, ela para e propõe a mudança, como a T03 e a T23 da
008c.

## 6. Fluxo

```
runLinearStatic(model)
 ├─ validate(model)                    → falha: todos os problemas (§7)
 ├─ seções: solveSection (r0², β, Sω incluídos)
 ├─ BeamData: + E·Iw, molas/continuidade de empenamento por ponta (§5.2)
 ├─ conjuntos: … + ExactBeamElement por (kind, pontas contínuas), Iso
 ├─ DofMap (com Warp); restrições e bimomentos nodais
 ├─ assemble → solve → reactions
 └─ StaticResult: + warping, reactionBimoments; BeamResult com B, Tsv, Tw, ψ

runSecondOrderStatic / runLinearBuckling
 └─ como na 008c, com profilesOf → ReferenceProfile completo (shared_ptr)
    e setReference em vez de setAxialForces
```

## 7. Tratamento de erros

| Situação | Comportamento | Requisito |
|----------|---------------|-----------|
| Empenamento em viga 2D, ou com Iw ausente ou ≤ 0 | Problema citando a atribuição (quando a forma não empena, a mensagem diz) | REQ-008d-17 |
| Mola de empenamento negativa | Problema citando o `WarpingEnd` | REQ-008d-17 |
| `WarpingEnd`, restrição `Warp` ou bimomento nodal sem empenamento | Problema citando o item e o elemento ou nó | REQ-008d-17 |
| Ponto de aplicação de carga em viga 2D | Problema citando a carga | REQ-008d-17 |
| GDL de empenamento sem rigidez | Falha com nó e direção "warp"; se todas as pontas no nó forem livres, a mensagem sugere impedir ou restringir | REQ-008d-18 |
| Instabilidade por flexo-torção na segunda ordem; fatores não encontrados | As recusas e a `note` da 008c, com o elemento quando a instabilidade está dentro de uma barra | REQ-008d-16 |

## 8. Estratégia de testes

| Teste | Tipo | Requisito | Referência/tolerância |
|-------|------|-----------|-----------------------|
| `test_beam_warping`: balanço de I com torque e engaste impedido (giro, B, Tsv, Tw ao longo); biapoiada com torque distribuído e concentrado, pontas livres e impedidas; G·J → 0 e E·Iw → 0; bimomento nodal e reação | verificação | REQ-008d-01, 02, 04, 07, 08 | 1e−8 contra senh/cosh |
| `test_beam_warping`, condições: dois vãos contínuos igual a uma barra; T de três barras; chapa de topo; mola (kw → 0 livre, kw → ∞ impedido, valor intermediário); liberação de torção e excentricidade; base elástica de torção | verificação | REQ-008d-03, 05 | 1e−8 |
| `test_beam_curved`, empenamento: raio grande igual à reta; arco e isoparamétrica concordando; viga curva em planta biengastada contra a integração independente das equações de Vlasov (tiro com Runge-Kutta no teste, no lugar de Yang e Kuo, 1987, decidido em 2026-11-18) | verificação/convergência | REQ-008d-06, 08 | 1e−6 entre formulações refinadas; 1e−8 contra a integração; taxa ≥ 3,5 |
| Tensões σw e τw nos pontos de I, U e paredes finas | verificação | REQ-008d-09 | 1e−8 contra as fórmulas |
| `test_section_*`: r0², βy e βz das formas, paredes finas (I monossimétrico, U, T) e poligonal; genérica com e sem β | unitário | REQ-008d-12 | 1e−10 contra as fórmulas fechadas (Trahair); β = 0 nas duplamente simétricas |
| `test_flexural_torsional`, consistência: K simétrica; só N igual à 008c; barra subdividida igual à inteira; contagem com subdivisão extra | unitário | REQ-008d-10, 11 | 1e−12 |
| `test_flexural_torsional`, vigas (AC-5): Mcr com momento uniforme, apoio de garfo, com e sem empenamento (retângulo alto, Iw = 0); empenamento impedido; balanço com carga na ponta; momento linear, carga uniforme e concentrada; altura da carga (mesa superior, centro, inferior); I monossimétrico com βz | verificação | REQ-008d-10 a 13 | 1e−8 nas fórmulas fechadas (momento uniforme, com e sem empenamento impedido, monossimétrico); nos casos de C1 e C2, 1e−6 contra a barra subdividida (autoconvergência) e 1% contra as tabelas (Trahair, 1993; NCCI SN003) |
| `test_flexural_torsional`, pilares (AC-6): cruciforme Pcr,T = (G·J + π²·E·Iw/L²)/r0²; U e T por flexo-torção (raiz da cúbica de Timoshenko e Gere); modos com giro e ψ | verificação | REQ-008d-10, 11, 12, 15 | 1e−8 |
| `test_flexural_torsional`, pórtico em L de Argyris (flambagem lateral de pórtico em ângulo reto): consistência dos momentos nas juntas | verificação | REQ-008d-10, ADR 0008 | 1% contra a literatura (Argyris, 1979; Yang e McGuire, 1986) |
| `test_second_order_analysis`, flexo-torção (AC-7): momento uniforme abaixo do crítico com pequena carga lateral ou torque, giro amplificado; efeito da altura; recusa acima de Mcr | verificação | REQ-008d-10, 13, 16 | 1e−6 contra a solução analítica da viga-coluna lateral (Trahair, 1993, cap. 6) |
| `test_buckling`, arcos (AC-8): fora do plano sob compressão uniforme e sob momento uniforme; raio grande igual à reta; arco e isoparamétrica; cadeia de barras retas | verificação/convergência | REQ-008d-14 | 1e−6 contra a cadeia (Richardson); 1% contra Yang e Kuo (1987) e Pi e Trahair; taxa ≥ 3,5 |
| `test_beam_validation`: cada problema do §7 e vários juntos; mecanismo "warp" com a sugestão | unitário | REQ-008d-17, 18 | a mensagem cita o item |
| Suítes da 007 a 008c | regressão | REQ-008d-19 | as mesmas tolerâncias; revisões da flexo-torção registradas em `tasks.md` |
| `test_allocations`: `compute` do exato e da isoparamétrica com perfil completo e empenamento | unitário | constituição §6 | zero alocações |
| `bench_frame --warping` (linear, `--second-order`, `--buckling 5`) e sem empenamento | desempenho | NFR-008d-02 | metas do §5.9 |
| Job de cobertura | CI | NFR-008d-03 | ≥ 80% |

## 9. Alternativas consideradas

| Alternativa | Por que foi descartada |
|-------------|------------------------|
| Linearizar o equilíbrio da barra na configuração deformada (θ×M₀ nos esforços locais), como a 008c fez com N | Com momentos de referência, o termo dá uma rigidez não simétrica (momentos "axiais" por vetor de rotação). Isso quebra a LDLᵀ, a contagem de Sturm e o gradiente conjugado. O funcional quadrático dá a mesma física com K simétrica (ADR 0008) |
| Fórmulas de norma (C1, C2, C3) para o Mcr | Fora de escopo (requisitos §6); o sistema calcula o Mcr elástico da estrutura real, de onde a norma parte |
| Estado aumentado com φ e φ' próprios na torção sem empenamento | O estado da 008b já tem θx e, sem empenamento, a taxa sai de T_S; o termo de Wagner entra na mesma relação, sem estados novos |
| Perfil de referência dentro do `BeamData` (como o `AxialProfile`) | 3 KB por cópia; mais de 150 MB no benchmark (§5.4) |
| Flexo-torção ligada só com empenamento | Rejeitada pelo mantenedor (requisitos §8): subestima o risco de perfis sem empenamento ativado |
| β pelo campo de empenamento numérico da poligonal | As integrais de β são polinomiais: as fórmulas de polígono são exatas e mais simples |

## 10. Riscos e mitigação

- **Sinais e termos do funcional** (convenções de momento e de β entre
  livros). → Os sinais são fixados pelos casos fechados (AC-5, AC-6) antes
  dos casos tabelados; a simetria de K e a redução à 008c são testes
  próprios.
- **Momentos nodais semitangenciais** mudam o significado de um momento
  aplicado num nó que gira, na segunda ordem e na flambagem. → ADR 0008;
  o pórtico em L de Argyris confere o equilíbrio das juntas; a convenção
  fica no comentário de `NodalForce`.
- **Teorias de arco divergem** (termos de curvatura na flexo-torção). → As
  duas formulações saem do mesmo funcional; o AC-8 compara uma com a
  outra, com a cadeia de barras retas e com a literatura (1%). Se a
  literatura escolhida não fechar em 1%, a escolha volta ao mantenedor.
- **Custo sem empenamento**: a flexo-torção sempre ativa leva as vigas com
  carga no vão ao Magnus (M parabólico) e pode tirar a flambagem da meta
  da 008c (12×). → A tarefa de custo mede antes da implementação completa
  e para, se passar (§1, ajuste do REQ-008d-19).
- **Custo com empenamento**: kL grande nos pilares-caixão (cerca de 150)
  pedia muitas partes na 008b. → Os ganhos da 008b continuam valendo
  (crescimento e⁸ por parte, resultados em paralelo); medida no §5.9.
- **Regra de partes conservadora demais** (partes demais com momentos
  grandes). → O teste de contagem com subdivisão extra garante a
  correção; a tarefa de custo mede quantas partes o benchmark pede.
- **Mudanças nos resultados da 008c** pela flexo-torção sempre ativa. → As
  suítes da 008c rodam a cada tarefa; cada mudança é revista contra a
  referência analítica e registrada (REQ-008d-19).

## 11. Novas dependências

Nenhuma.
