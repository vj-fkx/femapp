# Tarefas — 008d beam-warping

Status: em andamento
Design: [design.md](design.md) · Requisitos: [requirements.md](requirements.md)

Regras:
- Uma tarefa = uma mudança pequena, compilável e testável.
- Testes vêm antes ou junto da implementação que verificam.
- `[P]` marca tarefas que podem ser feitas em paralelo com a anterior.
- `[M]` marca tarefas que dependem de ação do mantenedor.
- Marque `[x]` somente com build e testes passando.
- Os IDs abaixo omitem o prefixo: `01` significa `REQ-008d-01`; `N01`
  significa `NFR-008d-01`.
- Em toda tarefa, as suítes da 007 a 008c continuam passando (19). Uma
  mudança de resultado da 008c pela flexo-torção só é aceita revista contra
  a referência analítica e registrada nas notas.
- Ponto de partida do empenamento no modelo: o código de `c19d42b`
  (anterior à T21b da 008b), adaptado ao que a 008c mudou (design §1).

## Fase 1 — Empenamento na estática linear

- [x] **T01** Interfaces do empenamento (design §4): `DofType::Warp`
  (8 tipos), `Assignment::warping`, `WarpingCondition` (Free, Restrained,
  Elastic, Continuous) e `WarpingEnd` com `stiffness`, `NodalForce::bimoment`,
  `StaticResult::warping` e `reactionBimoments`, `BeamForces::b`, `tsv`, `tw`
  e `BeamPoint::warping`. O `WarpingCondition` do elemento (beam_element.hpp)
  dá lugar a `BeamData::warpingSprings` e `warpingContinuous`. Sem uso ainda:
  as suítes passam sem mudança — arquivos: `dof_map.hpp/.cpp`,
  `model.hpp`, `static_analysis.hpp`, `beam_result.hpp`, `beam_element.hpp`,
  `beam_exact.cpp` — cobre: 01, 03, 04, 07
- [x] **T02** Testes da torção com empenamento em viga reta (AC-1, deve
  falhar): balanço de perfil I com torque na ponta e engaste impedido (giro,
  B, Tsv e Tw ao longo, por `at(x)`); biapoiada com torque distribuído e
  concentrado, pontas livres e impedidas; G·J → 0 e E·Iw → 0; bimomento
  nodal e reação de bimomento. Referência em senh/cosh, 1e−8 — arquivo:
  `tests/unit/core/test_beam_warping.cpp` — cobre: 01, 02, 04, 07, 08, N01
- [x] **T03** Caminho da análise com empenamento, a partir de `c19d42b`, até
  a T02 passar:
  - `beamSetup` com E·Iw;
  - condições resolvidas por ponta e GDL `Warp` por nó (design §5.2);
  - conjuntos por pontas contínuas;
  - restrição e bimomento nodal;
  - recuperação de ψ, B, Tsv e Tw nas pontas e em `at(x)`.
  Arquivos: `model_validation.cpp`, `detail/model_validation.hpp`,
  `static_analysis.cpp`, `detail/static_stages.hpp`, `beam_result.cpp`,
  `beam_exact.cpp`. Cobre: 01, 02, 04, 07, 08
- [x] **T04** Mola de empenamento (design §5.2): a ponta não contínua vira
  GDL interno condensado com kw (0 livre, kRigid impedido), inclusive na
  contagem de pivôs. Testes: kw → 0 igual a livre e kw → ∞ igual a
  impedido (1e−8), e um kw intermediário contra a solução analítica do
  balanço com mola de bimomento — arquivos: `beam_exact.cpp`,
  `test_beam_warping.cpp` — cobre: 03
- [x] **T05** Condições de empenamento (AC-2), 1e−8:
  - viga contínua de dois vãos com empenamento contínuo igual a uma barra;
  - T de três barras (contínuo nas colineares, livre na terceira);
  - chapa de topo;
  - liberação de torção e excentricidade com empenamento;
  - base elástica de torção contra a solução analítica.
  Arquivo: `test_beam_warping.cpp`. Cobre: 03, 05
- [x] **T06** [P] Validação e mecanismo (AC-9, parte do empenamento), cada
  um com o item citado e vários juntos:
  - empenamento em viga 2D;
  - Iw ausente ou ≤ 0, com a mensagem da forma que não empena;
  - mola de empenamento negativa;
  - `WarpingEnd`, restrição `Warp` ou bimomento em ponta ou nó sem
    empenamento;
  - mecanismo "warp" com a sugestão quando todas as pontas no nó são
    livres.
  Arquivos: `model_validation.cpp`, `static_analysis.cpp`,
  `test_beam_validation.cpp`. Cobre: 17, 18
- [x] **T07** [P] Tensões de empenamento (AC-4): `StressPoint::
  sectorialMoment` e `sectorialThickness` nas formas I e U e nos pontos de
  paredes finas; `PointStress::warpingNormal` (σw = B·ω/Iw) e
  `warpingShear` (τw = Tw·Sω/(Iw·t)), somadas a `normal` e `shear`. Testes
  contra as fórmulas, 1e−8 — arquivos: `section.hpp/.cpp`,
  `section_thin_walled.cpp`, `beam_result.cpp`,
  `test_section_thin_walled.cpp`, `test_beam_warping.cpp` — cobre: 09
- [x] **T08** Arco com empenamento (elemento exato em `Line3`): raio
  grande igual à reta com empenamento (1e−6); condições do §5.2 no arco —
  arquivos: `test_beam_curved.cpp` — cobre: 06, 08
- [x] **T09** Isoparamétrica com empenamento (design §5.3): ψ quadrático com
  GDL `Warp` nos três nós, penalidade k_ψ = 1e6·EIw/ℓ² com 2 pontos e mola
  nas pontas. Testes (AC-3):
  - arco e isoparamétrica concordando com o refinamento (1e−6, taxa
    ≥ 3,5);
  - viga curva em planta biengastada com empenamento impedido e carga
    vertical uniforme, contra a integração independente das equações de
    Vlasov por tiro com Runge-Kutta no teste, 1e−8 (no lugar de Yang e
    Kuo, 1987, cujos valores não estão abertos; decidido em 2026-11-18);
  - zero alocações em `compute`.
  Arquivos: `beam_iso.hpp/.cpp`, `test_beam_curved.cpp`,
  `test_allocations.cpp`. Cobre: 06, 08
- [x] **T10** [M] `bench_frame --warping` (pilares abertos, como em
  `c19d42b`), linear: registrar contra a meta de 2,2 vezes a linear sem
  empenamento (design §5.9). Se passar da meta, parar e propor — arquivos:
  `tests/perf/bench_frame.cpp` — cobre: N02

## Fase 2 — Seções e ponto de aplicação das cargas

- [x] **T11** r0², βy e βz (design §5.7). Testes:
  - formas por integrais de polígono, 1e−10 contra as fórmulas fechadas
    (Trahair); β = 0 nas duplamente simétricas;
  - paredes finas (I de mesas desiguais, U, T);
  - poligonal maciça;
  - genérica com e sem β informado.
  Arquivos: `section.hpp/.cpp`, `section_thin_walled.cpp`,
  `section_solid.cpp`, `test_section_thin_walled.cpp`,
  `test_section_solid.cpp`. Cobre: 12
- [x] **T12** [P] Ponto de aplicação das cargas no vão (design §4):
  - `point` em `DistributedLoad` e `ConcentratedLoad`, levado aos eixos
    principais e ao centro de cisalhamento em `SpanPiece` e `SpanPoint`;
  - na linear, só o torque muda. Teste: igual a um torque explícito, 1e−8;
    padrão no centroide igual à 008;
  - validação: ponto em viga 2D recusado.
  Arquivos: `model.hpp`, `beam_loads.hpp/.cpp`, `model_validation.cpp`,
  `test_beam_loads.cpp`, `test_beam_validation.cpp`. Cobre: 13, 17

## Fase 3 — Perfil de referência completo

- [x] **T13** `ReferenceProfile` (design §5.4) no lugar do `AxialProfile`:
  - seis esforços nos pontos de Gauss das partes, mais o `loadFactor`;
  - num `shared_ptr<const>` em `BeamData`;
  - `profilesOf` com os seis esforços (estática exata na reta, `stateAt`
    no arco, pontos reduzidos na isoparamétrica) e `setReference` em
    `StaticProblem`;
  - regra de P pelo esforço que mais varia.
  As suítes da 008c passam sem mudança, e `test_allocations` com o perfil
  cheio. Arquivos: `beam_element.hpp/.cpp`, `beam_state.cpp`,
  `beam_iso.cpp`, `detail/static_stages.hpp`, `static_analysis.cpp`,
  `second_order.cpp`, `buckling.cpp`, `test_beam_second_order.cpp`,
  `test_allocations.cpp`. Cobre: 10, 19, 20

## Fase 4 — Flexo-torção no motor exato

- [ ] **T14** Testes de consistência (deve falhar), 1e−12:
  - K da barra reta simétrica com N, momentos, cortantes e empenamento;
  - só com N, igual à 008c;
  - barra cortada em duas e condensada igual à inteira.
  Arquivo: `tests/unit/core/test_flexural_torsional.cpp`. Cobre: 10, 11
- [ ] **T15** Funcional de flexo-torção em `systemMatrix` (design §5.5,
  ADR 0008): Wagner (G·J + K), excentricidade do centro de cisalhamento na
  inclinação e acoplamento flexão-torção. Os sinais saem dos casos fechados,
  um elemento por vão, 1e−8:
  - Mcr com momento uniforme e apoio de garfo, com e sem empenamento
    (retângulo alto com Iw = 0);
  - empenamento impedido nas pontas;
  - I monossimétrico com βz;
  - Pcr,T do cruciforme;
  - flexo-torção de pilares U e T (raiz da cúbica).
  Até a T14 também passar. Arquivos: `beam_state.cpp`,
  `test_flexural_torsional.cpp`. Cobre: 10, 11, 12, N01
- [ ] **T16** Altura da carga (design §5.5): k_q = λ·(q·d) no termo do giro
  e salto ΔMx = λ·(P·d)·θx na concentrada; o centroide como padrão em seção
  monossimétrica. Testes:
  - carga uniforme e concentrada na mesa superior, no centro e na mesa
    inferior: 1e−6 contra a barra subdividida e 1% contra as tabelas de C2
    (Trahair, 1993; NCCI SN003);
  - K simétrica com a altura (1e−12).
  Arquivos: `beam_state.cpp`, `beam_loads.cpp`,
  `test_flexural_torsional.cpp`. Cobre: 13
- [ ] **T17** Regra de partes ampliada (design §5.6): limites de torção, de
  flexo-torção e da altura da carga; `kUnstableBar` além de `kMaxParts`.
  Testes: contagem igual com subdivisão extra; barra além das partes
  instável e limitada — arquivos: `beam_state.cpp`,
  `test_flexural_torsional.cpp` — cobre: 11, 16
- [ ] **T18** [M] Medida de custo (design §5.9, risco do §10): no
  `bench_frame`, com e sem `--warping`, montar com o perfil completo e
  registrar contra as metas:
  - segunda ordem ≤ 4×;
  - flambagem com 5 fatores ≤ 12× (008c, sem empenamento);
  - as mesmas razões sobre a linear com empenamento.
  Se alguma não fechar, parar e propor otimização ou mudança da meta antes
  de seguir. Arquivos: `tests/perf/bench_frame.cpp`. Cobre: N02, 19

## Fase 5 — Flambagem e segunda ordem

- [ ] **T19** Flambagem com flexo-torção (design §5.8):
  - K(λ) com `reference.scaled(λ)` e `loadFactor = λ`;
  - `BucklingMode::warping`, e ψ e B nas barras do modo;
  - a recusa olha também os momentos.
  Testes (AC-5, AC-6):
  - momento linear, carga uniforme e concentrada no meio, 1e−6 contra a
    barra subdividida e 1% contra as tabelas de C1;
  - balanço com carga na ponta;
  - modos normalizados com giro e ψ, e o modo de torção pelo giro;
  - caso só de tração e sem momento sem fator.
  Arquivos: `buckling.cpp`, `stability_analysis.hpp`, `test_buckling.cpp`,
  `test_flexural_torsional.cpp`. Cobre: 10, 11, 15, 16
- [ ] **T20** [P] Pórtico em L de Argyris (flambagem lateral de pórtico em
  ângulo reto): consistência dos momentos nas juntas (ADR 0008), 1% contra
  Argyris (1979) e Yang e McGuire (1986) — arquivo:
  `test_flexural_torsional.cpp` — cobre: 10
- [ ] **T21** Segunda ordem com flexo-torção (design §5.8): o Anderson
  sobre o perfil completo, e a variação com giros, ψ e esforços. Testes
  (AC-7):
  - momento uniforme abaixo do crítico com pequena carga lateral ou torque:
    giro amplificado contra a viga-coluna lateral (Trahair, 1993, cap. 6),
    1e−6;
  - efeito da altura da carga no giro;
  - recusa acima de Mcr com a mensagem.
  Arquivos: `second_order.cpp`, `test_second_order_analysis.cpp`. Cobre:
  10, 13, 16
- [ ] **T22** Revisão das suítes da 008c com a flexo-torção sempre ativa:
  cada resultado que mudar é conferido contra a referência analítica e
  registrado nas notas (o que mudou e por quê) — arquivos: testes da 008c,
  notas — cobre: 19

## Fase 6 — Vigas curvas e isoparamétrica

- [ ] **T23** Flexo-torção no arco exato (design §5.5, no referencial
  móvel). Testes (AC-8):
  - raio grande igual à reta (1e−6);
  - fora do plano sob compressão uniforme e sob momento uniforme, 1e−6
    contra a cadeia de barras retas exatas (Richardson) e 1% contra Yang
    e Kuo (1987) e Pi e Trahair.
  Se a literatura não fechar em 1%, parar e levar a escolha ao mantenedor.
  Arquivos: `beam_state.cpp`, `test_buckling.cpp`. Cobre: 14, N01
- [ ] **T24** Rigidez geométrica completa da isoparamétrica (design §5.3).
  Testes:
  - só com N igual à 008c;
  - os casos da T23 convergindo para o arco com taxa ≥ 3,5;
  - segunda ordem de um arco com flexo-torção (arco e isoparamétrica);
  - zero alocações.
  Arquivos: `beam_iso.cpp`, `test_buckling.cpp`,
  `test_second_order_analysis.cpp`, `test_allocations.cpp`. Cobre: 10, 14

## Fase 7 — Desempenho e documentação

- [ ] **T25** [M] Benchmarks finais com e sem `--warping`: linear,
  `--second-order` e `--buckling 5`, registrados nas notas contra as metas
  do design §5.9; sem empenamento, a meta da 008b (< 5 s) e as da 008c —
  arquivos: `tests/perf/bench_frame.cpp` — cobre: N02
- [ ] **T26** `specs/architecture.md` §3: empenamento no modelo, perfil de
  referência completo, flexo-torção e ADR 0008; a convenção dos momentos
  nodais no comentário de `NodalForce`; notas de implementação aqui —
  cobre: 20

## Fase 8 — Fechamento

- [ ] **T90** [M] Build e testes verdes nos jobs do CI, incluindo o de
  cobertura com `core` ≥ 80% (push com sua confirmação) — cobre: N03
- [ ] **T91** `clang-format` e `clang-tidy` sem pendências (também o do
  Linux, pelo CI); `core` sem headers Qt — cobre: 20
- [ ] **T92** Conferir os dez critérios de aceite de `requirements.md`
- [ ] **T93** Atualizar `specs/roadmap.md` para `concluída`

## Matriz de rastreabilidade

| Requisito | Tarefas | Testes |
|-----------|---------|--------|
| REQ-008d-01 | T01, T02, T03 | `test_beam_warping.cpp` |
| REQ-008d-02 | T02, T03 | `test_beam_warping.cpp` |
| REQ-008d-03 | T01, T04, T05 | `test_beam_warping.cpp` |
| REQ-008d-04 | T01, T02, T03 | `test_beam_warping.cpp` |
| REQ-008d-05 | T05 | `test_beam_warping.cpp` |
| REQ-008d-06 | T08, T09 | `test_beam_curved.cpp` |
| REQ-008d-07 | T01, T02, T03 | `test_beam_warping.cpp` |
| REQ-008d-08 | T02, T03, T08, T09 | `test_beam_warping.cpp`, `test_beam_curved.cpp` |
| REQ-008d-09 | T07 | `test_section_thin_walled.cpp`, `test_beam_warping.cpp` |
| REQ-008d-10 | T13, T14, T15, T19, T20, T21, T24 | `test_flexural_torsional.cpp`, `test_buckling.cpp`, `test_second_order_analysis.cpp` |
| REQ-008d-11 | T14, T15, T17, T19 | `test_flexural_torsional.cpp` |
| REQ-008d-12 | T11, T15 | `test_section_*.cpp`, `test_flexural_torsional.cpp` |
| REQ-008d-13 | T12, T16, T21 | `test_beam_loads.cpp`, `test_flexural_torsional.cpp`, `test_second_order_analysis.cpp` |
| REQ-008d-14 | T23, T24 | `test_buckling.cpp`, `test_second_order_analysis.cpp` |
| REQ-008d-15 | T19 | `test_buckling.cpp`, `test_flexural_torsional.cpp` |
| REQ-008d-16 | T17, T19, T21 | `test_flexural_torsional.cpp`, `test_second_order_analysis.cpp` |
| REQ-008d-17 | T06, T12 | `test_beam_validation.cpp` |
| REQ-008d-18 | T06 | `test_beam_validation.cpp` |
| REQ-008d-19 | todas, T13, T18, T22 | suítes da 007 a 008c |
| REQ-008d-20 | T13, T26, T91 | `check_no_qt_includes` |
| NFR-008d-01 | T02, T15, T23 | `test_beam_warping.cpp`, `test_flexural_torsional.cpp`, `test_buckling.cpp` |
| NFR-008d-02 | T10, T18, T25 | `bench_frame` |
| NFR-008d-03 | T90 | Job de cobertura no CI |

| Critério | Tarefas |
|----------|---------|
| AC-1 | T02, T03 |
| AC-2 | T04, T05 |
| AC-3 | T08, T09 |
| AC-4 | T07 |
| AC-5 | T15, T16, T19 |
| AC-6 | T15, T19 |
| AC-7 | T21 |
| AC-8 | T23, T24 |
| AC-9 | T06, T12 |
| AC-10 | T22, T25, T90, T91 |

## Notas de implementação

- T09: o mantenedor forneceu Yang e Kuo (1987), "Effect of Curvature on
  Stability of Curved Beams", J. Struct. Eng. 113(6). O artigo só tem
  exemplos de flambagem, nenhum estático; a T09 usa a referência
  independente (tiro com Runge-Kutta). Para a T23 ele dá, de forma
  fechada, o momento crítico de uma viga circular biarticulada sob My
  uniforme (fora do plano): C1·M² + C2·M + C3 = 0 (Eqs. 34 e 35, n = 1), com
  a seção de Yoo (1982): E = 29 000 ksi, G = 11 200 ksi, A = 14,4 in²,
  Iz = 273 in⁴, Iy = 93 in⁴, Cw = 2 070 in⁶, J = 1,414 in⁴, r = 5,042 in,
  L = 403,32 in, β de 0 a 180° (Figs. 6 e 7). A teoria deles inclui o
  jacobiano e as tensões radiais; a diferença para a nossa fica medida
  na T23 (tolerância de 1% do design).
- T09: na isoparamétrica, a tangente da curva quadrática quebra entre
  elementos (3,7e−3 com 4 elementos num quarto de anel), acima da
  tolerância de 1e−6 da continuidade padrão; o mantenedor manteve 1e−6, e
  a cadeia isoparamétrica declara `WarpingEnd Continuous` (design §5.2).
  Com isso, um quarto de anel com empenamento converge para o arco exato
  com taxa ≥ 3,5 a partir de 8 elementos (erro 2e−5 com 32). A referência
  independente do arco com empenamento (tiro com Runge-Kutta) precisa de
  kL pequeno (cerca de 3): com kL = 38 o tiro perde a precisão
  (e^(kL) ≈ 1e16) e deu uma falsa diferença de 7%.
- T10 (`bench_frame --warping`, Release, máquina do mantenedor, duas
  medidas): linear sem empenamento 3,16 e 3,37 s; com empenamento em todas
  as vigas (pilares em I) 5,29 e 5,31 s, 1,57 a 1,67 vez (meta 2,2); o
  maior deslocamento, 0,1435, é o do `c19d42b`.
- T11: βz < 0 num I com a mesa maior em cima, com y para cima (o termo
  −2ys domina); o sinal de β no funcional fica para os casos fechados da
  T15. Na poligonal, βy de um tê sai 1,8e−7 (−2zs com o centro de
  cisalhamento numérico), não zero.
- T12: o ponto de aplicação fica nas cargas do vão em eixos principais a
  partir do centroide (não do centro de cisalhamento, como dizia o §4 do
  design, ajustado). As forças fixas no espaço sobre arcos entram no motor
  por grupos de mesmo ponto, cada um com o torque P × c nos estados
  aumentados.
- T13: o perfil de referência guarda os seis esforços; o motor só usa N, e
  só trata N como variável onde ele varia (`axialVaries`), para que vigas
  com momento variável e N constante continuem no caminho constante da
  008c. Na reta, os esforços saem da estática exata (esforços de ponta e
  integrais das cargas), sem `stateAt`; sem cargas no vão, uma parte (M
  linear), com cargas, 4. A segunda ordem ainda itera só N (a T21 itera
  os seis). Os testes do `core` passam a ver `src/core` (os estágios da
  análise em `detail/`), como os do `io` e do `render`.
