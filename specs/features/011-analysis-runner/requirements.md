# Requisitos — 011 analysis-runner

Status: aprovado · Data: 2026-11-24

## 1. Contexto e objetivo

As features 007 a 008d deixaram no `core` três análises de barras:
- a estática linear;
- a estática de segunda ordem (P-Δ/P-δ);
- a flambagem linear.

A 010 monta o `core::Model` de um caso ou combinação a partir do documento.
Mas ainda não há como **rodar** uma análise pela interface. Esta feature
fecha esse elo:
- escolher a análise e as suas opções;
- conferir o modelo antes;
- executar fora da thread da interface, com progresso e cancelamento de
  verdade, que para o cálculo no `core`;
- guardar os resultados por caso e por combinação;
- mostrá-los em resumo e em tabelas.

A visualização gráfica (deformada, mapas de cores) fica para a 012, e os
diagramas de esforços para a 013. Ambas leem os resultados guardados aqui.

## 2. Histórias de usuário

- **US-1** Como engenheiro, quero rodar a análise do caso ou da combinação
  ativa com um comando, para ver a resposta da estrutura sem sair do
  aplicativo.
- **US-2** Como engenheiro, quero rodar todos os casos e combinações de uma
  vez, para ter os resultados de todos antes de dimensionar.
- **US-3** Como engenheiro, quero escolher entre análise linear, de segunda
  ordem e de flambagem, com as suas opções, para a verificação que a norma
  pede.
- **US-4** Como usuário, quero que a interface continue respondendo durante
  uma análise longa, ver o progresso e poder cancelá-la, para não perder
  tempo com um modelo errado.
- **US-5** Como usuário, quero que um modelo incompleto ou instável seja
  recusado com a indicação do problema, para corrigi-lo sem adivinhar.
- **US-6** Como engenheiro, quero ver deslocamentos, reações e esforços em
  resumo e em tabelas, com as unidades do modelo, e localizar no viewport o
  nó ou a barra de uma linha.
- **US-7** Como usuário, quero saber quando os resultados não correspondem
  mais ao modelo, para não usar números velhos.

## 3. Requisitos funcionais (EARS)

### Tipos de análise e opções

| ID | Requisito | História |
|----|-----------|----------|
| REQ-011-01 | O sistema DEVE oferecer três tipos de análise, com as opções de cada um e os valores padrão do `core`: <br>• estática linear; <br>• estática de segunda ordem, com tolerância e número máximo de iterações; <br>• flambagem linear, com o número de modos. <br>O tipo e as opções são do documento e valem para todos os casos. A troca entra no desfazer, como as outras edições do modelo (REQ-010-23). | US-3 |
| REQ-011-02 | O sistema DEVE executar a análise escolhida sobre o `core::Model` montado pela 010 (REQ-010-20) para o caso ou a combinação. Uma combinação é analisada como um caso só, a soma ponderada das ações, como pede a segunda ordem. | US-1, US-3 |

### Execução

| ID | Requisito | História |
|----|-----------|----------|
| REQ-011-03 | QUANDO o usuário pedir **Run analysis**, O sistema DEVE analisar o caso ou a combinação ativa. | US-1 |
| REQ-011-04 | QUANDO o usuário pedir **Run all**, O sistema DEVE analisar em sequência todos os casos e combinações do documento, guardando o resultado de cada um. A falha de um item NÃO DEVE interromper os seguintes; o fim da execução lista os que falharam e por quê. | US-2 |
| REQ-011-05 | ANTES de executar, O sistema DEVE conferir o modelo como a conferência da 010 (REQ-010-21). SE houver erros, ENTÃO DEVE recusar a execução daquele caso, abrir a conferência com os problemas e não alterar os resultados existentes. Os avisos (como o da treliça, REQ-010-10) não impedem a execução e são listados. | US-5 |
| REQ-011-06 | A análise DEVE rodar fora da thread da interface, sobre um instantâneo do modelo montado. ENQUANTO ela roda, a interface DEVE continuar respondendo, inclusive a edições do desenho e do modelo, que não afetam o cálculo em andamento. | US-4 |
| REQ-011-07 | ENQUANTO uma análise roda, O sistema DEVE mostrar o progresso: <br>• o caso (e "k de n" no Run all); <br>• a etapa (montagem do modelo, montagem das matrizes, fatoração, solução, iteração i da segunda ordem, iteração da flambagem); <br>• uma fração estimada. <br>Um novo pedido de análise DEVE ser recusado com aviso até ela terminar ou ser cancelada. | US-4 |
| REQ-011-08 | QUANDO o usuário cancelar, O sistema DEVE parar o cálculo no `core` na próxima verificação entre etapas, liberar a thread e manter os resultados anteriores do caso em andamento. No Run all, os casos já concluídos guardam o seu resultado, e os seguintes não rodam. | US-4 |
| REQ-011-09 | O `core` DEVE aceitar, em todas as análises, um sinal de cancelamento e um retorno de progresso opcionais, sem Qt. Sem eles, o comportamento e o resultado DEVEM ser os de hoje. O cancelamento DEVE ser verificado: <br>• entre a montagem e a fatoração; <br>• entre a fatoração e a solução; <br>• a cada iteração da segunda ordem e da flambagem; <br>• ao longo da montagem, por bloco de elementos. <br>Uma fatoração já iniciada termina antes da parada. | US-4 |
| REQ-011-10 | SE o `core` falhar (mecanismo, carga crítica, falta de convergência, nenhuma barra comprimida na flambagem), ENTÃO O sistema DEVE: <br>• mostrar a mensagem do `core` no console e na conferência; <br>• realçar no viewport os nós e elementos que ela cita, como na 010 (REQ-010-21); <br>• manter o resultado anterior daquele caso, marcado como desatualizado. | US-5 |
| REQ-011-18 | SE o `core` recusar a análise por instabilidade, ENTÃO a mensagem DEVE indicar onde está a causa: o nó e o GDL, e os elementos envolvidos, pelos ids externos: <br>• **GDL sem rigidez:** cada nó e GDL, com os elementos ligados ao nó; <br>• **mecanismo:** o nó e o GDL que mais se movem no modo do mecanismo (o deslocamento sem resistência), e os elementos que se movem nele; <br>• **perda da estabilidade na segunda ordem:** o nó e o GDL que mais se movem no modo da instabilidade, e os elementos que a provocam (os de energia negativa nesse modo); <br>• **instabilidade dentro de uma barra:** o elemento e o plano local (y ou z) em que ela flamba. <br>As listas de elementos param em 10 ids, seguidos de "and N more". Os nós e elementos citados são realçados como no REQ-011-10. *(Acrescentado em 2026-11-25, a pedido do mantenedor.)* | US-5 |

### Resultados

| ID | Requisito | História |
|----|-----------|----------|
| REQ-011-11 | O sistema DEVE guardar um resultado por caso e por combinação, com: <br>• o tipo de análise e as opções usadas; <br>• a geração da malha e a revisão do documento analisadas; <br>• o resultado do `core` (`StaticResult`, `SecondOrderResult` ou `BucklingResult`). <br>Uma nova análise do mesmo item substitui o resultado anterior. Os resultados ficam em memória; salvá-los é da 014. | US-2, US-6 |
| REQ-011-12 | QUANDO o documento, o desenho ou a malha mudarem depois de uma análise, O sistema DEVE marcar como desatualizados os resultados afetados (todos, nesta feature) e indicar isso na árvore e no dock de resultados. Desfazer até a revisão analisada DEVE torná-los atuais de novo. | US-7 |
| REQ-011-13 | QUANDO uma análise terminar, O sistema DEVE escrever no console um resumo do caso: <br>• estática e segunda ordem: deslocamento de maior módulo (valor, nó e direção), maior rotação, soma das reações por direção comparada à soma das cargas aplicadas, maiores esforço normal, cortante e momento com a barra; <br>• segunda ordem: também as iterações e a última variação; <br>• flambagem: os fatores de carga crítica e a nota do `core` quando houver menos modos que o pedido. | US-6 |
| REQ-011-14 | O sistema DEVE oferecer um dock **Results** com o caso ou combinação escolhido numa lista e tabelas: <br>• deslocamentos e rotações por nó (u, v, w, θx, θy, θz e empenamento quando houver); <br>• reações por nó apoiado; <br>• esforços por barra, nas duas pontas (N, Vy, Vz, T, My, Mz); <br>• na flambagem, os fatores de cada modo e o deslocamento do modo por nó, normalizado como no `core`. <br>Os nós e elementos aparecem pelos ids externos da malha, e os valores na unidade de cada grandeza do sistema do documento (REQ-010-02). | US-6 |
| REQ-011-15 | QUANDO o usuário selecionar linhas de uma tabela de resultados, O sistema DEVE realçar no viewport os nós ou elementos correspondentes, se a malha mostrada for a do modelo. | US-6 |
| REQ-011-16 | A árvore do modelo DEVE indicar em cada caso e combinação se há resultado, de que tipo, e se está atual ou desatualizado. | US-6, US-7 |

### Interface

| ID | Requisito | História |
|----|-----------|----------|
| REQ-011-17 | O sistema DEVE oferecer um menu **Analysis**: <br>• Settings… (tipo e opções); <br>• Run analysis (F5); <br>• Run all; <br>• Cancel (Esc, enquanto roda). <br>A barra de status DEVE mostrar o progresso e um botão de cancelar, como a geração de malha (009). | US-1, US-4 |

## 4. Requisitos não funcionais

| ID | Requisito | Como medir |
|----|-----------|------------|
| NFR-011-01 | Durante a análise linear do pórtico do `bench_line_mesh` (cerca de 10⁵ barras), Release, máquina do mantenedor, a thread da interface NÃO DEVE ficar bloqueada mais que 100 ms seguidos. | Teste com laço de eventos medindo o maior intervalo entre timers, e roteiro manual |
| NFR-011-02 | O cancelamento DEVE liberar a thread em ≤ 1 s, salvo durante uma fatoração já iniciada, nas mesmas condições do NFR-011-01. | Teste que cancela durante a montagem e na segunda ordem e mede o tempo |
| NFR-011-03 | As verificações de cancelamento e o progresso NÃO DEVEM custar mais que 2% do tempo de uma análise sem eles. | Benchmark com e sem os retornos |
| NFR-011-04 | As tabelas DEVEM abrir e rolar com 10⁵ linhas sem travar a interface (modelo de tabela virtual, sem uma linha de widget por valor). | Roteiro manual com o pórtico do benchmark |
| NFR-011-05 | A cobertura de linhas do `core` DEVE continuar ≥ 80%, e o código novo de execução e resultados, sem widgets, DEVE ser testado sem interface. | Job de cobertura no CI |

## 5. Critérios de aceite

- [ ] **AC-1** (REQ-011-01, 02, 03, 13) O pórtico 2×2 da 010 (T10), pelo
  documento:
  - a análise linear dá os mesmos deslocamentos, reações e esforços que
    `runLinearStatic` chamado direto sobre o modelo montado, em 1e−12;
  - a segunda ordem e a flambagem dão os mesmos resultados que as
    funções do `core` correspondentes;
  - o resumo traz os valores certos.
- [ ] **AC-2** (REQ-011-04, 11) Run all num documento com dois casos, uma
  combinação e um caso inválido:
  - três resultados guardados;
  - uma falha listada com o motivo;
  - a ordem dos casos respeitada.
- [ ] **AC-3** (REQ-011-05, 10) Recusas:
  - sem material, a conferência abre e nada roda;
  - um mecanismo dá a mensagem do `core` com o nó realçado e mantém o
    resultado anterior;
  - (REQ-011-18) no quadrado articulado sem diagonal, a mensagem cita um
    nó que se move e exatamente as três barras que se movem; um pilar
    acima da carga crítica cita a direção lateral e os elementos
    comprimidos, e não os descarregados; a instabilidade dentro de uma
    barra cita o elemento e o plano; os ids citados são os que o
    `idsInMessage` extrai;
  - a segunda ordem acima da carga crítica e a flambagem sem compressão
    falham com a mensagem do `core`.
- [ ] **AC-4** (REQ-011-06, 07, 08, 09, NFR-011-01, 02) Execução:
  - o laço de eventos segue respondendo durante a análise;
  - o progresso passa pelas etapas;
  - o cancelamento para o `core` dentro do prazo em cada ponto de
    verificação;
  - um segundo pedido durante a execução é recusado;
  - sem retornos, os resultados do `core` são os mesmos de antes, bit a
    bit.
- [ ] **AC-5** (REQ-011-12, 16) Desatualização:
  - editar uma carga, mover uma linha e gerar a malha de novo marcam os
    resultados como desatualizados;
  - desfazer a edição os torna atuais;
  - a árvore mostra o estado.
- [ ] **AC-6** (REQ-011-14, 15, 17, NFR-011-04, roteiro manual):
  - o menu Analysis e o progresso na barra de status;
  - o dock Results com as tabelas nas unidades do documento;
  - selecionar linhas realça nós e barras;
  - 10⁵ linhas rolam sem travar;
  - Esc cancela.
- [ ] **AC-7** (NFR-011-03, 05) O benchmark com e sem os retornos dentro de
  2%, a cobertura do `core` ≥ 80% e o CI verde nas quatro plataformas.

## 6. Fora de escopo

- Deformada, mapas de cores e legenda no viewport (012); diagramas de
  esforços e seção extrudada (013).
- Salvar resultados em arquivo, exportar `.vtu` ou CSV (014); copiar
  tabelas para a área de transferência.
- Análises que não são de barras (térmica, modal, dinâmica, sólidos e
  cascas): entram com os seus elementos.
- Combinações geradas por norma com os γ (REQ-010-26, feature futura) e
  envoltórias de combinações.
- Rodar vários casos em paralelo: o Run all é sequencial, e cada análise já
  usa todos os núcleos na montagem e na solução (REQ-G-22).
- Interromper uma fatoração no meio: o solver esparso não oferece esse
  ponto de parada.
- Tradução pt_BR dos textos novos (pendência do roadmap, como na 009 e na
  010).

## 7. Requisitos globais relacionados

REQ-G-05 (interface responsiva, progresso e cancelamento), REQ-G-14 (recusar
modelos incompletos ou singulares), REQ-G-15 (estática linear, segunda ordem
e flambagem de barras), REQ-G-22 (todos os núcleos), constituição §4.3.

## 8. Questões em aberto

- [x] Quais análises: as três de barras (decidido em 2026-11-24).
- [x] Cancelamento: de verdade, no `core`, com progresso (decidido em
  2026-11-24).
- [x] Casos: o ativo ou todos, com Run all (decidido em 2026-11-24).
- [x] Resultados: resumo no console e tabelas num dock (decidido em
  2026-11-24).
