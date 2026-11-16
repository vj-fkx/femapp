# Requisitos — 008b beam-warping-curved

Status: aprovado · Data: 2026-11-16

## 1. Contexto e objetivo

A 008 entregou a viga reta de 2 nós, com torção uniforme de Saint-Venant e
oito formas de seção paramétricas. Ela deixou para esta feature três temas,
por decisão do mantenedor:

- **torção com empenamento** (teoria de Vlasov, 7º GDL): perfis abertos de
  paredes finas (I, U, formados a frio) resistem à torção sobretudo pelo
  empenamento impedido, e a torção uniforme subestima muito a rigidez e
  ignora a tensão normal do bimomento;
- **viga curva** de 3 nós (`Line3` da malha), com a curvatura em qualquer
  direção em relação aos eixos da seção: arcos, anéis, vigas curvas em planta
  (balcões, passarelas) e, por encadeamento, hélices;
- **seções de forma livre**: polígono maciço com furos (concreto, caixões,
  perfis soldados) e perfil de paredes finas descrito pela linha média, com as
  propriedades calculadas numericamente.

Como na 007 e na 008, tudo é exercitado por testes, e a verificação segue a
constituição §5.1; nada muda na interface.

## 2. Histórias de usuário

- **US-1** Como engenheiro, quero considerar o empenamento na torção de
  perfis abertos, com as condições reais das pontas (livre, impedido, contínuo
  com a barra vizinha), para obter giros, bimomentos e tensões de empenamento
  corretos.
- **US-2** Como engenheiro, quero modelar barras curvas (arcos, anéis, vigas
  curvas em planta) com elementos curvos de verdade, em vez de muitas barras
  retas, com os mesmos recursos das vigas retas: cargas no vão, temperatura,
  ligações, excentricidades, base elástica e empenamento.
- **US-3** Como engenheiro, quero escolher, para a viga curva, entre a
  formulação exata em arco de circunferência e a isoparamétrica com nó do meio
  ativo, conforme a geometria e o que preciso ligar ao nó do meio.
- **US-4** Como engenheiro, quero descrever seções que não são uma das oito
  formas, por polígonos com furos ou pela linha média com espessuras, e obter
  automaticamente área, inércias, eixos principais, J, centro de
  cisalhamento, constante de empenamento e áreas de cisalhamento.
- **US-5** Como engenheiro, quero as tensões dessas seções e as de
  empenamento nos pontos da seção.
- **US-6** Como engenheiro, quero saber exatamente o que falta ou está errado
  no modelo antes de confiar no resultado.
- **US-7** Como desenvolvedor das próximas features (008c, interface 010,
  resultados 012, diagramas 013), quero esforços, bimomento e tensões
  avaliáveis em qualquer ponto de vigas retas e curvas, pela mesma interface
  da 008.

## 3. Requisitos funcionais (EARS)

### Seções

| ID | Requisito | História |
|----|-----------|----------|
| REQ-008b-01 | A seção genérica da 008 DEVE aceitar, opcionalmente, a constante de empenamento Iw ≥ 0 (em relação ao centro de cisalhamento). O sistema DEVE calcular Iw das oito formas paramétricas da 008 por fórmulas fechadas ou de paredes finas documentadas (Iw = 0 onde a teoria de paredes finas o anula, como cantoneira, perfil T e tubo circular) e a coordenada setorial ω nos pontos característicos de cada forma. | US-1, US-5 |
| REQ-008b-02 | O sistema DEVE oferecer a **seção poligonal maciça**: um ou mais contornos externos, cada um com zero ou mais furos, dados como polígonos no plano da seção (coordenadas y, z do desenho), de um único material. A, centroide, Iy, Iz, Iyz e eixos principais DEVEM ser exatos (fórmulas de polígono). J, centro de cisalhamento, Iw, Asy e Asz DEVEM ser calculados pela solução numérica dos problemas de empenamento de Saint-Venant e de cisalhamento por flexão numa triangulação própria da seção, em `core`, com refinamento ajustável e um padrão que atenda ao NFR-008b-02. | US-4 |
| REQ-008b-03 | O sistema DEVE oferecer a **seção de paredes finas por linha média**: segmentos retos entre pontos do plano da seção, cada um com sua espessura > 0, formando ramos abertos (inclusive ramificados) e células fechadas (uma ou mais), detectadas a partir das ligações entre os segmentos. A, inércias, J (Σ b·t³/3 nos ramos abertos e fluxo de Bredt nas células), centro de cisalhamento, Iw (coordenada setorial) e Asy, Asz (fluxo de cisalhamento) DEVEM vir da teoria de paredes finas, com as aproximações documentadas (cantos ignorados). | US-4 |
| REQ-008b-04 | SE um polígono tiver menos de três vértices, área nula, autointerseção, furo fora do seu contorno ou furo/contorno sobrepostos a outro, ou SE um segmento de paredes finas tiver comprimento nulo ou espessura ≤ 0, ou a seção de paredes finas for desconexa, ENTÃO O sistema DEVE recusar a seção informando o polígono, vértice ou segmento. | US-4, US-6 |
| REQ-008b-05 | As seções poligonal e de paredes finas DEVEM valer em todas as vigas da 008 e desta feature (com as mesmas regras de simetria da viga 2D) e, pela área, nas treliças da 007. | US-4 |
| REQ-008b-06 | O sistema DEVE calcular as tensões das novas seções em qualquer posição da barra: na poligonal, a tensão normal em qualquer ponto (y, z) e nos vértices, e as tensões de cisalhamento da torção e da cortante a partir da solução numérica, informando o ponto de máximo de von Mises; na de paredes finas, a tensão normal, as de cisalhamento (torção de Saint-Venant, fluxo de Bredt, cortante) e a de von Mises nas pontas e nos nós da linha média, nas duas faces da parede. | US-5 |

### Torção com empenamento

| ID | Requisito | História |
|----|-----------|----------|
| REQ-008b-07 | Cada atribuição de viga 3D DEVE poder ativar o empenamento (teoria de Vlasov, sem deformação por cisalhamento do empenamento), combinado com a flexão de Euler-Bernoulli ou de Timoshenko. A viga com empenamento DEVE ter um 7º GDL por nó, a taxa de giro θ' (empenamento), e a torção em torno do centro de cisalhamento DEVE ser a torção mista E·Iw·θ'''' − G·J·θ'' = mx. | US-1 |
| REQ-008b-08 | Sem base elástica, a viga reta com empenamento DEVE dar deslocamentos, rotações e empenamentos nodais exatos da teoria (solução em senh/cosh) para cargas nodais e para as cargas no vão do REQ-008b-15; com G·J → 0 e com E·Iw → 0 DEVE tender, respectivamente, à torção de empenamento pura e à torção uniforme da 008. | US-1 |
| REQ-008b-09 | O sistema DEVE permitir, em cada extremidade de uma viga com empenamento, a condição de empenamento: **livre** (bimomento nulo), **impedido** (θ' = 0, por exemplo chapa de topo ou enrijecedor) ou **contínuo** (as pontas contínuas que chegam ao mesmo nó compartilham o GDL de empenamento do nó). O padrão DEVE ser: contínuo entre pontas de barras com empenamento cujas tangentes no nó são colineares (tolerância documentada), livre nas demais. | US-1 |
| REQ-008b-10 | O sistema DEVE aceitar, nos nós com GDL de empenamento, a restrição do empenamento (apoio, valor zero) e o bimomento nodal aplicado, com a convenção de sinais documentada; a reação de bimomento DEVE ser devolvida. | US-1 |
| REQ-008b-11 | As liberações, molas de ligação, excentricidades e a base elástica da 008 DEVEM continuar valendo em vigas com empenamento: a condição de empenamento vale na ponta do trecho flexível, independente das liberações de esforços; a base elástica entra nas equações da viga com empenamento, e com ela os resultados DEVEM continuar exatos, como no REQ-008b-08 *(ajustado no design, 2026-11-16: o motor exato inclui a base elástica)*. | US-1 |

### Viga curva

| ID | Requisito | História |
|----|-----------|----------|
| REQ-008b-12 | O sistema DEVE fornecer a viga curva de 3 nós (`Line3` da malha: pontas e nó do meio), em 3D e em 2D (no plano XY), nas duas teorias de flexão e, em 3D, com ou sem empenamento. Cada atribuição DEVE escolher a formulação: **arco** ou **isoparamétrica**. | US-2, US-3 |
| REQ-008b-13 | Na formulação **arco**, a barra DEVE ser o arco de circunferência que passa pelos três nós, com GDL só nas pontas; o nó do meio só define a geometria. Sem base elástica, ela DEVE dar resultados nodais exatos da teoria de viga curva (incluindo o acoplamento entre flexão, esforço normal, torção e empenamento causado pela curvatura) para cargas nodais e para as cargas no vão. Nós colineares DEVEM dar a viga reta da 008 (ou desta feature, com empenamento). | US-2, US-3 |
| REQ-008b-14 | Na formulação **isoparamétrica**, a geometria DEVE ser a curva quadrática pelos três nós, com GDL nos três; o nó do meio DEVE poder receber apoios, cargas e outras barras. O elemento DEVE ser livre de travamento por cortante e de membrana (com a técnica documentada no design) e convergir com taxa documentada; nós colineares com o do meio no centro DEVEM reproduzir a viga reta em barra subdividida no patch test. | US-2, US-3 |
| REQ-008b-15 | Nas vigas curvas, os eixos locais DEVEM variar ao longo da barra: x tangente à curva, e y e z pela mesma regra da 008 (vetor de referência e ângulo de rotação), aplicada em cada ponto; a curvatura pode ter qualquer direção em relação aos eixos principais da seção. Liberações, molas de ligação, condições de empenamento, excentricidades (o trecho flexível passa pelas pontas deslocadas e pelo nó do meio) e base elástica (por unidade de comprimento de arco, eixos locais) DEVEM valer como na 008. Cargas no vão DEVEM aceitar posições ao longo do comprimento de arco (absolutas ou em fração) e valores por unidade de comprimento de arco, em eixos globais ou locais; peso próprio, temperatura uniforme, gradiente e pré-deformação DEVEM valer. | US-2 |
| REQ-008b-16 | SE os vetores de referência ficarem paralelos à tangente em algum ponto da curva, ou o arco tiver abertura ≥ 2π menos a tolerância documentada, ou o nó do meio coincidir com uma ponta, ou (isoparamétrica) o jacobiano não for positivo em toda a barra, ou (arco) o nó do meio receber apoio, carga, mola ou outro elemento, ENTÃO O sistema DEVE recusar a análise informando o elemento e o motivo. | US-6 |

### Ações, análise e resultados

| ID | Requisito | História |
|----|-----------|----------|
| REQ-008b-17 | As cargas distribuídas da 008 DEVEM aceitar, além das forças, um momento torçor distribuído mx (uniforme ou trapezoidal, em todo o trecho ou num intervalo) em vigas 3D retas e curvas; as cargas concentradas no vão DEVEM aceitar um bimomento em vigas com empenamento. | US-1, US-2 |
| REQ-008b-18 | QUANDO for pedida a análise estática linear, O sistema DEVE devolver, além dos resultados da 008: o empenamento θ' nos nós que o têm, a reação de bimomento nos empenamentos restringidos e, por viga, o bimomento B e a divisão da torção em Saint-Venant (Tsv) e de empenamento (Tw) nas pontas do trecho flexível. | US-1, US-7 |
| REQ-008b-19 | O sistema DEVE permitir avaliar, em qualquer posição de vigas retas e curvas (ao longo do comprimento de arco), os esforços locais, o bimomento, Tsv e Tw, os deslocamentos, as rotações e o empenamento, pela mesma interface da 008: exatos onde os REQ-008b-08 e 13 os exigem e, na isoparamétrica e com base elástica, os da solução aproximada. | US-7 |
| REQ-008b-20 | As tensões da 008 e do REQ-008b-06 DEVEM incluir, em vigas com empenamento, a tensão normal de empenamento σw = B·ω/Iw e a tensão de cisalhamento da torção de empenamento (fórmula de paredes finas documentada) nos pontos com ω conhecido. | US-5 |
| REQ-008b-21 | ANTES de montar, O sistema DEVE validar o modelo e listar todos os problemas, além dos da 008: empenamento em viga 2D ou com Iw ausente ou ≤ 0; condição de empenamento, bimomento ou restrição de empenamento em elemento ou nó sem empenamento; elemento `Line3` em viga reta da 008 ou `Line2` em viga curva; seção inválida (REQ-008b-04); os casos do REQ-008b-16. | US-6 |
| REQ-008b-22 | SE a estrutura for um mecanismo, ENTÃO O sistema DEVE informar ao menos um nó e a direção sem rigidez, incluindo o empenamento. | US-6 |
| REQ-008b-23 | Modelos sem empenamento, sem vigas curvas e sem as novas seções DEVEM dar os mesmos resultados da 008, e os testes da 007 e da 008 DEVEM continuar passando. | US-7 |
| REQ-008b-24 | O sistema DEVE manter tudo desta feature em `core`, sem Qt nem GPU (constituição §4.1), inclusive a triangulação da seção poligonal. | US-7 |

## 4. Requisitos não funcionais

| ID | Requisito | Como medir |
|----|-----------|------------|
| NFR-008b-01 | Os resultados exatos (REQ-008b-08 e 13) DEVEM reproduzir as soluções analíticas dos casos de verificação com erro relativo ≤ 1e−8 em deslocamentos, rotações, empenamentos, reações, esforços, bimomentos e tensões; as propriedades exatas de seção (REQ-008b-02 e 03), ≤ 1e−10. | Testes automatizados |
| NFR-008b-02 | Com o refinamento padrão, a seção poligonal DEVE dar J, Iw, centro de cisalhamento e áreas de cisalhamento com erro ≤ 0,5% contra as referências do AC-2, e calcular as propriedades de uma seção de até 200 vértices em menos de 0,5 s em Release. | Testes e benchmark |
| NFR-008b-03 | O pórtico espacial do benchmark da 008 DEVE continuar abaixo de 5 s; com empenamento ativo em todas as vigas (cerca de 34 mil GDL), DEVE ficar abaixo de 6 s em Release na máquina do mantenedor. | Benchmark |
| NFR-008b-04 | A cobertura de linhas de `core` DEVE continuar em pelo menos 80% (constituição §6.5). | Job de cobertura no CI |

## 5. Critérios de aceite

- [ ] **AC-1** (REQ-008b-01, 03, 04) Paredes finas e Iw: Iw e ω das formas
  paramétricas iguais às fórmulas fechadas (perfil I: Iw = Iz·h₀²/4 com as
  mesas; U contra tabelas; zero na cantoneira, no T e no tubo circular); seção de paredes finas de perfil I, U, Z, cartola
  e caixão de uma e de duas células igual às fórmulas de paredes finas;
  seções inválidas recusadas com a mensagem certa.
- [ ] **AC-2** (REQ-008b-02, 04, NFR-008b-02) Seção poligonal:
  - retângulo, círculo poligonal fino e seção com furo: A, inércias e eixos
    principais exatos;
  - J do retângulo contra a série de Saint-Venant, do tubo contra a solução
    exata, Iw e centro de cisalhamento de perfil I e U poligonais contra a
    seção de paredes finas equivalente (tolerância documentada), áreas de
    cisalhamento com ν = 0 contra Cowper: 5/6 no retângulo e 6/7 no círculo
    *(ajustado no design, 2026-11-16)*;
  - convergência de J com o refinamento na taxa documentada;
  - polígonos inválidos recusados.
- [ ] **AC-3** (REQ-008b-07, 08, 10, 18, 19, NFR-008b-01) Torção com
  empenamento, viga reta: balanço de perfil I com torque na ponta e engaste
  impedindo o empenamento (giro, bimomento e Tsv/Tw ao longo da barra pela
  solução em senh/cosh); biapoiada com torque distribuído e concentrado e
  extremidades livres e impedidas; limites G·J → 0 e E·Iw → 0; bimomento
  nodal aplicado.
- [ ] **AC-4** (REQ-008b-09, 11) Condições de empenamento: viga contínua de
  dois vãos com empenamento contínuo igual a uma barra só; T de três barras
  (contínuo nas colineares, livre na terceira); chapa de topo (impedido);
  liberação de torção e excentricidade com empenamento; base elástica de
  torção contra a solução analítica *(ajustado no design, 2026-11-16)*.
- [ ] **AC-5** (REQ-008b-12, 13, 15, NFR-008b-01) Arco exato, contra
  soluções fechadas (Roark ou equivalente): quarto de anel em balanço com
  carga na ponta no plano e fora do plano (flexão e torção acopladas); anel
  fechado sob carga diametral; arco biarticulado semicircular sob carga
  uniforme e sob temperatura; viga curva em planta biengastada sob carga
  vertical; arco com raio → ∞ igual à viga reta; resultados em qualquer
  ponto do arco.
- [ ] **AC-6** (REQ-008b-12, 14, 15) Isoparamétrica: patch test em barra
  reta; os casos do AC-5 convergindo para a solução exata na taxa
  documentada, sem travamento em barra esbelta (L/h = 1000); arco parabólico
  convergindo para a solução de referência documentada; nó do meio com apoio
  e com carga.
- [ ] **AC-7** (REQ-008b-07, 13, 15) Viga curva com empenamento: arco de
  perfil I em planta com raio grande igual à viga reta com empenamento;
  formulações arco e isoparamétrica concordando com o refinamento; caso de
  referência da literatura documentado no design.
- [ ] **AC-8** (REQ-008b-15, 17) Recursos da 008 em vigas curvas: liberações,
  molas de ligação, excentricidades, base elástica, cargas parciais e
  concentradas por comprimento de arco, eixos globais e locais, peso
  próprio e gradiente, cada um com sua solução analítica ou de referência.
- [ ] **AC-9** (REQ-008b-06, 20) Tensões: σw = B·ω/Iw nas pontas das mesas de
  perfil I; tensões das seções poligonal (retângulo sob torção: máximo no
  meio do lado maior contra a série) e de paredes finas contra as fórmulas;
  von Mises.
- [ ] **AC-10** (REQ-008b-16, 21, 22) Validação: cada problema do
  REQ-008b-21 gera uma mensagem com o item envolvido, e vários aparecem
  juntos; mecanismo de empenamento informa nó e direção.
- [ ] **AC-11** (REQ-008b-05, 23, 24, NFR-008b-03, 04) Os testes da 007 e da
  008 passam sem alteração de resultado; `core` segue sem Qt; benchmark do
  pórtico espacial com e sem empenamento registrado em `tasks.md`; CI verde
  nas quatro plataformas, com a cobertura de `core` ≥ 80%.

## 6. Fora de escopo

- **Feature 008c:** análise de segunda ordem P-Δ, flambagem e instabilidade
  lateral com torção.
- Interface, atribuição pelo viewport e desfazer/refazer — 010; execução
  assíncrona — 011; deformada e cores — 012; diagramas, seção extrudada e
  desenho da seção — 013; arquivo do projeto — 014.
- Variante de Benscoter (cisalhamento do empenamento), distorção da seção e
  empenamento secundário.
- Seções de vários materiais (mistas aço-concreto), seção variável ao longo
  da barra, seção poligonal com contornos curvos (arcos de circunferência no
  contorno são aproximados por polígonos).
- Tensões de empenamento na seção genérica (sem ω conhecido) e na poligonal
  maciça além das que a solução numérica dá.
- Arco formulado com geometria não circular (a parábola e outras curvas são
  atendidas pela formulação isoparamétrica); hélice num único elemento.
- Molas de empenamento (rigidez de bimomento) nas pontas; cargas projetadas.
- Gerador de malha 2D geral da seção (feature 022); a triangulação desta
  feature só atende às seções.

## 7. Requisitos globais relacionados

- **REQ-G-12:** atribuição de materiais, seções, restrições e cargas a
  grupos e listas da malha.
- **REQ-G-13:** treliças e vigas (retas e curvas) no mesmo modelo.
- **REQ-G-14:** recusar modelos incompletos ou singulares indicando o que falta.
- **REQ-G-15:** análise estática linear.
- **REQ-G-16:** verificação por patch test, convergência e soluções analíticas.
- **REQ-G-22:** montagem paralela, herdada da 006.

## 8. Questões em aberto

- [x] Empenamento entre barras no mesmo nó: condição por ponta (livre,
  impedido, contínuo), padrão contínuo entre colineares (decidido em
  2026-11-16).
- [x] Viga curva com todos os recursos da 008 e com empenamento (decidido em
  2026-11-16).
- [x] Seções: polígono maciço com furos, por triangulação própria em `core`,
  e paredes finas pela linha média (decidido em 2026-11-16).
- [x] Teoria: Vlasov nas duas teorias de flexão; Benscoter fora (decidido em
  2026-11-16).
- [x] Viga curva: as duas formulações, arco exato e isoparamétrica, à
  escolha do usuário por atribuição (decidido em 2026-11-16).
- [x] Tamanho: a feature segue inteira, com a seção poligonal maciça
  (decidido em 2026-11-16, na aprovação dos requisitos).
