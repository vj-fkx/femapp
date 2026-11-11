# Requisitos — 002 gpu-viewport

Status: aprovado · Data: 2026-11-10

## 1. Contexto e objetivo

A feature 001 entregou uma janela vazia. Esta feature coloca nela o **viewport
3D**, renderizado por GPU através da Qt RHI, com uma câmera que o usuário
controla pelo mouse e duas referências visuais: a grade do plano de trabalho e o
indicador de eixos. Ainda não há modelo para mostrar; o objetivo é ter a base de
renderização e navegação sobre a qual a malha (004), o desenho (005) e os
resultados (012) serão exibidos.

## 2. Histórias de usuário

- **US-1** Como engenheiro, quero ver um viewport 3D ao abrir o aplicativo, para
  ter onde visualizar o modelo.
- **US-2** Como engenheiro, quero orbitar, deslocar e aproximar a vista com o
  mouse, para examinar o modelo de qualquer ângulo.
- **US-3** Como engenheiro, quero vistas padrão (frontal, superior, lateral,
  isométrica) e alternar entre perspectiva e ortográfica, para conferir o modelo
  em vistas técnicas.
- **US-4** Como engenheiro, quero uma grade e um indicador de eixos, para não
  perder a orientação e a noção de escala.
- **US-5** Como desenvolvedor, quero a lógica de câmera testável sem GPU e o
  viewport verificado no CI, para evoluir a renderização com segurança.

## 3. Requisitos funcionais (EARS)

### Viewport e backend gráfico

| ID | Requisito | História |
|----|-----------|----------|
| REQ-002-01 | O sistema DEVE exibir, como área central da janela principal, um viewport 3D renderizado por GPU através da Qt RHI (`QRhiWidget`). | US-1 |
| REQ-002-02 | O sistema DEVE selecionar por padrão o backend gráfico da plataforma: Direct3D 11 no Windows, Metal no macOS e Vulkan no Linux. | US-1 |
| REQ-002-03 | SE o backend padrão não puder ser inicializado, ENTÃO O sistema DEVE tentar OpenGL antes de desistir. | US-1 |
| REQ-002-04 | QUANDO a variável de ambiente `FEM_RHI_BACKEND` tiver um dos valores `d3d11`, `d3d12`, `metal`, `vulkan`, `opengl` ou `null`, O sistema DEVE usar esse backend em vez do padrão. | US-5 |
| REQ-002-05 | SE `FEM_RHI_BACKEND` tiver um valor desconhecido ou indisponível na plataforma, ENTÃO O sistema DEVE registrar um aviso e usar o backend padrão. | US-5 |
| REQ-002-06 | SE nenhum backend gráfico puder ser inicializado, ENTÃO O sistema DEVE manter a janela aberta e mostrar, no lugar do viewport, uma mensagem que informa a falha, sem encerrar o aplicativo. | US-1 |
| REQ-002-07 | O sistema DEVE mostrar, na caixa "Sobre", o backend gráfico em uso. | US-1 |
| REQ-002-08 | QUANDO o viewport for redimensionado ou movido para uma tela com outra densidade de pixels, O sistema DEVE ajustar a imagem ao novo tamanho sem distorcer as proporções. | US-1 |
| REQ-002-09 | O sistema DEVE redesenhar o viewport apenas quando algo mudar (câmera, tamanho, conteúdo), e não continuamente. | US-1 |
| REQ-002-10 | O sistema DEVE confinar todo uso de `QRhi` e de shaders ao módulo `render`; o módulo `app` usa apenas a interface pública do viewport. | US-5 |

### Câmera e navegação

| ID | Requisito | História |
|----|-----------|----------|
| REQ-002-11 | O sistema DEVE adotar o eixo Z como vertical ("para cima") e XY como plano de trabalho. | US-2 |
| REQ-002-12 | QUANDO o usuário arrastar com o botão do meio do mouse, O sistema DEVE deslocar a vista (pan) de modo que o ponto sob o cursor acompanhe o cursor. | US-2 |
| REQ-002-13 | QUANDO o usuário arrastar com Shift + botão do meio, O sistema DEVE orbitar a câmera em torno do ponto de foco, mantendo o eixo Z vertical na tela. | US-2 |
| REQ-002-14 | QUANDO o usuário girar a roda do mouse, O sistema DEVE aproximar ou afastar a vista mantendo fixo o ponto sob o cursor. | US-2 |
| REQ-002-15 | O sistema DEVE limitar o zoom a uma faixa que impeça a câmera de atravessar o ponto de foco ou de se afastar a ponto de perder precisão numérica. | US-2 |
| REQ-002-16 | O sistema DEVE oferecer projeção perspectiva e ortográfica, e alternar entre elas por comando de menu, preservando o ponto de foco e o tamanho aparente da cena nesse ponto. | US-3 |
| REQ-002-17 | O sistema DEVE oferecer, no menu "Exibir", as vistas padrão Frontal, Posterior, Esquerda, Direita, Superior, Inferior e Isométrica, cada uma com atalho de teclado. | US-3 |
| REQ-002-18 | QUANDO o usuário escolher "Enquadrar tudo", O sistema DEVE ajustar a câmera para que todo o conteúdo visível caiba no viewport; sem conteúdo, DEVE enquadrar a região padrão da grade. | US-3 |
| REQ-002-19 | QUANDO o aplicativo iniciar, O sistema DEVE mostrar a vista isométrica em projeção perspectiva, enquadrando a região padrão da grade. | US-1 |

### Grade e eixos

| ID | Requisito | História |
|----|-----------|----------|
| REQ-002-20 | O sistema DEVE desenhar uma grade no plano XY (Z = 0), com linhas principais e secundárias distinguíveis. | US-4 |
| REQ-002-21 | QUANDO o zoom mudar, O sistema DEVE adaptar o espaçamento da grade em potências de 10, de modo que a densidade de linhas na tela permaneça legível. | US-4 |
| REQ-002-22 | O sistema DEVE destacar na grade as linhas dos eixos X e Y que passam pela origem, com as cores dos eixos. | US-4 |
| REQ-002-23 | O sistema DEVE esmaecer a grade com a distância e próximo ao horizonte, sem cintilação visível ao mover a câmera. | US-4 |
| REQ-002-24 | O sistema DEVE desenhar, em um canto do viewport, um indicador de eixos que acompanha a orientação da câmera, com X em vermelho, Y em verde e Z em azul, e rótulos "X", "Y" e "Z". | US-4 |
| REQ-002-25 | O sistema DEVE manter o indicador de eixos com tamanho fixo em pixels lógicos, independente do zoom e do tamanho do viewport. | US-4 |
| REQ-002-26 | O sistema DEVE permitir ligar e desligar a grade e o indicador de eixos pelo menu "Exibir". | US-4 |
| REQ-002-27 | O sistema DEVE traduzir para pt_BR todos os textos novos de menu e de mensagens. | US-1 |

### Testes e CI

| ID | Requisito | História |
|----|-----------|----------|
| REQ-002-28 | O sistema DEVE manter a lógica de câmera (matrizes, órbita, pan, zoom, vistas padrão, enquadramento) em código sem dependência de GPU, coberto por testes unitários. | US-5 |
| REQ-002-29 | QUANDO executado com `--smoke-test` e `FEM_RHI_BACKEND=null`, O sistema DEVE criar o viewport, renderizar ao menos um quadro e encerrar com código 0; esse teste DEVE rodar nos quatro jobs do CI. | US-5 |
| REQ-002-30 | O sistema DEVE compilar os shaders para todos os backends durante o build, a partir de uma única fonte por shader. | US-5 |

## 4. Requisitos não funcionais

| ID | Requisito | Como medir |
|----|-----------|------------|
| NFR-002-01 | Com a cena desta feature (grade e eixos), a navegação DEVE manter ≥ 60 quadros por segundo em janela 1920×1080 em GPU integrada recente. | Contador de quadros em build Release, na máquina do mantenedor |
| NFR-002-02 | Com a câmera parada, o viewport NÃO DEVE consumir GPU nem CPU de forma contínua. | Uso de CPU/GPU do processo em repouso próximo de zero no gerenciador de tarefas |
| NFR-002-03 | As operações de câmera DEVEM usar precisão dupla, para suportar modelos com coordenadas grandes (ex.: 1e6 unidades) sem tremor na vista. | Teste unitário com foco distante da origem |
| NFR-002-04 | Os testes unitários de `render` desta feature DEVEM rodar sem GPU e em menos de 1 s. | Tempo no `ctest` |

## 5. Critérios de aceite

- [x] **AC-1** (REQ-002-01, 19) Quando abro o `femapp`, vejo o viewport ocupando a
  área central, em vista isométrica, com grade e indicador de eixos.
- [ ] **AC-2** (REQ-002-02, 07) Em cada plataforma, a caixa "Sobre" mostra o
  backend esperado (Direct3D 11, Metal ou Vulkan).
- [ ] **AC-3** (REQ-002-03, 04, 05) Com `FEM_RHI_BACKEND=opengl`, "Sobre" mostra
  OpenGL; com um valor inválido, o aplicativo abre com o backend padrão e
  registra um aviso.
- [x] **AC-4** (REQ-002-06) Forçando a falha de todos os backends (teste
  automatizado), a janela continua aberta e mostra a mensagem de falha.
- [x] **AC-5** (REQ-002-12, 13, 14, 15) Roteiro manual: pan com o botão do meio,
  órbita com Shift + botão do meio e zoom com a roda se comportam como
  descrito; o ponto sob o cursor fica fixo no pan e no zoom.
- [x] **AC-6** (REQ-002-11, 13, 28) Testes unitários: após qualquer sequência de
  órbitas, o eixo Z do mundo permanece vertical na tela; pan e zoom preservam o
  ponto sob o cursor dentro de uma tolerância.
- [x] **AC-7** (REQ-002-16, 17, 18) Cada vista padrão e a troca de projeção
  funcionam pelo menu e pelo atalho; testes unitários conferem a direção de
  visada de cada vista e o enquadramento de uma caixa conhecida.
- [x] **AC-8** (REQ-002-20 a 23) Roteiro manual: a grade muda de espaçamento ao
  aproximar e afastar, os eixos X e Y aparecem coloridos, e não há cintilação
  ao orbitar com a câmera rasante.
- [x] **AC-9** (REQ-002-24, 25, 26) O indicador de eixos gira com a câmera, não
  muda de tamanho com o zoom, e ele e a grade somem e voltam pelo menu "Exibir".
- [ ] **AC-10** (REQ-002-08) Ao redimensionar a janela e ao movê-la entre telas
  com escalas diferentes, a imagem não distorce nem fica borrada.
- [x] **AC-11** (REQ-002-09, NFR-002-02) Com a câmera parada, o contador de
  quadros renderizados não aumenta.
- [x] **AC-12** (REQ-002-29, 30) Os quatro jobs do CI passam, incluindo o smoke
  test do viewport com o backend `null`.
- [x] **AC-13** (REQ-002-10) Nenhum arquivo fora de `src/render` inclui headers
  de `QRhi` (verificação automática, como a de Qt em `core`).
- [x] **AC-14** (REQ-002-27) Com o sistema em português, os itens novos do menu
  aparecem em português.

## 6. Fora de escopo

- Exibição de malha, geometria ou resultados (features 004, 005, 012).
- Seleção de objetos no viewport (picking) — feature 004.
- Snaps, cursor de mira e uso da grade como referência de desenho — feature 005.
- Cubo de vistas clicável, animação de transição entre vistas e navegação por
  teclado ou por touchpad/gestos.
- Docks, barra de ferramentas e demais painéis da janela — feature 010.
- Configuração persistente (cores, unidades da grade, botões do mouse).
- Antisserrilhamento além do MSAA oferecido pelo `QRhiWidget`.
- Meta de 30 FPS com 1 milhão de elementos (REQ-G-20) — medida na feature 004.

## 7. Requisitos globais relacionados

REQ-G-02 (backend nativo com fallback OpenGL), REQ-G-03 (viewport 3D na janela
principal, parcial), REQ-G-04 (órbita, pan e zoom pelo mouse).

## 8. Questões em aberto

Todas decididas em 2026-11-10, na aprovação dos requisitos:

- [x] **Botões do mouse:** padrão do AutoCAD — meio = pan, Shift + meio =
  órbita, roda = zoom.
- [x] **Eixo vertical:** Z para cima, plano de trabalho em XY.
- [x] **Linux sem Vulkan:** Vulkan como padrão, OpenGL como alternativa
  automática.
- [x] **Usuários sem botão do meio:** fica para uma feature posterior.
