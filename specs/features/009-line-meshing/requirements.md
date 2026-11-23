# Requisitos — 009 line-meshing

Status: aprovado · Data: 2026-11-22

## 1. Contexto e objetivo

A 005 desenha pontos, linhas e polilinhas, e a 007 a 008d analisam treliças
e vigas sobre uma `core::Mesh`. Hoje uma malha só chega ao `core` importada
de um `.msh` ou montada à mão nos testes: o desenho não vira modelo.

Esta feature dá o primeiro gerador do módulo `mesher`. Ele divide as linhas
e polilinhas do desenho em elementos de barra (`Line2` ou `Line3`) e:

- funde os nós coincidentes;
- divide as linhas onde outras terminam (nós em T) ou as cruzam, e onde há
  pontos sobre elas, para que as barras se liguem ali;
- liga cada elemento e cada nó à entidade de origem, para que as
  atribuições da 010 fiquem nas entidades e sobrevivam a uma nova malha.

O comando **MESH** gera a malha do desenho inteiro pela linha de comando e
mostra a malha no viewport.

## 2. Histórias de usuário

- **US-1** Como engenheiro, quero desenhar um pórtico com linhas e obter a
  malha de barras pronta para a análise, com os nós das ligações fundidos e
  sem montar nós e elementos à mão.
- **US-2** Como engenheiro, quero que as barras se liguem onde uma linha
  termina no meio de outra ou a cruza, e onde marquei um ponto sobre ela,
  sem ter que quebrar as linhas no desenho.
- **US-3** Como engenheiro, quero controlar a divisão de cada entidade (um
  elemento por trecho, um número de divisões, um comprimento alvo,
  graduação para uma ponta ou para as duas) e a ordem dos elementos
  (lineares ou quadráticos).
- **US-4** Como engenheiro, quero digitar MESH e ver a malha no viewport,
  com a contagem de nós e elementos e o que foi dividido ou fundido.
- **US-5** Como desenvolvedor da 010 a 014, quero saber de que entidade veio
  cada elemento e cada nó (grupos por entidade e um mapa), para atribuir
  materiais, seções, apoios e cargas às entidades.

## 3. Requisitos funcionais (EARS)

### Geração

| ID | Requisito | História |
|----|-----------|----------|
| REQ-009-01 | O sistema DEVE gerar uma `core::Mesh` a partir de um `geom::Drawing`, com as linhas e as polilinhas (abertas e fechadas) divididas em elementos de barra e os pontos como nós. Também DEVE ser possível gerar a partir de um subconjunto de entidades, dado por IDs. | US-1 |
| REQ-009-02 | Todo vértice de polilinha, toda ponta de linha e todo ponto DEVE ser um nó da malha. Dois nós a uma distância menor que a tolerância do desenho (`Drawing::tolerance()`) DEVEM ser um nó só. | US-1 |
| REQ-009-03 | QUANDO a ponta de uma linha ou um vértice cair sobre o interior de um trecho de outra entidade (nó em T), ou QUANDO dois trechos se cruzarem num ponto interior de ambos, ou QUANDO um ponto cair sobre o interior de um trecho, O sistema DEVE dividir os trechos ali e pôr um nó comum. Tudo isso vale em 3D, com a tolerância do desenho para "sobre". | US-2 |
| REQ-009-04 | SE dois trechos forem colineares com um pedaço comum de comprimento maior que a tolerância (barras sobrepostas), ENTÃO O sistema DEVE recusar a malha, listando cada par de entidades e o pedaço comum. Trechos de comprimento menor que a tolerância DEVEM ser ignorados e listados no relatório. | US-1 |
| REQ-009-05 | As divisões DEVEM ser feitas por trecho, isto é, o pedaço de uma entidade entre dois nós obrigatórios (vértices, pontas e as divisões do REQ-009-03). O padrão é um elemento por trecho, que basta para as vigas exatas da 008b a 008d. | US-1, US-3 |
| REQ-009-06 | Para cada entidade, o sistema DEVE aceitar: um número de divisões por trecho; ou um comprimento alvo (o trecho de comprimento L recebe n = max(1, ⌈L/h − tol⌉) divisões); e, em qualquer caso, uma graduação geométrica. A graduação é dada pela razão entre o último e o primeiro elemento, em direção à ponta final da entidade ou em direção às duas pontas (simétrica). Sem a graduação, a divisão é uniforme. Também DEVE haver valores padrão para todo o desenho, que valem nas entidades sem valores próprios. | US-3 |
| REQ-009-07 | Para cada entidade, e com um padrão para o desenho, o sistema DEVE gerar `Line2` (o padrão) ou `Line3`, com o nó do meio no ponto médio do elemento sobre o trecho. | US-3 |
| REQ-009-08 | A malha DEVE ser determinística: o mesmo desenho com os mesmos valores dá os mesmos nós, elementos, IDs externos e grupos, independentemente do número de threads. Os IDs externos dos nós e dos elementos são numerados a partir de 1, na ordem das entidades no desenho e ao longo de cada uma. | US-1, US-5 |

### Ligação com a geometria

| ID | Requisito | História |
|----|-----------|----------|
| REQ-009-09 | Cada entidade DEVE virar um grupo da malha com um nome estável, derivado do seu ID. Linhas e polilinhas viram grupos de dimensão 1, com os seus elementos. Pontos viram grupos de dimensão 0, com um elemento de ponto. Assim, os alvos por grupo do `Model` (`GroupRef`) atingem as entidades. | US-5 |
| REQ-009-10 | O resultado DEVE trazer, além da malha: para cada elemento, a entidade e o índice do trecho de origem; para cada nó, as entidades que passam por ele e, quando houver, o vértice ou o ponto de origem. | US-5 |
| REQ-009-11 | O resultado DEVE trazer um relatório: o número de nós e de elementos; as divisões feitas pelo REQ-009-03, com o tipo e as entidades; os nós fundidos; os trechos ignorados. | US-4 |

### Comando MESH

| ID | Requisito | História |
|----|-----------|----------|
| REQ-009-12 | QUANDO o usuário digitar `MESH` (atalho `ME`) na linha de comando, O sistema DEVE pedir o tamanho dos elementos ou Enter para o padrão (um elemento por trecho, `Line2`), e então gerar a malha do desenho inteiro com esse comprimento alvo como padrão global. | US-4 |
| REQ-009-13 | ENQUANTO a malha estiver sendo gerada, O sistema DEVE manter a interface responsiva (fora da thread da interface), como na importação de `.msh` da 004. Ao terminar, DEVE mostrar a malha no viewport junto com o desenho e escrever no console o relatório do REQ-009-11. | US-4 |
| REQ-009-14 | SE a geração falhar (REQ-009-04, desenho sem linhas), ENTÃO O sistema DEVE escrever no console o motivo, com as entidades envolvidas, e manter a malha anterior inalterada (REQ-G-10). | US-4 |
| REQ-009-15 | O comando MESH não DEVE alterar o desenho: não entra no desfazer. QUANDO o desenho for editado depois de uma malha, O sistema DEVE avisar uma vez no console que a malha mostrada está desatualizada. A malha continua visível até o próximo MESH. | US-4 |
| REQ-009-16 | Os textos do comando (nome, pedido, mensagens e relatório) DEVEM passar por `tr()`, prontos para a tradução. Nesta feature eles ficam só em inglês: a tradução pt_BR fica para depois *(ajustado na implementação, 2026-11-22, decidido pelo mantenedor)*. | US-4 |

### Arquitetura

| ID | Requisito | História |
|----|-----------|----------|
| REQ-009-17 | O gerador DEVE ficar em `mesher` (`LineMesher`), sem Qt, dependendo só de `core` e `geom`, como no grafo da arquitetura. O comando MESH fica no `app`: o `CommandSession` de `geom` não pode depender de `mesher`. | US-5 |

## 4. Requisitos não funcionais

| ID | Requisito | Como medir |
|----|-----------|------------|
| NFR-009-01 | Num desenho de 100 mil trechos (o caso do NFR-005-02), com nós em T e cruzamentos, a geração DEVE levar no máximo 2 s em Release na máquina do mantenedor. A busca de interseções DEVE usar um índice espacial (sem pares por força bruta). | Benchmark `bench_line_mesh` |
| NFR-009-02 | As interseções e o "sobre" DEVEM ser robustos: o mesmo resultado com o desenho transladado para longe da origem (coordenadas da ordem de 1e6) e escalado, com a tolerância relativa do desenho. | Testes automatizados |
| NFR-009-03 | A cobertura de linhas de `mesher` DEVE ser de pelo menos 80%, como a de `core` (constituição §6.5, estendida). | Job de cobertura no CI |

## 5. Critérios de aceite

- [ ] **AC-1** (REQ-009-01, 02, 05, 08) Pórtico plano de dois vãos e dois
  andares desenhado com linhas que se tocam nas pontas:
  - um elemento por trecho e os nós das ligações fundidos;
  - as contagens e os IDs esperados;
  - o mesmo resultado com 1 thread e com todas;
  - analisado pela `runLinearStatic` com as atribuições por grupo,
    concordando com o mesmo pórtico montado à mão.
- [ ] **AC-2** (REQ-009-03, 11) Ligações:
  - viga que termina no meio de um pilar (T);
  - duas diagonais de contraventamento que se cruzam;
  - uma linha que cruza uma polilinha num vértice;
  - um ponto sobre uma viga;
  - um cruzamento em 3D e duas retas reversas, que não se ligam;
  - cada divisão com o nó comum e listada no relatório.
- [ ] **AC-3** (REQ-009-04, 14) Falhas:
  - duas linhas sobrepostas recusadas, com as entidades e o pedaço;
  - trecho de comprimento nulo ignorado e listado;
  - desenho sem linhas recusado.
- [ ] **AC-4** (REQ-009-06, 07) Divisão:
  - número de divisões;
  - comprimento alvo, com o arredondamento do REQ-009-06 e os casos L/h
    inteiro e quase inteiro;
  - graduação para a ponta final e simétrica, com as razões medidas e a
    soma dos comprimentos igual ao trecho em 1e−12 relativo;
  - valores por entidade sobrepondo os padrões;
  - `Line3` com o nó do meio no ponto médio.
- [ ] **AC-5** (REQ-009-09, 10) Ligação com a geometria:
  - um grupo por entidade, com o nome estável;
  - o mapa de elementos e nós para as entidades, os trechos e os vértices;
  - depois de mover uma entidade e gerar de novo, o grupo dela tem o mesmo
    nome.
- [ ] **AC-6** (REQ-009-12 a 16, roteiro manual na interface, com os
  passos em `tasks.md`):
  - MESH com Enter e com um tamanho;
  - a malha no viewport e o relatório no console;
  - uma falha que mantém a malha anterior;
  - o aviso de malha desatualizada depois de uma edição;
  - o desfazer não afetado;
  - os textos em inglês, por `tr()` (a tradução pt_BR ficou para depois).
- [ ] **AC-7** (NFR-009-01, 02, 03, REQ-009-17):
  - o benchmark de 100 mil trechos dentro da meta;
  - a robustez com translação e escala;
  - `mesher` e `geom` sem Qt;
  - CI verde nas quatro plataformas, com a cobertura de `mesher` ≥ 80%.

## 6. Fora de escopo

- Curvas (arco, círculo, elipse, spline): chegam na 016 e serão divididas
  pelo `LineMesher` então, pela parametrização do OCCT (015).
- Malha de superfícies e sólidos (021 em diante).
- A interface para os valores por entidade (divisões, graduação, ordem) e
  as atribuições às entidades: 010. Aqui, os valores por entidade existem
  na API, e o MESH usa só o padrão global.
- Salvar a malha e os valores no projeto (014).
- Refinamento automático por curvatura ou por resultado.

## 7. Requisitos globais relacionados

- REQ-G-05: interface responsiva durante a geração.
- REQ-G-08: elementos conformes respeitando os tamanhos.
- REQ-G-10: falha informa a região e mantém a malha anterior.
- REQ-G-12: atribuição às entidades, preparada pelos grupos e pelo mapa.

## 8. Questões em aberto

Nenhuma.
