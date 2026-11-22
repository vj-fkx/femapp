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

- [x] **T14** Testes de consistência (deve falhar), 1e−12:
  - K da barra reta simétrica com N, momentos, cortantes e empenamento;
  - só com N, igual à 008c;
  - barra cortada em duas e condensada igual à inteira.
  Arquivo: `tests/unit/core/test_flexural_torsional.cpp`. Cobre: 10, 11
- [x] **T15** Funcional de flexo-torção em `systemMatrix` (design §5.5,
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
- [x] **T16** Altura da carga (design §5.5): k_q = λ·(q·d) no termo do giro
  e salto ΔMx = λ·(P·d)·θx na concentrada; o centroide como padrão em seção
  monossimétrica. Testes:
  - carga uniforme e concentrada na mesa superior, no centro e na mesa
    inferior: 1e−6 contra um Rayleigh-Ritz independente no teste e, contra
    as tabelas de C1 e C2 (NCCI SN003), 1% no centro de cisalhamento e 5%
    com altura *(ajustado em 2026-11-19: a fórmula de três fatores fica
    2,7 a 3,2% longe com a carga concentrada na mesa; decidido pelo
    mantenedor)*;
  - K simétrica com a altura (1e−12).
  Arquivos: `beam_state.cpp`, `beam_loads.cpp`,
  `test_flexural_torsional.cpp`. Cobre: 13
- [x] **T17** Regra de partes ampliada (design §5.6): limites de torção, de
  flexo-torção e da altura da carga; `kUnstableBar` além de `kMaxParts`.
  Testes: contagem igual com subdivisão extra; barra além das partes
  instável e limitada — arquivos: `beam_state.cpp`,
  `test_flexural_torsional.cpp` — cobre: 11, 16
- [x] **T18** [M] Medida de custo (design §5.9, risco do §10): no
  `bench_frame`, com e sem `--warping`, montar com o perfil completo e
  registrar contra as metas:
  - segunda ordem ≤ 4×;
  - flambagem com 5 fatores ≤ 12× (008c, sem empenamento) — mudada para
    ≤ 400× após a T18b;
  - as mesmas razões sobre a linear com empenamento.
  Se alguma não fechar, parar e propor otimização ou mudança da meta antes
  de seguir. Arquivos: `tests/perf/bench_frame.cpp`. Cobre: N02, 19

- [x] **T18b** Busca mais rápida dos fatores (decidido pelo mantenedor em
  2026-11-19, após a T18: flambagem de 704× e 282× a linear, meta 12×):
  - estimativas pelo pencil quadrático K0 − λG + λ²K2 quando o termo em λ²
    pesa na primeira estimativa linear: Rayleigh-Ritz na base de K0⁻¹Gx e
    K0⁻¹K2x, forma companheira, e K2 reajustado para passar por K(λ) na
    primeira estimativa;
  - cada fator perdido: bissecção na contagem global só até 10⁻³, iteração
    inversa e refinamento a partir da fatoração na ponta de baixo; se uma
    barra sobe a contagem interna no intervalo, a bissecção é só dela,
    confirmada pela contagem global; se nada disso fecha, a bissecção
    global vai até o fim.
  Meta mudada pelo mantenedor para ≤ 400× (NFR-008d-02, design §5.9).
  Testes: as suítes da 008c e desta feature dão os mesmos fatores —
  arquivos: `buckling.cpp`, `detail/static_stages.hpp`,
  `static_analysis.cpp`, `src/core/CMakeLists.txt` (`/bigobj`), ADR 0007
  (adendo) — cobre: N02, 11

## Fase 5 — Flambagem e segunda ordem

- [x] **T19** Flambagem com flexo-torção (design §5.8):
  - K(λ) com `reference.scaled(λ)` e `loadFactor = λ`;
  - `BucklingMode::warping`, e ψ e B nas barras do modo;
  - a recusa olha também os momentos.
  Testes (AC-5, AC-6):
  - momento linear, carga uniforme e concentrada no meio, 1e−6 contra a
    barra subdividida e 1% contra as tabelas de C1 (no momento linear,
    1e−6 contra Rayleigh-Ritz e 5% contra a tabela: decisão do mantenedor,
    2026-11-19);
  - balanço com carga na ponta;
  - modos normalizados com giro e ψ, e o modo de torção pelo giro;
  - caso só de tração e sem momento sem fator.
  Arquivos: `buckling.cpp`, `stability_analysis.hpp`, `test_buckling.cpp`,
  `test_flexural_torsional.cpp`. Cobre: 10, 11, 15, 16
- [x] **T20** [P] Pórtico em L de Argyris (flambagem lateral de pórtico em
  ângulo reto): consistência dos momentos nas juntas (ADR 0008), 1% contra
  Argyris (1979) e Yang e McGuire (1986) — arquivo:
  `test_flexural_torsional.cpp` — cobre: 10. Referência: dedução
  independente (decisão do mantenedor, 2026-11-19), 1e−6; a literatura
  (±622 N·mm) em 1%. Correção nas pontas da barra em `beam_state.cpp`
  (adendo do ADR 0008).
- [x] **T21** Segunda ordem com flexo-torção (design §5.8): o Anderson
  sobre o perfil completo, e a variação com giros, ψ e esforços. Testes
  (AC-7):
  - momento uniforme abaixo do crítico com pequena carga lateral ou torque:
    giro amplificado contra a viga-coluna lateral (Trahair, 1993, cap. 6),
    1e−6;
  - efeito da altura da carga no giro;
  - recusa acima de Mcr com a mensagem.
  Arquivos: `second_order.cpp`, `test_second_order_analysis.cpp`. Cobre:
  10, 13, 16
- [x] **T21b** Torque de referência (decidido pelo mantenedor em
  2026-11-20, após a T21; design §5.5, ADR 0008): o termo
  Mx(v'w'' − w'v'') no motor exato (curvaturas e linhas de momento,
  simétrico), o limite das partes pelo Greenhill da parte engastada, e a
  recusa da flambagem olhando também o torque. Testes:
  - K simétrica com torque;
  - eixo de Greenhill (rótulas esféricas, torque nas pontas):
    2π·EI/L, 1e−8;
  - pórtico em L com torque em AB que vira flexão em BC, contra a barra
    linearizada: ±27967,6735 N·mm, 1e−6, com uma barra ou duas por membro.
  Arquivos: `beam_state.cpp`, `buckling.cpp`, `test_flexural_torsional.cpp`
  — cobre: 10, 11, 16
- [x] **T22** Revisão das suítes da 008c com a flexo-torção sempre ativa:
  cada resultado que mudar é conferido contra a referência analítica e
  registrado nas notas (o que mudou e por quê) — arquivos: testes da 008c,
  notas — cobre: 19

## Fase 6 — Vigas curvas e isoparamétrica

- [x] **T23** Flexo-torção no arco exato (design §5.5, no referencial
  móvel). Testes (AC-8), com as referências revistas pelo mantenedor em
  2026-11-22 (a cadeia de cordas retas não converge para a viga curva na
  precisão prevista):
  - momento uniforme (viga de Yoo): forma fechada das Eqs. 32 de Yang e
    Kuo com r = 0 e o empenamento de Vlasov, 1e−8, com e sem empenamento;
    Eq. 34 publicada em 1%; cadeia de 32 cordas com empenamento contínuo
    em 1%;
  - compressão uniforme: forma fechada da mesma teoria (com r = 0 é a
    fórmula de Timoshenko e Gere), 1e−6; cadeias de 32 e 64 cordas
    extrapoladas em 1%;
  - raio grande: a média dos dois momentos críticos igual à reta (1e−8).
  Arquivos: `beam_state.cpp`, `test_buckling.cpp`. Cobre: 14, N01
- [x] **T24** Rigidez geométrica completa da isoparamétrica (design §5.3).
  Testes:
  - só com N igual à 008c (as suítes da 008c seguem);
  - os casos da T23 convergindo para o arco (16, 32 e 64 elementos) com
    taxa ≥ 3 e 1e−5 (decisão do mantenedor, 2026-11-22), com áreas de
    cisalhamento;
  - viga reta isoparamétrica monossimétrica com a carga na mesa superior,
    taxa ≥ 3,5 e 1e−5 contra a barra exata;
  - zero alocações com o perfil completo.
  A segunda ordem do arco com flexo-torção foi para a T24b.
  Arquivos: `beam_iso.cpp`, `linear_solver.cpp`, `test_flexural_torsional.cpp`,
  `test_allocations.cpp`. Cobre: 10, 14
- [x] **T24b** A segunda ordem como P-δ (decidido pelo mantenedor em
  2026-11-22, após a pesquisa da T24b): a barra linear em torno da
  configuração indeformada, alimentada pelos esforços espaciais do
  equilíbrio deslocado. A rotação da seção antes da flambagem (Kirby e
  Nethercot) fica de fora e vai para a 008e (análise não linear
  geométrica). Os elementos passam a realimentar os esforços espaciais:
  - barras retas: a estática exata mais a diferença entre os esforços
    espaciais do estado e a estática, amostrada ao longo da barra (o P-δ da
    deformada da própria barra);
  - arco: δm = m_e + P(M0)·θ em vez do estado m_e;
  - isoparamétrica: δm = C·κ + θ×M0 e n = C·Γ + θ×n0.
  Testes: balanço reto, uma e quatro barras concordando em 1e−6 e a
  iteração P-δ independente em 1e−3 (ela é não simétrica, ver as notas);
  segunda ordem de um arco com flexo-torção, isoparamétrica convergindo
  para o arco (taxa ≥ 3, 1e−4); as suítes da 008c seguem. O design §5.8
  registra a definição e o que fica de fora (o README não tem seção de
  comportamento das análises). Arquivos: `static_analysis.cpp`, `beam_state.cpp`,
  `beam_iso.cpp`, `test_second_order_analysis.cpp`,
  `test_flexural_torsional.cpp` — cobre: 10, 14

## Fase 7 — Desempenho e documentação

- [x] **T25** [M] Benchmarks finais com e sem `--warping`: linear,
  `--second-order` e `--buckling 5`, registrados nas notas contra as metas
  do design §5.9; sem empenamento, a meta da 008b (< 5 s) e as da 008c —
  arquivos: `tests/perf/bench_frame.cpp` — cobre: N02
- [x] **T26** `specs/architecture.md` §3: empenamento no modelo, perfil de
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
- T14: "só com N, igual à 008c" vale para os planos de flexão; a torção
  ganha o termo de Wagner N·r0² mesmo em seção duplamente simétrica
  (G·J → G·J + N·r0²), e o teste confere isso. O termo de Wagner usa r0²
  inteiro (em relação ao centro de cisalhamento): a inclinação do
  centroide dá N·κx·(ez·θz + ey·θy) e o resto vem em K. A regra de partes
  também passou a subdividir onde os momentos variam (mudança de
  M·h/√(EI·GJ), como a de N), senão o Magnus de uma parte só não fecha a
  barra cortada em duas.
- T15: os termos de flexo-torção entram em `systemMatrix` pelo ponto de
  referência: Wagner G·J + K (K = N·r0² + βy·My − βz·Mz); a inclinação do
  centroide com o giro (N·ez·κx, N·ey·κx); e os momentos na seção girada,
  m = M − θ×M0 no giro, na flexibilidade de flexão, com o conjugado na
  torção. Os casos fechados passam em 1e−8 com o sinal de θ×M0 tal como
  escrito. Na flambagem, três mudanças, achadas nos casos fechados e
  antecipadas da T17 e da T19:
  - a regra de partes tem os limites de torção (com empenamento) e de
    flexo-torção, e uma barra sem empenamento cujo K anula G·J fica
    instável (`kUnstableBar`): sem empenamento, a torção flamba em todo
    comprimento de uma vez;
  - com momento uniforme, K(λ) é par em λ (±M flambam igual) e o pencil
    linear K0·φ = μ·G·φ não tem estimativas (G nulo, ou só arredondamento,
    que dava estimativas absurdas): abaixo de |G| ≤ 1e−12·|K0|, só a
    contagem acha os fatores;
  - a bissecção dos fatores que os vetores perderam agora procura abaixo
    do fator achado (muitos fatores de torção dentro da barra abaixo do
    de Euler), limitada a 64 e parando no salto de uma barra além do
    alcance; a recusa da flambagem olha também os momentos (T19).
- T16: a carga a uma altura d do centro de cisalhamento entra como
  λ·(q·d) no termo do giro (distribuída) e como salto ΔMx = λ·(P·d)·θx
  (concentrada), com λ o `loadFactor` do perfil (1 na segunda ordem).
  A referência independente é um Rayleigh-Ritz no teste (120 senos), e
  o motor bate com ele em 1e−6 nas três alturas, com carga uniforme e
  concentrada no meio. O momento parabólico da carga uniforme pedia mais
  do que o perfil linear por partes dá (3e−4 com 4 partes; chegar a 1e−8
  pediria umas 700): na barra reta o motor passou a calcular o estado de
  referência pela estática exata, dos esforços de ponta e das cargas
  (`ReferenceProfile::statics`), em vez de interpolar. A segunda ordem
  ainda itera só N, sem a estática exata (T21). As cargas globais sobre
  arcos (forças fixas no espaço) ainda não têm o termo da altura.
- T17: os limites de torção e de flexo-torção vieram na T15; aqui entra
  o da altura da carga, a parte biengastada estável com a base de torção
  λ·(q·d) e as molas pontuais λ·(P·d) negativas (EIw·x² + G'·x ≥ 4|k|, e
  4G'/l ou 192EIw/l³ ≥ 4|k|). Uma barra ou três dão o mesmo fator (1e−8)
  com a carga na mesa superior e na torção de um pilar; sem empenamento
  com Wagner além de G·J, ou com um momento que pediria mais de 8192
  partes, a barra fica instável.
- T18 (`bench_frame --second-order --buckling 5`, Release, máquina do
  mantenedor): sem empenamento, linear 3,12 s, segunda ordem 7,32 s =
  2,34× (meta 4×), flambagem 2198 s = 704× (meta 12×); com empenamento,
  linear 5,28 s (1,69× a sem empenamento, meta 2,2×), segunda ordem
  11,85 s = 2,24×, flambagem 1489 s = 282×. Os primeiros fatores (15,28 a
  16,24 sem empenamento; 18,51 a 21,20 com) são flambagens laterais das
  vigas de 6 m sem contenção lateral, sob λ vezes o peso próprio: modos
  dentro das barras, que só a contagem acha. A meta da flambagem não
  fecha; o mantenedor escolheu otimizar a busca (T18b).
- T18b (mesma máquina, Release): sem empenamento, flambagem 1015 s =
  326× (era 704×), fatores 15,28 a 16,24 iguais; com empenamento, 1073 s
  = 204× (era 282×), fatores 18,51 a 21,20. No pórtico 2×2×4, 35 s → 7,4 s.
  O traço mostrou que os fatores "dentro das barras" eram, na maioria,
  fatores nodais que o refinamento pulava (ele converge para o fator mais
  perto do deslocamento, não da estimativa), e que uma contagem custa ~9 s
  (3× a linear): 12× não fecha. O mantenedor mudou a meta para ≤ 400×.
  Pendente para depois: no pórtico grande sem empenamento as estimativas
  ainda saem ~3× acima (48 contra 15,3) e σ cai a 0.
- T19: `BucklingMode::warping` traz ψ só nos nós com o GDL (empenamento
  contínuo entre barras); com o empenamento livre nas pontas, ψ fica dentro
  da barra e sai em `at(x)`. A normalização olha só translações e as três
  rotações (ψ é uma taxa de giro). No momento linear (ψ = 0) o C1 exato é
  1,804 para κ = 0,38, contra 1,879 da tabela (4%): a tabela ignora κ;
  Rayleigh-Ritz confere em 1e−6 e a tabela fica em 5% (decisão do
  mantenedor). O balanço com carga na ponta confere com γ = 4,012599
  (Timoshenko e Gere; a constante por shooting) em 1e−6.
- T20: a literatura não traz os apoios do pórtico de Argyris nos textos
  abertos (só o valor ±622 N·mm, que é o momento crítico de cada barra
  sozinha). A referência veio de uma dedução independente: cada membro como
  barra linearizada em vetores globais (dr' = θ×t, θ' = C⁻¹(dm − θ×M0),
  dm' = −t×dn), todos os vetores contínuos na junta, momentos aplicados
  semitangenciais; o fator é a raiz do determinante das condições de
  ponta (script em Python, solução por exponencial de matriz e Brent). Ela
  reproduz a barra sozinha (622,208) e dá, no pórtico com rótulas em Z
  (Argyris), ±622,208; com garfos, +176,3477 e −445,8604. O motor dava
  +397/−315 e ±311,1: a segunda variação da barra trazia, em cada ponta,
  meio acoplamento a mais entre o giro e as rotações de flexão
  ((t·wᵀ + w·tᵀ)/2, w = t×M), que se cancela entre barras colineares e
  partes mas não numa junta em ângulo. Removido em `stateBarMatrices`;
  nenhum teste anterior mudou (todos de barras retas ou colineares).
  Pendente: a mesma comparação com torque de referência mostra diferenças
  entre pontas (±T/2 entre as rotações de flexão das duas pontas) contra a
  barra linearizada; a verificar com a viga-coluna sob torque na T21
  (AC-7) e na isoparamétrica na T23.
- T21: o Anderson itera os seis esforços de cada ponto e, nas barras retas,
  os esforços do início que alimentam a estática exata. A viga-coluna
  lateral (torque ou carga lateral no meio, com e sem empenamento) confere
  com as fórmulas fechadas em 1e−6 com o eixo maior 10⁶ vezes mais rígido;
  uma barra linearizada independente confirma as fórmulas em 1e−15. Com a
  seção real a diferença é de 0,9% (torque) a 5,8% (carga lateral) e cai na
  proporção de E·Iy/E·Iz: é a deformação no plano maior antes do giro
  (Kirby e Nethercot, 1979), que a segunda ordem inclui e a teoria
  clássica não. Benchmark (Release): segunda ordem 19,5 s = 5,75× a linear
  sem empenamento e 26,9 s = 5,14× com (3 iterações; eram 2,34× e 2,24×
  com só N): cada montagem leva os momentos e divide as barras. O
  mantenedor mudou a meta para ≤ 6× (NFR-008d-02, design §5.9). O torque
  de referência nas juntas (nota da T20) fica para uma verificação à
  parte.
- T21b: o torque de referência faltava no funcional (design §5.5). A
  barra linearizada da T20 deu o pórtico em L com torque em AB e flexão em
  BC (±27967,67 N·mm) e o eixo de Greenhill engastado contra a flexão
  (T·L/EI = 8,9868189568 = 2,8606π, o valor clássico); o motor errava 39%
  no pórtico. Com os momentos semitangenciais dos estados, p = m − θ×M0/2,
  o acoplamento do torque entra com T/2 (My − T·θz/2 e Mz + T·θy/2 nas
  curvaturas, e o conjugado); com T inteiro o Greenhill saía 50% fora. Nos
  momentos fletores o termo de giro leva o momento inteiro porque a outra
  metade é o termo de ponta da T20. As partes ficam abaixo de
  2π·EI/(margem·|T|), e a recusa da flambagem olha também o torque. Os dois
  casos conferem em 1e−6 e 1e−8, com uma ou mais barras.
- T22: nenhuma expectativa nem tolerância das suítes da 008c mudou; as
  edições nos testes da 008c durante a 008d foram só a troca de
  `AxialProfile` por `ReferenceProfile`. Todas passam com as tolerâncias
  originais, e nenhum resultado delas muda: os casos são planos (a
  flexo-torção fica fora em 2D) ou espaciais só com N. O único termo novo
  que os alcança é o de Wagner, N·r0², no pilar "in space"; ele põe o fator
  de torção em G·J/r0² ≈ 2,3·10⁸ N, contra 2,5·10⁶ e 9,9·10⁶ N dos três
  fatores de flexão pedidos. Nos testes de barra, r0² = 0 no `BeamData`.
  O que muda é o pórtico do benchmark (Release, versão final da 008c
  construída à parte): os fatores passam de 47,57 a 57,56 (pilares, só N)
  para 15,28 a 16,24 (flambagem lateral das vigas de 6 m sob o peso
  próprio, que a 008c não via); a segunda ordem quase não muda (1,011× →
  1,010× o deslocamento linear). Os fatores da 008c são exatamente as
  estimativas do pencil linear da 008d no mesmo pórtico (47,9 a 57,5): é
  por isso que essas estimativas saem ~3× acima (pendência da T18b).
- T23: o arco exato errava a flexo-torção: dava quase o valor da viga reta
  (2998 contra 11121 kip·in a 30°). Os estados de momento do motor são
  m − P(M0)·θ, com P os termos de giro (e os do torque, T21b); no
  referencial móvel a derivada deles traz a curvatura, P(Ω×M0) + P·Ω −
  Ω·P nas linhas de momento, que faltavam (zero na barra reta). Com eles o
  arco bate com uma barra linearizada ao longo do círculo (sem
  empenamento) e com a forma fechada das Eqs. 32 de Yang e Kuo com r = 0
  e o empenamento de Vlasov em 1e−9. A Eq. 34 publicada fica 5e−4 a 8e−4
  acima: ela soma o jacobiano e as tensões radiais (termos em r²/R²) e
  usa outra medida de empenamento (θ'' + θ/R² em vez de θ'' + v''/R).
  Achados no caminho:
  - as cordas retas convergem devagar para a viga curva (sem empenamento,
    10547 → 10860 → 10964 contra 10972,6 a 30°; com empenamento contínuo
    nas dobras elas vão para outro modelo de empenamento; sob compressão,
    em primeira ordem): o mantenedor manteve a cadeia como verificação
    de 1%;
  - a compressão só é uniforme com o arco axialmente rígido: com
    A = 14,4 in² o encurtamento entre os pinos tira 28% do empuxo e flete
    o arco; o teste usa A = 1e8 (Wagner fica coberto nas barras retas);
  - braços rígidos para o garfo estragavam a escala da matriz e a
    contagem perto do fator (singular em 1e−8); os garfos vieram de
    liberações de My e Mz nas pontas com o nó travado nas rotações;
  - a contagem que encontra matriz singular passa a se afastar 1e−8, 1e−7
    e 1e−6 abaixo de λ (antes, três vezes 1e−8): o pivô nulo é relativo ao
    maior, e barras rígidas alargam a faixa singular em volta do fator;
  - com raio grande os dois momentos críticos se separam em primeira ordem
    em 1/R (≈ (EI + GJ)/2R); a média deles é a da viga reta.
- T24: a segunda variação da barra em componentes globais, com as
  deformações de segunda ordem Γ₂ = −θ×u' + θ×(θ×t)/2 e κ₂ = −θ×θ'/2 (o
  vetor de rotação, sem a curvatura de referência), dá o momento
  semitangencial do motor exato. N fica no termo da 008c; os cortantes
  entram na forma de pequenas deformações, −(v0·θ)(t·θ)/2 (com u' inteiro,
  o acoplamento com a deformação axial travava com 3 pontos, e o arco
  plano da 008c deixava de convergir); mais Wagner, a excentricidade do
  centro de cisalhamento e a altura da carga, como no motor exato. Achados:
  - a penalidade de Euler-Bernoulli (G·As = 1e6·EI/ℓ²) e a de Vlasov
    (1e6·EIw/ℓ²) estragam a escala das malhas finas: com 64 elementos o
    fator piorava (3e−5 e crescendo) e a segunda ordem não convergia; com
    áreas de cisalhamento a convergência é limpa (até 7e−8 com 64);
  - o pivô nulo da fatoração indefinida passou a ser julgado contra a
    diagonal da própria equação (antes, contra o maior pivô): as
    penalidades e as barras muito rígidas alargavam a faixa singular em
    volta de um fator e a busca parava;
  - na segunda ordem, os dois elementos concordam depois de uma iteração
    (a mesma rigidez), mas realimentam esforços em convenções diferentes:
    T24b.
- T24b (pesquisa): uma barra exata não linear (Kirchhoff com
  compressibilidade axial, tiro com Newton, cargas mortas), num balanço com
  carga P vertical e H lateral pequena na ponta, deu a referência; a
  linearizada clássica, a segunda ordem do motor e uma iteração P-δ
  independente foram medidas contra ela (d(y)/dH, P = 0,3 e 0,6·Pcr):
  clássica +0,73% e +3,8%; motor +0,37% e +2,2%; P-δ não simétrica
  +0,36% e +2,2% (a mesma parte). O que falta para a exata é a rotação da
  seção (energia cúbica ε₁·C·ε₂), que esforços realimentados não
  representam; uma primeira tentativa de a pôr como deformação imposta
  −ε₂ deu 72% de erro e ficou para a 008e, com o mantenedor. Achados:
  - nas barras retas, a estática a partir do início perdia o P-δ da
    própria barra: uma barra contra quatro diferia de até 9e−4; com os
    esforços espaciais dos estados amostrados em 4 partes, 1e−7;
  - o P-δ exato da barra linear é não simétrico quando o torque do P-δ
    varia ao longo dela; a rigidez simétrica do ADR 0008 (contagem de
    Sturm, Cholesky, gradiente conjugado) fica com a parte simétrica: a
    diferença é 1,2e−4 e 6,9e−4 aqui. Um termo −P(M0' − estática)·θ foi
    tentado e retirado: é quase todo antissimétrico e a simetrização da
    barra o descarta;
  - arco e isoparamétrica passam a realimentar os momentos espaciais e a
    isoparamétrica converge para o arco (5e−5 com 32 elementos; antes,
    parava a 4e−4); as forças da isoparamétrica ficam como na 008c (a lei
    linearizada do motor exato não distingue força material e espacial);
  - o teste de escala da T21 (lacuna proporcional a EIy/EIz) passou a
    "a lacuna some com o eixo maior rígido": com o P-δ da barra ela não é
    só de EIy/EIz (razão 840 para 1000).
  - custo (Release, benchmark): os estados de cada barra reta em cada
    iteração levaram a segunda ordem a 30× a linear; com os estados da
    barra resolvidos uma vez para todos os pontos (`statesAt`), 9,57× sem
    empenamento (30,1 s) e 8,06× com (42,4 s), 3 iterações. O mantenedor
    mudou a meta para ≤ 10× (NFR-008d-02, design §5.9).
- T25 (`bench_frame --second-order --buckling 5`, Release, máquina do
  mantenedor): sem empenamento, linear 3,135 s (meta da 008b < 5 s),
  segunda ordem 29,2 s = 9,32× (meta 10×), flambagem 1020 s = 325× (meta
  400×), fatores 15,2846 a 16,2486; com empenamento, linear 5,173 s
  (1,65× a sem, meta 2,2×), segunda ordem 42,9 s = 8,29×, flambagem 336 s
  = 65×, fatores 18,5037 a 21,1969. Todas dentro das metas. Os fatores
  mudaram pouco desde a T18b (15,2844 → 15,2846; 20,132 → 20,129) pelas
  correções da junta (T20), do torque (T21b) e do arco (T23); a flambagem
  com empenamento ficou mais rápida (204× → 65×).

