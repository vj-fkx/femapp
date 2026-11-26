# Requisitos — 012 results-visualization

Status: aprovado · Data: 2026-11-26

## 1. Contexto e objetivo

A 011 roda as três análises de barras (estática linear, segunda ordem e
flambagem) e guarda um resultado por caso e por combinação. Esses resultados
aparecem num resumo no console e em tabelas no dock Results. Ainda não há
como **vê-los** no modelo.

Esta feature desenha os resultados no viewport:
- a deformada com fator de escala, curva dentro de cada barra, como dá o
  `core` (`BeamResult::at`);
- um mapa de cores, com legenda, de deslocamentos, rotações ou esforços ao
  longo das barras;
- a animação dos modos de flambagem;
- as reações de apoio com os seus valores;
- marcas no máximo e no mínimo da grandeza mostrada;
- a consulta do valor num ponto clicado.

Os diagramas de esforços e a seção extrudada ficam para a 013. As tensões na
seção ficam para depois.

## 2. Histórias de usuário

- **US-1** Como engenheiro, quero ver a deformada da estrutura com uma escala
  ajustável, inclusive a flexão dentro das barras, para conferir se a
  resposta faz sentido.
- **US-2** Como engenheiro, quero colorir as barras por um deslocamento, uma
  rotação ou um esforço, com legenda e unidades, para achar de relance as
  regiões críticas.
- **US-3** Como engenheiro, quero ver os modos de flambagem animados, com o
  fator de cada um, para entender como a estrutura perde a estabilidade.
- **US-4** Como engenheiro, quero ver as reações de apoio com os seus
  valores, para conferir o equilíbrio e passar as cargas às fundações.
- **US-5** Como engenheiro, quero clicar num ponto da estrutura e ler o
  valor ali, e ver onde estão o máximo e o mínimo, sem procurar nas tabelas.
- **US-6** Como usuário, quero que os desenhos continuem fluidos com modelos
  grandes, e saber quando o que vejo não corresponde mais ao modelo.

## 3. Requisitos funcionais (EARS)

### Exibição dos resultados

| ID | Requisito | História |
|----|-----------|----------|
| REQ-012-01 | O sistema DEVE oferecer uma exibição de resultados, ligada e desligada no menu View e no dock Results. Ela mostra o resultado escolhido no dock Results (o caso ou a combinação e, na flambagem, o modo). Sem resultado escolhido, a exibição fica indisponível. | US-1, US-2 |
| REQ-012-02 | ENQUANTO a exibição de resultados estiver ligada, O sistema DEVE desenhar o resultado sobre a malha analisada, a guardada no resultado (REQ-011-11). SE o resultado estiver desatualizado (REQ-011-12), ENTÃO o viewport DEVE indicar isso, como o dock Results. | US-6 |
| REQ-012-03 | ENQUANTO a exibição de resultados estiver ligada, O sistema DEVE esconder os símbolos de cargas da 010 e manter os de apoios. Desligada, o viewport volta a ser o da 010. | US-1 |
| REQ-012-04 | QUANDO o usuário escolher outro caso, combinação ou modo no dock Results, O sistema DEVE atualizar o desenho para ele, mantendo a grandeza, a escala escolhida pelo usuário e as opções de exibição. | US-1, US-3 |

### Deformada

| ID | Requisito | História |
|----|-----------|----------|
| REQ-012-05 | O sistema DEVE desenhar a configuração deformada: cada nó deslocado por fator × deslocamento. Dentro de cada viga, a linha DEVE seguir a deformada do `core` (`BeamResult::at`), amostrada em pontos ao longo da barra que incluem as pontas, o meio e os dois lados de cada carga concentrada no vão. As treliças são retas entre os nós. | US-1 |
| REQ-012-06 | O fator de escala padrão DEVE ser automático: o maior deslocamento desenhado vale 10% da maior dimensão da caixa envolvente da malha. O usuário DEVE poder trocar o fator digitando um valor, voltar ao automático e usar a escala real (fator 1). O fator em uso DEVE ficar visível. | US-1 |
| REQ-012-07 | O sistema DEVE permitir mostrar, junto com a deformada, a configuração original em traço fino e cor neutra, e desligar a deformada (o mapa de cores fica então sobre a forma original). | US-1 |
| REQ-012-08 | QUANDO o usuário trocar o fator de escala, O sistema DEVE redesenhar sem refazer nem reenviar a geometria das barras. | US-1, US-6 |

### Mapa de cores

| ID | Requisito | História |
|----|-----------|----------|
| REQ-012-09 | O sistema DEVE colorir as barras pela grandeza escolhida, variando ao longo de cada barra nos pontos de amostragem do REQ-012-05: <br>• deslocamento u, v, w (globais) e o módulo; <br>• rotação θx, θy, θz (globais); <br>• esforços N, Vy, Vz, T, My, Mz (eixos locais principais, a convenção do `core`, REQ-008-21). <br>Nos modos de flambagem, só os deslocamentos e as rotações. Também DEVE ser possível não colorir (cores da 004). | US-2 |
| REQ-012-10 | Os esforços DEVEM ser os de cada barra, sem média nos nós: uma barra e a seguinte podem ter cores diferentes no mesmo nó. Numa carga concentrada no vão, a cor DEVE mudar no ponto da carga, com os valores dos dois lados (`Side::Before` e `Side::After`). | US-2 |
| REQ-012-11 | O sistema DEVE mostrar uma legenda com: <br>• o nome da grandeza e a unidade do sistema do documento (REQ-010-02); <br>• a escala de cores com valores; <br>• o mínimo e o máximo. <br>A faixa padrão é do mínimo ao máximo da grandeza no resultado mostrado. O usuário DEVE poder fixar o mínimo e o máximo; os valores fora da faixa recebem as cores das pontas. | US-2 |
| REQ-012-12 | O sistema DEVE oferecer cores contínuas e cores em faixas (número de faixas escolhido pelo usuário), numa escala do azul (mínimo) ao vermelho (máximo). | US-2 |
| REQ-012-13 | QUANDO o usuário trocar a grandeza, a faixa ou as faixas de cor, O sistema DEVE recolorir sem refazer a geometria das barras. | US-2, US-6 |

### Flambagem

| ID | Requisito | História |
|----|-----------|----------|
| REQ-012-14 | Num resultado de flambagem, O sistema DEVE mostrar o modo escolhido, normalizado como no `core`, com o número do modo e o seu fator de carga crítica no viewport. | US-3 |
| REQ-012-15 | O sistema DEVE animar o modo mostrado: o fator de escala oscila entre −f e +f, sendo f o fator escolhido, com tocar e pausar e a velocidade ajustável. A animação também DEVE servir aos resultados estáticos, oscilando entre 0 e f. Ela NÃO DEVE refazer a geometria a cada quadro. | US-3 |

### Reações, extremos e consulta

| ID | Requisito | História |
|----|-----------|----------|
| REQ-012-16 | ENQUANTO a exibição de resultados estiver ligada, num resultado estático ou de segunda ordem, O sistema DEVE poder mostrar as reações de cada nó apoiado (REQ-011-11): <br>• uma seta por componente de força não nula e um símbolo de momento por componente de momento não nula, no sentido da reação; <br>• o valor de cada componente, com a unidade. <br>As reações são ligadas e desligadas pelo usuário. | US-4 |
| REQ-012-17 | O sistema DEVE marcar no desenho o ponto do máximo e o do mínimo da grandeza colorida, entre os pontos de amostragem, com o valor e o id do nó ou da barra. As marcas podem ser desligadas. | US-5 |
| REQ-012-18 | QUANDO o usuário clicar num nó ou num ponto de uma barra com a exibição de resultados ligada, O sistema DEVE mostrar, junto ao ponto: <br>• o id do nó, ou o id da barra e a distância x desde o seu início; <br>• o valor da grandeza colorida; <br>• o deslocamento (u, v, w). <br>O clique vale para a forma desenhada (deformada ou não), e o valor é o do `core` naquele ponto, não uma interpolação das cores. | US-5 |
| REQ-012-19 | Os valores mostrados no viewport (legenda, reações, extremos, consulta) DEVEM usar a unidade de cada grandeza no sistema do documento e o mesmo formato numérico das tabelas da 011. | US-2, US-4, US-5 |

## 4. Requisitos não funcionais

| ID | Requisito | Como medir |
|----|-----------|------------|
| NFR-012-01 | Com a deformada e o mapa de cores do pórtico do `bench_line_mesh` (cerca de 10⁵ barras), na máquina do mantenedor, Release, o viewport DEVE manter ≥ 30 FPS ao orbitar e durante a animação (REQ-G-20). | Roteiro manual com o contador de FPS |
| NFR-012-02 | Preparar o desenho de um resultado (amostrar as barras e calcular os valores) DEVE ocorrer fora da thread da interface, sem bloqueá-la mais que 100 ms seguidos, e terminar em ≤ 2 s para o pórtico do NFR-012-01, Release. | Teste com laço de eventos e medição do tempo, como o NFR-011-01 |
| NFR-012-03 | A amostragem, os valores por ponto, os extremos, a faixa, as cores e o fator automático DEVEM ser testados sem GPU nem interface. A parte de GPU DEVE passar no teste de fumaça com o backend `Null` do QRhi. | Testes unitários e o teste de fumaça no CI |
| NFR-012-04 | A memória da geometria de resultados na GPU NÃO DEVE passar de 64 bytes por ponto de amostragem. | Teste que mede o tamanho dos buffers montados |

## 5. Critérios de aceite

- [ ] **AC-1** (REQ-012-05, 06) Viga biapoiada com carga uniforme q, pelo
  documento:
  - os pontos da deformada são os de `BeamResult::at` em 1e−12 relativo,
    antes da conversão para a GPU;
  - o meio da barra está entre os pontos e tem a flecha 5qL⁴/384EI em
    1e−9;
  - o fator automático leva o maior deslocamento a 10% da maior
    dimensão da malha.
- [ ] **AC-2** (REQ-012-09, 10, 11) Na mesma viga, colorida por Mz:
  - o máximo da legenda é qL²/8 em 1e−9;
  - numa viga com carga concentrada no vão, os dois lados da carga têm os
    valores de `Side::Before` e `Side::After` de Vy;
  - num pórtico, as barras de um mesmo nó mantêm os seus esforços, sem
    média.
- [ ] **AC-3** (REQ-012-11, 12) Faixa fixada pelo usuário: os valores fora
  dela recebem as cores das pontas; com N faixas, há exatamente N cores, e
  um valor no limite entre faixas cai numa delas de forma determinada.
- [ ] **AC-4** (REQ-012-14, 15) No pilar engastado e livre, o modo 1 é
  mostrado com o seu fator, que é o do `core`, e a animação oscila entre
  −f e +f sem refazer a geometria.
- [ ] **AC-5** (REQ-012-16, 19) No pórtico 2×2 da 010, as reações
  desenhadas são as do resultado, com os valores e as unidades do
  documento, e a soma delas equilibra a carga aplicada (como o resumo da
  011).
- [ ] **AC-6** (REQ-012-17, 18) As marcas de máximo e mínimo ficam nos
  pontos certos, com o id; a consulta num ponto do vão devolve o valor de
  `BeamResult::at` naquele x, e não o da amostra mais próxima.
- [ ] **AC-7** (REQ-012-02, 04, 08, 13) Desatualização e trocas:
  - editar o modelo mostra o aviso no viewport, e o desenho continua sobre
    a malha analisada;
  - trocar de caso mantém a grandeza e a escala do usuário;
  - trocar a escala, a grandeza ou as faixas não refaz a geometria.
- [ ] **AC-8** (REQ-012-01, 03, 07, 12, 15, 16, NFR-012-01, roteiro
  manual): ligar e desligar a exibição, a deformada sobre a original, as
  cores contínuas e em faixas, a animação, as reações e a consulta pelo
  clique; 30 FPS com o pórtico grande.
- [ ] **AC-9** (NFR-012-02, 03, 04) Os testes sem GPU, o teste de fumaça
  com o backend `Null`, o tempo de preparação, o tamanho dos buffers e o
  CI verde nas quatro plataformas.

## 6. Fora de escopo

- Diagramas de esforços N/V/M e seção extrudada no viewport (013).
- Tensões na seção (normal, cisalhamento, von Mises) e o empenamento como
  grandeza do mapa de cores.
- Campos de elementos que não são barras (sólidos, cascas, térmica): entram
  com os seus elementos.
- Animação de históricos no tempo (031, 034) e de modos de vibração (033).
- Isolinhas, escalas de cor definidas pelo usuário e escala logarítmica.
- Envoltórias e comparação de dois resultados lado a lado.
- Exportar imagem, vídeo da animação ou `.vtu` (014).
- Tradução pt_BR dos textos novos (pendência do roadmap, como na 009 a 011).

## 7. Requisitos globais relacionados

REQ-G-17 (mapa de cores com legenda, mínimo, máximo e unidades), REQ-G-18
(deformada com escala e animação de modos), REQ-G-20 (≥ 30 FPS),
constituição §3 (renderização pela Qt RHI) e §5.2 (unidades sempre exibidas).

## 8. Questões em aberto

- [x] Deformada dentro das barras: curva exata, amostrada (decidido em
  2026-11-26).
- [x] Grandezas do mapa de cores: deslocamentos, rotações e esforços; as
  tensões ficam para depois (decidido em 2026-11-26).
- [x] Animação dos modos de flambagem: sim (decidido em 2026-11-26).
- [x] Textos no viewport: reações, máximo e mínimo, e consulta (decidido em
  2026-11-26).
- [x] O fator automático leva o maior deslocamento desenhado a 10% da
  maior dimensão da caixa envolvente da malha (decidido em 2026-11-26). O
  número de pontos de amostragem por viga é do design, ímpar para incluir o
  meio.
