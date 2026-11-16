# Requisitos — 007 truss-elements

Status: aprovado · Data: 2026-11-15

## 1. Contexto e objetivo

A 006 entregou o núcleo de elementos finitos, mas ele só foi exercitado por
um elemento de teste. Esta feature traz o primeiro elemento físico, a barra
de treliça de 2 nós em 2D e em 3D, e o `Model` em `core`. O `Model` reúne
malha, materiais, seções, atribuições, apoios e ações, e é o documento que a
interface vai editar a partir da 010. A análise estática linear de um `Model`
de treliça devolve deslocamentos, reações e esforço normal, deformação e
tensão em cada barra. A verificação segue a constituição §5.1: patch test,
convergência e comparação com soluções analíticas. Nesta feature tudo é
exercitado por testes; nada muda na interface.

## 2. Histórias de usuário

- **US-1** Como engenheiro, quero analisar treliças planas e espaciais sob
  cargas nodais, recalques, peso próprio, variação de temperatura e
  pré-deformação, para obter deslocamentos, reações e esforços normais.
- **US-2** Como engenheiro, quero saber exatamente o que falta ou está errado
  num modelo (material, seção, apoio, mecanismo) antes de confiar no
  resultado.
- **US-3** Como desenvolvedor das próximas features (viga 008, interface 010,
  execução 011, resultados 012), quero um `Model` e uma análise estática
  linear reutilizáveis, para só acrescentar elementos e telas.

## 3. Requisitos funcionais (EARS)

### Materiais e seções

| ID | Requisito | História |
|----|-----------|----------|
| REQ-007-01 | O sistema DEVE representar um material isotrópico linear com nome e as propriedades módulo de elasticidade E, coeficiente de Poisson ν, massa específica ρ, coeficiente de dilatação térmica α, condutividade k e calor específico c. | US-1, US-3 |
| REQ-007-02 | O sistema DEVE representar uma seção de barra com nome e área A. As demais propriedades de seção (inércias, torção, forma) ficam para a 008. | US-1, US-3 |

### Modelo

| ID | Requisito | História |
|----|-----------|----------|
| REQ-007-03 | O sistema DEVE oferecer um `Model` em `core` que contém uma malha e listas nomeadas de materiais e seções, atribuições de elementos, apoios, deslocamentos prescritos e ações; o `Model` DEVE poder ser editado (incluir, alterar e remover cada item) e copiado. | US-3 |
| REQ-007-04 | O sistema DEVE permitir atribuir a um conjunto de elementos (um grupo da malha ou uma lista de elementos) um tipo de elemento de treliça (2D ou 3D), um material e uma seção. | US-1, US-3 |
| REQ-007-05 | O sistema DEVE permitir definir, num conjunto de nós (um grupo da malha ou uma lista de nós), apoios por direção (u, v, w) e deslocamentos prescritos não nulos (recalques). | US-1 |
| REQ-007-06 | O sistema DEVE permitir definir ações, todas num único caso de carga: forças nodais (componentes Fx, Fy, Fz num conjunto de nós); peso próprio (vetor de aceleração da gravidade aplicado a todo o modelo); variação de temperatura ΔT uniforme num conjunto de elementos; e pré-deformação axial ε₀ num conjunto de elementos (defeito de montagem ou pré-tensão). | US-1 |
| REQ-007-07 | O núcleo DEVE ser agnóstico a unidades: o `Model` guarda números sem unidade, e o usuário é responsável por usar um sistema consistente (constituição §5.2). | US-1 |

### Elemento de treliça

| ID | Requisito | História |
|----|-----------|----------|
| REQ-007-08 | O sistema DEVE fornecer o elemento de treliça de 2 nós (`Line2` da malha) em 3D, com GDL u, v, w por nó, e em 2D, no plano XY, com GDL u, v por nó. A rigidez DEVE ser a da barra elástica linear, com esforço normal constante: k = EA/L na direção da barra. | US-1 |
| REQ-007-09 | SE uma barra de treliça 2D tiver nó com coordenada z diferente de zero (tolerância relativa ao tamanho do modelo), ENTÃO O sistema DEVE recusar a análise informando o elemento. | US-2 |
| REQ-007-10 | SE um elemento atribuído como treliça não for do tipo `Line2`, ENTÃO O sistema DEVE recusar a análise informando o elemento e o tipo. | US-2 |
| REQ-007-11 | O elemento DEVE gerar as cargas nodais equivalentes do peso próprio (ρ·A·L·g, metade em cada nó), da variação de temperatura (esforço E·A·α·ΔT ao longo da barra) e da pré-deformação (esforço E·A·ε₀ ao longo da barra). | US-1 |

### Análise e resultados

| ID | Requisito | História |
|----|-----------|----------|
| REQ-007-12 | QUANDO for pedida a análise estática linear de um `Model`, O sistema DEVE montar e resolver o sistema com o núcleo da 006 e devolver: deslocamentos nodais; reações nos GDL prescritos; e, por barra, esforço normal N, deformação mecânica ε e tensão σ = N/A (tração positiva). | US-1, US-3 |
| REQ-007-13 | A análise DEVE ler o `Model` sem modificá-lo, para que possa rodar sobre uma cópia enquanto o original é editado (base da execução assíncrona da 011). | US-3 |
| REQ-007-14 | ANTES de montar, O sistema DEVE validar o modelo e, SE faltar algo, ENTÃO DEVE recusar a análise listando todos os problemas encontrados, cada um com o item envolvido: elemento sem atribuição (quando nenhum elemento tiver atribuição, o modelo está vazio); atribuição com material ou seção inexistente; E ≤ 0 ou A ≤ 0; ρ ausente ou ≤ 0 com peso próprio definido; α ausente com ΔT definido; grupo, nó ou elemento inexistente; apoio ou carga em nó sem GDL (sem barra ligada); barra de comprimento nulo. | US-2 |
| REQ-007-15 | SE a estrutura for um mecanismo (matriz de rigidez singular, por apoio insuficiente ou nó sem rigidez numa direção), ENTÃO O sistema DEVE recusar a solução informando ao menos um nó e a direção (u, v ou w) sem rigidez. | US-2 |
| REQ-007-16 | O sistema DEVE manter tudo desta feature em `core`, sem Qt nem GPU (constituição §4.1). | US-3 |

## 4. Requisitos não funcionais

| ID | Requisito | Como medir |
|----|-----------|------------|
| NFR-007-01 | Os resultados DEVEM reproduzir as soluções analíticas dos casos de verificação (AC-3 a AC-6) com erro relativo ≤ 1e−10 em deslocamentos, reações e esforços. | Testes automatizados |
| NFR-007-02 | Uma treliça espacial de cerca de 16 mil GDL DEVE ser validada, montada, resolvida e ter os esforços recuperados em menos de 5 s em Release na máquina do mantenedor. Treliças 3D maiores ficam com o solver iterativo (feature 025) e com o solver externo opcional (feature 038). *(Alterado em 2026-11-15, a pedido do mantenedor: a meta de 30 mil GDL em 5 s não é atingível com a fatoração simplicial, cujo tempo cresce com o quadrado do tamanho em 3D — 17 s medidos.)* | Benchmark |
| NFR-007-03 | A cobertura de linhas de `core` DEVE continuar em pelo menos 80% (constituição §6.5). | Job de cobertura no CI |

## 5. Critérios de aceite

- [x] **AC-1** (REQ-007-01 a 07, 13) Testes do `Model`: incluir, alterar,
  remover e copiar materiais, seções, atribuições, apoios e ações; atribuição
  por grupo da malha e por lista; a análise não altera o `Model`.
- [x] **AC-2** (REQ-007-08, 11) Testes do elemento: matriz de rigidez de uma
  barra em orientação qualquer igual à fórmula fechada; invariância sob
  rotação; modos de corpo rígido com energia nula (3 em 2D, 5 de 6 na barra
  isolada em 3D, mais a rotação em torno do próprio eixo); cargas
  equivalentes de peso próprio, ΔT e ε₀.
- [x] **AC-3** (REQ-007-12, NFR-007-01) Patch test: uma treliça triangulada
  com deslocamentos prescritos de um campo de deformação uniforme nos nós do
  contorno reproduz o campo nos nós internos e esforços coerentes em todas as
  barras.
- [x] **AC-4** (REQ-007-12, NFR-007-01) Soluções analíticas, em 2D e em 3D:
  barra simples sob força axial; treliça em V simétrica (deslocamento do nó
  carregado); treliça plana isostática (esforços pelo método dos nós);
  tripé espacial; treliça hiperestática de três barras concorrentes;
  recalque de apoio.
- [x] **AC-5** (REQ-007-11, 12, NFR-007-01) Ações distribuídas: barra presa
  nas duas pontas com ΔT (N = −E·A·α·ΔT e deslocamentos nulos); a mesma barra
  com ε₀ (N = −E·A·ε₀); barra livre numa ponta com ΔT (alongamento α·ΔT·L e
  N = 0); barra pendurada sob peso próprio (reação ρ·g·A·L).
- [x] **AC-6** (REQ-007-12, NFR-007-01) Convergência: barra pendurada
  subdividida em n elementos sob peso próprio; deslocamentos nodais exatos
  para qualquer n e erro L2 do esforço normal caindo com taxa ≈ 1 (± 0,1).
- [x] **AC-7** (REQ-007-09, 10, 14, 15) Testes de validação: cada problema
  do REQ-007-14 gera uma mensagem com o item envolvido, e vários problemas
  aparecem juntos; z ≠ 0 em treliça 2D e elemento que não é `Line2` são
  recusados; um mecanismo (nó sem apoio numa direção, ou treliça plana com
  nó solto) informa nó e direção.
- [x] **AC-8** (REQ-007-16, NFR-007-02, 03) `core` segue sem Qt; benchmark
  da treliça espacial registrado em `tasks.md`; CI verde nas quatro
  plataformas, com a cobertura de `core` ≥ 80%.

## 6. Fora de escopo

- Interface: árvore do modelo, propriedades, atribuição pelo viewport e
  desfazer/refazer — feature 010.
- Execução assíncrona com progresso e cancelamento — feature 011.
- Visualização de resultados (deformada, cores, reações) — feature 012;
  diagramas de esforços — 013; salvar o `Model` em arquivo — 014.
- Atribuições às entidades geométricas (IDs de `geom`) — a 007 atribui a
  grupos e listas da malha; a ligação com a geometria vem com a 009/010.
- Vigas, rótulas, seções com inércia e forma — feature 008.
- Barras só de tração ou compressão (cabos), não linearidade geométrica,
  flambagem, apoios elásticos (molas) e apoios inclinados.
- Vários casos de carga e combinações.
- Gradiente de temperatura e ΔT variável ao longo da barra.

## 7. Requisitos globais relacionados

- **REQ-G-12:** atribuição de materiais, seções, restrições e cargas, aqui a
  grupos e listas da malha.
- **REQ-G-14:** recusar modelos incompletos ou singulares indicando o que falta.
- **REQ-G-15:** análise estática linear.
- **REQ-G-16:** verificação por patch test, convergência e soluções analíticas.
- **REQ-G-22:** montagem paralela, herdada da 006.

## 8. Questões em aberto

- [x] Objetos do modelo na 007: `Material`, seção e também o `Model`
  completo (decidido em 2026-11-15).
- [x] Ações além de cargas nodais e recalques: peso próprio, variação de
  temperatura e pré-deformação (decidido em 2026-11-15).
- [x] Uso antes da interface: só por testes, sem ferramenta de linha de
  comando (decidido em 2026-11-15).
- [x] Mecanismo: informar nó e direção sem rigidez (decidido em 2026-11-15).
