# Requisitos — 005 drafting-basics

Status: aprovado · Data: 2026-11-12

## 1. Contexto e objetivo

A versão 0.2 analisa pórticos: o engenheiro desenha as barras, atribui seções e
cargas e resolve. Esta feature cria a base do módulo de desenho no estilo
AutoCAD: pontos, linhas e polilinhas em 2D e 3D, a linha de comando com
coordenadas exatas, snaps a objetos, encaixe na grade e modo ortogonal, e os
comandos de edição mover, copiar e apagar, com desfazer e refazer. A geometria
fica em `geom`, sem Qt, para ser testada sem interface; as features 009 (malha
de linhas) e 010 (atribuições) vão consumi-la. O núcleo OpenCASCADE só entra
na feature 015.

## 2. Histórias de usuário

- **US-1** Como engenheiro, quero desenhar pontos, linhas e polilinhas com o
  mouse e pela linha de comando, para montar o eixo de um pórtico.
- **US-2** Como engenheiro, quero digitar coordenadas absolutas, relativas e
  polares, para desenhar com medidas exatas.
- **US-3** Como engenheiro, quero que o cursor se prenda a extremidades, meios,
  interseções, à grade e às direções ortogonais, para ligar barras sem erro.
- **US-4** Como engenheiro, quero mover, copiar e apagar o que desenhei e
  desfazer ou refazer cada passo, para corrigir o modelo com segurança.
- **US-5** Como engenheiro, quero ver o desenho junto da malha importada, sem
  que uma seleção atrapalhe a outra.

## 3. Requisitos funcionais (EARS)

### Entidades e documento (`geom`)

| ID | Requisito | História |
|----|-----------|----------|
| REQ-005-01 | O sistema DEVE manter um documento de desenho com entidades do tipo ponto, linha (segmento entre dois pontos) e polilinha (sequência de dois ou mais vértices, aberta ou fechada, só com trechos retos), todas com coordenadas 3D em `double`. | US-1 |
| REQ-005-02 | O sistema DEVE dar a cada entidade um ID estável, único no documento e nunca reutilizado, que não muda quando a entidade é movida. | US-1, US-4 |
| REQ-005-03 | O sistema DEVE manter o documento, as entidades, os comandos, a interpretação da linha de comando e os snaps no módulo `geom`, sem Qt nem GPU, cobertos por testes automáticos. | US-1 |

### Linha de comando

| ID | Requisito | História |
|----|-----------|----------|
| REQ-005-04 | O sistema DEVE oferecer uma linha de comando acoplada à janela, com área de histórico das mensagens e campo de entrada, que mostra o pedido do comando ativo (por exemplo, "Especifique o próximo ponto ou [Desfazer/Fechar]:"). | US-1 |
| REQ-005-05 | O sistema DEVE aceitar os comandos POINT (PO), LINE (L), PLINE (PL), MOVE (M), COPY (CO), ERASE (E), UNDO (U) e REDO, sem diferenciar maiúsculas, pelo nome ou pelo atalho, e completar o nome a partir das primeiras letras com Tab. | US-1, US-4 |
| REQ-005-06 | QUANDO o usuário pressionar Enter ou Espaço com a entrada vazia e nenhum comando ativo, O sistema DEVE repetir o último comando. | US-1 |
| REQ-005-07 | QUANDO o usuário pressionar Esc ou clicar com o botão direito no viewport, O sistema DEVE cancelar o comando ativo, mantendo o que ele já tiver concluído (por exemplo, os segmentos já desenhados de uma LINE). | US-1 |
| REQ-005-08 | SE o texto digitado não for um comando, uma opção do pedido atual ou uma entrada válida, ENTÃO O sistema DEVE mostrar uma mensagem no histórico e manter o comando ativo no mesmo passo. | US-1, US-2 |
| REQ-005-09 | O sistema DEVE guardar o histórico das entradas da sessão e percorrê-lo com as setas para cima e para baixo. | US-1 |

### Entrada de pontos e números

| ID | Requisito | História |
|----|-----------|----------|
| REQ-005-10 | O sistema DEVE aceitar coordenadas absolutas `x,y` e `x,y,z` (z = 0 quando omitido), com ponto como separador decimal e vírgula entre as coordenadas. | US-2 |
| REQ-005-11 | O sistema DEVE aceitar coordenadas relativas ao último ponto `@dx,dy` e `@dx,dy,dz`. | US-2 |
| REQ-005-12 | O sistema DEVE aceitar coordenadas polares relativas `@d<a` e absolutas `d<a`, com a distância `d` e o ângulo `a` em graus, medido no plano XY a partir do eixo +X, no sentido anti-horário; o z do ponto é o do ponto de referência. | US-2 |
| REQ-005-13 | QUANDO o usuário digitar só um número ao pedir um ponto que tem ponto anterior, O sistema DEVE usar o ponto a essa distância do anterior, na direção do cursor (já restringida pelo modo ortogonal, se ligado). | US-2 |
| REQ-005-14 | QUANDO o usuário clicar no viewport ao pedir um ponto, O sistema DEVE usar o ponto do snap ativo ou, sem snap, o ponto do plano Z = 0 sob o cursor (ajustado pela grade e pelo modo ortogonal quando ligados). | US-1, US-3 |

### Comandos de desenho

| ID | Requisito | História |
|----|-----------|----------|
| REQ-005-15 | O comando POINT DEVE criar um ponto a cada ponto informado, até Enter ou Esc. | US-1 |
| REQ-005-16 | O comando LINE DEVE pedir o primeiro ponto e criar uma linha para cada ponto seguinte, ligada ao anterior; a opção Desfazer (U) retira o último segmento do comando, a opção Fechar (C) liga o último ponto ao primeiro e encerra (com pelo menos dois segmentos), e Enter encerra. | US-1 |
| REQ-005-17 | O comando PLINE DEVE criar uma única polilinha com os pontos informados, com as mesmas opções Desfazer e Fechar da LINE; Enter encerra e cria a polilinha aberta se ela tiver ao menos dois vértices. | US-1 |
| REQ-005-18 | ENQUANTO um comando pede um ponto que tem ponto anterior, O sistema DEVE mostrar o trecho elástico do ponto anterior até o cursor. | US-1 |

### Precisão: snaps, grade e ortogonal

| ID | Requisito | História |
|----|-----------|----------|
| REQ-005-19 | ENQUANTO o snap a objetos estiver ligado e um ponto for pedido, O sistema DEVE oferecer, entre os modos ligados, o ponto de snap mais próximo do cursor dentro de uma abertura de 10 pixels lógicos: extremidade (vértices de linhas e polilinhas e os pontos), meio (de cada linha e de cada trecho de polilinha) e interseção (de dois trechos que se cruzam no espaço, dentro da tolerância do documento). | US-3 |
| REQ-005-20 | O sistema DEVE desenhar o marcador do snap encontrado junto ao ponto (quadrado para extremidade, triângulo para meio, X para interseção) e mostrar o nome do modo perto do cursor. | US-3 |
| REQ-005-21 | O sistema DEVE permitir ligar e desligar o snap a objetos com F3 e cada modo (extremidade, meio, interseção) num menu; todos ligados por padrão. | US-3 |
| REQ-005-22 | ENQUANTO o encaixe na grade estiver ligado (F9), O sistema DEVE arredondar o ponto do plano Z = 0 sob o cursor para múltiplos de um passo configurável (padrão 1), independente do espaçamento da grade desenhada; o snap a objetos tem prioridade sobre a grade. | US-3 |
| REQ-005-23 | ENQUANTO o modo ortogonal estiver ligado (F8) e o ponto pedido tiver ponto anterior, O sistema DEVE restringir o ponto do cursor à direção X ou Y que passa pelo ponto anterior (a mais próxima do cursor), no plano horizontal desse ponto; o snap a objetos tem prioridade sobre o modo ortogonal, e a entrada digitada não é restringida. | US-3 |
| REQ-005-24 | O sistema DEVE mostrar na barra de status as coordenadas x, y e z do ponto que seria usado e botões que indicam e alternam o snap a objetos, a grade e o modo ortogonal. | US-3 |

### Seleção e edição

| ID | Requisito | História |
|----|-----------|----------|
| REQ-005-25 | O sistema DEVE permitir selecionar entidades do desenho com os mesmos gestos da feature 004 (clique, janela, cruzamento, Shift para retirar, Esc para limpar), com a tolerância de clique de alguns pixels sobre linhas e pontos, e realçar as selecionadas, com marcadores nos vértices das linhas e polilinhas selecionadas (só exibição; arrastá-los é da feature 017). *(Marcadores acrescentados em 2026-11-12, a pedido do mantenedor.)* | US-4 |
| REQ-005-26 | O sistema DEVE oferecer, no menu Editar, o alvo da seleção sem comando ativo: Desenho (padrão) ou Malha; com Malha, valem as regras de seleção da feature 004; os comandos de edição sempre selecionam entidades do desenho. | US-5 |
| REQ-005-27 | QUANDO um comando de edição for iniciado com entidades do desenho já selecionadas, O sistema DEVE usá-las; senão, DEVE pedir "Selecione objetos" e aceitar clique, janela e cruzamento até Enter. | US-4 |
| REQ-005-28 | O comando MOVE DEVE pedir o ponto base e o segundo ponto e deslocar as entidades pelo vetor entre eles, mostrando a pré-visualização das entidades no cursor. | US-4 |
| REQ-005-29 | O comando COPY DEVE pedir o ponto base e criar uma cópia deslocada (com novos IDs) para cada segundo ponto informado, até Enter ou Esc, com pré-visualização. | US-4 |
| REQ-005-30 | O comando ERASE e a tecla Delete com entidades selecionadas DEVEM remover as entidades do documento; a tecla Delete funciona também com o foco na linha de comando, quando a entrada está vazia. *(Alterado em 2026-11-12: o Delete não chegava com o foco na linha de comando; o botão direito cancela, a pedido do mantenedor.)* | US-4 |

### Desfazer e refazer

| ID | Requisito | História |
|----|-----------|----------|
| REQ-005-31 | O sistema DEVE desfazer (Ctrl+Z, Editar ▸ Desfazer, comando UNDO) e refazer (Ctrl+Y ou Ctrl+Shift+Z, Editar ▸ Refazer, comando REDO) cada operação concluída no documento: um comando POINT, LINE, PLINE, MOVE, COPY ou ERASE inteiro é um passo. | US-4 |
| REQ-005-32 | QUANDO uma operação for desfeita, O sistema DEVE restaurar as entidades com os mesmos IDs que tinham. | US-4 |

### Exibição e integração

| ID | Requisito | História |
|----|-----------|----------|
| REQ-005-33 | O sistema DEVE desenhar as entidades no viewport junto da malha: linhas e trechos de polilinha como linhas e pontos como marcadores de tamanho fixo na tela, numa cor própria, sem tremor longe da origem. | US-1, US-5 |
| REQ-005-34 | QUANDO houver desenho, "Enquadrar tudo" DEVE enquadrar o desenho e a malha juntos. | US-5 |
| REQ-005-35 | O sistema DEVE traduzir para pt_BR os textos novos da interface e os pedidos da linha de comando; os nomes e atalhos dos comandos permanecem em inglês, como no AutoCAD. | US-1 |

## 4. Requisitos não funcionais

| ID | Requisito | Como medir |
|----|-----------|------------|
| NFR-005-01 | A busca de snap DEVE responder em menos de 5 ms por movimento do mouse com um desenho de 100 mil trechos. | Benchmark em Release, na máquina do mantenedor |
| NFR-005-02 | A navegação DEVE manter ≥ 30 quadros por segundo com 100 mil trechos desenhados, junto com a malha de 1 milhão de tetraedros. | Contador de quadros em Release. *Medição adiada para a feature 014 (decidido em 2026-11-12): só então dá para abrir um desenho desse tamanho.* |
| NFR-005-03 | Com a câmera parada e nenhum movimento do mouse, o viewport NÃO DEVE consumir CPU nem GPU de forma contínua, também com um comando ativo. | Gerenciador de Tarefas em repouso |

## 5. Critérios de aceite

- [x] **AC-1** (REQ-005-01, 02, 03) Testes de `geom` sem Qt: criar, mover,
  copiar e apagar entidades preservam a geometria e os IDs esperados; IDs
  apagados não voltam a ser usados.
- [x] **AC-2** (REQ-005-05, 10 a 13) Testes da interpretação: comandos e
  atalhos, coordenadas absolutas, relativas, polares (absolutas e relativas) e
  distância direta, com 2 e 3 componentes; entradas inválidas são recusadas
  com mensagem.
- [x] **AC-3** (REQ-005-15 a 17, 28 a 32) Testes das máquinas de estado dos
  comandos sem interface: sequências de entradas produzem as entidades
  esperadas, incluindo Desfazer, Fechar, Enter, Esc, cópias múltiplas e
  desfazer/refazer com IDs restaurados.
- [x] **AC-4** (REQ-005-19, 22, 23) Testes de snap: extremidade, meio e
  interseção (também de trechos em 3D), abertura em pixels, prioridade do snap
  a objetos sobre grade e ortogonal.
- [x] **AC-5** (REQ-005-04 a 09, 14, 18, 20, 21, 24) Roteiro manual: desenhar
  um pórtico plano e um espacial pelo mouse e pela linha de comando, com
  snaps, grade e ortogonal; trecho elástico, marcadores, barra de status,
  histórico, Tab, Enter/Espaço e Esc se comportam como descrito.
- [x] **AC-6** (REQ-005-25 a 30) Roteiro manual: selecionar antes e depois do
  comando; mover, copiar e apagar com pré-visualização; Delete; alvo da
  seleção alternando entre Desenho e Malha sem misturar as seleções.
- [x] **AC-7** (REQ-005-31, 32) Roteiro manual: desfazer e refazer pelo menu,
  pelo teclado e pela linha de comando, passo a passo.
- [x] **AC-8** (REQ-005-33, 34) Desenho e malha juntos na tela; "Enquadrar
  tudo" enquadra os dois; um desenho a 1e6 da origem não treme.
- [x] **AC-9** (REQ-005-35) Com o sistema em português, menus e pedidos
  aparecem em português e os comandos continuam aceitos em inglês.
- [x] **AC-10** (NFR-005-01, 03) Medições registradas em `tasks.md`; a do
  NFR-005-02 fica para a feature 014.

## 6. Fora de escopo

- Círculo, arco, elipse, retângulo, polígono, spline e trechos em arco na
  polilinha — feature 016.
- Camadas, cores por entidade e painel de propriedades — features 016 e 010.
- Snaps de centro, perpendicular, tangente e mais próximo; rastreamento polar;
  entrada dinâmica junto ao cursor — feature 016.
- Rotacionar, espelhar, escalar, offset, trim, estender, fillet, chanfro,
  matriz, explodir, unir e grips — feature 017.
- Planos de trabalho (UCS): o mouse trabalha no plano Z = 0 — feature 019.
- Núcleo OpenCASCADE — feature 015.
- Medição e cotas — feature 018.
- Salvar e abrir o desenho — feature 014 (o desenho vive só na sessão).
- Desfazer edições da malha ou de outras partes do modelo — feature 010.
- Malhar as linhas — feature 009.

## 7. Requisitos globais relacionados

REQ-G-03 (barra de ferramentas e console: aqui, a linha de comando), REQ-G-06
(desfazer/refazer, para o desenho), REQ-G-07 (desenho por mouse e linha de
comando com snaps, na fatia básica), REQ-G-07b (entrada numérica exata),
REQ-G-20 (desempenho de exibição).

## 8. Questões em aberto

- [x] **Desfazer/refazer:** decidido em 2026-11-12 — já na 005, para o
  documento de desenho.
- [x] **Desenho e malha:** decidido em 2026-11-12 — juntos na tela, com
  seleções separadas e o alvo da seleção escolhido no menu Editar.
- [x] **Grade:** decidido em 2026-11-12 — encaixe com passo fixo configurável,
  independente da grade visual.
- [x] **Seleção para edição:** decidido em 2026-11-12 — antes ou depois do
  comando, como no AutoCAD.
- [x] **Onde configurar o passo da grade:** decidido em 2026-11-12 — diálogo
  em Ferramentas ▸ Configurações de desenho (passo da grade e abertura do
  snap).
- [x] **Cor das entidades:** decidido em 2026-11-12 — branco-gelo sobre o fundo
  escuro, com o realce de seleção laranja da 004.
