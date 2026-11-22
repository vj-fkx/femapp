# Requisitos — 008e beam-nonlinear-geometry

Status: aprovado (feature adiada para depois da 1.0 em 2026-11-22) · Data: 2026-11-22

## 1. Contexto e objetivo

A 008c e a 008d analisam barras em segunda ordem e em flambagem linear, mas
com deslocamentos e rotações pequenos: o equilíbrio é escrito na geometria
inicial, e a segunda ordem é um P-δ simétrico. Ficam de fora:

- os grandes deslocamentos e rotações;
- o caminho depois de um ponto limite (snap-through e snap-back) e a
  pós-flambagem;
- as cargas que acompanham a deformação (pressão hidrostática em anéis e
  arcos);
- o efeito da deformação antes da flambagem sobre a carga crítica. Na
  flambagem lateral com torção é a rotação da seção (Kirby e Nethercot):
  0,7% a 3,7% num balanço com EIy/EIz = 0,05, medido na T24b da 008d contra
  uma barra exata não linear.

Esta feature acrescenta ao `core` a **análise estática geometricamente não
linear** de treliças e vigas retas e curvas, inclusive com empenamento,
seguindo o caminho de equilíbrio com:

- controle de carga, de deslocamento e por comprimento de arco;
- detecção e localização dos pontos críticos, com troca de ramo nas
  bifurcações;
- cargas seguidoras;
- a flambagem linearizada a partir de um estado deformado.

Como nas features anteriores, tudo é exercitado por testes; nada muda na
interface.

## 2. Histórias de usuário

- **US-1** Como engenheiro, quero analisar estruturas de barras esbeltas
  com grandes deslocamentos e rotações (cabos de treliça, arcos abatidos,
  balanços flexíveis, pórticos esbeltos), com o equilíbrio na configuração
  deformada.
- **US-2** Como engenheiro, quero seguir o caminho de equilíbrio além dos
  pontos limite e das bifurcações (snap-through, snap-back, pós-flambagem),
  para conhecer a carga máxima e o comportamento depois dela.
- **US-3** Como engenheiro, quero saber onde o caminho fica instável (a
  carga crítica a partir do estado deformado, inclusive com a rotação da
  seção antes da flambagem lateral) e quanto falta, a partir de um estado,
  para a instabilidade.
- **US-4** Como engenheiro, quero aplicar cargas que acompanham a
  deformação (pressão normal ao eixo deformado, forças e momentos que giram
  com o nó), como a pressão hidrostática num anel ou num arco.
- **US-5** Como engenheiro, quero usar nessa análise os mesmos recursos das
  análises anteriores: vigas retas e curvas (arco e isoparamétrica),
  treliças, empenamento, ligações, excentricidades, base elástica, apoios
  elásticos, recalques, cargas no vão, peso próprio e temperatura.
- **US-6** Como engenheiro, quero ser avisado quando o caminho não pode
  continuar, com o que foi calculado até ali e o motivo, em vez de perder
  tudo.
- **US-7** Como desenvolvedor das próximas features (análise assíncrona
  011, resultados 012, diagramas 013), quero o caminho como uma sequência de
  estados, cada um com os resultados de uma análise estática, e os modos
  como campos nodais.
- **US-8** Como engenheiro, quero manter uma parte do carregamento
  constante (o peso próprio, as cargas permanentes) enquanto a outra cresce
  (a carga lateral, a variável), como na verificação de projeto.

## 3. Requisitos funcionais (EARS)

### Análise e teoria

| ID | Requisito | História |
|----|-----------|----------|
| REQ-008e-01 | O sistema DEVE oferecer a análise estática geometricamente não linear de um `Model` com treliças e vigas (2D e 3D), com as mesmas entradas da análise linear: apoios, apoios elásticos, recalques, cargas nodais, cargas no vão, peso próprio, temperatura e deformação inicial. O caso de carga é multiplicado por um fator de carga λ que o caminho faz variar; a parte constante do REQ-008e-22, se houver, não é multiplicada. | US-1, US-5 |
| REQ-008e-02 | A teoria DEVE ser a de grandes deslocamentos e rotações com deformações pequenas e material elástico linear. As rotações 3D DEVEM ser finitas: compostas, sem limite de ângulo e sem singularidade em nenhuma rotação. A formulação (corrotacional, lagrangiana total ou barra geometricamente exata) DEVE ser escolhida e justificada no design, com um ADR. | US-1 |
| REQ-008e-03 | O sistema DEVE tratar as vigas retas nas duas teorias (Euler-Bernoulli e Timoshenko), com ligações, molas de ligação, excentricidades rígidas e base elástica; as treliças; e as vigas curvas da 008b, em arco e isoparamétricas. A precisão de cada uma DEVE ser documentada no design e verificada com o refinamento da malha. | US-1, US-5 |
| REQ-008e-04 | Nas vigas com empenamento (7º GDL da 008d), o empenamento DEVE entrar na formulação de grandes rotações. Isso inclui as condições por ponta, o bimomento, os termos de Wagner e de monossimetria, a excentricidade do centro de cisalhamento e a altura da carga. | US-1, US-3, US-5 |
| REQ-008e-05 | Nos limites de carga pequena, a análise DEVE coincidir com as análises anteriores dentro da precisão documentada no design: com a análise linear da 007 a 008d na primeira ordem; com a segunda ordem da 008c/008d até a ordem que o P-δ representa; e o primeiro ponto crítico de uma estrutura sem deformação antes da flambagem com a flambagem linear da 008c/008d. | US-1, US-3 |

### Caminho de equilíbrio

| ID | Requisito | História |
|----|-----------|----------|
| REQ-008e-06 | O sistema DEVE oferecer três controles do caminho: de carga (incrementos de λ); de deslocamento (incrementos de uma componente de um nó escolhido); e por comprimento de arco (com a forma da restrição documentada no design). O tamanho do passo DEVE ser adaptativo, pelo número de iterações do passo anterior, entre um mínimo e um máximo ajustáveis. | US-1, US-2 |
| REQ-008e-07 | Cada passo DEVE ser resolvido por Newton-Raphson com a matriz tangente consistente. Considera-se convergido quando o resíduo e a correção dos deslocamentos ficam abaixo de tolerâncias relativas (padrões no design, ajustáveis), com um limite de iterações por passo. SE um passo não convergir, ENTÃO o sistema DEVE reduzir o passo e tentar de novo, até o passo mínimo. | US-1, US-6 |
| REQ-008e-08 | A análise DEVE parar no primeiro destes eventos: o fator de carga alvo, o deslocamento alvo, o número máximo de passos ou, se pedido, o primeiro ponto crítico. O resultado DEVE dizer qual deles ocorreu. | US-2, US-7 |
| REQ-008e-22 | O sistema DEVE aceitar a carga em duas fases. Cada carga do caso (nodal, no vão, peso próprio, temperatura, deformação inicial e recalque) pode ser marcada como constante; as não marcadas formam a parte variável. A primeira fase aplica a parte constante inteira por controle de carga, em passos, com a detecção de pontos críticos. A segunda segue o caminho em λ só com a parte variável, mantendo a constante. SE a primeira fase não chegar ao fim, ENTÃO o sistema DEVE devolver o que calculou com a mensagem (REQ-008e-18), dizendo que a parte constante sozinha não é suportada. Sem cargas marcadas, a análise é a de uma fase. | US-8 |
| REQ-008e-09 | Com o controle por comprimento de arco, o caminho DEVE atravessar pontos limite de carga e de deslocamento (snap-through e snap-back), escolhendo o sentido de avanço por um critério documentado no design, sem voltar pelo trecho já percorrido. | US-2 |

### Pontos críticos e flambagem

| ID | Requisito | História |
|----|-----------|----------|
| REQ-008e-10 | O sistema DEVE detectar os pontos críticos ao longo do caminho pela variação da contagem de pivôs negativos da matriz tangente, com as contagens internas das barras como na 008c/008d. Cada ponto DEVE ser localizado dentro de uma tolerância ajustável e classificado como ponto limite ou bifurcação, e o resultado DEVE trazer o fator de carga, o estado e o modo. | US-2, US-3 |
| REQ-008e-11 | QUANDO pedido, numa bifurcação detectada, O sistema DEVE trocar de ramo e seguir o ramo secundário, perturbando o estado na direção do modo crítico (critério e tamanho da perturbação documentados no design). Numa bifurcação simétrica, o lado do ramo DEVE poder ser escolhido. | US-2 |
| REQ-008e-12 | O sistema DEVE oferecer a flambagem linearizada a partir de um estado convergido do caminho. A partir da matriz tangente desse estado, calcula os menores fatores μ tais que o estado mais μ vezes o caso de carga (só a parte variável, com a carga em duas fases) torne a estrutura instável, com os modos, como na 008c/008d: mesma quantidade padrão, mesma normalização dos modos e mesmas mensagens. No estado inicial, o resultado DEVE ser o da flambagem linear da 008c/008d. | US-3 |
| REQ-008e-13 | A carga crítica obtida pelo caminho DEVE incluir o efeito da deformação antes da flambagem, inclusive a rotação da seção na flambagem lateral com torção (Kirby e Nethercot), e DEVE ser verificada contra a barra exata não linear da T24b da 008d. | US-3 |

### Cargas seguidoras

| ID | Requisito | História |
|----|-----------|----------|
| REQ-008e-14 | Cada carga distribuída no vão DEVE poder ser morta (direção global fixa, o padrão, como nas features anteriores) ou seguidora. Uma carga seguidora fica nos eixos locais da barra deformada (normal ou tangente ao eixo, como a pressão hidrostática) e é medida por comprimento deformado ou indeformado, conforme o design. | US-4 |
| REQ-008e-15 | Cada força e momento nodal DEVE poder ser morto (o padrão) ou seguidor, girando com a rotação do nó. Os momentos mortos DEVEM seguir a convenção da 008d (semitangenciais), documentada no design para as rotações finitas. | US-4 |
| REQ-008e-16 | As cargas seguidoras DEVEM entrar na matriz tangente pela rigidez de carga, que pode não ser simétrica. O sistema DEVE resolver os sistemas não simétricos e contar os pivôs negativos neles. Com cargas não conservativas, a análise é estática: os pontos críticos encontrados são de divergência, e o resultado DEVE avisar que a instabilidade dinâmica (flutter) não é verificada. | US-4 |

### Resultados, falhas e arquitetura

| ID | Requisito | História |
|----|-----------|----------|
| REQ-008e-17 | O resultado DEVE ser o caminho: a sequência dos estados convergidos. Cada estado traz: o fator de carga; os deslocamentos; as rotações finitas dos nós (em representação documentada no design); o empenamento; as reações; e os esforços e as tensões das barras nos eixos da seção deformada. Os esforços DEVEM ser avaliáveis ao longo de cada viga (`BeamResult::at(x)` ou equivalente), com a posição deformada. O resultado traz também os pontos críticos do REQ-008e-10, o motivo da parada do REQ-008e-08 e as iterações de cada passo. Quais estados guardar (todos ou a cada k passos) DEVE ser ajustável. | US-7 |
| REQ-008e-18 | SE o passo mínimo não convergir, ou SE a matriz tangente não puder ser fatorada fora de um ponto crítico, ENTÃO O sistema DEVE devolver o caminho calculado até ali junto com a mensagem do motivo: o passo, o fator de carga e, quando houver, o nó e a direção indicados pela fatoração ou o elemento. Isso não pode ser uma recusa sem resultado. | US-6 |
| REQ-008e-19 | O `Model` DEVE mudar só pelo necessário: a marcação de carga seguidora (REQ-008e-14 e 15) e a de carga constante (REQ-008e-22). As opções da análise (controle, passos, tolerâncias, alvos, troca de ramo, estados guardados) DEVEM ser parâmetros da função de análise, com padrões. A validação da 007 a 008d DEVE valer, com mensagens novas só para o que a marcação seguidora não admite (se o design encontrar casos). | US-5, US-6, US-8 |
| REQ-008e-20 | As análises existentes (linear, segunda ordem e flambagem linear) DEVEM continuar com os mesmos resultados. Nelas, as cargas seguidoras DEVEM ser tratadas como mortas e as constantes como cargas comuns do caso, com isso documentado. | US-5 |
| REQ-008e-21 | Tudo DEVE ficar em `core`, sem Qt. | US-7 |

## 4. Requisitos não funcionais

| ID | Requisito | Como medir |
|----|-----------|------------|
| NFR-008e-01 | Os casos de verificação DEVEM reproduzir as referências: soluções fechadas (anel de momento constante, treliça de von Mises) com erro relativo ≤ 1e−6; soluções por integrais elípticas ou por uma integração independente no teste (elástica, a barra exata não linear da T24b) com ≤ 1e−5, na malha documentada, e com a taxa de convergência documentada no design; benchmarks publicados (pórtico de Lee, pórtico de Williams, curva de 45° de Bathe e Bolourchi, arcos) dentro da precisão com que são publicados. | Testes automatizados |
| NFR-008e-02 | O design DEVE medir o custo de um passo convergido no pórtico espacial do benchmark da 008 (com e sem empenamento) e propor as metas do passo e de um caminho típico; a tarefa de custo as confirma. As análises existentes DEVEM manter as metas da 008b, 008c e 008d. Medidas em Release, na máquina do mantenedor. | Benchmark |
| NFR-008e-03 | A cobertura de linhas de `core` DEVE continuar em pelo menos 80% (constituição §6.5). | Job de cobertura no CI |

## 5. Critérios de aceite

- [ ] **AC-1** (REQ-008e-01, 02, 03, 05, NFR-008e-01) Grandes rotações de
  barras retas:
  - balanço com momento na ponta enrolado num círculo completo
    (M = 2π·E·I/L), em 2D e 3D, com a forma fechada;
  - balanço com força na ponta (elástica, por integrais elípticas), morta,
    em vários níveis de carga;
  - curva de 45° de Bathe e Bolourchi com força fora do plano;
  - limite de carga pequena: a análise linear e a segunda ordem da
    008c/008d.
- [ ] **AC-2** (REQ-008e-01, 06, 07, 08, 09, 22, NFR-008e-01) Caminho:
  - treliça de von Mises (duas barras abatidas) com snap-through, contra a
    solução analítica;
  - pórtico de Lee e pórtico de Williams (snap-back), contra os valores
    publicados;
  - o controle de carga parando no ponto limite e o de comprimento de arco
    passando por ele;
  - o controle de deslocamento;
  - os alvos de parada e o motivo da parada no resultado;
  - carga em duas fases (REQ-008e-22): pilar com compressão constante e
    carga lateral crescente, contra a solução da viga-coluna no limite de
    deslocamentos pequenos e contra a elástica adiante; pórtico com cargas
    gravitacionais constantes e lateral crescente; uma parte constante
    acima da crítica devolvida com a mensagem da primeira fase; a
    flambagem linearizada multiplicando só a parte variável.
- [ ] **AC-3** (REQ-008e-10, 11, 12, 05) Pontos críticos e pós-flambagem:
  - pilar de Euler com a bifurcação na carga de Euler (a da 008c);
  - a troca de ramo seguindo a pós-flambagem contra a elástica de Euler
    (relação entre carga e rotação da ponta);
  - o ponto limite da treliça de von Mises classificado como limite;
  - a flambagem linearizada no estado inicial igual à da 008c/008d, e num
    estado adiante concordando com o ponto crítico do caminho.
- [ ] **AC-4** (REQ-008e-04, 10, 13, NFR-008e-01) Flexo-torção com
  grandes rotações:
  - o balanço da T24b (EIy/EIz = 0,05), com a carga crítica pelo caminho
    contra a barra exata não linear (o efeito de Kirby e Nethercot);
  - a viga I biapoiada com garfos sob momento uniforme, com e sem
    empenamento, com a crítica tendendo à da 008d quando EIy/EIz → 0;
  - um caso com altura da carga.
- [ ] **AC-5** (REQ-008e-14, 15, 16, NFR-008e-01) Cargas seguidoras:
  - anel sob pressão hidrostática, com a crítica contra a forma fechada da
    pressão seguidora, diferente da carga morta;
  - arco sob pressão seguidora contra a referência do design;
  - balanço com força seguidora na ponta contra a referência do design;
  - momento nodal seguidor;
  - o aviso de não verificação do flutter.
- [ ] **AC-6** (REQ-008e-03, 04, NFR-008e-01) Vigas curvas e
  isoparamétricas com grandes rotações:
  - arco circular profundo com carga concentrada (limite e bifurcação),
    contra a referência do design;
  - arco e isoparamétrica concordando com o refinamento.
- [ ] **AC-7** (REQ-008e-17, 18) Resultados e falhas:
  - os estados guardados conforme pedido;
  - os esforços ao longo da viga deformada;
  - um caminho que não pode continuar devolvido até onde chegou, com a
    mensagem.
- [ ] **AC-8** (REQ-008e-19, 20, 21, NFR-008e-02, 03):
  - os testes da 007 a 008d passam;
  - as cargas seguidoras são tratadas como mortas e as constantes como
    cargas comuns nas análises antigas;
  - `core` segue sem Qt;
  - os benchmarks ficam registrados em `tasks.md` dentro das metas
    aprovadas;
  - CI verde nas quatro plataformas, com a cobertura de `core` ≥ 80%.

## 6. Fora de escopo

- Não linearidade do material (plasticidade, rótulas plásticas), contato e
  apoios só de compressão.
- Grandes deformações (a deformação continua pequena; só deslocamentos e
  rotações são grandes).
- Análise dinâmica e verificação de flutter das cargas não conservativas.
- Variante de Benscoter, distorção da seção e flambagem local, distorcional
  e de chapas (como na 008d).
- Imperfeições iniciais como dado do modelo (curvatura inicial, cargas
  nocionais, imperfeição pelo modo): continuam na pendência da 008c (010 ou
  feature própria). Aqui, a perturbação da troca de ramo é interna.
- Combinações de casos de carga (a carga em duas fases do REQ-008e-22 é
  dentro de um caso só).
- Execução assíncrona com progresso e cancelamento (011); desenho do caminho,
  da deformada e dos diagramas (012 e 013).

## 7. Requisitos globais relacionados

- REQ-G-14 (recusa de modelos incompletos), REQ-G-15 (análises) e REQ-G-16
  (reproduzir referências).
- **Alteração em `specs/product.md`**, aplicada com a aprovação destes
  requisitos e desfeita quando a feature foi adiada para depois da 1.0
  (2026-11-22); volta quando ela for retomada:
  - o §3.1 hoje põe "não linearidade geométrica geral (grandes
    deslocamentos)" fora da 1.0. Passa a excluir só a de cascas e sólidos, e
    a tabela ganha a linha "Não linearidade geométrica de barras (feature
    008e)";
  - o REQ-G-15 ganha, para barras, a análise geometricamente não linear.

## 8. Questões em aberto

- [x] **Carga em duas fases.** Entra nesta feature, pela marcação de carga
  constante no `Model` (REQ-008e-22; decidido pelo mantenedor em
  2026-11-22).
- [ ] **Formulação.** Será decidida no design (REQ-008e-02), com um ADR.
  Duas linhas candidatas:
  - corrotacional, reaproveitando no referencial local o elemento exato da
    008c/008d (o P-δ e a flexo-torção já exatos na barra, Battini e Pacoste
    para o empenamento);
  - barra geometricamente exata (Simo e Reissner, com o empenamento de
    Simo e Vu-Quoc).
  A escolha muda a precisão por elemento e o custo; o design traz a
  comparação.
