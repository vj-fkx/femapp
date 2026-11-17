# Arquitetura — femapp

> Design técnico global. Cada `design.md` de feature deve ser coerente com este
> documento; mudanças estruturais aqui exigem ADR em `specs/decisions/`.

## 1. Stack tecnológica

| Item | Escolha | Motivo |
|------|---------|--------|
| Linguagem | C++20 | Concepts, ranges, `std::span`, `std::jthread` |
| Build | CMake ≥ 3.25 + `CMakePresets.json` | Padrão multiplataforma |
| Dependências | vcpkg (modo manifesto) para libs C++; Qt via instalador oficial/`aqtinstall` | Reprodutível; evita compilar Qt do zero |
| GUI | Qt ≥ 6.7 — Widgets | Maturidade para apps de engenharia (docks, árvores, tabelas) |
| GPU | Qt RHI (`QRhiWidget`) | Um código de render → D3D11/12, Metal, Vulkan, OpenGL |
| Shaders | GLSL 440 → `qsb` (`qt_add_shaders`) | Compilação cruzada automática para HLSL/MSL/SPIR-V |
| Álgebra linear | Eigen 3.4 | Header-only, esparsa + solvers, MPL-2.0 |
| Núcleo geométrico | OpenCASCADE (OCCT) ≥ 7.8, link dinâmico | B-rep, booleanas, STEP/IGES; LGPL 2.1 com exceção (ver ADR-0001) |
| Autovalores | Spectra | Header-only sobre Eigen, Lanczos com shift-invert, MPL-2.0 |
| Paralelismo | oneTBB | Montagem e geração de malha multithread, Apache-2.0 |
| Predicados geométricos | Predicados exatos de Shewchuk (domínio público) | Robustez do Delaunay |
| Testes | Catch2 v3 + CTest | Leve, bom suporte a comparações de ponto flutuante |
| Logging | spdlog | Rápido, sinks customizáveis (console da UI) |
| JSON | nlohmann/json | Arquivo de projeto |
| CI | GitHub Actions (matriz Windows/macOS/Linux) | Garante o princípio multiplataforma |

> `QRhi` tem garantia de compatibilidade limitada entre versões menores do Qt.
> A versão do Qt é fixada no CI e todo uso de RHI fica confinado ao módulo `render`.

## 2. Módulos

```
fem_app/
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json
├── src/
│   ├── core/      # fem::core   — núcleo numérico (sem Qt, sem GPU)
│   ├── geom/      # fem::geom   — modelador geométrico (sem Qt, sem GPU)
│   ├── mesher/    # fem::mesher — geradores de malha (sem Qt, sem GPU)
│   ├── io/        # fem::io     — leitura/escrita de arquivos
│   ├── render/    # fem::render — renderização via QRhi
│   └── app/       # fem::app    — interface Qt Widgets, executável
├── shaders/       # *.vert / *.frag
├── resources/     # ícones, traduções, .qrc
├── tests/
│   ├── unit/          # por módulo
│   ├── verification/  # patch tests, convergência, benchmarks
│   └── data/          # malhas e resultados de referência
└── specs/
```

### Dependências permitidas

```
app ──► render ──────────► core
 │                          ▲
 ├────► io ─────────────────┤
 │       │                  │
 │       ▼                  │
 └────► mesher ──► geom     │
           └────────────────┘
```

| Módulo | Tipo | Pode depender de |
|--------|------|------------------|
| `core` | lib estática | STL, Eigen, `tl-expected`, Spectra, oneTBB, spdlog |
| `geom` | lib estática | STL, Eigen, OpenCASCADE |
| `mesher` | lib estática | `core` (apenas `Mesh`), `geom`, oneTBB |
| `io` | lib estática | `core`; `geom` e nlohmann/json quando chegarem STEP/IGES/DXF e o arquivo de projeto |
| `render` | lib estática | `core`, `geom`, Qt Gui (RHI, headers privados), Qt Widgets (`QRhiWidget`); Qt Shader Tools no build — ver ADR 0002 |
| `app` | executável | todos, Qt Widgets |

## 3. Núcleo (`core`)

Conceitos principais:

- **`Mesh`** — coordenadas nodais (SoA, `double`), conectividade por bloco de
  tipo de elemento (índices de 32 bits), ids externos comprimidos em faixas,
  grupos nomeados de elementos por dimensão. Imutável, construída e validada
  por `MeshBuilder`. Aceita os 90 tipos de elemento de número fixo de nós do
  Gmsh (ordem 1 a 10), com a ordem local de nós do Gmsh; a tabela de tipos é
  gerada da API do Gmsh por `tools/generate_element_types.py` (feature 003).
- **`extractSurface`** — faces dos elementos 2D e casca externa dos 3D
  (faces de um só elemento, orientadas para fora), com as arestas, pelos
  vértices (feature 004).
- **`ShapeFunctions` / `Quadrature`** — por topologia (linha, triângulo,
  quadrilátero, tetraedro, hexaedro, cunha) e ordem (linear, quadrática);
  independentes da física. *Feita na 006:* 16 tipos (mais a pirâmide de 5
  nós), funções de Lagrange obtidas da Vandermonde inversa nos nós de
  referência do Gmsh (ordem do Gmsh por construção), avaliadas sem alocação;
  quadraturas de Gauss (grau ≤ 9) e simétricas positivas nos simplexos
  (grau ≤ 5); `evaluateGeometry` com jacobiano e medida de linhas e
  superfícies em 3D.
- **`Element`** (interface por formulação, sem alocação por elemento) — declara
  os GDL que usa em cada nó e calcula rigidez `K`, massa `M`, capacidade/condutividade
  e forças locais. Formulações: treliça, viga, sólido estrutural, casca MITC,
  condução de calor.
- **`Material`** — propriedades (E, ν, ρ, k, c) e matriz constitutiva `D`.
- **`Section`** — seção de barra/viga (A, I, J, orientação) e espessura de casca.
  *Feito na 008:* `section.hpp` com a seção genérica (A, Iy, Iz, Iyz, J,
  centro de cisalhamento, áreas de cisalhamento) ou oito formas paramétricas
  (retângulo, círculo, tubos circular e retangular, perfis I, U, T e
  cantoneira); `sectionProperties` devolve tudo nos eixos principais, e
  `stressPoints` dá os pontos característicos para as tensões.
  *Ampliado na 008b:* Iw e coordenada setorial ω das formas; seção de paredes
  finas pela linha média (`ThinWalled`: grafo, Bredt com várias células,
  setorial, centro de Trefftz, áreas pelo fluxo) e polígono maciço com furos
  (`SolidPolygon`), triangulado em `core` (`detail/section_mesh.hpp`, ADR
  0005) e resolvido em T6 (empenamento de Saint-Venant, funções de
  cisalhamento); `solveSection` resolve uma vez e guarda os campos
  (`SectionSolution`), usados por `stressAt` e `largestVonMises`.
- **`Model`** — malha + materiais + seções + atribuições + condições de contorno
  + cargas. É o documento editado pela UI.
  *Feito na 007:* agregado de valor (`model.hpp`) com a malha em
  `shared_ptr<const Mesh>` (cópia barata para o instantâneo da 011);
  `Material` e `Section` (`material.hpp`) referenciados pelo nome; alvos por
  grupo da malha ou por ids externos (`ElementIds`, `NodeIds`); um caso de
  carga com forças nodais, peso próprio, ΔT e pré-deformação.
  *Ampliado na 008:* vigas (`ElementKind::Beam2D/Beam3D`, teoria e
  orientação na atribuição), momentos nodais e rotações prescritas, e os
  itens só de viga: ligações de extremidade (liberação ou mola por esforço),
  excentricidades rígidas, base elástica, cargas distribuídas (inteiras ou
  parciais, uniformes ou trapezoidais), cargas concentradas no vão e
  gradiente de temperatura.
  *Ampliado na 008b:* vigas curvas em `Line3`, com a formulação na atribuição
  (`CurvedFormulation`: arco ou isoparamétrica) e torque distribuído nas
  cargas.
- **Análise estática** (`static_analysis.hpp`, feita na 007) — `validate`
  lista todos os problemas do modelo; `runLinearStatic` monta, verifica a
  diagonal (nó sem rigidez numa direção), fatora (equação singular →
  mecanismo com nó e direção, via `SparseCholesky::singularEquation`) e
  devolve deslocamentos e reações por nó e N, ε e σ por barra
  (`StaticResult`). Elemento: `TrussElement` 2D/3D (`truss_element.hpp`).
  *Ampliada na 008:* a validação mora em `model_validation.cpp` (privado,
  `detail/`), que também resolve cada viga (`beamSetup`); treliças e vigas
  são montadas juntas, e só nós de viga têm rotações. `BeamElement` 2D/3D
  (`beam_element.hpp`): fórmula fechada de Timoshenko com o parâmetro φ (φ = 0
  é Euler-Bernoulli), centro de cisalhamento, excentricidades, ligações
  condensadas no elemento e base elástica consistente, sem alocação.
  `beam_loads.hpp` leva as cargas do vão aos eixos principais e às cargas
  nodais equivalentes. O `StaticResult` traz rotações, momentos de reação e
  um `BeamResult` por viga (`beam_result.hpp`), com os esforços nas pontas,
  `at(x)` exato ao longo da barra e `sectionStresses`.
  *Ampliada na 008b:* um motor exato de viga por espaço de estados
  (`beam_state.hpp`: exponencial de matriz do Eigen, ADR 0006; partes
  condensadas para não perder precisão; base elástica exata; cargas fixas no
  espaço sobre arcos; `stateAt` em qualquer ponto) e duas formulações novas:
  `ExactBeamElement` (arco de circunferência pelos três nós, nó do meio sem
  GDL) e `IsoBeamElement` (`beam_iso.hpp`: quadrática com integração reduzida
  seletiva, nó do meio com GDL). Cada seção é resolvida uma vez por modelo.
  O empenamento (Vlasov) existe no motor e no elemento exato, sem acesso pelo
  `Model`: fica para a 008d.
- **`DofMap`** — GDL **variáveis por nó** (u, v, w, θx, θy, θz, T), definidos
  pelos elementos conectados; trata restrições e a compatibilização entre barras,
  cascas e sólidos (REQ-G-13). *Feito na 006:* máscara por nó vinda das
  `ElementFormulation`, numeração determinística, apoios e recalques por
  eliminação; vínculos multiponto ficam para as features 008 e 036.
- **`Assembler`** — monta matrizes esparsas e vetores; paralelo por coloração
  de elementos (oneTBB, ADR 0004). *Feito na 006:* padrão do triângulo
  inferior montado uma vez a partir dos GDL dos elementos e preenchido direto,
  cor por cor, sem travas; o resultado não depende do número de threads;
  reações por uma segunda passada.
- **`LinearSolver`** (interface) — `SparseCholesky` (Eigen `SimplicialLDLT`,
  reordenação AMD, pivôs verificados; feito na 006) e `ConjugateGradient`
  (pré-condicionador IC/diagonal, feature 025). A interface permite
  plugar solvers externos (ex.: PARDISO) sem tocar no restante.
- **`EigenSolver`** — problema generalizado `K φ = λ M φ` via Spectra (shift-invert).
- **`TimeIntegrator`** — Newmark (dinâmica) e θ-method (térmica transiente).
- **`Analysis`** (interface) — `StaticAnalysis`, `ThermalAnalysis`,
  `ModalAnalysis`, `TransientAnalysis`; orquestram montagem → solução →
  recuperação de resultados, reportam progresso e aceitam cancelamento por
  `std::stop_token`.
- **`ResultSet`** — campos nodais/elementares nomeados, por passo de tempo ou modo
  (deslocamento, tensão, von Mises, esforços de barra, temperatura, fluxo…).

## 3a. Geometria (`geom`)

Duas camadas (ADR-0001):

**Camada de desenho (própria)** — o que o usuário manipula, no estilo AutoCAD:

- `Drawing` — documento geométrico: entidades, camadas (`Layer`), planos de
  trabalho (`WorkPlane`/UCS).
- `Entity` — `Point`, `Line`, `Polyline`, `Circle`, `Arc`, `Ellipse`, `Spline`,
  `Region`, `Solid`. Cada uma tem **ID estável**: atribuições do modelo
  (material, seção, apoio, carga) referenciam o ID e sobrevivem a edições e à
  regeneração da malha.
- `SnapEngine` — snaps a objetos, grade, ortogonal e rastreamento polar; usa
  índice espacial para responder em tempo de movimento do mouse.
- `Command` (máquina de estados por comando: `LineCommand`, `TrimCommand`…) —
  recebe pontos, números e opções vindos do mouse **ou** da linha de comando; é
  independente de Qt e testável sem interface.
- `CommandParser` — nomes e atalhos, coordenadas absolutas, relativas e polares.

**Núcleo geométrico (OpenCASCADE)** — encapsulado, nunca exposto fora de `geom`:

- Curvas e superfícies exatas, interseções, offset, fillet/chamfer.
- Regiões, sólidos, extrusão, revolução, operações booleanas.
- Leitura/escrita STEP e IGES; reparo de geometria importada.
- Parametrização de curvas e superfícies consumida pelo `mesher`.

Os headers públicos de `geom` não incluem headers do OCCT (padrão pimpl), para
manter os tempos de compilação e permitir trocar o núcleo no futuro.

**Já implementado (feature 005):** `Drawing` com pontos, linhas e polilinhas
retas em `double` e IDs estáveis; `DrawingEdit` (entidades que saem e que
entram, com os IDs), base do desfazer; `parseInput` (comandos, opções,
coordenadas absolutas, relativas, polares e distância direta, com o ponto
como separador decimal em qualquer locale); `PointResolver` (snaps de
extremidade, meio e interseção 3D por força bruta, grade e ortogonal, a partir
de uma projeção de tela passada como matriz); `CommandSession` (POINT, LINE,
PLINE, MOVE, COPY, ERASE, um `DrawingEdit` por comando). Os pedidos e as
mensagens saem como IDs e são traduzidos no `app`. O OCCT ainda não é usado
pelo desenho (feature 015).

## 3b. Geração de malha (`mesher`)

| Gerador | Algoritmo |
|---------|-----------|
| `LineMesher` | Divisão uniforme ou graduada de curvas |
| `TransfiniteMesher` | Interpolação transfinita (quad/tri estruturado) |
| `DelaunayMesher2D` | Delaunay restrito + refinamento de Ruppert/Chew; predicados exatos |
| `QuadRecombiner` | Recombinação de triângulos em quadriláteros |
| `SurfaceMesher` | Delaunay no espaço paramétrico de superfícies curvas (faces de sólidos e CAD importado) |
| `SweepMesher` | Extrusão/revolução de malha 2D → Hex8/Wedge6 |
| `DelaunayMesher3D` | Delaunay restrito 3D com recuperação de fronteira e refinamento |
| `OrderElevator` | Linear → quadrática, com nós de meio de aresta sobre a curva |
| `QualityMetrics` | Razão de aspecto, jacobiano escalado, ângulos mínimo/máximo |

Todos produzem `core::Mesh` com grupos ligados às entidades de `geom`, reportam
progresso e aceitam cancelamento. `DelaunayMesher3D` é o componente de maior
risco técnico do projeto.

Erros: `std::expected`-like (`tl::expected` enquanto C++23 não for o piso) para
falhas esperadas (arquivo inválido, modelo singular); exceções apenas para bugs.

## 4. Renderização (`render`)

- **`Viewport`** (`QRhiWidget`) — dono do `QRhi`, do laço de render e dos eventos de entrada.
- **`Camera`** — órbita/pan/zoom (arcball), projeção perspectiva e ortográfica.
- **`MeshScene`** — montada fora da thread da interface a partir de `core::Mesh`
  e de `core::extractSurface`: triângulos, segmentos e pontos em `float`
  relativos ao centro da malha, faixas por dimensão e cores por dimensão ou
  grupo; `MeshGpu` envia-a à GPU (feature 004).
- **Passes**: faces sombreadas → arestas (wireframe sem z-fighting) →
  realce de seleção → sobreposições (eixos, legenda).
- **Resultados**: escalar por vértice + textura 1D de colormap, mapeada no
  fragment shader; deformada = posição + escala × deslocamento no vertex shader
  (sem reenviar a malha).
- **Seleção (picking)**: calculada na CPU pela `MeshPicker` — raio contra os
  triângulos visíveis no clique, projeção dos vértices em janela e cruzamento
  (ADR 0003, feature 004). Itens selecionáveis, combináveis: elementos, faces e
  arestas da casca externa dos sólidos e nós.
- **Geometria**: curvas e superfícies de `geom` tesseladas para exibição e seleção.
- **Barras e cascas**: linhas com opção de seção extrudada; cascas com espessura
  opcional; diagramas de esforços como geometria gerada sobre os elementos.
- **Elementos quadráticos**: faces subdivididas na geração dos buffers para
  mostrar a curvatura.
- **Animação**: modos e passos de tempo interpolados no vertex shader.
- Backends: Windows → D3D11 (D3D12 opcional); macOS → Metal; Linux → Vulkan,
  com fallback OpenGL. Sobreposição por variável de ambiente `FEM_RHI_BACKEND`.

## 5. Aplicação (`app`)

- **`MainWindow`** com docks: árvore do modelo, propriedades, console.
- **Documento/Comando**: toda edição do `Model` é um `QUndoCommand` (REQ-G-06).
  Para o desenho (feature 005), cada comando concluído vira um `QUndoCommand`
  que embrulha o `geom::DrawingEdit`, testado sem Qt em `geom`.
- **`CommandLineWidget`**: linha de comando estilo CAD com histórico e
  autocompletar; encaminha texto ao `geom::CommandParser`.
- **`DraftingController`**: liga eventos de mouse do viewport ao comando ativo e
  ao `PointResolver` (o `SnapEngine` desta seção); desenha pré-visualização
  elástica e marcadores de snap (grips na feature 017). O `Viewport` tem o modo
  "pedir ponto" e o alvo da seleção (desenho ou malha).
- **Painel de camadas** e barra de status com alternadores (snap, grade, orto, polar).
- **`MeshRunner`**: mesma mecânica do `AnalysisRunner`, para os geradores de malha.
- **`AnalysisRunner`**: executa `core::Analysis` em thread de trabalho; comunica
  progresso/conclusão por sinais Qt (conexão enfileirada). A UI nunca acessa o
  `Model` enquanto o solver o lê — o runner recebe um instantâneo imutável.
- Internacionalização com `tr()` + arquivos `.ts`.

## 6. Arquivos (`io`)

- `readGmsh` — `.msh` 4.1 ASCII e binário (parser próprio, sem SDK do Gmsh): nós,
  elementos dos tipos suportados e grupos físicos (feature 003).
- `ProjectFile` — JSON versionado (`"formatVersion"`), malha embutida ou referenciada.
- `StepIgesIo` — fachada sobre os leitores/escritores do OCCT (via `geom`).
- `DxfIo` — leitor/escritor DXF próprio (entidades 2D/3D básicas, camadas).
- `VtuWriter` — exportação de malha + resultados.

## 7. Estratégia de testes

| Nível | Onde | O que verifica |
|-------|------|----------------|
| Unitário | `tests/unit/` | funções de forma, quadratura, montagem, parsers |
| Verificação | `tests/verification/` | patch tests, taxas de convergência, benchmarks NAFEMS |
| Malha | `tests/unit/mesher/` | conformidade, propriedade de Delaunay, limites de qualidade, casos degenerados |
| Render | `tests/unit/render/` | lógica sem GPU (câmera, geração de buffers); smoke test com backend `Null` do QRhi |
| UI | manual + smoke test de inicialização | janela abre e fecha em cada plataforma no CI |

## 8. Riscos conhecidos

| Risco | Mitigação |
|-------|-----------|
| API do QRhi mudar entre versões do Qt | Fixar versão; isolar em `render` |
| Diferenças entre backends gráficos | CI nas 3 plataformas; evitar recursos opcionais do RHI |
| Desempenho do solver direto em 3D grande | Solver iterativo disponível desde o MVP |
| Precisão `float` na GPU em modelos grandes | Coordenadas relativas ao centro do modelo |
| Gerador de tetraedros próprio (recuperação de fronteira, slivers) | Entregar por último; varredura e importação `.msh` cobrem 3D até lá |
| Travamento (locking) em cascas e vigas | Formulações MITC / integração reduzida; benchmarks específicos |
| GDL heterogêneos (barra + casca + sólido) | `DofMap` com GDL por nó desde a primeira feature do núcleo |
| OCCT: build pesado, API extensa, geometria importada defeituosa | vcpkg com cache binário no CI; encapsular em `geom`; etapa de reparo na importação |
| Módulo de desenho CAD é um produto em si | Entregar em fatias: básico para barras (v0.2), completo depois (v0.3) |
| Escopo amplo da 1.0 | Versões intermediárias utilizáveis (roadmap) |
