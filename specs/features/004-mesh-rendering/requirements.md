# Requisitos — 004 mesh-rendering

Status: aprovado (ampliação: faces, arestas e nós aprovada em 2026-11-12) · Data: 2026-11-12

## 1. Contexto e objetivo

O femapp já tem um viewport 3D (feature 002) e sabe ler malhas do Gmsh
(feature 003), mas as duas coisas ainda não se encontram. Esta feature leva a
malha para a tela: importação pela interface, desenho de pontos, linhas,
faces e da casca externa dos sólidos, modos de exibição, cores por grupo e a
seleção de elementos e nós com o mouse, no estilo do AutoCAD. É a base visual
sobre a qual a feature 010 vai atribuir materiais, apoios e cargas, e a 012 vai
mostrar resultados.

## 2. Histórias de usuário

- **US-1** Como engenheiro, quero abrir uma malha do Gmsh pelo menu ou
  arrastando o arquivo para a janela, para vê-la sem sair do femapp.
- **US-2** Como engenheiro, quero ver a malha com faces sombreadas e arestas,
  ou só o aramado, e distinguir os grupos físicos por cor, para entender o
  modelo.
- **US-3** Como engenheiro, quero selecionar elementos e nós com clique, janela
  e cruzamento, como no AutoCAD, para depois atribuir propriedades a eles.
- **US-4** Como engenheiro, quero navegar com fluidez em malhas de até 1 milhão
  de elementos, para trabalhar com modelos reais.
- **US-5** Como engenheiro, quero selecionar faces e arestas da superfície
  dos sólidos e ver os nós da malha, para depois aplicar cargas e apoios nelas
  e conferir a discretização. *(Acrescentada em 2026-11-12, a pedido do
  mantenedor.)*

## 3. Requisitos funcionais (EARS)

### Importação

| ID | Requisito | História |
|----|-----------|----------|
| REQ-004-01 | QUANDO o usuário escolher Arquivo ▸ Importar malha, O sistema DEVE abrir um diálogo de arquivo filtrado por `.msh` e importar o arquivo escolhido. | US-1 |
| REQ-004-02 | QUANDO o usuário soltar um arquivo `.msh` sobre a janela, O sistema DEVE importá-lo. | US-1 |
| REQ-004-03 | QUANDO o aplicativo for iniciado com o caminho de um arquivo `.msh` na linha de comando, O sistema DEVE importá-lo ao abrir. | US-1 |
| REQ-004-04 | ENQUANTO uma importação estiver em andamento, O sistema DEVE manter a interface responsiva, mostrar um indicador de progresso na barra de status e impedir outra importação. | US-1 |
| REQ-004-05 | QUANDO a importação terminar com sucesso, O sistema DEVE substituir a malha exibida pela nova, limpar a seleção e enquadrar a malha inteira. | US-1 |
| REQ-004-06 | QUANDO a leitura produzir avisos (por exemplo, tipos de elemento ignorados), O sistema DEVE mostrá-los ao usuário depois de exibir a malha. | US-1 |
| REQ-004-07 | SE a leitura falhar, ENTÃO O sistema DEVE mostrar a mensagem de erro do leitor e manter inalterada a malha exibida. | US-1 |
| REQ-004-08 | O sistema DEVE mostrar na barra de status o nome do arquivo e o número de nós, elementos e grupos da malha exibida. | US-1 |

### Exibição

| ID | Requisito | História |
|----|-----------|----------|
| REQ-004-09 | O sistema DEVE desenhar elementos 0D como pontos de tamanho fixo na tela, 1D como linhas, 2D como faces e, dos elementos 3D, apenas as faces da casca externa (faces que pertencem a um único elemento 3D). | US-2 |
| REQ-004-10 | O sistema DEVE desenhar elementos de ordem maior que 1 pelos seus vértices, com arestas retas e faces planas. | US-2 |
| REQ-004-11 | O sistema DEVE oferecer, no menu Exibir, três modos mutuamente exclusivos: sólido com arestas (padrão), sólido e aramado. | US-2 |
| REQ-004-12 | O sistema DEVE sombrear as faces pela orientação em relação à câmera, iluminando os dois lados das faces de elementos 2D. | US-2 |
| REQ-004-13 | O sistema DEVE desenhar as arestas dos elementos visíveis sem que fiquem escondidas ou cintilando por disputa de profundidade com as faces. | US-2 |
| REQ-004-14 | O sistema DEVE oferecer, no menu Exibir, colorir por dimensão do elemento (padrão) ou por grupo; por grupo, cada grupo tem uma cor distinta e os elementos sem grupo, uma cor neutra. | US-2 |
| REQ-004-15 | QUANDO um elemento pertencer a mais de um grupo e a cor for por grupo, O sistema DEVE usar a cor do primeiro desses grupos na ordem da malha. | US-2 |
| REQ-004-16 | O sistema DEVE permitir mostrar e ocultar, no menu Exibir, os elementos de cada dimensão (0D, 1D, 2D e 3D). | US-2 |
| REQ-004-17 | QUANDO houver malha, "Enquadrar tudo" DEVE enquadrar a caixa envolvente da malha. | US-2 |
| REQ-004-18 | O sistema DEVE exibir sem tremor malhas com coordenadas grandes (por exemplo, 1e6 unidades da origem). | US-2, US-4 |

### Seleção

| ID | Requisito | História |
|----|-----------|----------|
| REQ-004-19 | O sistema DEVE oferecer, no menu Editar, quatro tipos de seleção que podem ser ligados ao mesmo tempo: elementos (o único ligado por padrão), faces de sólidos, arestas (de sólidos e de elementos 2D) e nós; pelo menos um tipo fica sempre ligado. *(Alterado em 2026-11-12, a pedido do mantenedor: antes eram dois modos exclusivos; depois os quatro ligados por padrão; por fim só elementos.)* | US-3, US-5 |
| REQ-004-20 | QUANDO o usuário clicar com o botão esquerdo, O sistema DEVE acrescentar à seleção um único item, de um tipo ligado, nesta ordem: o nó visível até alguns pixels do cursor; senão, a aresta visível até alguns pixels; senão, a face de sólido visível mais próxima da câmera sob o cursor; senão, o elemento visível mais próximo (linhas e pontos até alguns pixels). Assim, ligar um tipo no menu já o torna alcançável pelo clique. *(Alterado em 2026-11-12, a pedido do mantenedor; a ordem elemento → face → aresta → nó, usada por um tempo, foi revertida.)* | US-3, US-5 |
| REQ-004-21 | QUANDO o usuário arrastar com o botão esquerdo da esquerda para a direita (janela), O sistema DEVE acrescentar à seleção os itens de todos os tipos ligados, das dimensões visíveis, cuja projeção na tela esteja inteiramente dentro do retângulo, mesmo que escondidos por outros. *(Alterado em 2026-11-12: tipos ligados.)* | US-3, US-5 |
| REQ-004-22 | QUANDO o usuário arrastar com o botão esquerdo da direita para a esquerda (cruzamento), O sistema DEVE acrescentar à seleção os itens de todos os tipos ligados, das dimensões visíveis, cuja projeção na tela esteja dentro do retângulo ou o cruze, mesmo que escondidos por outros. *(Alterado em 2026-11-12: tipos ligados.)* | US-3, US-5 |
| REQ-004-23 | ENQUANTO o usuário arrasta, O sistema DEVE desenhar o retângulo: contínuo e azul para janela, tracejado e verde para cruzamento. | US-3 |
| REQ-004-24 | QUANDO a tecla Shift estiver pressionada durante clique, janela ou cruzamento, O sistema DEVE retirar os itens da seleção em vez de acrescentá-los. | US-3 |
| REQ-004-25 | QUANDO o usuário pressionar Esc, escolher Editar ▸ Limpar seleção ou clicar sem Shift num ponto onde nenhum item dos tipos ligados seria escolhido (fora da malha), O sistema DEVE esvaziar a seleção. *(Alterado em 2026-11-12, a pedido do mantenedor: clique fora da malha.)* | US-3 |
| REQ-004-26 | O sistema DEVE realçar os itens selecionados com uma cor própria, visível em qualquer modo de exibição: as faces visíveis dos elementos selecionados e suas arestas visíveis (inclusive as compartilhadas com vizinhos, mas não as escondidas atrás de faces) como linhas mais grossas, as faces de sólido selecionadas, as arestas selecionadas como linhas mais grossas e os nós selecionados como pontos; arestas e nós selecionados que estão escondidos atrás de faces não são realçados. *(Alterado em 2026-11-12; arestas dos elementos selecionados e só o que é visível a pedido do mantenedor.)* | US-3, US-5 |
| REQ-004-27 | O sistema DEVE mostrar na barra de status quantos itens de cada tipo estão selecionados, omitindo os tipos sem itens. *(Alterado em 2026-11-12.)* | US-3, US-5 |
| REQ-004-28 | QUANDO uma dimensão for ocultada, O sistema DEVE retirar da seleção os elementos dessa dimensão e, se for a 2D, as arestas de elementos 2D; se for a 3D, as faces e arestas de sólido. *(Alterado em 2026-11-12.)* | US-3, US-5 |
| REQ-004-32 | O sistema DEVE considerar como faces de sólido as faces da casca externa (REQ-004-09) e como arestas de sólido as arestas dessas faces; como arestas de elementos 2D, as arestas das faces dos elementos 2D. Cada aresta é um único item dentro do seu grupo (sólido ou 2D), mesmo sendo comum a duas faces. Faces e arestas internas dos sólidos não são selecionáveis. *(Acrescentado em 2026-11-12; arestas 2D a pedido do mantenedor.)* | US-5 |
| REQ-004-33 | QUANDO o usuário desligar um tipo de seleção, O sistema DEVE retirar da seleção os itens desse tipo. *(Acrescentado em 2026-11-12.)* | US-5 |
| REQ-004-34 | O sistema DEVE oferecer, no menu Exibir, a opção "Mostrar nós" (ligada por padrão), que desenha como pontos pretos de tamanho fixo todos os nós dos elementos das dimensões visíveis, inclusive os intermediários dos elementos de ordem alta (dos sólidos, só os que ficam sobre a casca externa); esses nós também podem ser escolhidos por clique. *(Acrescentado em 2026-11-12; ligada por padrão, em preto e com os nós intermediários a pedido do mantenedor.)* | US-5 |
| REQ-004-35 | ENQUANTO "Mostrar nós" estiver ligada, nos modos sólido e sólido com arestas, O sistema NÃO DEVE desenhar os nós escondidos atrás de faces; no aramado, DEVE desenhar todos. *(Acrescentado em 2026-11-12.)* | US-5 |
| REQ-004-36 | QUANDO for importada uma malha sem elementos 3D contida num plano coordenado, O sistema DEVE mostrar a vista padrão perpendicular a esse plano (Superior para XY, Frontal para XZ, Direita para YZ) antes de enquadrá-la; qualquer outra malha importada DEVE abrir na vista isométrica. *(Acrescentado em 2026-11-12, a pedido do mantenedor; a vista isométrica para as demais corrige uma malha 3D aberta depois de uma plana, que herdava a vista do plano.)* | US-2 |
| REQ-004-37 | O sistema DEVE oferecer, no menu Exibir, a opção "Mostrar só a seleção" (desligada por padrão), que mostra só os elementos selecionados, como uma peça isolada (com a casca do conjunto, inclusive faces que eram internas), nas cores normais e sem realce, com as arestas e os nós visíveis da peça sempre desenhados. *(Acrescentado em 2026-11-12, a pedido do mantenedor; depois sem realce e com arestas e nós sempre visíveis.)* | US-3 |
| REQ-004-38 | ENQUANTO "Mostrar só a seleção" estiver ligada, O sistema DEVE apenas retirar itens: clique, janela e cruzamento com Shift retiram (o clique, só elementos e nós da peça visível); sem Shift não fazem nada. *(Acrescentado em 2026-11-12.)* | US-3 |
| REQ-004-39 | QUANDO não restar elemento selecionado (Esc, Limpar seleção ou retirando o último), O sistema DEVE desligar "Mostrar só a seleção" e voltar a mostrar a malha inteira; a opção não liga sem elemento selecionado. *(Acrescentado em 2026-11-12.)* | US-3 |

### Interface, camadas e testes

| ID | Requisito | História |
|----|-----------|----------|
| REQ-004-29 | O sistema DEVE traduzir para pt_BR todos os textos novos da interface. | US-1 |
| REQ-004-30 | O sistema DEVE manter em `core` (sem Qt nem GPU) a extração da casca externa e das arestas, e testá-la sem GPU; a lógica de seleção que não depende da GPU também DEVE ter testes automáticos. | US-4 |
| REQ-004-31 | QUANDO executado com `--smoke-test` e um arquivo `.msh` na linha de comando, O sistema DEVE importar a malha, renderizar ao menos um quadro com ela e encerrar com código 0; esse teste DEVE rodar nos quatro jobs do CI. | US-1 |

## 4. Requisitos não funcionais

| ID | Requisito | Como medir |
|----|-----------|------------|
| NFR-004-01 | A navegação DEVE manter ≥ 30 quadros por segundo com a malha de 1 milhão de tetraedros da feature 003, no modo sólido com arestas, em janela 1920×1080 (REQ-G-20). | Contador de quadros em build Release, na máquina do mantenedor; registrar o modelo da GPU |
| NFR-004-02 | Do fim do diálogo de arquivo até a malha de 1 milhão de tetraedros aparecer na tela DEVE levar menos de 3 s. | Medição em build Release, na máquina do mantenedor |
| NFR-004-03 | Uma seleção por clique DEVE responder em menos de 100 ms, e uma por janela ou cruzamento em menos de 500 ms, com a malha de 1 milhão de tetraedros, com os quatro tipos de seleção ligados. | Medição em build Release, na máquina do mantenedor |
| NFR-004-04 | Com a câmera parada, o viewport NÃO DEVE consumir CPU nem GPU de forma contínua, também com malha carregada. | Gerenciador de Tarefas em repouso |

## 5. Critérios de aceite

- [x] **AC-1** (REQ-004-01, 02, 03, 05, 08) Importando pelo menu, arrastando o
  arquivo e pela linha de comando, a malha aparece enquadrada e a barra de
  status mostra nome, nós, elementos e grupos.
- [x] **AC-2** (REQ-004-04, NFR-004-02) Durante a importação da malha de 1
  milhão de tetraedros a janela continua respondendo e o indicador aparece; a
  malha surge em menos de 3 s.
- [x] **AC-3** (REQ-004-06, 07) Um arquivo com tipo não suportado mostra o
  aviso; um arquivo inválido mostra o erro com a linha e mantém a malha
  anterior.
- [x] **AC-4** (REQ-004-09, 10, 30) Testes sem GPU: a casca externa de malhas
  conhecidas (um cubo de hexaedros, uma caixa de tetraedros, uma malha mista
  com cunhas e pirâmides) tem o número esperado de faces e arestas, e nenhuma
  face interna.
- [x] **AC-5** (REQ-004-09 a 13) Roteiro manual com os arquivos de exemplo da
  feature 003: pontos, linhas, faces e casca externa aparecem; os três modos
  funcionam; as arestas não cintilam ao orbitar.
- [x] **AC-6** (REQ-004-14, 15, 16) Cores por dimensão e por grupo e
  visibilidade por dimensão funcionam pelo menu, conferidas no roteiro manual;
  a regra de cor de elemento em vários grupos tem teste automático.
- [x] **AC-7** (REQ-004-17, 18) "Enquadrar tudo" mostra a malha inteira; uma
  malha deslocada 1e6 unidades da origem não treme ao orbitar e aproximar.
- [x] **AC-8** (REQ-004-19 a 25) Roteiro manual: clique, janela, cruzamento,
  Shift e Esc se comportam como descrito, nos dois modos; testes automáticos
  conferem os critérios de janela e cruzamento sobre projeções conhecidas.
- [x] **AC-9** (REQ-004-26, 27, 28) A seleção aparece realçada nos três modos
  de exibição, a contagem aparece na barra de status e ocultar uma dimensão
  retira seus elementos da seleção.
- [x] **AC-10** (REQ-004-29) Com o sistema em português, os textos novos
  aparecem em português.
- [ ] **AC-11** (REQ-004-31) O smoke test com malha passa nos quatro jobs do CI.
- [x] **AC-12** (NFR-004-01, 03, 04) Medições registradas em `tasks.md`, dentro
  das metas, com o modelo da GPU.
- [x] **AC-13** (REQ-004-19, 20, 32, 33) Testes automáticos num bloco de
  hexaedros: com faces ligadas, o clique no meio de uma face da casca seleciona
  essa face; com arestas e nós ligados, o clique perto de um vértice pega o nó
  e perto do meio de uma aresta pega a aresta; uma aresta comum a duas faces
  conta uma vez; desligar um tipo retira seus itens.
- [x] **AC-14** (REQ-004-21, 22, 26, 27, 28) Roteiro manual com faces, arestas
  e nós ligados juntos: janela e cruzamento pegam os três tipos, o realce de
  cada tipo aparece nos três modos de exibição e a barra de status mostra a
  contagem por tipo.
- [x] **AC-15** (REQ-004-34, 35) Roteiro manual: "Mostrar nós" desenha os nós
  das dimensões visíveis, esconde os de trás no modo sólido e mostra todos no
  aramado; os nós selecionados continuam com a cor de seleção.

## 6. Fora de escopo

- Selecionar faces e arestas internas dos sólidos (um elemento 2D já é
  selecionável como elemento; suas arestas, como arestas).
- Atribuir cargas e apoios às faces e arestas selecionadas — feature 010.
- Numeração (IDs) de nós ou elementos na tela.
- Mostrar e ocultar grupos individualmente e árvore do modelo — feature 010.
- Cores de resultados, legenda e deformada — feature 012.
- Curvatura de elementos de ordem alta — feature 029.
- Seção extrudada de barras e espessura de cascas — feature 013.
- Planos de corte, vistas em corte, transparência, elementos encolhidos e
  realce ao passar o mouse.
- Mais de uma malha ao mesmo tempo, salvar a malha ou a seleção, desfazer a
  seleção.
- Cancelar uma importação em andamento (o leitor da 003 não é interrompível;
  1 milhão de elementos leva menos de 1 s).

## 7. Requisitos globais relacionados

REQ-G-03 (viewport na janela principal), REQ-G-05 (interface responsiva
durante operações longas, aqui só a importação, sem cancelamento), REQ-G-11
(importação de `.msh` na interface), REQ-G-12 (seleção no viewport, base para
atribuições), REQ-G-20 (≥ 30 FPS com 1 milhão de elementos).

## 8. Questões em aberto

- [x] **Seleção:** decidido em 2026-11-12 — elementos e nós, com clique,
  janela, cruzamento, Shift para retirar e Esc para limpar.
- [x] **Exibição:** decidido em 2026-11-12 — três modos, cor por dimensão ou
  por grupo, visibilidade por dimensão.
- [x] **Importação:** decidido em 2026-11-12 — menu, arrastar arquivo e linha
  de comando, com leitura em segundo plano.
- [x] **Desempenho:** decidido em 2026-11-12 — a meta do produto, ≥ 30 FPS com
  1 milhão de elementos.
- [x] **Faces, arestas e nós (ampliação):** decidido em 2026-11-12 — ampliar
  a 004; só faces e arestas da casca externa; tipos de seleção combináveis;
  "Mostrar nós" liga e desliga os pontos de todos os nós, sem numeração.
- [x] **Prioridade do clique (REQ-004-20):** decidido em 2026-11-12 — nó →
  aresta → face → elemento, com só elementos ligado por padrão; ligar faces,
  arestas ou nós no menu os torna clicáveis (a ordem elemento → face → aresta
  → nó, testada antes, deixava esses tipos inalcançáveis pelo clique).
- [x] **GPU da medição:** a máquina do mantenedor tem uma NVIDIA GeForce RTX
  2060 (consultado no sistema em 2026-11-12), uma GPU dedicada de gama média,
  que é o caso do REQ-G-20.
