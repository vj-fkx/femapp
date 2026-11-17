# Design — 008c beam-second-order

Status: aprovado · Data: 2026-11-17
Requisitos: [requirements.md](requirements.md)

## 1. Resumo da abordagem

Tudo em `core`, sem Qt, sobre a análise, o motor exato e as formulações de
barra da 008 e da 008b:

1. **O esforço normal entra no motor exato** (`beam_state.cpp`). A equação
   de momentos ganha o termo de segunda ordem linearizado
   M' = … − u' × (N e₁), e a lei do cortante de Timoshenko segue Engesser
   (cortante perpendicular ao eixo deformado). Com N constante na parte, A
   continua constante e a exponencial dá a barra exata: funções de
   estabilidade em compressão e tração, nas duas teorias, com base elástica,
   cargas no vão e qualquer `at(x)`. Com N variável ao longo da barra (carga
   axial no vão, arcos), cada parte usa o integrador de Magnus de 4ª ordem.
2. **Treliça e isoparamétrica** ganham a rigidez geométrica clássica, linear
   em N: a da barra articulada, exata na teoria, e a da viga quadrática,
   integrada nos pontos de Gauss e convergente com a malha.
3. **Excentricidades rígidas** ganham o termo de segunda ordem do braço
   rígido, que gira com o nó levando a força que transmite.
4. **Segunda ordem** (`runSecondOrderStatic`): a análise linear dá o
   primeiro N, e depois a iteração de ponto fixo sobre os esforços normais
   (acelerada por Anderson) remonta e resolve até convergir. A fatoração
   simbólica é feita uma vez.
5. **Flambagem** (`runLinearBuckling`): os esforços normais da análise
   linear, multiplicados por λ, entram de forma exata nas barras. Assim, a
   flambagem é um problema de autovalor **não linear**, K(λ)·φ = 0. Ele é
   resolvido por iteração com deslocamento (subespaço sobre o problema
   linearizado em λ), e a contagem de Sturm de Wittrick-Williams garante que
   nenhum fator ficou para trás (ADR 0007).
6. **A contagem de Sturm** soma os pivôs negativos da matriz global (LDLᵀ
   sem pivoteamento, que dá a inércia) e os de cada barra: as junções das
   partes do motor e os GDL internos condensados das ligações. Cada parte é
   curta o bastante para não ter autovalor próprio abaixo do λ procurado.
   A mesma contagem acusa a instabilidade na segunda ordem (REQ-008c-08).

**Ajustes nos requisitos** (aprovados junto com este design e já aplicados em `requirements.md`):

- **REQ-008c-02 e NFR-008c-01:** "exatamente" só é possível com N constante
  na barra, o caso de quase todos os pilares e vigas. Com carga axial no vão
  (peso próprio de um pilar), N varia e a barra é integrada pelo Magnus de
  4ª ordem, com erro ≤ 1e−8 calibrado (§5.2). Texto proposto para o
  REQ-008c-02: "… exatamente na rigidez … com N constante na barra; com N
  variável ao longo dela (cargas axiais no vão), com erro relativo ≤ 1e−8".
- **REQ-008c-08:** a instabilidade pode estar dentro de uma barra (pilar
  com as pontas liberadas comprimido além da sua carga crítica), sem pivô
  negativo na matriz global. Nesse caso, a mensagem nomeia a barra. Texto
  proposto: "… o nó e a direção indicados pela fatoração ou, se a
  instabilidade estiver dentro de uma barra, o elemento".

## 2. Verificação da constituição

| Princípio | Atende? | Observação |
|-----------|---------|------------|
| Multiplataforma (§2) | Sim | C++20 e Eigen; nada específico de sistema |
| GPU somente via QRhi (§3) | Sim | Nada de GPU |
| Camadas / `core` sem Qt (§4) | Sim | Tudo em `core` (REQ-008c-15) |
| Correção numérica (§5) | Sim | Soluções analíticas: viga-coluna de Timoshenko-Gere, Euler nos quatro casos, Engesser, Hetényi, Greenhill (peso próprio), pórticos por funções de estabilidade; convergência: isoparamétrica e arcos contra cadeia de barras retas exatas; patch: barra exata subdividida igual à inteira; tolerâncias explícitas |
| Qualidade (§6) | Sim | Cobertura de `core` ≥ 80%; `compute` continua sem alocação (perfil de N de tamanho fixo) |
| Simplicidade / dependências (§7) | Sim | Nenhuma dependência nova. Sem o Spectra: os poucos fatores pedidos saem de iteração de subespaço com os solvers densos do Eigen (ADR 0007). Uma só formulação exata (o motor) para a barra com N, em vez de funções de estabilidade fechadas por caso |

## 3. Módulos e arquivos afetados

| Módulo | Arquivo | Novo/Alterado | Responsabilidade |
|--------|---------|---------------|------------------|
| core | `include/fem/core/stability_analysis.hpp`, `src/core/second_order.cpp`, `src/core/buckling.cpp` | novo | API e laços da segunda ordem e da flambagem |
| core | `src/core/detail/static_stages.hpp`, `src/core/static_analysis.cpp` | novo / alterado | `runLinearStatic` partido em etapas reaproveitadas (preparar, montar com N, resolver, recuperar), sem mudar o resultado |
| core | `include/fem/core/beam_element.hpp` | alterado | `AxialProfile` em `BeamData` |
| core | `src/core/beam_state.cpp`, `include/fem/core/beam_state.hpp` | alterado | termo de N em A (Engesser), Magnus de 4ª ordem com N variável, regra de partes por N, pivôs negativos das junções |
| core | `src/core/beam_exact.cpp` | alterado | ligações condensadas com inércia (indefinidas permitidas com N), braço rígido de segunda ordem, contagem por elemento |
| core | `src/core/truss_element.cpp`, `include/fem/core/truss_element.hpp` | alterado | rigidez geométrica da treliça |
| core | `src/core/beam_iso.cpp`, `include/fem/core/beam_iso.hpp` | alterado | rigidez geométrica da isoparamétrica |
| core | `src/core/linear_solver.cpp`, `include/fem/core/linear_solver.hpp` | alterado | fatoração indefinida com inércia; reuso da análise simbólica |
| core | `src/core/beam_result.cpp` | alterado | `at(x)` da barra com N (vem do motor sem código novo); resultados dos modos |
| tests | `tests/unit/core/test_beam_second_order.cpp`, `test_buckling.cpp`, `test_second_order_analysis.cpp`, `test_linear_solver.cpp` (ampliado), `tests/perf/bench_frame.cpp` (argumento) | novo / alterado | verificação (§8) e benchmark |
| specs | `architecture.md`, ADR 0007 | alterado / novo | registro |

## 4. Interfaces públicas

```cpp
// beam_element.hpp — REQ-008c-02, 04: the axial force of the reference
// state along the flexible length (tension positive). count 0: none (first
// order); 1: constant; otherwise 2 per part of `parts` equal parts, at the
// two Gauss points of each part (N linear within a part).
inline constexpr int kMaxAxialParts = 32;
struct AxialProfile
{
    int count = 0;
    std::array<double, 2 * kMaxAxialParts> values{};
    [[nodiscard]] double at(double s, double length) const; // linear in its part
    [[nodiscard]] double largestCompression() const;        // >= 0
};
struct BeamData
{
    // … as in 008b …
    AxialProfile axialForce; // 008c
};

// beam_state.hpp — REQ-008c-02, 10: the stiffness with the profile of
// `beam.axialForce`, and the negative pivots met when condensing the parts
// (for the Sturm count, design 008c §5.4).
struct StateBarMatrices
{
    StateBarMatrix stiffness;
    StateBarVector loads;
    int negativePivots = 0;
};
// The number of profile parts and their Gauss points for a bar (§5.2).
[[nodiscard]] int axialParts(const BeamData& beam, double length, const SpanLoads& loads);

// beam_state.hpp / beam_iso.hpp / truss_element.hpp — the element
// formulations take an optional span of per-element counts, written by
// compute() for its own element (thread safe in the coloured assembly).
ExactBeamElement(ElementKind kind, std::span<const BeamData> beams,
                 std::span<const SpanLoads> loads, std::span<int> negativePivots = {});
TrussElement(ElementKind kind, std::span<const double> axialForce = {}); // Kg when set
// IsoBeamData gains `std::array<double, 2> axialForce` (at the reduced points).

// linear_solver.hpp — REQ-008c-08, 10
class SparseCholesky final : public LinearSolver
{
public:
    // … as before …
    // LDL^T without the positive check: fails only on a zero pivot. The
    // symbolic analysis is kept while the pattern is the same.
    [[nodiscard]] Expected<void> factorizeIndefinite(const SparseMatrix& lowerTriangle);
    [[nodiscard]] std::int64_t negativePivots() const; // inertia after a factorization
};

// stability_analysis.hpp — REQ-008c-01, 05, 06, 09, 11, 12, 13
struct SecondOrderOptions
{
    StaticOptions base;
    double tolerance = 1e-8;
    int maxIterations = 50;
};
struct SecondOrderResult
{
    StaticResult result; // beams: at(x) with the moment N delta
    int iterations = 0;
    double change = 0.0; // last relative change
};
[[nodiscard]] Expected<SecondOrderResult> runSecondOrderStatic(const Model& model,
                                                               const SecondOrderOptions& options = {});

struct BucklingOptions
{
    StaticOptions base;
    int count = 1;           // factors wanted
    double tolerance = 1e-10; // relative, on the factors
};
struct BucklingMode
{
    double factor = 0.0;
    std::vector<Eigen::Vector3d> displacements; // per node, largest translation 1
    std::vector<Eigen::Vector3d> rotations;
    std::vector<std::optional<BeamResult>> beams; // at(x).displacement: the shape along the bar
};
struct BucklingResult
{
    std::vector<BucklingMode> modes; // increasing factors
    std::string note;                // REQ-008c-12: fewer factors than asked, and why
};
[[nodiscard]] Expected<BucklingResult> runLinearBuckling(const Model& model,
                                                         const BucklingOptions& options = {});
```

## 5. Dados e algoritmos

### 5.1 A barra exata com esforço normal (REQ-008c-02)

Linearização em torno do estado de referência com esforço normal N (e
cortantes e momentos de referência desprezados: são termos de flexo-torção
e Wagner, da 008d):

- equilíbrio de momentos na geometria deformada: M' = −Ω×M − (e₁ + u')×F −
  m ≈ (linear da 008b) − u'×(N e₁), onde u' é a inclinação do **eixo do
  centroide** (o termo do giro de torção pelo centro de cisalhamento, que
  acopla flexo-torção, fica de fora, como no §6 dos requisitos);
- cortante de Engesser: o cortante que deforma a seção é o perpendicular ao
  eixo deformado, Q = F⊥ − N u'⊥ = G As γ. Com u'⊥ = θ×e₁ + γ:
  u'⊥ = (θ×e₁ + F⊥ / G As) / (1 + N / G As).

Os dois entram em `systemMatrix` como termos lineares nos estados (u'
substituído pela expressão acima), e A continua constante para N constante.
Em Euler-Bernoulli, 1/G As = 0. Isso reproduz:

- Euler: Pcr = π²EI/(kL)² para um elemento por vão;
- Engesser: Pcr = Pe/(1 + Pe/G As);
- viga-coluna: as funções de Timoshenko-Gere em compressão (sen/cos) e
  tração (senh/cosh).

Escolheu-se Engesser, e não Haringx, porque é a forma que a energia
½G As γ² + ½N u'² da isoparamétrica dá. Assim, as duas formulações
convergem uma para a outra. Engesser também é a mais usada em normas e
textos (Timoshenko-Gere §2.18). Com N ≤ −G As, o fator 1 + N/G As se anula:
a barra está além de qualquer carga crítica de cortante, e o elemento
informa instabilidade (contagem infinita, §5.4).

**Subdivisão**: além das regras da 008b (crescimento ≤ e⁸, curvatura),
cada parte em compressão tem comprimento h tal que a carga crítica
biengastada da parte, Engesser com 4π²EI_min/h², fique acima de 4|N|. Isso
dá margem para Tdf não ser singular e garante que a parte não tem autovalor
próprio abaixo do λ (contagem, §5.4). Em tração, o crescimento √(N/EI)
entra na taxa da regra e⁸.

### 5.2 N variável ao longo da barra (REQ-008c-02, 04)

N varia com a carga axial no vão (pilar com peso próprio, carga axial
distribuída) e nos arcos, onde uma força fixa no espaço tem componente
axial que muda com a tangente. O `AxialProfile` guarda N em P partes iguais,
nos dois pontos de Gauss de cada uma (N linear na parte). P vem de
`axialParts`:

- 1 valor sem carga axial no vão, em barra reta;
- P = 1 com carga axial uniforme (N linear: exato);
- P = 4 com carga trapezoidal, parcial ou concentrada axial (quebras nos
  pontos de carga, calibrado no teste);
- nos arcos, P = ⌈ângulo/(π/16)⌉, até 32.

Cada parte de integração do motor (que pode ser menor que a do perfil, pelas
regras do §5.1) usa o **Magnus de 4ª ordem**:
Φ = exp(h/2·(A₁ + A₂) + √3h²/12·[A₂, A₁]), com A₁ e A₂ nos pontos de Gauss
da parte. A matriz aumentada das cargas (Van Loan, 008b §5.3) entra no
gerador do mesmo jeito. Erro global O(h⁴). A calibração (tarefa) escolhe
mais subdivisões, quando preciso, para erro ≤ 1e−8 no pilar de Greenhill e
no arco.

**De onde vem o perfil**: da solução corrente (segunda ordem) ou da linear
(flambagem):

- barra reta: N no início, pelos esforços de ponta, mais a integral exata
  da carga axial no vão;
- arco: `stateAt` nos pontos de Gauss (2P avaliações por arco, só onde há
  arcos);
- isoparamétrica: N nos dois pontos reduzidos, da deformação do próprio
  elemento (008b §5.5);
- treliça: N da barra.

### 5.3 Treliça, isoparamétrica e braço rígido (REQ-008c-03, 04, 02)

- **Treliça**: K_g = N/L·(I − e eᵀ) nos blocos de translação ([G −G; −G
  G]), exata na teoria de pequenas rotações (corda).
- **Isoparamétrica**: K_g = Σ_gauss w·|J|·N(ξ)·B⊥ᵀB⊥, com B⊥ = (I − t tᵀ)·∂u/∂s
  (inclinação total, coerente com Engesser), integrada com 3 pontos e N
  interpolado linearmente dos 2 pontos reduzidos. É linear em N, e a
  convergência com a malha é verificada contra o arco exato.
- **Braço rígido** (excentricidade da 008): a ponta do braço é
  x = x_n + θ×e + ½θ×(θ×e) + O(θ³). O termo quadrático, com a força P que o
  braço transmite à parte flexível, dá no bloco de rotações do nó a matriz
  simétrica H = ½(P eᵀ + e Pᵀ) − (P·e)·I, com o sinal da energia fixado
  pelo teste contra o braço modelado como barra muito rígida. Sem ele, um
  pilar com excentricidade na ponta perde parte do P-Δ.

### 5.4 Contagem de Sturm (Wittrick-Williams) (REQ-008c-08, 10)

Para N = λN_ref nas barras, o número de autovalores λ' < λ do problema não
linear é J(λ) = s{K(λ)} + Σ_e J₀,e(λ):

- s{K(λ)} são os pivôs negativos da LDLᵀ global (`negativePivots()`);
- J₀,e(λ) é o número de autovalores da barra com as pontas presas, abaixo de
  λ. Como cada parte do motor é curta demais para ter autovalor próprio
  abaixo de λ (§5.1), J₀,e é o número de pivôs negativos nas condensações
  internas: as junções entre partes e os GDL internos das ligações (soltas
  ou com mola). Esses blocos têm no máximo 7×7, e a inércia sai do
  `SelfAdjointEigenSolver` do Eigen. A condensação usa a mesma
  decomposição, que aceita blocos indefinidos.
- treliças e isoparamétricas não têm GDL internos: J₀ = 0.

Na **segunda ordem**, J > 0 depois de uma iteração significa que a carga
está acima da primeira crítica: instabilidade. O nó e a direção vêm do
primeiro pivô negativo global ou, se só houver negativos internos, a barra
(ajuste do REQ-008c-08).

### 5.5 Iteração de segunda ordem (REQ-008c-05, 06, 08)

1. Etapas da análise linear (`static_stages`): validar, montar o `DofMap`,
   as cargas e os dados das barras. As barras retas passam à
   `ExactBeamElement` (o caminho fechado da 008 fica para N = 0, REQ-008c-14).
2. n₀ = perfis de N da solução linear.
3. Para k = 1, 2, …:
   - montar K(n_{k−1}) e as cargas equivalentes;
   - fatorar (`factorizeIndefinite`, reaproveitando a análise simbólica) e
     contar (J > 0 → instabilidade);
   - resolver;
   - calcular os perfis g(n_{k−1}) da nova solução;
   - medir a variação r = max(‖Δu‖/‖u‖, ‖Δn‖/‖n‖);
   - parar se r ≤ tolerância;
   - n_k = Anderson(g, histórico de até 5 resíduos).
4. Se um passo acelerado der J > 0, o histórico é zerado e o passo cai à
   metade, rumo ao último n estável (até 5 vezes), antes de declarar
   instabilidade. Isso evita alarmes falsos perto da crítica, onde o ponto
   fixo puro converge devagar (taxa ≈ P/Pcr): o Anderson leva os casos com
   P/Pcr = 0,95 a convergir em menos de 50 iterações (teste).
5. Resultados pela mesma recuperação da análise linear, com os perfis
   finais nas barras. `at(x)` usa o motor com N, e portanto inclui N·δ.

### 5.6 Flambagem (REQ-008c-09 a 12)

1. Análise linear do caso de carga (todas as ações, inclusive temperatura e
   recalques, multiplicadas por λ) → perfis N_ref. Sem compressão em
   nenhuma barra → recusa (REQ-008c-12).
2. Limite de busca: λ_max = min sobre as barras de G As/|N_ref| (Engesser) e
   10¹²; acima dele não há fator a procurar.
3. **Estimativa e refinamento por deslocamento** (ADR 0007):
   - em σ (início 0), monta-se K(σ) e K'(σ) ≈ dK/dλ (no início, diferença
     central com passo 10⁻⁴·λ_escala; depois, a secante entre as duas
     últimas montagens, que sai de graça);
   - fatora-se K(σ);
   - iteração de subespaço (bloco de p = count + 4 vetores) sobre o
     problema linearizado K(σ)φ = −μ K'(σ)φ: soluções com a fatoração,
     Rayleigh-Ritz com o `GeneralizedSelfAdjointEigenSolver` denso do
     Eigen;
   - os μ dão as estimativas λ ≈ σ + μ;
   - para cada fator ainda não convergido, em ordem: σ ← estimativa, até
     |μ_min| ≤ tolerância·λ (convergência quadrática quando K' é a
     derivada; superlinear com a secante).
4. **Garantia** por contagem: J(λ_i·(1 − ε)) = i − 1 e
   J(λ_count·(1 + ε)) = count, com ε = 10⁻⁶. Se faltar fator (J maior que o
   esperado), bissecção entre os fatores conhecidos até isolar o que
   faltou, depois o refinamento do passo 3. Fatores múltiplos (simetria)
   aparecem como saltos de 2 em J e são devolvidos repetidos, com modos
   independentes.
5. **Modos**: vetor de K(λ_i) no espaço nulo (última iteração de
   subespaço), normalizado com a maior translação = 1 (se não houver
   translação, a maior rotação). As barras do modo são `BeamResult` com os
   perfis λ_i·N_ref e sem cargas. `at(x).displacement` dá a forma ao longo
   da barra (a senoide de Euler num pilar biarticulado).
6. Fatores pedidos acima de λ_max ou não encontrados → `note` com os fatores
   achados (REQ-008c-12). Fatores negativos (inversão da carga) não são
   procurados.

### 5.7 Custo (NFR-008c-02)

Na 008b, o motor reto sem empenamento custa uma exponencial de 14×14 por
barra (cerca de 10 a 20 µs). No pórtico de cerca de 5 mil barras do
benchmark, isso dá algo como 0,1 s por montagem. O peso fica na
fatoração.

- **Segunda ordem**: tipicamente 4 a 6 iterações, cada uma com montagem,
  fatoração numérica (a simbólica é reaproveitada), solução e perfis. A
  estimativa é de 2,5 a 3,5 vezes a linear (meta: 4).
- **Flambagem com 5 fatores**: de 10 a 20 fatorações, contando as das
  contagens. A estimativa é de 4 a 6 vezes a linear (meta: 6).

Uma tarefa inicial mede antes de construir os laços. Se a estimativa
falhar, para-se para propor a mudança da meta, como na 008b.

## 6. Fluxo

```
runSecondOrderStatic(model)
  stages = prepare(model)              // validate, DofMap, loads, beam setup
  linear = solveWith(stages, profiles = 0)
  n = profilesOf(linear)
  loop: K(n), f(n) → factorizeIndefinite → count → solve → g(n) → Anderson
  recover(stages, final solution, profiles)

runLinearBuckling(model)
  stages = prepare(model); linear = solveWith(stages, 0); nRef = profilesOf(linear)
  σ = 0: K(σ), K'(σ) → subspace → estimates
  for each wanted factor: shift until |μ| small; Sturm checks; bisect if needed
  modes: null vectors → normalize → BeamResult per beam with λ nRef
```

A montagem segue paralela por cores (oneTBB). Cada elemento escreve a sua
contagem de pivôs negativos no seu índice, e a soma é feita depois. Não há
estado compartilhado novo.

## 7. Tratamento de erros

| Situação | Comportamento | Requisito |
|----------|---------------|-----------|
| Problemas do modelo | As mensagens da 007 a 008b, antes de iterar | REQ-008c-13 |
| Mecanismo já na análise linear | A mensagem da 007 (nó e direção) | REQ-008c-13 |
| J > 0 numa iteração (após as 5 metades) | "The structure is unstable under these loads: the second-order stiffness lost its stability at iteration k (node n, direction d)" ou "(inside the element e)" | REQ-008c-08 |
| Limite de iterações | "The second-order analysis did not converge in k iterations (last change r)" | REQ-008c-08 |
| Flambagem sem compressão | "No bar is compressed by these loads: there is no buckling factor" | REQ-008c-12 |
| Menos fatores que o pedido | Resultado com os achados e `note` explicando (acima de λ_max ou além do limite de busca) | REQ-008c-12 |
| Pivô zero exato na fatoração da flambagem | Deslocar σ em 10⁻⁸ relativo e refatorar | REQ-008c-10 |

## 8. Estratégia de testes

| Teste | Tipo | Requisito | Referência/tolerância |
|-------|------|-----------|-----------------------|
| `test_beam_second_order`: motor com N constante: rigidez da barra contra as funções de estabilidade fechadas (2D e 3D, compressão e tração, EB e Engesser), barra subdividida igual à inteira (patch), N → 0 igual à 008b | unitário/verificação | REQ-008c-02 | 1e−10 |
| `test_beam_second_order`: viga-coluna biapoiada (carga uniforme e concentrada, compressão e tração), balanço com força lateral, `at(x)` ao longo | verificação | REQ-008c-02, 06, AC-1 | Timoshenko-Gere, 1e−8 |
| `test_beam_second_order`: N variável: pilar de Greenhill sob peso próprio (q L³/EI = 7,837347) e viga-coluna com carga axial trapezoidal contra integração muito refinada | verificação/convergência | REQ-008c-02 | 1e−8 (ajuste do REQ-008c-02) |
| `test_beam_second_order`: treliça (Kg contra a forma fechada; corda tensa), isoparamétrica (Kg; convergência h² ou melhor contra o exato), braço rígido contra barra muito rígida | unitário/verificação | REQ-008c-03, 04 | 1e−10; taxa documentada; 1e−6 |
| `test_second_order_analysis`: pórtico plano de um andar contra funções de estabilidade; 1/(1 − P/Pcr) na ordem de grandeza; iterações e variação no resultado; P/Pcr = 0,95 convergindo em < 50 | verificação | REQ-008c-01, 05, 06, AC-2 | 1e−8 |
| `test_second_order_analysis`: treliça (corda tensa, P-Δ global) | verificação | REQ-008c-03, AC-3 | 1e−8 |
| `test_buckling`: Euler nos quatro casos (2D e 3D, EB e Engesser), mola rotacional, liberação, Hetényi (número de semiondas), modos normalizados e forma senoidal ao longo da barra | verificação | REQ-008c-09 a 11, AC-4 | 1e−8 |
| `test_buckling`: pórtico deslocável e indeslocável, fatores em ordem, pilar com excentricidade, fatores repetidos por simetria, contagem de Sturm com fator perdido de propósito (estimativa ruim) e recuperado | verificação | REQ-008c-09, 10, AC-5 | 1e−8 |
| `test_buckling` e `test_second_order_analysis`: arcos (exato e isoparamétrico) contra cadeia de barras retas exatas refinada com extrapolação de Richardson | convergência | REQ-008c-04, AC-6 | tolerância documentada na tarefa |
| Falhas: acima da crítica, limite de iterações, só tração, instabilidade dentro da barra | unitário | REQ-008c-08, 12, AC-7 | mensagens |
| `test_linear_solver`: inércia de matrizes indefinidas conhecidas; reuso da análise simbólica | unitário | REQ-008c-08, 10 | exato |
| Suítes da 007 a 008b inalteradas; `test_allocations` com o perfil | regressão | REQ-008c-13, 14 | — |
| `bench_frame --second-order` e `--buckling 5` | benchmark | NFR-008c-02 | 4× e 6× a linear |

## 9. Alternativas consideradas

| Alternativa | Por que foi descartada |
|-------------|------------------------|
| Rigidez geométrica polinomial (K + Kg) nas vigas retas | Exige dividir os pilares (erro de 22% na carga de Euler com um elemento biarticulado); o usuário escolheu o exato |
| Funções de estabilidade em forma fechada | Uma fórmula por caso (Timoshenko, base elástica, ligações, cargas no vão); o motor já faz todos de uma vez e está verificado |
| Flambagem por autovalor linear K φ = −λ Kg φ (Spectra) | Não é exata com um elemento por vão; nova dependência para um problema que, com N exato, é não linear de qualquer jeito |
| Wittrick-Williams puro (só bissecção) | Exato, mas 30 a 40 fatorações por fator; a iteração com deslocamento converge em poucas e usa a contagem só para garantir |
| Ponto fixo sem aceleração | Converge com taxa P/Pcr: inviável perto da crítica com 50 iterações |
| Newton completo na segunda ordem | Exige a derivada da rigidez exata em relação a N de cada barra; o Anderson chega perto sem ela |
| Haringx no lugar de Engesser | Diverge da energia da isoparamétrica; Engesser é a referência mais comum |

## 10. Riscos e mitigação

- **LDLᵀ sem pivoteamento em matriz indefinida pode perder precisão perto
  de um pivô quase nulo** → σ nunca fica exatamente num fator (deslocamento
  de 10⁻⁸); a contagem só é lida em pontos afastados dos fatores (ε =
  10⁻⁶); teste de inércia em matrizes indefinidas.
- **Tempo** (NFR-008c-02) → medida no início (§5.7); a análise simbólica é
  reaproveitada; a secante evita montagens extras.
- **Anderson pode oscilar** → zerar o histórico e meio passo (§5.5); teste
  perto da crítica.
- **Perfil de N nos arcos com muitas avaliações de `stateAt`** → só onde
  há arcos; P limitado a 32.
- **Fatores repetidos** (estruturas simétricas) → contagem detecta a
  multiplicidade; subespaço com folga (count + 4).
- **Sinal e forma do termo do braço rígido** → teste contra barra muito
  rígida antes de usar.

## 11. Novas dependências

Nenhuma. O Eigen (`SelfAdjointEigenSolver`,
`GeneralizedSelfAdjointEigenSolver`, `SimplicialLDLT`) já está no projeto.
O Spectra, previsto em `architecture.md` para a análise modal, continua
fora até a feature modal (ADR 0007).
