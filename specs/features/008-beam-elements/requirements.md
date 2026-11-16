# Requisitos — 008 beam-elements

Status: aprovado · Data: 2026-11-16

## 1. Contexto e objetivo

A 007 entregou a treliça, o `Model` e a análise estática linear. Esta feature
traz a viga reta de 2 nós, de Euler-Bernoulli e de Timoshenko, no plano e no
espaço. Junto com ela vêm:

- seções simétricas e assimétricas, com inércia e forma;
- a orientação dos eixos locais;
- as ligações de extremidade: liberações (rótulas), molas e excentricidades
  rígidas;
- molas distribuídas ao longo da barra (base elástica);
- as ações próprias de pórticos: momentos nodais, cargas distribuídas
  uniformes e trapezoidais em todo o vão ou em parte dele, cargas
  concentradas no vão e gradiente de temperatura.

A análise devolve os esforços N, V, T e M nas extremidades e ao longo de cada
barra, que são a base dos diagramas da 013, e as tensões nos pontos da seção.

A pedido do mantenedor, o tema das vigas foi dividido em três features:

- **008 (esta):** a viga reta;
- **008b:** torção com empenamento, viga curva e seção poligonal;
- **008c:** análise de segunda ordem (P-Δ).

A verificação segue a constituição §5.1. Como na 007, tudo é exercitado por
testes; nada muda na interface.

## 2. Histórias de usuário

- **US-1** Como engenheiro, quero analisar vigas, pórticos planos, pórticos
  espaciais e grelhas sob forças, momentos, cargas distribuídas e
  concentradas, recalques, peso próprio e temperatura, para obter
  deslocamentos, rotações, reações e esforços.
- **US-2** Como engenheiro, quero descrever a seção pela forma, simétrica ou
  não (retangular, circular, tubos, perfil I, cantoneira, U, T), ou pelas
  propriedades, e orientar a seção no espaço, para não calcular inércias,
  centro de cisalhamento e eixos principais à mão.
- **US-3** Como engenheiro, quero modelar rótulas, ligações semirrígidas,
  nós rígidos de tamanho finito e vigas sobre base elástica, para representar
  a estrutura real.
- **US-4** Como engenheiro, quero as tensões nos pontos da seção (normal,
  cisalhamento e von Mises), para verificar o material.
- **US-5** Como engenheiro, quero saber exatamente o que falta ou está errado
  no modelo antes de confiar no resultado.
- **US-6** Como desenvolvedor das próximas features (008b, 008c, interface
  010, resultados 012, diagramas 013), quero esforços e tensões avaliáveis
  em qualquer ponto da barra, para desenhar diagramas sem refazer a
  mecânica.

## 3. Requisitos funcionais (EARS)

### Material e seções

| ID | Requisito | História |
|----|-----------|----------|
| REQ-008-01 | O sistema DEVE usar o módulo de cisalhamento G = E / (2(1 + ν)) do material da 007 nas vigas; o coeficiente de Poisson ν DEVE ser exigido quando a viga precisar de G (torção em 3D e cisalhamento de Timoshenko). | US-1 |
| REQ-008-02 | O sistema DEVE representar uma seção genérica de viga com: área A; momentos de inércia Iy, Iz e produto de inércia Iyz em torno de eixos que passam pelo centroide; constante de torção J; posição do centro de cisalhamento em relação ao centroide (padrão: coincidentes); e, opcionalmente, áreas efetivas de cisalhamento Asy e Asz. A seção de área só, da 007, DEVE continuar valendo para treliças. | US-2 |
| REQ-008-03 | O sistema DEVE calcular A, Iy, Iz, Iyz, eixos principais, J, centro de cisalhamento, Asy e Asz das formas paramétricas: retangular (b, h), circular cheia (d), tubo circular (D, t), tubo retangular (b, h, t), perfil I duplamente simétrico (h, bf, tf, tw), cantoneira de abas desiguais (b, h, t), perfil U (h, bf, tf, tw) e perfil T (h, bf, tf, tw). As fórmulas e suas aproximações (torção de retângulo e de paredes finas, centro de cisalhamento de paredes finas, coeficientes de cisalhamento) DEVEM ser documentadas. | US-2 |
| REQ-008-04 | SE uma dimensão da forma for não positiva ou geometricamente impossível (por exemplo, tubo com t ≥ D/2, perfil com 2·tf ≥ h ou tw > bf), ENTÃO O sistema DEVE recusar a seção informando a dimensão. | US-2, US-5 |
| REQ-008-05 | Para seções assimétricas, o sistema DEVE trabalhar nos eixos principais da seção e considerar a excentricidade do centro de cisalhamento: a carga transversal aplicada no eixo da barra (centroide) DEVE gerar a torção correspondente, e a torção DEVE ser resistida em torno do centro de cisalhamento (torção uniforme; o empenamento fica para a 008b). | US-2 |

### Elemento de viga

| ID | Requisito | História |
|----|-----------|----------|
| REQ-008-06 | O sistema DEVE fornecer a viga reta de 2 nós (`Line2` da malha) em 3D, com GDL u, v, w, θx, θy, θz por nó, e em 2D, no plano XY, com GDL u, v, θz por nó. Em 2D a seção DEVE ser simétrica em relação ao plano XY (Iyz = 0 e centro de cisalhamento no eixo y local). | US-1 |
| REQ-008-07 | Cada atribuição de viga DEVE escolher a teoria: Euler-Bernoulli (sem deformação por cortante) ou Timoshenko (com deformação por cortante, usando Asy e Asz). A viga de Timoshenko DEVE ser livre de travamento por cortante: com Asy, Asz → ∞ ela DEVE reproduzir a de Euler-Bernoulli. | US-1 |
| REQ-008-08 | Sem base elástica, o elemento DEVE dar deslocamentos e rotações nodais exatos da teoria escolhida (flexão, axial e torção uniforme de Saint-Venant) para barras retas sob cargas nodais e sob as cargas no vão do REQ-008-16. | US-1 |
| REQ-008-09 | Em 3D, a orientação da seção DEVE ser dada por um vetor de referência e por um ângulo de rotação em torno do eixo da barra, com um padrão documentado quando nada for informado (incluindo o caso de barra paralela ao vetor padrão). O eixo local x vai do primeiro ao segundo nó. Em 2D, o eixo local y é o eixo x local girado de 90° no plano XY. | US-2 |
| REQ-008-10 | SE o vetor de referência for paralelo ao eixo da barra (tolerância documentada), ENTÃO O sistema DEVE recusar a análise informando o elemento. | US-5 |

### Ligações de extremidade e base elástica

| ID | Requisito | História |
|----|-----------|----------|
| REQ-008-11 | O sistema DEVE permitir liberar, em cada extremidade de uma viga, qualquer combinação dos esforços locais (N, Vy, Vz, T, My, Mz em 3D; N, Vy, Mz em 2D); a rótula é a liberação dos momentos fletores. | US-3 |
| REQ-008-12 | O sistema DEVE permitir, em vez da liberação total, uma mola de ligação em cada esforço local de cada extremidade (rigidez de translação ou de rotação ≥ 0, ligação semirrígida); a mola de rigidez zero equivale à liberação, e a ausência de mola, à ligação rígida. | US-3 |
| REQ-008-13 | SE as liberações tornarem a barra instável por si só (por exemplo, N ou T liberados nas duas extremidades, ou cortante e momento do mesmo plano liberados de forma que a barra gire livre) e nenhuma mola distribuída lhe der rigidez, ENTÃO O sistema DEVE recusar a análise informando o elemento e as liberações. | US-3, US-5 |
| REQ-008-14 | O sistema DEVE permitir excentricidades rígidas em cada extremidade (vetor em eixos globais do nó ao início do trecho flexível); a barra deforma só no trecho flexível entre as excentricidades, e liberações e molas de ligação valem nas pontas desse trecho. SE o trecho flexível tiver comprimento nulo ou negativo, ENTÃO O sistema DEVE recusar a análise informando o elemento. | US-3, US-5 |
| REQ-008-15 | O sistema DEVE permitir molas distribuídas ao longo do trecho flexível de uma viga (base elástica de Winkler), com rigidezes por unidade de comprimento ≥ 0 nas direções locais x, y e z e na rotação em torno de x, constantes ao longo da barra. Com base elástica, os resultados DEVEM convergir para a solução analítica com o refinamento (AC-10), em vez de serem exatos. | US-3 |

### Ações

| ID | Requisito | História |
|----|-----------|----------|
| REQ-008-16 | Além das ações da 007, o sistema DEVE aceitar, no mesmo caso de carga: momentos nodais (Mx, My, Mz num conjunto de nós, só Mz em 2D); rotações prescritas nos apoios; cargas distribuídas por unidade de comprimento num conjunto de vigas, uniformes ou trapezoidais, em todo o trecho flexível ou num intervalo dele (posições inicial e final ao longo da barra), em eixos globais ou locais; forças e momentos concentrados num ponto do vão (posição ao longo da barra), em eixos globais ou locais; e gradiente de temperatura num conjunto de vigas (∂T/∂y e ∂T/∂z locais, uniforme ao longo da barra). | US-1 |
| REQ-008-17 | SE a posição de uma carga no vão sair do trecho flexível ou o intervalo tiver início depois do fim, ENTÃO O sistema DEVE recusar a análise informando a carga e o elemento. As posições DEVEM poder ser dadas em comprimento ou em fração do trecho flexível. | US-5 |
| REQ-008-18 | O peso próprio, a variação de temperatura uniforme e a pré-deformação da 007 DEVEM valer também para vigas; o peso próprio da viga DEVE ser uma carga distribuída uniforme ρ·A·g no trecho flexível. | US-1 |
| REQ-008-19 | O sistema DEVE gerar, para cada carga no vão e gradiente de temperatura, as cargas nodais equivalentes consistentes com a teoria da barra (incluindo o efeito das liberações, das molas de ligação e das excentricidades), de modo que o REQ-008-08 se mantenha. | US-1 |

### Modelo misto, análise e resultados

| ID | Requisito | História |
|----|-----------|----------|
| REQ-008-20 | O `Model` DEVE aceitar treliças e vigas (2D com 2D, 3D com 3D) no mesmo modelo; um nó só recebe GDL de rotação se pertencer a pelo menos uma viga. | US-1 |
| REQ-008-21 | QUANDO for pedida a análise estática linear, O sistema DEVE devolver, além dos resultados da 007: rotações nodais; momentos de reação nos GDL de rotação prescritos; e, por viga, os esforços locais N, Vy, Vz, T, My, Mz nas duas extremidades do trecho flexível, com a convenção de sinais documentada. | US-1, US-6 |
| REQ-008-22 | O sistema DEVE permitir avaliar, em qualquer posição ao longo do trecho flexível de cada viga, os esforços locais e os deslocamentos e rotações, exatos para as cargas do REQ-008-16 dentro da teoria escolhida (aproximados com base elástica, como no REQ-008-15), incluindo os saltos sob cargas concentradas. | US-6 |
| REQ-008-23 | O sistema DEVE calcular, para as seções de forma paramétrica, em qualquer posição da barra e nos pontos característicos da seção (documentados por forma), a tensão normal da flexão composta (N, My, Mz nos eixos principais), as tensões de cisalhamento da cortante e da torção (fórmulas documentadas, de paredes finas quando aplicável) e a tensão equivalente de von Mises; para a seção genérica, a tensão normal em pontos (y, z) dados pelo usuário. | US-4 |
| REQ-008-24 | ANTES de montar, O sistema DEVE validar o modelo e listar todos os problemas, além dos da 007: Iy, Iz ou J ausente ou ≤ 0 quando a viga precisar (Iz em 2D; Iy, Iz e J em 3D); Iy·Iz − Iyz² ≤ 0; Asy ou Asz ausente ou ≤ 0 em viga de Timoshenko; seção assimétrica em viga 2D; ν ausente ou fora de (−1, 0,5) quando G for necessário; viga 2D fora do plano XY; elemento de viga que não é `Line2`; momento ou rotação prescrita em nó sem GDL de rotação; carga no vão, gradiente, mola de ligação ou mola distribuída em elemento que não é viga; rigidez de mola negativa; vetor de referência paralelo à barra; liberações instáveis; trecho flexível nulo; carga fora do trecho flexível. | US-5 |
| REQ-008-25 | SE a estrutura for um mecanismo, ENTÃO O sistema DEVE informar ao menos um nó e a direção sem rigidez, incluindo as rotações (θx, θy, θz). | US-5 |
| REQ-008-26 | A análise de modelos só de treliça DEVE dar os mesmos resultados da 007, e os testes da 007 DEVEM continuar passando. | US-1 |
| REQ-008-27 | O sistema DEVE manter tudo desta feature em `core`, sem Qt nem GPU (constituição §4.1). | US-6 |

## 4. Requisitos não funcionais

| ID | Requisito | Como medir |
|----|-----------|------------|
| NFR-008-01 | Os resultados DEVEM reproduzir as soluções analíticas dos casos de verificação (AC-4 a AC-9) com erro relativo ≤ 1e−10 em deslocamentos, rotações, reações, esforços e tensões, exceto nos casos de convergência (AC-10) e nas fórmulas aproximadas de seção (tolerância documentada em AC-1). | Testes automatizados |
| NFR-008-02 | Um pórtico espacial de cerca de 30 mil GDL (edifício de vigas e pilares) DEVE ser validado, montado, resolvido e ter os esforços de extremidade recuperados em menos de 5 s em Release na máquina do mantenedor. | Benchmark |
| NFR-008-03 | A cobertura de linhas de `core` DEVE continuar em pelo menos 80% (constituição §6.5). | Job de cobertura no CI |

## 5. Critérios de aceite

- [ ] **AC-1** (REQ-008-02 a 05) Seções: propriedades das oito formas
  (inércias, eixos principais, centro de cisalhamento, J, áreas de
  cisalhamento) iguais às fórmulas fechadas ou a valores tabelados, com
  tolerância documentada para as aproximações; dimensões inválidas
  recusadas.
- [ ] **AC-2** (REQ-008-06, 07, 09) Elemento: matriz de rigidez de Euler-
  Bernoulli e de Timoshenko iguais às fórmulas fechadas em orientação
  qualquer; invariância sob rotação; seis modos de corpo rígido com energia
  nula em 3D e três em 2D; Timoshenko → Euler-Bernoulli com áreas de
  cisalhamento grandes; eixos locais pelo vetor padrão, por vetor dado e por
  ângulo.
- [ ] **AC-3** (REQ-008-08, NFR-008-01) Patch test: barra subdividida sob
  momento constante (curvatura uniforme) e sob esforço normal e torção
  constantes reproduz o campo exato em todos os nós e esforços constantes em
  todas as barras.
- [ ] **AC-4** (REQ-008-08, 16, 21, 22) Vigas, nas duas teorias:
  - balanço com força e com momento na ponta;
  - biapoiada e biengastada com carga uniforme, trapezoidal, parcial e
    concentrada no vão (flecha, rotações, reações e diagramas M e V ao longo
    da barra, com o salto sob a carga concentrada);
  - viga contínua de dois vãos;
  - recalque e rotação prescrita.
- [ ] **AC-5** (REQ-008-08, 20, 21) Pórticos e grelhas: pórtico plano com
  carga lateral; pórtico espacial em L com flexão e torção; grelha com carga
  fora do plano; modelo misto de viga com tirante de treliça.
- [ ] **AC-6** (REQ-008-05, 09) Seções assimétricas: balanço de cantoneira
  com carga na ponta (flexão oblíqua nos eixos principais); balanço de perfil
  U com carga no centroide (torção pela excentricidade do centro de
  cisalhamento); resultados iguais ao mesmo modelo com a seção girada e a
  carga aplicada no centro de cisalhamento.
- [ ] **AC-7** (REQ-008-11 a 14, 19) Ligações:
  - viga com rótula interna (Gerber);
  - barra com rótulas nas duas pontas igual a uma treliça;
  - liberação de torção;
  - viga com molas de ligação de rotação: a solução analítica da ligação
    semirrígida, rigidez → 0 igual à rótula e → ∞ igual ao engaste;
  - excentricidade igual ao mesmo modelo com barra rígida explícita;
  - cargas no vão com rótula, com mola e com excentricidade;
  - liberações instáveis e trecho nulo recusados.
- [ ] **AC-8** (REQ-008-16 a 19, NFR-008-01) Temperatura e peso:
  biengastada com gradiente (M = E·I·α·∂T/∂y, deslocamentos nulos);
  biapoiada com gradiente (rotações e flecha exatas); ΔT uniforme e
  pré-deformação em pórtico; peso próprio de viga inclinada.
- [ ] **AC-9** (REQ-008-07, 23, NFR-008-01) Timoshenko e tensões:
  - balanço curto com a flecha de flexão mais a de cortante exata;
  - viga esbelta igual a Euler-Bernoulli (sem travamento) dentro de 1e−6;
  - tensões normal, de cisalhamento e de von Mises nos pontos
    característicos das formas iguais às fórmulas de resistência dos
    materiais.
- [ ] **AC-10** (REQ-008-08, 15) Convergência:
  - quarto de anel em balanço com carga na ponta, discretizado em n barras
    retas: o deslocamento da ponta converge para a solução de viga curva com
    taxa ≈ 2 (± 0,2);
  - viga sobre base elástica (Hetényi) com carga concentrada: o erro do
    deslocamento sob a carga cai com a taxa documentada no design.
- [ ] **AC-11** (REQ-008-10, 13, 14, 17, 24 a 26) Validação: cada problema do
  REQ-008-24 gera uma mensagem com o item envolvido, e vários aparecem
  juntos; mecanismo de rotação informa nó e direção; os testes da 007 passam
  sem alteração de resultado.
- [ ] **AC-12** (REQ-008-27, NFR-008-02, 03) `core` segue sem Qt; benchmark
  do pórtico espacial registrado em `tasks.md`; CI verde nas quatro
  plataformas, com a cobertura de `core` ≥ 80%.

## 6. Fora de escopo

- **Feature 008b:** torção com empenamento (7º GDL), viga curva (`Line3`, com
  curvatura em qualquer direção) e seção poligonal de forma livre com
  propriedades calculadas numericamente.
- **Feature 008c:** análise de segunda ordem P-Δ.
- Interface, atribuição pelo viewport e desfazer/refazer — feature 010;
  execução assíncrona — 011; deformada e cores — 012; diagramas N/V/M e
  seção extrudada no viewport — 013; arquivo do projeto — 014.
- Viga de seção variável; molas distribuídas variáveis ao longo da barra e
  molas de flexão distribuídas (θy, θz).
- Cargas projetadas (por comprimento horizontal).
- Apoios elásticos e inclinados nos nós; molas não lineares.
- Não linearidade geométrica completa, flambagem, vários casos de carga e
  combinações; massa e análise modal (032/033).

## 7. Requisitos globais relacionados

- **REQ-G-12:** atribuição de materiais, seções, restrições e cargas a
  grupos e listas da malha.
- **REQ-G-13:** treliças e vigas no mesmo modelo.
- **REQ-G-14:** recusar modelos incompletos ou singulares indicando o que falta.
- **REQ-G-15:** análise estática linear.
- **REQ-G-16:** verificação por patch test, convergência e soluções analíticas.
- **REQ-G-22:** montagem paralela, herdada da 006.

## 8. Questões em aberto

- [x] Seções: genérica e oito formas paramétricas, simétricas e
  assimétricas; a seção poligonal vai para a 008b (decidido em 2026-11-16).
- [x] Ações novas: momentos nodais, cargas distribuídas uniformes e
  trapezoidais em todo o vão ou em parte dele, cargas concentradas no vão e
  gradiente de temperatura (decidido em 2026-11-16).
- [x] Extremidades: liberações, molas de ligação e excentricidades rígidas
  (decidido em 2026-11-16).
- [x] Resultados: esforços nas extremidades e ao longo da barra, e tensões na
  seção (decidido em 2026-11-16).
- [x] Divisão em três features: 008 (viga reta), 008b (empenamento, viga
  curva, seção poligonal) e 008c (P-Δ iterativo com rigidez geométrica)
  (decidido em 2026-11-16).
- [x] Meta do NFR-008-02 (30 mil GDL em 5 s): mantida; o design estima o
  tempo a partir da 007 e o benchmark da tarefa confirma (decidido em
  2026-11-16, na aprovação dos requisitos).

**Nota para a 008b** (não é questão desta feature): a seção poligonal exige
uma malha 2D da seção, e o gerador de malha 2D só vem na 022. O design da
008b decide entre uma triangulação simples própria da seção e adiar a seção
poligonal.
