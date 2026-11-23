# Requisitos — 010 model-setup-ui

Status: aprovado · Data: 2026-11-22

## 1. Contexto e objetivo

O `core` analisa treliças e vigas (007 a 008d), e a 009 transforma o
desenho numa malha com um grupo por entidade. Falta o meio do caminho:
hoje um `core::Model` só existe montado em código, nos testes. Não há onde
o usuário escolha unidades, defina materiais e seções, atribua isso às
barras, ponha apoios e cargas.

Esta feature cria a **preparação do modelo** na interface:

- um documento de modelo, guardado nas entidades do desenho (sobrevive a
  edições e a uma nova malha) e, quando o usuário quiser, em grupos e
  seleções de uma malha;
- a árvore do modelo e o painel de propriedades;
- o sistema de unidades;
- todos os dados que o `core` aceita, em casos de carga e combinações;
- símbolos de apoios e cargas no viewport;
- desfazer e refazer de tudo;
- a montagem do `core::Model` de um caso ou combinação sobre a malha
  atual, que a 011 vai executar.

A análise (execução, progresso, resultados) fica para a 011 e a 012.

## 2. Histórias de usuário

- **US-1** Como engenheiro, quero escolher um sistema de unidades coerente
  e vê-lo sempre, para não errar a ordem de grandeza dos dados.
- **US-2** Como engenheiro, quero definir materiais (com uma biblioteca de
  materiais comuns) e seções de todos os tipos do `core`, vendo as
  propriedades calculadas e o desenho da seção.
- **US-3** Como engenheiro, quero atribuir material, seção e tipo de barra
  às linhas do desenho, com orientação, teoria, empenamento e formulação das
  curvas, e os detalhes de viga (ligações, excentricidades, base elástica,
  condições de empenamento).
- **US-4** Como engenheiro, quero pôr apoios e recalques e todas as cargas
  do `core` em casos de carga, e combinar os casos com fatores.
- **US-5** Como engenheiro, quero que o que atribuí às entidades sobreviva
  quando eu edito o desenho e gero a malha de novo.
- **US-6** Como engenheiro que importou uma malha `.msh`, quero atribuir
  por grupo físico ou por elementos e nós escolhidos na malha.
- **US-7** Como engenheiro, quero ver no viewport os apoios e as cargas do
  caso ativo, e os eixos locais das barras quando pedir.
- **US-8** Como engenheiro, quero desfazer e refazer qualquer mudança no
  modelo, junto com as do desenho.
- **US-9** Como engenheiro, quero conferir o modelo antes de analisar e ver
  a lista do que falta ou está errado.
- **US-10** Como desenvolvedor da 011 a 014, quero o documento do modelo
  separado da interface, testável sem Qt, e uma função que monte o
  `core::Model` de um caso ou combinação.

## 3. Requisitos funcionais (EARS)

### Documento e unidades

| ID | Requisito | História |
|----|-----------|----------|
| REQ-010-01 | O sistema DEVE manter um documento de modelo, separado do desenho e da malha, com: o sistema de unidades, os materiais, as seções, as atribuições de barra, os detalhes de viga, os apoios, os casos de carga com as suas cargas e as combinações. O documento e a montagem do `core::Model` (REQ-010-20) NÃO DEVEM depender de Qt. | US-10 |
| REQ-010-02 | O sistema DEVE oferecer sistemas de unidades coerentes pré-definidos, ao menos: N-m-kg-s, kN-m-t-s, N-mm-t-s, kgf-cm e lb-in, com força, comprimento, massa, tempo, temperatura e as derivadas (tensão, momento, rigidez, massa específica) definidas de forma coerente. O sistema ativo DEVE aparecer sempre na barra de status, e cada campo numérico DEVE mostrar a sua unidade. | US-1 |
| REQ-010-03 | QUANDO o usuário trocar o sistema de unidades, O sistema DEVE converter todos os valores do documento para o novo sistema, mantendo as grandezas físicas, e a troca DEVE poder ser desfeita. O desenho não é convertido: as coordenadas ficam como desenhadas, no comprimento do sistema novo. O sistema DEVE avisar isso antes de trocar. | US-1 |

### Materiais e seções

| ID | Requisito | História |
|----|-----------|----------|
| REQ-010-04 | O sistema DEVE permitir criar, editar, renomear, duplicar e apagar materiais (E, ν, ρ, α, k, c, como o `core::Material`). DEVE oferecer uma biblioteca com materiais comuns (aço estrutural, concreto, alumínio, madeira), copiados para o documento nas unidades ativas. Apagar um material em uso DEVE ser recusado, com a lista de onde ele é usado. | US-2 |
| REQ-010-05 | O sistema DEVE permitir criar e editar seções de todos os tipos do `core`, com as mesmas regras de apagar do REQ-010-04: <br>• genérica, pelas propriedades; <br>• as oito formas paramétricas; <br>• paredes finas, por uma tabela de nós e segmentos com espessura; <br>• polígono maciço, a partir de polilinhas fechadas selecionadas no desenho (a de fora e os furos), copiadas para a seção. | US-2 |
| REQ-010-06 | O painel de uma seção DEVE mostrar as propriedades calculadas pelo `core`: A, Iy, Iz, J, Iw, centroide, centro de cisalhamento, áreas de cisalhamento e coeficientes de monossimetria. DEVE mostrar também um desenho da seção com os eixos principais, o centroide, o centro de cisalhamento e os pontos de tensão. SE os dados forem inválidos, ENTÃO DEVE mostrar a mensagem do `core` em vez das propriedades. | US-2 |

### Atribuições e detalhes de viga

| ID | Requisito | História |
|----|-----------|----------|
| REQ-010-07 | O sistema DEVE permitir atribuir aos alvos do REQ-010-12: <br>• o tipo de barra (treliça 2D/3D, viga 2D/3D); <br>• o material e a seção; <br>• nas vigas, a teoria (Euler-Bernoulli ou Timoshenko), a orientação (vetor de referência e ângulo de giro), o empenamento e a formulação das curvas (arco ou isoparamétrica). <br>Uma atribuição nova sobre um alvo já atribuído DEVE substituir a anterior nele. | US-3 |
| REQ-010-08 | O sistema DEVE permitir atribuir às barras os detalhes de viga do `core`: <br>• ligações de extremidade, por ponta e por esforço (rígida, liberada ou mola); <br>• excentricidades rígidas nas duas pontas; <br>• base elástica (translacional e torcional); <br>• condições de empenamento por ponta (livre, impedido, mola, contínuo). | US-3 |
| REQ-010-09 | QUANDO o usuário pedir, O sistema DEVE mostrar no viewport os eixos locais das vigas selecionadas, ou de todas. | US-3, US-7 |
| REQ-010-10 | QUANDO uma atribuição for de treliça, O sistema DEVE avisar, no painel dela e na conferência do modelo (REQ-010-21), que a barra de treliça não flamba entre os nós na segunda ordem nem na flambagem, e que, para isso, ela deve ser modelada como viga (pendência da 008c). | US-3, US-9 |

### Alvos

| ID | Requisito | História |
|----|-----------|----------|
| REQ-010-11 | Tudo o que é atribuído (barras, detalhes de viga, apoios e cargas) DEVE ser guardado no documento pelo alvo, não pelos números da malha, para sobreviver a uma nova malha. | US-5 |
| REQ-010-12 | Os alvos DEVEM ser: <br>• entidades do desenho, pelo ID estável (linhas e polilinhas para barras e cargas no vão; pontos, linhas e polilinhas para apoios e cargas nodais, isto é, os nós delas); <br>• grupos físicos de uma malha, pelo nome e pela dimensão; <br>• elementos e nós escolhidos na malha atual, pelos IDs externos. <br>A atribuição usa a seleção do viewport (desenho ou malha, conforme o alvo da seleção da 004/005). | US-5, US-6 |
| REQ-010-13 | QUANDO a malha mostrada for substituída (MESH ou importação), O sistema DEVE marcar como inválidos os alvos por elementos e nós da malha anterior. Eles ficam no documento, com um aviso na árvore e na conferência, até o usuário apagá-los ou reatribuí-los. Os alvos por entidade e por grupo continuam valendo. | US-5, US-6 |
| REQ-010-14 | QUANDO uma entidade com atribuições for apagada do desenho, O sistema DEVE manter as atribuições dela no documento e marcá-las como inválidas, porque o desfazer pode trazer a entidade de volta. A conferência DEVE listá-las. | US-5 |

### Apoios, casos de carga e combinações

| ID | Requisito | História |
|----|-----------|----------|
| REQ-010-15 | O sistema DEVE permitir pôr apoios por GDL (u, v, w, θx, θy, θz e empenamento), com atalhos para engaste, articulação e rolete. Um recalque (valor não nulo num GDL apoiado) DEVE pertencer a um caso de carga (REQ-010-16). | US-4 |
| REQ-010-16 | O sistema DEVE manter casos de carga com nome, um deles ativo. Cada caso DEVE aceitar todas as ações do `core`: <br>• forças, momentos e bimomentos nodais; <br>• cargas distribuídas (globais ou locais, uniformes ou trapezoidais, inteiras ou parciais, com torque e ponto de aplicação na seção); <br>• cargas concentradas no vão; <br>• peso próprio (vetor de aceleração); <br>• variação de temperatura e gradiente; <br>• deformação inicial; <br>• recalques. | US-4 |
| REQ-010-17 | O sistema DEVE manter combinações com nome, cada uma uma soma de casos multiplicados por fatores. Uma combinação DEVE poder ser usada onde um caso é usado: na montagem do REQ-010-20 ela vira um único conjunto de ações, a soma ponderada, válida também para a segunda ordem e a flambagem, que não superpõem resultados. | US-4 |
| REQ-010-18 | O sistema DEVE permitir criar, editar, copiar entre casos e apagar cargas e apoios, pela árvore e pelo painel. | US-4 |

### Interface

| ID | Requisito | História |
|----|-----------|----------|
| REQ-010-19 | A janela DEVE ter: <br>• um dock com a árvore do modelo (unidades, materiais, seções, atribuições, apoios, casos de carga com as suas cargas, combinações e o estado da malha); <br>• um dock de propriedades que edita o item selecionado na árvore, com validação de cada campo e a unidade dele; <br>• menus e comandos para criar itens e atribuí-los à seleção atual. <br>Selecionar um item da árvore DEVE realçar os alvos dele no viewport. | US-2, US-3, US-4 |
| REQ-010-20 | O sistema DEVE montar o `core::Model` de um caso ou de uma combinação sobre a malha atual e o mapa dela para a geometria (009). Os alvos por entidade viram os grupos `entity <id>`, os alvos por grupo e por seleção viram os da malha, e as ações viram as do caso ou a soma da combinação. Os problemas da montagem (alvo inválido, malha ausente ou desatualizada, entidade sem malha) DEVEM ser devolvidos como uma lista, cada um com o item do documento envolvido. | US-9, US-10 |
| REQ-010-21 | QUANDO o usuário pedir a conferência do modelo, para o caso ou a combinação ativa, O sistema DEVE listar num painel os problemas da montagem (REQ-010-20), os de `core::validate` e os avisos (REQ-010-10, 13, 14). Clicar num problema DEVE selecionar o item envolvido na árvore. | US-9 |
| REQ-010-22 | O sistema DEVE desenhar no viewport, pelo renderizador RHI: <br>• os apoios, com um símbolo por padrão de GDL apoiados (engaste, articulação, rolete, genérico); <br>• as cargas do caso ou da combinação ativa: setas para forças, arcos para momentos e setas distribuídas ao longo das barras. <br>Os símbolos têm tamanho constante na tela e podem ser ligados e desligados. | US-7 |
| REQ-010-23 | Toda mudança no documento DEVE ser um passo de desfazer e refazer, na mesma pilha do desenho. Atribuir um item a vários alvos de uma vez DEVE ser um passo só. | US-8 |
| REQ-010-24 | Os textos novos da interface DEVEM passar por `tr()`, em inglês. A tradução pt_BR fica para uma passada de traduções, como a dos textos da MESH (009) *(decidido pelo mantenedor, 2026-11-22)*. | US-1 |

### Core

| ID | Requisito | História |
|----|-----------|----------|
| REQ-010-25 | A resolução dos alvos por grupo no `core` (`resolve`, `Mesh::findGroup`) DEVE achar um grupo sem percorrer todos, porque um modelo com um grupo por entidade tem 10⁵ grupos (nota da T15 da 009). As análises existentes DEVEM continuar com os mesmos resultados. | US-10 |

## 4. Requisitos não funcionais

| ID | Requisito | Como medir |
|----|-----------|------------|
| NFR-010-01 | Num desenho com 100 mil entidades (o pórtico do `bench_line_mesh` da 009), Release, máquina do mantenedor: <br>• atribuir um item a todas as entidades selecionadas DEVE levar ≤ 1 s, e o desfazer, idem; <br>• montar o `core::Model` de uma combinação de três casos DEVE levar ≤ 2 s; <br>• a interação com o viewport, com os símbolos de 10 mil cargas e apoios, DEVE ficar ≥ 30 FPS (constituição §3.4). | Benchmark e medida manual |
| NFR-010-02 | A conversão de unidades DEVE ser exata dentro de 1e−12 relativo numa ida e volta entre quaisquer dois sistemas. | Testes automatizados |
| NFR-010-03 | O documento e a montagem DEVEM ter cobertura de linhas ≥ 80%, como o `core` (constituição §6.5, estendida). | Job de cobertura no CI |

## 5. Critérios de aceite

- [ ] **AC-1** (REQ-010-01, 02, 03, NFR-010-02) Unidades:
  - os sistemas pré-definidos coerentes (E·A/L numa unidade de rigidez de
    cada sistema);
  - a troca convertendo materiais, seções, cargas e recalques, com ida e
    volta em 1e−12;
  - o aviso sobre o desenho;
  - o desfazer da troca;
  - a unidade nos campos e na barra de status.
- [ ] **AC-2** (REQ-010-04, 05, 06) Materiais e seções:
  - a biblioteca nas unidades ativas;
  - as propriedades de cada tipo de seção iguais às do `core`;
  - o polígono maciço a partir de polilinhas do desenho;
  - a tabela de paredes finas;
  - o desenho da seção;
  - apagar um item em uso recusado, com os usos.
- [ ] **AC-3** (REQ-010-07, 08, 09, 10, 11, 12, 20) O pórtico 2×2 da T10 da
  009, montado pela interface sobre o desenho, deve dar o mesmo
  `core::Model` (e a mesma análise linear) que o do teste:
  - materiais, seções, vigas, apoios nos pontos e a carga;
  - os mesmos dados repetidos com ligações, excentricidades, base
    elástica e empenamento numa viga 3D;
  - o aviso de treliça;
  - os eixos locais no viewport.
- [ ] **AC-4** (REQ-010-13, 14, 11) Alvos que mudam:
  - editar o desenho e gerar a malha de novo mantém as atribuições por
    entidade;
  - uma malha nova invalida as atribuições por seleção, com o aviso;
  - apagar uma entidade invalida as dela, e desfazer o apagamento as
    devolve;
  - uma malha `.msh` importada aceita atribuições por grupo e por
    seleção.
- [ ] **AC-5** (REQ-010-15, 16, 17, 18) Casos e combinações:
  - um caso com cada tipo de ação do `core`, montado igual ao `core::Model`
    feito à mão;
  - uma combinação 1,4·G + 1,5·Q montada como a soma das ações, e a
    segunda ordem dela igual à segunda ordem do modelo com as ações já
    somadas;
  - recalques por caso;
  - copiar cargas entre casos.
- [ ] **AC-6** (REQ-010-19, 21, 22, 23, roteiro manual com os passos em
  `tasks.md`):
  - a árvore e o painel;
  - realçar os alvos;
  - a conferência com um modelo incompleto, clicando nos problemas;
  - os símbolos de apoios e cargas do caso ativo, ligar e desligar;
  - desfazer e refazer misturando desenho e modelo.
- [ ] **AC-7** (REQ-010-25, NFR-010-01, 03):
  - `resolve` sem busca linear, com as suítes da 007 a 009 passando;
  - os benchmarks de atribuição e de montagem dentro das metas;
  - os FPS medidos à mão;
  - a cobertura do documento ≥ 80%;
  - CI verde nas quatro plataformas.

## 6. Fora de escopo

- Execução das análises, progresso, cancelamento e resultados: 011 a 013.
- Salvar e abrir o projeto: 014. O documento é feito para ser salvo, mas
  aqui vive só na memória.
- Imperfeições iniciais (curvatura, cargas nocionais, modo de flambagem):
  continuam como pendência da 008c, para uma feature própria.
- Apoios elásticos nodais (molas nos nós): o `core` não os tem hoje; a
  008c os cita, mas não chegaram ao `Model`. Ficam como pendência.
- Superposição de resultados de casos (a combinação aqui monta as ações,
  REQ-010-17); envoltórias de combinações.
- Edição de propriedades da malha (refino) pela árvore: o MESH da 009
  continua sendo a forma de gerar a malha; os valores por entidade do
  `LineMesher` na interface ficam para uma feature de malha posterior.
- Cascas, sólidos e térmica: a árvore é feita para crescer, mas só tem o
  que o `core` oferece hoje.

## 7. Requisitos globais relacionados

- REQ-G-03: árvore do modelo, painel de propriedades e console.
- REQ-G-06: desfazer e refazer de todas as edições do modelo.
- REQ-G-12: atribuição a entidades geométricas, grupos e seleções.
- REQ-G-14: o que falta, apontado antes da análise (a conferência).
- Constituição §5.2: o sistema de unidades escolhido e sempre visível.

## 8. Questões em aberto

- [x] **Tradução pt_BR dos textos novos.** Ficam em inglês por `tr()`, e a
  tradução vai para uma passada de traduções, junto com a da MESH
  (decidido pelo mantenedor em 2026-11-22, REQ-010-24).
