# Requisitos — 004 mesh-rendering

Status: aprovado · Data: 2026-11-12

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
| REQ-004-19 | O sistema DEVE oferecer dois modos de seleção, elementos (padrão) e nós, escolhidos no menu Editar. | US-3 |
| REQ-004-20 | QUANDO o usuário clicar com o botão esquerdo sobre um elemento visível (modo elementos) ou perto de um nó visível (modo nós, até alguns pixels de distância), O sistema DEVE acrescentar à seleção o item mais próximo da câmera sob o cursor. | US-3 |
| REQ-004-21 | QUANDO o usuário arrastar com o botão esquerdo da esquerda para a direita (janela), O sistema DEVE acrescentar à seleção os itens das dimensões visíveis cuja projeção na tela esteja inteiramente dentro do retângulo, mesmo que escondidos por outros. | US-3 |
| REQ-004-22 | QUANDO o usuário arrastar com o botão esquerdo da direita para a esquerda (cruzamento), O sistema DEVE acrescentar à seleção os itens das dimensões visíveis cuja projeção na tela esteja dentro do retângulo ou o cruze, mesmo que escondidos por outros. | US-3 |
| REQ-004-23 | ENQUANTO o usuário arrasta, O sistema DEVE desenhar o retângulo: contínuo e azul para janela, tracejado e verde para cruzamento. | US-3 |
| REQ-004-24 | QUANDO a tecla Shift estiver pressionada durante clique, janela ou cruzamento, O sistema DEVE retirar os itens da seleção em vez de acrescentá-los. | US-3 |
| REQ-004-25 | QUANDO o usuário pressionar Esc ou escolher Editar ▸ Limpar seleção, O sistema DEVE esvaziar a seleção. | US-3 |
| REQ-004-26 | O sistema DEVE realçar os itens selecionados com uma cor própria, visível em qualquer modo de exibição: as faces e arestas visíveis dos elementos selecionados e os nós selecionados como pontos. | US-3 |
| REQ-004-27 | O sistema DEVE mostrar na barra de status quantos elementos ou nós estão selecionados. | US-3 |
| REQ-004-28 | QUANDO uma dimensão for ocultada, O sistema DEVE retirar da seleção os elementos dessa dimensão. | US-3 |

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
| NFR-004-03 | Uma seleção por clique DEVE responder em menos de 100 ms, e uma por janela ou cruzamento em menos de 500 ms, com a malha de 1 milhão de tetraedros. | Medição em build Release, na máquina do mantenedor |
| NFR-004-04 | Com a câmera parada, o viewport NÃO DEVE consumir CPU nem GPU de forma contínua, também com malha carregada. | Gerenciador de Tarefas em repouso |

## 5. Critérios de aceite

- [ ] **AC-1** (REQ-004-01, 02, 03, 05, 08) Importando pelo menu, arrastando o
  arquivo e pela linha de comando, a malha aparece enquadrada e a barra de
  status mostra nome, nós, elementos e grupos.
- [ ] **AC-2** (REQ-004-04, NFR-004-02) Durante a importação da malha de 1
  milhão de tetraedros a janela continua respondendo e o indicador aparece; a
  malha surge em menos de 3 s.
- [ ] **AC-3** (REQ-004-06, 07) Um arquivo com tipo não suportado mostra o
  aviso; um arquivo inválido mostra o erro com a linha e mantém a malha
  anterior.
- [ ] **AC-4** (REQ-004-09, 10, 30) Testes sem GPU: a casca externa de malhas
  conhecidas (um cubo de hexaedros, uma caixa de tetraedros, uma malha mista
  com cunhas e pirâmides) tem o número esperado de faces e arestas, e nenhuma
  face interna.
- [ ] **AC-5** (REQ-004-09 a 13) Roteiro manual com os arquivos de exemplo da
  feature 003: pontos, linhas, faces e casca externa aparecem; os três modos
  funcionam; as arestas não cintilam ao orbitar.
- [ ] **AC-6** (REQ-004-14, 15, 16) Cores por dimensão e por grupo e
  visibilidade por dimensão funcionam pelo menu, conferidas no roteiro manual;
  a regra de cor de elemento em vários grupos tem teste automático.
- [ ] **AC-7** (REQ-004-17, 18) "Enquadrar tudo" mostra a malha inteira; uma
  malha deslocada 1e6 unidades da origem não treme ao orbitar e aproximar.
- [ ] **AC-8** (REQ-004-19 a 25) Roteiro manual: clique, janela, cruzamento,
  Shift e Esc se comportam como descrito, nos dois modos; testes automáticos
  conferem os critérios de janela e cruzamento sobre projeções conhecidas.
- [ ] **AC-9** (REQ-004-26, 27, 28) A seleção aparece realçada nos três modos
  de exibição, a contagem aparece na barra de status e ocultar uma dimensão
  retira seus elementos da seleção.
- [ ] **AC-10** (REQ-004-29) Com o sistema em português, os textos novos
  aparecem em português.
- [ ] **AC-11** (REQ-004-31) O smoke test com malha passa nos quatro jobs do CI.
- [ ] **AC-12** (NFR-004-01, 03, 04) Medições registradas em `tasks.md`, dentro
  das metas, com o modelo da GPU.

## 6. Fora de escopo

- Seleção de faces e arestas da casca externa (para cargas e apoios) — feature
  010.
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
- [x] **GPU da medição:** a máquina do mantenedor tem uma NVIDIA GeForce RTX
  2060 (consultado no sistema em 2026-11-12), uma GPU dedicada de gama média,
  que é o caso do REQ-G-20.
