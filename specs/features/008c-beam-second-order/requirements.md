# Requisitos — 008c beam-second-order

Status: aprovado · Data: 2026-11-17

## 1. Contexto e objetivo

A 008 e a 008b resolvem pórticos e treliças em primeira ordem: o equilíbrio
é escrito na geometria inicial, e um pilar comprimido tem o mesmo momento que
teria sem o esforço normal. Em pórticos esbeltos isso subestima momentos e
deslocamentos (efeito P-Δ, pelo deslocamento dos nós, e P-δ, pela curvatura
dentro da barra) e não diz quão longe a estrutura está da instabilidade.

Esta feature acrescenta ao `core`:

- a **análise estática de segunda ordem**: o esforço normal de cada barra
  entra na sua rigidez, e o sistema é resolvido de forma iterativa até o
  esforço normal e os deslocamentos convergirem;
- a **flambagem linear**: os fatores de carga crítica e os modos de
  flambagem de um caso de carga.

O efeito do esforço normal entra exatamente na viga (funções de
estabilidade, pelo motor de espaço de estados da 008b), de modo que uma
barra por vão já dá P-δ e a carga crítica de Euler sem subdividir. Como nas
features anteriores, tudo é exercitado por testes; nada muda na interface.

## 2. Histórias de usuário

- **US-1** Como engenheiro, quero analisar pórticos e treliças em segunda
  ordem, com os momentos amplificados pelo esforço normal (P-Δ e P-δ), sem
  precisar dividir os pilares em vários elementos.
- **US-2** Como engenheiro, quero saber o fator de carga crítica e o modo de
  flambagem de um caso de carga, para avaliar a esbeltez global da estrutura
  e os comprimentos de flambagem das barras.
- **US-3** Como engenheiro, quero usar nessas análises os mesmos recursos da
  análise linear: vigas retas e curvas, treliças, ligações, excentricidades,
  base elástica, apoios elásticos, recalques, cargas no vão e temperatura.
- **US-4** Como engenheiro, quero ser avisado quando a carga passa da
  crítica ou a iteração não converge, em vez de receber um resultado sem
  sentido.
- **US-5** Como desenvolvedor das próximas features (análise assíncrona
  011, resultados 012, diagramas 013), quero os resultados de segunda ordem
  pela mesma interface da análise linear, e os modos de flambagem como
  campos nodais.

## 3. Requisitos funcionais (EARS)

### Segunda ordem

| ID | Requisito | História |
|----|-----------|----------|
| REQ-008c-01 | O sistema DEVE oferecer a análise estática de segunda ordem de um `Model` com treliças e vigas (2D e 3D), com as mesmas entradas da análise linear (apoios, apoios elásticos, recalques, cargas nodais, cargas no vão, peso próprio, temperatura, deformação inicial) e os mesmos resultados (`StaticResult`: deslocamentos, rotações, reações, esforços e tensões das barras). | US-1, US-3, US-5 |
| REQ-008c-02 | Nas vigas retas, o esforço normal N DEVE entrar exatamente na rigidez e nas cargas equivalentes da barra (funções de estabilidade, com compressão e tração), nas duas teorias (Euler-Bernoulli e Timoshenko) e com ligações, molas de ligação, excentricidades, base elástica e cargas no vão, com N constante na barra; com N variável ao longo dela (cargas axiais no vão), com erro relativo ≤ 1e−8 *(ajustado no design, 2026-11-17)*. Um único elemento por vão DEVE reproduzir o P-δ. Na teoria de Timoshenko, a interação do cortante com N DEVE seguir uma formulação documentada no design (Engesser ou Haringx). | US-1, US-3 |
| REQ-008c-03 | Nas treliças, o esforço normal DEVE entrar pela rigidez geométrica da barra articulada (P-Δ da corda). | US-1, US-3 |
| REQ-008c-04 | Nas vigas curvas (arco exato e isoparamétrica da 008b), o esforço normal ao longo da barra DEVE entrar na rigidez, com a precisão documentada no design e verificada contra uma referência. | US-1, US-3 |
| REQ-008c-05 | A análise DEVE ser iterativa: começar pela solução linear, recalcular o esforço normal de cada barra a partir da solução corrente e repetir até que a variação relativa dos deslocamentos e dos esforços normais fique abaixo de uma tolerância (padrão 1e−8, ajustável), com um limite de iterações (padrão 50, ajustável). | US-1 |
| REQ-008c-06 | O resultado DEVE informar o número de iterações feitas e a variação final, e os esforços ao longo das vigas (`BeamResult::at(x)`) DEVEM incluir o efeito de segunda ordem dentro da barra (momento N·δ). | US-1, US-5 |
| REQ-008c-07 | As cargas DEVEM manter a direção que têm no modelo (cargas conservativas, sem acompanhar a deformação), e a teoria é a de deslocamentos e rotações pequenos: o equilíbrio considera o efeito do esforço normal nos deslocamentos, sem atualizar a geometria. | US-1 |
| REQ-008c-08 | SE a iteração não convergir no limite de iterações, ou SE a rigidez de segunda ordem deixar de ser positiva definida (carga igual ou acima da crítica), ENTÃO O sistema DEVE recusar o resultado com uma mensagem que diga qual dos dois casos ocorreu e, no segundo, a iteração em que ocorreu e o nó e a direção indicados pela fatoração ou, se a instabilidade estiver dentro de uma barra, o elemento *(ajustado no design, 2026-11-17)*. | US-4 |

### Flambagem linear

| ID | Requisito | História |
|----|-----------|----------|
| REQ-008c-09 | O sistema DEVE oferecer a análise de flambagem linear de um `Model` com treliças e vigas (2D e 3D): os esforços normais vêm da análise linear do caso de carga, e o sistema DEVE calcular os menores fatores de carga positivos λ (padrão: 1 fator; quantidade ajustável) tais que o caso de carga multiplicado por λ torne a estrutura instável, com o modo de flambagem de cada um. | US-2 |
| REQ-008c-10 | Nas vigas retas, o fator crítico DEVE ser exato para a barra (o mesmo efeito do REQ-008c-02): um elemento por vão DEVE dar a carga de Euler e os comprimentos de flambagem clássicos, nas duas teorias e com ligações, molas, excentricidades e base elástica. Nas treliças vale o REQ-008c-03; nas vigas curvas, o REQ-008c-04. | US-2 |
| REQ-008c-11 | Cada modo DEVE trazer os deslocamentos e as rotações dos nós, normalizados com a maior componente de translação igual a 1, e a deformada ao longo de cada viga, avaliável em qualquer ponto. | US-2, US-5 |
| REQ-008c-12 | SE o caso de carga não produzir compressão em nenhuma barra, ou SE não houver fator positivo na quantidade pedida, ENTÃO O sistema DEVE informar isso, com os fatores que encontrou. | US-2, US-4 |

### Modelo, validação e arquitetura

| ID | Requisito | História |
|----|-----------|----------|
| REQ-008c-13 | O `Model` não DEVE mudar: as opções das análises novas (tolerância, limite de iterações, número de fatores) DEVEM ser parâmetros das funções de análise, com padrões. A validação da 007 a 008b DEVE valer para as análises novas, sem mensagens novas além das do REQ-008c-08 e do REQ-008c-12. | US-3, US-4 |
| REQ-008c-14 | A análise linear (`runLinearStatic`) DEVE continuar com os mesmos resultados da 008 e da 008b. | US-3 |
| REQ-008c-15 | Tudo DEVE ficar em `core`, sem Qt. | US-5 |

## 4. Requisitos não funcionais

| ID | Requisito | Como medir |
|----|-----------|------------|
| NFR-008c-01 | Nas vigas retas e nas treliças, os casos de verificação (AC-1 a AC-6) DEVEM reproduzir as soluções analíticas com erro relativo ≤ 1e−8 em deslocamentos, esforços e fatores críticos (também com N variável na barra, REQ-008c-02); nas vigas curvas, com a tolerância do design. | Testes automatizados |
| NFR-008c-02 | No pórtico espacial do benchmark da 008 (cerca de 30 mil GDL), a análise de segunda ordem DEVE levar no máximo 4 vezes o tempo da análise linear do mesmo modelo, e a flambagem com 5 fatores no máximo 12 vezes *(alterado de 6 vezes em 2026-11-17, após a medida da T23)*, em Release na máquina do mantenedor. | Benchmark |
| NFR-008c-03 | A cobertura de linhas de `core` DEVE continuar em pelo menos 80% (constituição §6.5). | Job de cobertura no CI |

## 5. Critérios de aceite

- [x] **AC-1** (REQ-008c-02, 06, NFR-008c-01) Barra isolada em segunda
  ordem, um elemento: viga-coluna biapoiada com carga transversal uniforme e
  concentrada sob compressão e sob tração (deslocamento e momento no meio
  contra as fórmulas fechadas de Timoshenko-Gere); balanço com força lateral
  e compressão na ponta; momentos ao longo da barra por `at(x)`; Timoshenko
  contra a fórmula fechada da formulação escolhida.
- [x] **AC-2** (REQ-008c-01, 05, 06) Pórtico: pórtico plano de um andar com
  carga lateral e cargas verticais nos pilares, contra a solução exata por
  funções de estabilidade; o fator de amplificação aproximado
  1/(1 − P/Pcr) confere a ordem de grandeza; o número de iterações e a
  variação final aparecem no resultado.
- [x] **AC-3** (REQ-008c-03, 05) Treliça: barra articulada tracionada que
  enrijece sob carga transversal no nó (corda tensa), contra a solução
  analítica linearizada; treliça plana comprimida com P-Δ global.
- [x] **AC-4** (REQ-008c-09, 10, 11, NFR-008c-01) Flambagem de barras, um
  elemento por vão: Euler nos quatro casos clássicos de apoio (comprimentos
  de flambagem 2L, L, 0,7L e 0,5L), em 2D e 3D, nas duas teorias; barra com
  mola rotacional na ponta e com liberação; barra sobre base elástica
  (Hetényi, com o número de semiondas certo); modos normalizados e a
  deformada ao longo da barra igual à senoide de Euler.
- [x] **AC-5** (REQ-008c-09, 10) Flambagem de pórticos: pórtico plano de um
  andar, deslocável e indeslocável, contra o fator exato por funções de
  estabilidade; os primeiros fatores em ordem crescente; pilar com
  excentricidade nas pontas.
- [x] **AC-6** (REQ-008c-04, 10) Vigas curvas: arco biarticulado
  semicircular sob carga concentrada no topo e sob carga distribuída, em
  segunda ordem e em flambagem, com a formulação em arco e a isoparamétrica
  convergindo uma para a outra e para a referência documentada no design.
- [x] **AC-7** (REQ-008c-08, 12) Falhas: carga acima da crítica numa
  segunda ordem recusada com a mensagem da instabilidade; limite de
  iterações baixo recusado com a mensagem de não convergência; caso de carga
  só com tração na flambagem informado.
- [x] **AC-8** (REQ-008c-13, 14, 15, NFR-008c-02, 03) Os testes da 007, da
  008 e da 008b passam sem alteração de resultado; `core` segue sem Qt;
  benchmark da segunda ordem e da flambagem registrado em `tasks.md`; CI
  verde nas quatro plataformas, com a cobertura de `core` ≥ 80%.

## 6. Fora de escopo

- **Instabilidade lateral com torção (flexo-torção)** e o termo de Wagner:
  vão para a 008d, junto com o empenamento (decidido em 2026-11-17).
- Grandes deslocamentos e rotações (corrotacional, Lagrangiano total),
  pós-flambagem, controle por comprimento de arco, cargas que acompanham a
  deformação (pressão hidrostática).
- Não linearidade do material, contato, apoios só de compressão.
- Imperfeições iniciais (curvatura inicial, cargas nocionais, imperfeição
  pelo modo de flambagem) e combinações de casos de carga: ficam para a
  interface (010) ou para uma versão futura.
- Flambagem local da seção, distorcional e de chapas.
- Flambagem da própria barra de treliça entre os nós (a treliça não tem
  flexão; quem precisa disso modela a barra como viga).
- Execução assíncrona com progresso e cancelamento — 011; desenho dos modos
  e dos diagramas de segunda ordem — 012 e 013.

## 7. Requisitos globais relacionados

- REQ-G-14 (recusar modelo singular, indicando o que falta), REQ-G-16
  (soluções analíticas), REQ-G-22 (todos os núcleos).
- **Alteração proposta em `specs/product.md`:** hoje §3.1 põe "não
  linearidade geométrica" e "flambagem" fora da 1.0, mas o roadmap já tem a
  008c na v0.2. Proposta: acrescentar à tabela de §3.1 a linha
  "Segunda ordem e flambagem de barras: P-Δ/P-δ iterativo e flambagem linear
  de pórticos e treliças", restringir o "Fora da 1.0" a "não linearidade
  geométrica geral (grandes deslocamentos), flambagem de cascas e sólidos"
  e acrescentar ao REQ-G-15 "e, para barras, estática de segunda ordem e
  flambagem linear". Aplicada em 2026-11-17, na aprovação destes
  requisitos.

## 8. Questões em aberto

- [x] Efeitos: P-Δ e P-δ, com N exato na viga por funções de estabilidade
  (decidido em 2026-11-17).
- [x] Flambagem linear incluída (decidido em 2026-11-17).
- [x] Elementos: vigas retas, treliças e vigas curvas (arco e
  isoparamétrica) (decidido em 2026-11-17).
- [x] Flexo-torção: fora, com a 008d (decidido em 2026-11-17).
- [x] Metas do NFR-008c-02 (segunda ordem ≤ 4× a linear; flambagem com 5
  fatores ≤ 6×): mantidas (decidido em 2026-11-17, na aprovação).
