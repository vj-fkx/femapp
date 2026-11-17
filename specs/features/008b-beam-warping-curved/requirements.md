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
| REQ-008b-01 | A seção genérica da 008 DEVE aceitar, opcionalmente, a constante de empenamento Iw ≥ 0 (em relação ao centro de cisalhamento). O sistema DEVE calcular Iw das oito formas paramétricas da 008 por fórmulas fechadas ou de paredes finas documentadas (Iw = 0 onde a teoria de paredes finas o anula, como cantoneira, perfil T e tubo circular) e a coordenada setorial ω nos pontos característicos de cada forma. *(Mantido em 2026-11-16 como propriedade da seção, base da versão futura com empenamento.)* | US-4 |
| REQ-008b-02 | O sistema DEVE oferecer a **seção poligonal maciça**: um ou mais contornos externos, cada um com zero ou mais furos, dados como polígonos no plano da seção (coordenadas y, z do desenho), de um único material. A, centroide, Iy, Iz, Iyz e eixos principais DEVEM ser exatos (fórmulas de polígono). J, centro de cisalhamento, Iw, Asy e Asz DEVEM ser calculados pela solução numérica dos problemas de empenamento de Saint-Venant e de cisalhamento por flexão numa triangulação própria da seção, em `core`, com refinamento ajustável e um padrão que atenda ao NFR-008b-02. | US-4 |
| REQ-008b-03 | O sistema DEVE oferecer a **seção de paredes finas por linha média**: segmentos retos entre pontos do plano da seção, cada um com sua espessura > 0, formando ramos abertos (inclusive ramificados) e células fechadas (uma ou mais), detectadas a partir das ligações entre os segmentos. A, inércias, J (Σ b·t³/3 nos ramos abertos e fluxo de Bredt nas células), centro de cisalhamento, Iw (coordenada setorial) e Asy, Asz (fluxo de cisalhamento) DEVEM vir da teoria de paredes finas, com as aproximações documentadas (cantos ignorados). | US-4 |
| REQ-008b-04 | SE um polígono tiver menos de três vértices, área nula, autointerseção, furo fora do seu contorno ou furo/contorno sobrepostos a outro, ou SE um segmento de paredes finas tiver comprimento nulo ou espessura ≤ 0, ou a seção de paredes finas for desconexa, ENTÃO O sistema DEVE recusar a seção informando o polígono, vértice ou segmento. | US-4, US-6 |
| REQ-008b-05 | As seções poligonal e de paredes finas DEVEM valer em todas as vigas da 008 e desta feature (com as mesmas regras de simetria da viga 2D) e, pela área, nas treliças da 007. | US-4 |
| REQ-008b-06 | O sistema DEVE calcular as tensões das novas seções em qualquer posição da barra: na poligonal, a tensão normal em qualquer ponto (y, z) e nos vértices, e as tensões de cisalhamento da torção e da cortante a partir da solução numérica, informando o ponto de máximo de von Mises; na de paredes finas, a tensão normal, as de cisalhamento (torção de Saint-Venant, fluxo de Bredt, cortante) e a de von Mises nas pontas e nos nós da linha média, nas duas faces da parede. | US-5 |

### Torção com empenamento

| ID | Requisito | História |
|----|-----------|----------|
| REQ-008b-07 | *Removido em 2026-11-16: a torção com empenamento foi adiada para uma versão futura (§6).* | — |
| REQ-008b-08 | *Removido em 2026-11-16: a torção com empenamento foi adiada para uma versão futura (§6).* | — |
| REQ-008b-09 | *Removido em 2026-11-16: a torção com empenamento foi adiada para uma versão futura (§6).* | — |
| REQ-008b-10 | *Removido em 2026-11-16: a torção com empenamento foi adiada para uma versão futura (§6).* | — |
| REQ-008b-11 | *Removido em 2026-11-16: a torção com empenamento foi adiada para uma versão futura (§6).* | — |

### Viga curva

| ID | Requisito | História |
|----|-----------|----------|
| REQ-008b-12 | O sistema DEVE fornecer a viga curva de 3 nós (`Line3` da malha: pontas e nó do meio), em 3D e em 2D (no plano XY), nas duas teorias de flexão. Cada atribuição DEVE escolher a formulação: **arco** ou **isoparamétrica**. | US-2, US-3 |
| REQ-008b-13 | Na formulação **arco**, a barra DEVE ser o arco de circunferência que passa pelos três nós, com GDL só nas pontas; o nó do meio só define a geometria. A barra DEVE dar resultados nodais exatos da teoria de viga curva (incluindo o acoplamento entre flexão, esforço normal e torção causado pela curvatura) para cargas nodais e para as cargas no vão, também com base elástica. Nós colineares DEVEM dar a viga reta da 008. | US-2, US-3 |
| REQ-008b-14 | Na formulação **isoparamétrica**, a geometria DEVE ser a curva quadrática pelos três nós, com GDL nos três; o nó do meio DEVE poder receber apoios, cargas e outras barras. O elemento DEVE ser livre de travamento por cortante e de membrana (com a técnica documentada no design) e convergir com taxa documentada; nós colineares com o do meio no centro DEVEM reproduzir a viga reta em barra subdividida no patch test. | US-2, US-3 |
| REQ-008b-15 | Nas vigas curvas, os eixos locais DEVEM variar ao longo da barra: x tangente à curva, e y e z pela mesma regra da 008 (vetor de referência e ângulo de rotação), aplicada em cada ponto; a curvatura pode ter qualquer direção em relação aos eixos principais da seção. Liberações, molas de ligação, excentricidades (o trecho flexível passa pelas pontas deslocadas e pelo nó do meio) e base elástica (por unidade de comprimento de arco, eixos locais) DEVEM valer como na 008. Cargas no vão DEVEM aceitar posições ao longo do comprimento de arco (absolutas ou em fração) e valores por unidade de comprimento de arco, em eixos globais ou locais; peso próprio, temperatura uniforme, gradiente e pré-deformação DEVEM valer. | US-2 |
| REQ-008b-16 | SE os vetores de referência ficarem paralelos à tangente em algum ponto da curva, ou o arco tiver abertura ≥ 2π menos a tolerância documentada, ou o nó do meio coincidir com uma ponta, ou (isoparamétrica) o jacobiano não for positivo em toda a barra, ou (arco) o nó do meio receber apoio, carga, mola ou outro elemento, ENTÃO O sistema DEVE recusar a análise informando o elemento e o motivo. | US-6 |

### Ações, análise e resultados

| ID | Requisito | História |
|----|-----------|----------|
| REQ-008b-17 | As cargas distribuídas da 008 DEVEM aceitar, além das forças, um momento torçor distribuído mx (uniforme ou trapezoidal, em todo o trecho ou num intervalo) em vigas 3D retas e curvas. *(O bimomento concentrado foi removido em 2026-11-16 com o empenamento.)* | US-2 |
| REQ-008b-18 | *Removido em 2026-11-16: a torção com empenamento foi adiada para uma versão futura (§6).* | — |
| REQ-008b-19 | O sistema DEVE permitir avaliar, em qualquer posição de vigas retas e curvas (ao longo do comprimento de arco), os esforços locais, os deslocamentos e as rotações, pela mesma interface da 008: exatos onde o REQ-008b-13 os exige e, na isoparamétrica, os da solução aproximada. | US-7 |
| REQ-008b-20 | *Removido em 2026-11-16: a torção com empenamento foi adiada para uma versão futura (§6).* | — |
| REQ-008b-21 | ANTES de montar, O sistema DEVE validar o modelo e listar todos os problemas, além dos da 008: elemento `Line3` em viga reta da 008 ou `Line2` em viga curva; seção inválida (REQ-008b-04); os casos do REQ-008b-16. | US-6 |
| REQ-008b-22 | *Removido em 2026-11-16: a torção com empenamento foi adiada para uma versão futura (§6).* A detecção de mecanismo da 008 (REQ-008-25) continua valendo para as vigas curvas. | — |
| REQ-008b-23 | Modelos sem vigas curvas e sem as novas seções DEVEM dar os mesmos resultados da 008, e os testes da 007 e da 008 DEVEM continuar passando. | US-7 |
| REQ-008b-24 | O sistema DEVE manter tudo desta feature em `core`, sem Qt nem GPU (constituição §4.1), inclusive a triangulação da seção poligonal. | US-7 |

## 4. Requisitos não funcionais

| ID | Requisito | Como medir |
|----|-----------|------------|
| NFR-008b-01 | Os resultados exatos (REQ-008b-13) DEVEM reproduzir as soluções analíticas dos casos de verificação com erro relativo ≤ 1e−8 em deslocamentos, rotações, reações, esforços e tensões; as propriedades exatas de seção (REQ-008b-02 e 03), ≤ 1e−10. | Testes automatizados |
| NFR-008b-02 | Com o refinamento padrão, a seção poligonal DEVE dar J, Iw, centro de cisalhamento e áreas de cisalhamento com erro ≤ 0,5% contra as referências do AC-2, e calcular as propriedades de uma seção de até 200 vértices em menos de 0,5 s em Release. | Testes e benchmark |
| NFR-008b-03 | O pórtico espacial do benchmark da 008 DEVE continuar abaixo de 5 s em Release na máquina do mantenedor. *(A meta com empenamento foi removida em 2026-11-16 com ele.)* | Benchmark |
| NFR-008b-04 | A cobertura de linhas de `core` DEVE continuar em pelo menos 80% (constituição §6.5). | Job de cobertura no CI |

## 5. Critérios de aceite

- [x] **AC-1** (REQ-008b-01, 03, 04) Paredes finas e Iw: Iw e ω das formas
  paramétricas iguais às fórmulas fechadas (perfil I: Iw = Iz·h₀²/4 com as
  mesas; U contra tabelas; zero na cantoneira, no T e no tubo circular); seção de paredes finas de perfil I, U, Z, cartola
  e caixão de uma e de duas células igual às fórmulas de paredes finas;
  seções inválidas recusadas com a mensagem certa.
- [x] **AC-2** (REQ-008b-02, 04, NFR-008b-02) Seção poligonal:
  - retângulo, círculo poligonal fino e seção com furo: A, inércias e eixos
    principais exatos;
  - J do retângulo contra a série de Saint-Venant, do tubo contra a solução
    exata, Iw e centro de cisalhamento de perfil I e U poligonais contra a
    seção de paredes finas equivalente (tolerância documentada), áreas de
    cisalhamento com ν = 0 contra Cowper: 5/6 no retângulo e 6/7 no círculo
    *(ajustado no design, 2026-11-16)*;
  - convergência de J com o refinamento na taxa documentada;
  - polígonos inválidos recusados.
- [x] **AC-3** *Removido em 2026-11-16: a torção com empenamento foi adiada para uma versão futura (§6).*
- [x] **AC-4** *Removido em 2026-11-16: a torção com empenamento foi adiada para uma versão futura (§6).*
- [x] **AC-5** (REQ-008b-12, 13, 15, NFR-008b-01) Arco exato, contra
  soluções fechadas (Roark ou equivalente): quarto de anel em balanço com
  carga na ponta no plano e fora do plano (flexão e torção acopladas); anel
  fechado sob carga diametral; arco biarticulado semicircular sob carga
  uniforme e sob temperatura; viga curva em planta biengastada sob carga
  vertical; arco com raio → ∞ igual à viga reta; resultados em qualquer
  ponto do arco.
- [x] **AC-6** (REQ-008b-12, 14, 15) Isoparamétrica: patch test em barra
  reta; os casos do AC-5 convergindo para a solução exata na taxa
  documentada, sem travamento em barra esbelta (L/h = 1000); arco parabólico
  convergindo para a solução de referência documentada; nó do meio com apoio
  e com carga.
- [x] **AC-7** *Removido em 2026-11-16: a torção com empenamento foi adiada para uma versão futura (§6).*
- [x] **AC-8** (REQ-008b-15, 17) Recursos da 008 em vigas curvas: liberações,
  molas de ligação, excentricidades, base elástica, cargas parciais e
  concentradas por comprimento de arco, eixos globais e locais, peso
  próprio e gradiente, cada um com sua solução analítica ou de referência.
- [x] **AC-9** (REQ-008b-06) Tensões: das seções poligonal (retângulo sob
  torção: máximo no meio do lado maior contra a série) e de paredes finas
  contra as fórmulas; von Mises.
- [x] **AC-10** (REQ-008b-16, 21) Validação: cada problema do REQ-008b-21
  gera uma mensagem com o item envolvido, e vários aparecem juntos.
- [x] **AC-11** (REQ-008b-05, 23, 24, NFR-008b-03, 04) Os testes da 007 e da
  008 passam sem alteração de resultado; `core` segue sem Qt; benchmark do
  pórtico espacial registrado em `tasks.md`; CI verde nas quatro
  plataformas, com a cobertura de `core` ≥ 80%.
## 6. Fora de escopo

- **Torção com empenamento (7º GDL), adiada para uma versão futura**
  (decidido em 2026-11-16, durante a implementação): os REQ-008b-07 a 11,
  18, 20 e 22 e os AC-3, 4 e 7 saíram. Ficam no `core`, sem acesso pelo
  modelo, como base para essa versão: Iw e ω das seções (REQ-008b-01) e a
  capacidade de empenamento do motor exato, já verificadas.

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
- [x] Empenamento adiado para uma versão futura (decidido em 2026-11-16,
  durante a implementação, depois da T21; ver §6).
