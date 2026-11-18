# Requisitos — 008d beam-warping

Status: aprovado · Data: 2026-11-18

## 1. Contexto e objetivo

Duas features deixaram temas para esta, por decisão do mantenedor:

- **a 008b adiou a torção com empenamento** (teoria de Vlasov, 7º GDL). Perfis
  abertos de paredes finas (I, U, Z, formados a frio) resistem à torção
  sobretudo pelo empenamento impedido. A torção uniforme de Saint-Venant
  subestima muito a rigidez deles e ignora a tensão normal do bimomento. O
  motivo do adiamento foi o tempo do benchmark do pórtico, de 6,2 a 7,1 s
  contra a meta de 6 s. Ficaram prontos no `core`, sem acesso pelo `Model`:
  - Iw e ω das seções (REQ-008b-01);
  - o empenamento no motor exato de espaço de estados e no elemento exato,
    já verificado.
- **a 008c adiou a flexo-torção**: a instabilidade lateral com torção de
  vigas fletidas, a flambagem por torção e por flexo-torção de pilares e o
  termo de Wagner. Hoje a segunda ordem e a flambagem consideram só o
  esforço normal no eixo do centroide, o que superestima a capacidade de
  vigas e pilares de perfil aberto.

Esta feature acrescenta ao `core` as duas coisas, em vigas retas e curvas:

- a torção com empenamento na análise estática linear;
- a flexo-torção na análise de segunda ordem e na flambagem, com ou sem
  empenamento.

Como nas features anteriores, tudo é exercitado por testes; nada muda na
interface.

## 2. Histórias de usuário

- **US-1** Como engenheiro, quero considerar o empenamento na torção de
  perfis abertos, com as condições reais das pontas (livre, impedido,
  elástico, contínuo com a barra vizinha), para obter giros, bimomentos e
  tensões de empenamento corretos.
- **US-2** Como engenheiro, quero o momento crítico de flambagem lateral com
  torção das vigas e a carga crítica de flambagem por torção e por
  flexo-torção dos pilares. Esses valores devem considerar a distribuição de
  momentos, a altura da carga na seção, a monossimetria do perfil e as
  condições de empenamento das pontas, sem fórmulas de norma nem
  coeficientes C1, C2 e C3 à mão.
- **US-3** Como engenheiro, quero a segunda ordem com o giro de torção
  amplificado pela flexão, para obter os esforços de vigas e pilares
  esbeltos de perfil aberto.
- **US-4** Como engenheiro, quero a flambagem fora do plano de arcos e de
  vigas curvas em planta.
- **US-5** Como engenheiro, quero saber exatamente o que falta ou está errado
  no modelo antes de confiar no resultado.
- **US-6** Como desenvolvedor das próximas features (interface 010,
  resultados 012, diagramas 013), quero o bimomento, a divisão da torção e o
  empenamento em qualquer ponto das vigas, pela mesma interface da 008.

## 3. Requisitos funcionais (EARS)

Os requisitos de empenamento retomam os REQ-008b-07 a 11, 18 a 20 e 22,
removidos da 008b em 2026-11-16, com os ajustes desta feature.

### Torção com empenamento (estática linear)

| ID | Requisito | História |
|----|-----------|----------|
| REQ-008d-01 | Cada atribuição de viga 3D DEVE poder ativar o empenamento. A teoria é a de Vlasov, sem deformação por cisalhamento do empenamento, combinada com a flexão de Euler-Bernoulli ou de Timoshenko. A viga com empenamento DEVE ter um 7º GDL por nó, a taxa de giro θ' (o empenamento). A torção em torno do centro de cisalhamento DEVE ser a torção mista E·Iw·θ'''' − G·J·θ'' = mx. | US-1 |
| REQ-008d-02 | A viga reta com empenamento DEVE dar deslocamentos, rotações e empenamentos nodais exatos da teoria. Isso vale para cargas nodais e para as cargas no vão, também com base elástica, como no motor exato da 008b. Com G·J → 0 o resultado DEVE tender à torção de empenamento pura; com E·Iw → 0, à torção uniforme da 008. | US-1 |
| REQ-008d-03 | Cada extremidade de uma viga com empenamento DEVE poder ter uma destas condições de empenamento: **livre** (bimomento nulo); **impedido** (θ' = 0, por exemplo chapa de topo ou enrijecedor); **elástico** (mola de bimomento kw ≥ 0, B = kw·θ'); **contínuo** (as pontas contínuas que chegam ao mesmo nó compartilham o GDL de empenamento do nó). O padrão DEVE ser contínuo entre pontas de barras com empenamento cujas tangentes no nó são colineares (tolerância documentada) e livre nas demais. | US-1 |
| REQ-008d-04 | O sistema DEVE aceitar, nos nós com GDL de empenamento, a restrição do empenamento (apoio, valor zero) e o bimomento nodal aplicado, com a convenção de sinais documentada, e DEVE devolver a reação de bimomento. | US-1 |
| REQ-008d-05 | As liberações, as molas de ligação, as excentricidades e a base elástica da 008 DEVEM continuar valendo em vigas com empenamento. A condição de empenamento vale na ponta do trecho flexível, independente das liberações de esforços. A base elástica (inclusive a de torção) entra nas equações da viga com empenamento, e os resultados continuam exatos (REQ-008d-02). | US-1 |
| REQ-008d-06 | As vigas curvas da 008b DEVEM aceitar o empenamento nas duas formulações. Na formulação **arco**, os resultados nodais DEVEM ser exatos para a teoria de viga curva com empenamento escolhida no design. Na **isoparamétrica**, o empenamento DEVE ser um campo próprio, livre de travamento, convergindo com a taxa documentada. As duas formulações DEVEM concordar entre si com o refinamento, e um arco de raio grande DEVE tender à viga reta com empenamento. | US-1 |
| REQ-008d-07 | QUANDO for pedida a análise estática (linear ou de segunda ordem), O sistema DEVE devolver, além dos resultados da 008 e da 008c: o empenamento θ' nos nós que o têm; a reação de bimomento nos empenamentos restringidos; por viga, o bimomento B e a divisão da torção em Saint-Venant (Tsv) e de empenamento (Tw) nas pontas do trecho flexível. | US-1, US-6 |
| REQ-008d-08 | O sistema DEVE permitir avaliar, em qualquer posição de vigas retas e curvas com empenamento, o bimomento, Tsv, Tw e o empenamento, além do que a 008b já dá, pela mesma interface (`BeamResult::at`): exatos onde o REQ-008d-02 e o arco do REQ-008d-06 os exigem; na isoparamétrica, os da solução aproximada. | US-6 |
| REQ-008d-09 | As tensões da 008 e da 008b DEVEM incluir, em vigas com empenamento, a tensão normal de empenamento σw = B·ω/Iw e a tensão de cisalhamento da torção de empenamento (fórmula de paredes finas documentada), nos pontos com ω conhecido: os pontos característicos das formas paramétricas e os pontos da seção de paredes finas. | US-1 |

### Flexo-torção (segunda ordem e flambagem)

| ID | Requisito | História |
|----|-----------|----------|
| REQ-008d-10 | Na segunda ordem e na flambagem, toda viga 3D DEVE considerar a flexo-torção, com ou sem empenamento ativado. Os esforços de referência da barra (N, momentos fletores My e Mz, cortantes e torque, variáveis ao longo dela) DEVEM entrar na rigidez pela teoria linearizada de flexo-torção documentada no design. Essa teoria DEVE incluir: o acoplamento do giro de torção com os momentos fletores; o termo de Wagner com o raio de giração polar em relação ao centro de cisalhamento; a excentricidade do centro de cisalhamento em relação ao centroide; os coeficientes de monossimetria. As vigas 2D seguem como na 008c. | US-2, US-3 |
| REQ-008d-11 | Na viga reta, a flexo-torção DEVE ser exata para a barra, como o REQ-008c-02 é para o esforço normal: um elemento por vão DEVE dar o momento crítico exato de uma viga sob momento uniforme e a carga crítica exata de flambagem por torção e por flexo-torção de um pilar, nas duas teorias de flexão, com e sem empenamento. O mesmo vale com ligações, molas, excentricidades, base elástica e as condições de empenamento do REQ-008d-03. Com momentos variáveis na barra, o resultado DEVE ter a precisão documentada no design. | US-2, US-3 |
| REQ-008d-12 | As seções DEVEM fornecer o raio de giração polar em relação ao centro de cisalhamento e os coeficientes de monossimetria βy e βz (integrais de seção documentadas). Eles DEVEM ser calculados nas oito formas paramétricas (fórmulas fechadas ou de paredes finas), na seção de paredes finas (pela linha média) e na poligonal maciça (exatos, pelas fórmulas de polígono). A seção genérica DEVE aceitar βy e βz opcionais, com padrão zero (seção duplamente simétrica), e o sistema DEVE documentar essa hipótese. | US-2 |
| REQ-008d-13 | As cargas no vão das vigas 3D (distribuídas e concentradas) DEVEM aceitar, opcionalmente, o ponto de aplicação na seção (coordenadas locais y, z em relação ao centroide). O padrão DEVE ser o centroide, como na 008. Na análise linear, esse ponto DEVE mudar só o torque da carga em relação ao centro de cisalhamento. Na segunda ordem e na flambagem, DEVE entrar também a parcela de segunda ordem da altura da carga, que desestabiliza a carga acima do centro de cisalhamento e estabiliza a carga abaixo dele. O peso próprio age no centroide. | US-2, US-3 |
| REQ-008d-14 | Nas vigas curvas (arco e isoparamétrica), a flexo-torção DEVE entrar na segunda ordem e na flambagem, inclusive a flambagem fora do plano, com a teoria escolhida no design e verificada contra uma referência documentada, com a tolerância do design. | US-4 |
| REQ-008d-15 | Os modos de flambagem DEVEM trazer, além do que a 008c já dá, o giro de torção e o empenamento dos nós e ao longo das vigas. A normalização da 008c DEVE ser mantida; o modo puramente de torção é normalizado pelo maior giro de torção igual a 1. | US-2, US-6 |
| REQ-008d-16 | As recusas da 008c (REQ-008c-08 e 12) DEVEM valer também para a instabilidade por flexo-torção, com a mensagem indicando o elemento quando a instabilidade está dentro de uma barra. | US-3, US-5 |

### Modelo, validação e arquitetura

| ID | Requisito | História |
|----|-----------|----------|
| REQ-008d-17 | ANTES de montar, O sistema DEVE validar o modelo e listar todos os problemas, além dos da 008 a 008c: empenamento em viga 2D; empenamento com Iw ausente ou ≤ 0; mola de empenamento negativa; condição, restrição ou bimomento de empenamento em ponta ou nó sem empenamento; ponto de aplicação de carga em viga 2D. | US-5 |
| REQ-008d-18 | SE a estrutura for um mecanismo, ENTÃO O sistema DEVE informar ao menos um nó e a direção sem rigidez, incluindo o empenamento. Se todas as pontas no nó forem livres, a mensagem DEVE sugerir impedir ou restringir o empenamento. | US-5 |
| REQ-008d-19 | Modelos sem empenamento DEVEM dar, na análise linear, os mesmos resultados da 008 e da 008b, e os testes da 007, da 008 e da 008b DEVEM continuar passando. Na segunda ordem e na flambagem, os testes da 008c DEVEM continuar passando. Quando a flexo-torção do REQ-008d-10 mudar um resultado (por exemplo, um modo de torção abaixo do de flexão), o teste é revisto com a referência analítica, e a mudança fica registrada em `tasks.md`. | US-6 |
| REQ-008d-20 | As opções novas das análises, se houver, DEVEM ser parâmetros das funções de análise, com padrões, como no REQ-008c-13. Tudo DEVE ficar em `core`, sem Qt. | US-6 |

## 4. Requisitos não funcionais

| ID | Requisito | Como medir |
|----|-----------|------------|
| NFR-008d-01 | Os resultados exatos (REQ-008d-02, o arco do REQ-008d-06 e o REQ-008d-11) DEVEM reproduzir as soluções analíticas dos casos de verificação com erro relativo ≤ 1e−8 em deslocamentos, rotações, empenamentos, reações, esforços, bimomentos, tensões, fatores críticos e momentos críticos. Os casos com referência tabelada ou numérica (momentos variáveis, arcos, isoparamétrica) usam a tolerância documentada no design. | Testes automatizados |
| NFR-008d-02 | O design DEVE medir o pórtico espacial do benchmark da 008 com empenamento em todas as vigas, na análise linear, na segunda ordem e na flambagem com 5 fatores, e propor as metas de tempo para aprovação. Sem empenamento, as metas da 008b (NFR-008b-03) e da 008c (NFR-008c-02) DEVEM continuar valendo. As medidas são em Release, na máquina do mantenedor. | Benchmark |
| NFR-008d-03 | A cobertura de linhas de `core` DEVE continuar em pelo menos 80% (constituição §6.5). | Job de cobertura no CI |

## 5. Critérios de aceite

- [ ] **AC-1** (REQ-008d-01, 02, 04, 07, 08, NFR-008d-01) Torção com
  empenamento em viga reta, contra a solução em senh/cosh:
  - balanço de perfil I com torque na ponta e engaste impedindo o
    empenamento (giro, B, Tsv e Tw ao longo da barra);
  - biapoiada com torque distribuído e concentrado, com extremidades livres
    e impedidas;
  - os limites G·J → 0 e E·Iw → 0;
  - bimomento nodal aplicado e reação de bimomento.
- [ ] **AC-2** (REQ-008d-03, 05) Condições de empenamento:
  - viga contínua de dois vãos com empenamento contínuo, igual a uma barra
    só;
  - T de três barras: contínuo nas colineares, livre na terceira;
  - chapa de topo (impedido);
  - mola de empenamento, com kw → 0 dando livre e kw → ∞ dando impedido,
    e um valor intermediário contra a solução analítica;
  - liberação de torção e excentricidade com empenamento;
  - base elástica de torção contra a solução analítica.
- [ ] **AC-3** (REQ-008d-06, 08) Viga curva com empenamento:
  - arco de perfil I em planta com raio grande, igual à viga reta com
    empenamento;
  - arco e isoparamétrica concordando com o refinamento;
  - caso de referência da literatura documentado no design (por exemplo,
    viga curva em planta de Yang e Kuo, 1987).
- [ ] **AC-4** (REQ-008d-09) Tensões de empenamento: σw e τw nos pontos
  característicos de perfis I e U e nos pontos da seção de paredes finas,
  contra as fórmulas.
- [ ] **AC-5** (REQ-008d-10, 11, 12, 13, NFR-008d-01) Flambagem lateral com
  torção de vigas retas, um elemento por vão:
  - perfil I biapoiado com apoio de garfo sob momento uniforme, contra
    Mcr = (π/L)·√(E·Iz·G·J·(1 + π²·E·Iw/(G·J·L²))), com e sem empenamento
    ativado (seção retangular alta: Iw = 0);
  - empenamento impedido nas pontas;
  - balanço;
  - momento linear, carga uniforme e carga concentrada no meio, contra
    referências documentadas no design;
  - carga na mesa superior, no centro de cisalhamento e na mesa inferior;
  - perfil I monossimétrico (mesas desiguais) sob momento uniforme, contra
    a solução fechada com βz.
- [ ] **AC-6** (REQ-008d-10, 11, 12, 15, NFR-008d-01) Flambagem de pilares
  por torção e por flexo-torção:
  - perfil duplamente simétrico de torção crítica baixa (por exemplo,
    cruciforme), contra Pcr,T = (G·J + π²·E·Iw/L²)/r0²;
  - perfil monossimétrico (U ou T), contra a solução fechada da flambagem
    por flexo-torção;
  - modos normalizados com giro e empenamento.
- [ ] **AC-7** (REQ-008d-10, 13, 16) Segunda ordem com flexo-torção:
  - viga sob momento uniforme abaixo do crítico, com uma pequena carga
    lateral ou torque, com o giro amplificado contra a solução analítica;
  - o efeito da altura da carga no giro;
  - recusa acima do momento crítico, com a mensagem da instabilidade.
- [ ] **AC-8** (REQ-008d-14, NFR-008d-01) Flexo-torção em vigas curvas:
  - flambagem fora do plano de arco circular sob compressão uniforme e sob
    momento uniforme, contra as referências documentadas no design;
  - arco de raio grande igual à viga reta;
  - arco e isoparamétrica concordando com o refinamento.
- [ ] **AC-9** (REQ-008d-17, 18) Validação: cada problema do REQ-008d-17 gera
  uma mensagem com o item envolvido, e vários aparecem juntos; um mecanismo
  de empenamento (todas as pontas livres num nó sem rigidez) é informado
  com o nó, a direção e a sugestão.
- [ ] **AC-10** (REQ-008d-19, 20, NFR-008d-02, 03)
  - os testes da 007 a 008c passam, e as revisões da flexo-torção ficam
    registradas;
  - `core` segue sem Qt;
  - os benchmarks com e sem empenamento ficam registrados em `tasks.md`,
    dentro das metas aprovadas;
  - CI verde nas quatro plataformas, com a cobertura de `core` ≥ 80%.

## 6. Fora de escopo

- Variante de Benscoter (cisalhamento do empenamento), distorção da seção,
  empenamento secundário e flambagem local, distorcional e de chapas.
- Restrição lateral ou torcional contínua pela laje ou pelas terças, além
  da base elástica que já existe (mola de rotação em torno do eixo da barra
  e de translação).
- Ponto de aplicação para cargas nodais: o nó é comum a várias barras.
  Quem precisa modela um braço rígido.
- Grandes deslocamentos e rotações, pós-flambagem e imperfeições iniciais
  (pendências da 008c no roadmap).
- Tensões de empenamento na seção genérica (sem ω conhecido) e, na
  poligonal maciça, além do que a solução numérica dá.
- Coeficientes de norma (C1, C2, C3, χLT): o sistema calcula o momento
  crítico elástico; a verificação de norma é de uma feature futura.
- Interface, execução assíncrona e desenho dos modos e diagramas — 010 a 013.

## 7. Requisitos globais relacionados

- **REQ-G-12:** atribuição de seções, restrições e cargas a grupos e listas
  da malha.
- **REQ-G-14:** recusar modelos incompletos ou singulares indicando o que
  falta.
- **REQ-G-15:** análise estática linear e, para barras, segunda ordem e
  flambagem linear.
- **REQ-G-16:** verificação por soluções analíticas e referências.
- **REQ-G-22:** montagem paralela, herdada da 006.

## 8. Questões em aberto

- [x] Tamanho: uma feature só, com o empenamento e a flexo-torção (decidido
  em 2026-11-18).
- [x] Vigas curvas com empenamento nas duas formulações, arco e
  isoparamétrica (decidido em 2026-11-18).
- [x] Metas de desempenho com empenamento: medidas e propostas no design
  (decidido em 2026-11-18).
- [x] Entram a altura da carga, as seções monossimétricas e as molas de
  empenamento (decidido em 2026-11-18).
- [x] Flexo-torção sempre ativa em vigas 3D na segunda ordem e na
  flambagem, com ou sem empenamento (decidido em 2026-11-18).
- [x] Flexo-torção também nas vigas curvas, inclusive a flambagem fora do
  plano (decidido em 2026-11-18).
