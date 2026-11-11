# Produto — femapp

> Visão, escopo e requisitos globais. As specs de cada feature detalham e
> refinam o que está aqui. Itens marcados **[A CONFIRMAR]** dependem de decisão
> do mantenedor.

## 1. Visão

Um aplicativo desktop moderno, rápido e multiplataforma para análise por
elementos finitos de uso profissional, cobrindo o ciclo completo **geometria →
malha → análise → pós-processamento** em uma única interface, com visualização
3D acelerada por GPU.

## 2. Usuários-alvo

**Engenheiros em uso profissional.** Consequências para o produto:

- Robustez e desempenho têm prioridade sobre didática.
- Resultados precisam ser verificáveis (benchmarks documentados) e reprodutíveis.
- Interoperabilidade de arquivos (importação/exportação) é requisito, não extra.
- Modelos grandes (milhões de graus de liberdade) são caso de uso normal.

## 3. Escopo da versão 1.0

O escopo é amplo; a entrega é incremental, em versões intermediárias utilizáveis
(ver `specs/roadmap.md`).

### 3.1 Tipos de análise

| Análise | Detalhe |
|---------|---------|
| Estática linear | Elasticidade 2D (estado plano de tensão/deformação, axissimétrico) e 3D; estruturas de barras, vigas, placas e cascas |
| Térmica | Condução de calor estacionária e transiente (2D e 3D) |
| Modal | Frequências naturais e modos de vibração |
| Dinâmica linear | Resposta transiente (Newmark) e por superposição modal |

Fora da 1.0: não linearidade (geométrica, material, contato), flambagem,
acoplamento termomecânico, fadiga.

### 3.2 Biblioteca de elementos

| Família | Elementos |
|---------|-----------|
| Barras (2D e 3D) | Treliça (2 nós), viga de Euler-Bernoulli e de Timoshenko (2 nós) |
| Sólidos 2D lineares | T3, Q4 |
| Sólidos 2D quadráticos | T6, Q8 |
| Sólidos 3D lineares | Tet4, Hex8, Wedge6 |
| Sólidos 3D quadráticos | Tet10, Hex20 |
| Placas e cascas | Casca plana triangular e quadrilateral (formulação MITC), 6 GDL por nó |

Os elementos sólidos servem tanto à análise estrutural quanto à térmica.

### 3.3 Geometria (módulo de desenho estilo AutoCAD)

| Área | Capacidade |
|------|------------|
| Entidades | Ponto, linha, polilinha, retângulo, polígono, círculo, arco, elipse, spline |
| Entrada | Linha de comando com comandos e atalhos (`LINE`/`L`, `CIRCLE`/`C`…), coordenadas absolutas (`x,y`), relativas (`@dx,dy`) e polares (`@d<ângulo`), entrada dinâmica junto ao cursor |
| Precisão | Snaps a objetos (extremidade, meio, centro, interseção, perpendicular, tangente, mais próximo), grade, modo ortogonal, rastreamento polar |
| Edição | Mover, copiar, rotacionar, espelhar, escalar, offset, aparar (trim), estender, concordância (fillet), chanfro, matriz retangular/polar, explodir, unir; grips nos objetos |
| Organização | Camadas (nome, cor, visibilidade, bloqueio), seleção por janela e por cruzamento, propriedades |
| Medição | Distância, ângulo, área, cotas básicas |
| 3D | Planos de trabalho (UCS), linhas e polilinhas 3D para pórticos espaciais, regiões a partir de contornos, extrusão, revolução, primitivas (bloco, cilindro, esfera), operações booleanas |
| Importação | STEP, IGES e DXF |
| Exportação | STEP e DXF |

A geometria é a base do modelo: materiais, seções, apoios e cargas são atribuídos
às entidades geométricas e sobrevivem à regeneração da malha.

### 3.4 Geração de malha (implementação própria)

| Etapa | Capacidade |
|-------|------------|
| Malha 1D | Divisão de linhas para barras e vigas |
| Malha 2D estruturada | Mapeada/transfinita em quadriláteros e triângulos |
| Malha 2D não estruturada | Triangulação de Delaunay com refinamento de qualidade; quadriláteros por recombinação |
| Malha em superfícies curvas | Triângulos/quadriláteros sobre faces de sólidos e CAD importado |
| Malha 3D por varredura | Extrusão/revolução de malha 2D (Hex8, Wedge6) |
| Malha 3D não estruturada | Tetraedros por Delaunay 3D |
| Controle | Tamanho global e local, refinamento por região, métricas de qualidade |
| Ordem | Conversão de malha linear em quadrática |

Importação de Gmsh `.msh` permanece como recurso auxiliar (interoperabilidade
e malhas de referência para verificação), na versão 4.1 ASCII e binário
(decidido em 2026-11-11, na especificação da feature 003).

### 3.5 Demais áreas

| Área | Versão 1.0 |
|------|------------|
| Materiais | Isotrópico linear (E, ν, ρ, k, c); biblioteca de materiais |
| Seções | Seções de barra/viga (retangular, circular, perfil I, genérica) e espessura de casca |
| Condições | Apoios, deslocamento prescrito, forças e momentos nodais, carga distribuída em linha/face, pressão, peso próprio; temperatura, fluxo e convecção |
| Solvers | Direto esparso, iterativo (CG pré-condicionado), autovalores (Lanczos/shift-invert), integração no tempo |
| Pós-processamento | Deslocamentos, tensões, von Mises, esforços em barras (N, V, M), temperatura e fluxo, modos animados, deformada com escala, mapa de cores, legenda, consulta de valores |
| Arquivos | Projeto próprio (`.femapp`, JSON), CAD (STEP, IGES, DXF), exportação VTK `.vtu`, importação `.msh` |

## 4. Requisitos globais (EARS)

Notação: **QUANDO** <gatilho>, **O sistema DEVE** <resposta>.

### Plataforma e build
- **REQ-G-01** O sistema DEVE compilar a partir do mesmo código-fonte em Windows 10+,
  macOS 13+ e Linux (Ubuntu 22.04+), usando apenas CMake e os presets do repositório.
- **REQ-G-02** O sistema DEVE selecionar automaticamente o backend gráfico nativo
  da plataforma (Direct3D, Metal, Vulkan) e recorrer a OpenGL se indisponível.

### Interface
- **REQ-G-03** O sistema DEVE apresentar janela principal com: viewport 3D, árvore do
  modelo, painel de propriedades, barra de ferramentas e console de mensagens.
- **REQ-G-04** QUANDO o usuário arrastar o mouse no viewport, O sistema DEVE orbitar,
  deslocar (pan) ou aplicar zoom na câmera conforme o botão pressionado.
- **REQ-G-05** QUANDO uma análise ou geração de malha estiver em execução, O sistema
  DEVE manter a interface responsiva, exibir progresso e permitir cancelamento.
- **REQ-G-06** O sistema DEVE oferecer desfazer/refazer para todas as edições do modelo.

### Geometria e malha
- **REQ-G-07** O sistema DEVE permitir criar e editar geometria 2D e 3D diretamente
  na interface, por mouse e por linha de comando, com snaps a objetos, no estilo
  de um aplicativo CAD.
- **REQ-G-07a** QUANDO o usuário importar um arquivo STEP, IGES ou DXF válido, O
  sistema DEVE convertê-lo em geometria editável e malhável; SE o arquivo for
  inválido ou contiver entidades não suportadas, ENTÃO DEVE listar o que foi
  ignorado e por quê.
- **REQ-G-07b** O sistema DEVE aceitar entrada numérica exata de coordenadas,
  distâncias e ângulos em todos os comandos de desenho e edição.
- **REQ-G-08** QUANDO o usuário solicitar a malha de uma geometria válida, O sistema
  DEVE gerar elementos conformes respeitando os tamanhos especificados.
- **REQ-G-09** O sistema DEVE calcular e exibir métricas de qualidade da malha e
  destacar elementos abaixo de um limite configurável.
- **REQ-G-10** SE a geração de malha falhar, ENTÃO O sistema DEVE informar a região
  problemática e manter a malha anterior inalterada.
- **REQ-G-11** QUANDO o usuário importar um arquivo `.msh` válido, O sistema DEVE
  carregar nós, elementos e grupos físicos; SE o arquivo for inválido, ENTÃO DEVE
  informar o motivo e manter o modelo atual inalterado.

### Modelo e análise
- **REQ-G-12** O sistema DEVE permitir atribuir materiais, seções, restrições e
  cargas a entidades geométricas, grupos e seleções feitas no viewport.
- **REQ-G-13** O sistema DEVE permitir combinar, no mesmo modelo, elementos de
  barra, casca e sólido com graus de liberdade compatibilizados.
- **REQ-G-14** QUANDO o usuário solicitar a análise de um modelo incompleto ou
  singular, O sistema DEVE recusar a execução indicando o que falta.
- **REQ-G-15** O sistema DEVE executar análises estática linear, térmica
  (estacionária e transiente), modal e dinâmica linear.
- **REQ-G-16** O sistema DEVE reproduzir soluções analíticas e benchmarks de
  referência (NAFEMS e equivalentes) dentro das tolerâncias definidas em cada feature.

### Visualização de resultados
- **REQ-G-17** O sistema DEVE exibir campos escalares como mapa de cores sobre a
  malha, com legenda, valores mínimo/máximo e unidades.
- **REQ-G-18** O sistema DEVE exibir a configuração deformada com fator de escala
  ajustável e animar modos de vibração e históricos no tempo.
- **REQ-G-19** O sistema DEVE exibir diagramas de esforços (normal, cortante,
  momento) em elementos de barra.

### Desempenho
- **REQ-G-20** O sistema DEVE manter ≥ 30 FPS ao manipular malhas de até 1 milhão
  de elementos em GPU dedicada de gama média.
- **REQ-G-21** O sistema DEVE resolver um modelo estático linear 3D com 1 milhão de
  graus de liberdade em menos de 60 s em um desktop de 8 núcleos. **[A CONFIRMAR]**
- **REQ-G-22** O sistema DEVE usar todos os núcleos disponíveis na montagem e na solução.

### Persistência
- **REQ-G-23** O sistema DEVE salvar e reabrir projetos sem perda de informação
  (geometria, malha, modelo e resultados).
- **REQ-G-24** O sistema DEVE exportar malha e resultados em formato VTK `.vtu`.

## 5. Critérios de sucesso da versão 1.0

1. Fluxo completo (criar geometria → gerar malha → definir modelo → resolver →
   visualizar) funcionando nas três plataformas, sem ferramentas externas.
2. Suíte de verificação numérica (patch tests, convergência, benchmarks) 100% verde
   no CI para todos os elementos e tipos de análise.
3. Instaladores gerados automaticamente pelo CI para Windows, macOS e Linux.

## 6. Questões em aberto

- [x] Manter a importação de `.msh` como recurso auxiliar? Sim, 4.1 ASCII e binário
  (decidido em 2026-11-11).
- [ ] Malha hexaédrica não estruturada é necessária, ou varredura basta?
- [ ] Meta de desempenho do solver (REQ-G-21).
- [ ] Importação de DWG (formato fechado da Autodesk) é necessária? DXF cobre a
      troca com o AutoCAD sem depender de biblioteca proprietária ou GPL.
- [ ] Titular do copyright no arquivo `LICENSE` (hoje: "femapp authors").

Decididas: licença **MIT**; nome **femapp**; importação de CAD na 1.0;
barras e vigas antes dos demais elementos.
- [ ] Idiomas da interface além de inglês e pt_BR.
