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
| `core` | lib estática | STL, Eigen, Spectra, oneTBB, spdlog |
| `geom` | lib estática | STL, Eigen, OpenCASCADE |
| `mesher` | lib estática | `core` (apenas `Mesh`), `geom`, oneTBB |
| `io` | lib estática | `core`, `geom`, nlohmann/json |
| `render` | lib estática | `core`, `geom`, Qt Gui (RHI, headers privados), Qt Widgets (`QRhiWidget`); Qt Shader Tools no build — ver ADR 0002 |
| `app` | executável | todos, Qt Widgets |

## 3. Núcleo (`core`)

Conceitos principais:

- **`Mesh`** — coordenadas nodais (SoA, `double`), conectividade por bloco de
  tipo de elemento, grupos físicos (conjuntos nomeados de nós/arestas/faces/elementos).
- **`ShapeFunctions` / `Quadrature`** — por topologia (linha, triângulo,
  quadrilátero, tetraedro, hexaedro, cunha) e ordem (linear, quadrática);
  independentes da física.
- **`Element`** (interface por formulação, sem alocação por elemento) — declara
  os GDL que usa em cada nó e calcula rigidez `K`, massa `M`, capacidade/condutividade
  e forças locais. Formulações: treliça, viga, sólido estrutural, casca MITC,
  condução de calor.
- **`Material`** — propriedades (E, ν, ρ, k, c) e matriz constitutiva `D`.
- **`Section`** — seção de barra/viga (A, I, J, orientação) e espessura de casca.
- **`Model`** — malha + materiais + seções + atribuições + condições de contorno
  + cargas. É o documento editado pela UI.
- **`DofMap`** — GDL **variáveis por nó** (u, v, w, θx, θy, θz, T), definidos
  pelos elementos conectados; trata restrições e a compatibilização entre barras,
  cascas e sólidos (REQ-G-13).
- **`Assembler`** — monta matrizes esparsas (triplets → CSR) e vetores;
  paralelo por coloração de elementos (oneTBB).
- **`LinearSolver`** (interface) — `SparseCholesky` (Eigen `SimplicialLDLT`) e
  `ConjugateGradient` (pré-condicionador IC/diagonal). A interface permite
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
- **`MeshGpuData`** — converte `core::Mesh` em buffers de vértice/índice
  (somente a casca externa em 3D), em `float` relativo ao centro do modelo.
- **Passes**: faces sombreadas → arestas (wireframe sem z-fighting) →
  realce de seleção → sobreposições (eixos, legenda).
- **Resultados**: escalar por vértice + textura 1D de colormap, mapeada no
  fragment shader; deformada = posição + escala × deslocamento no vertex shader
  (sem reenviar a malha).
- **Seleção (picking)**: passe offscreen com ID codificado em cor.
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
- **`CommandLineWidget`**: linha de comando estilo CAD com histórico e
  autocompletar; encaminha texto ao `geom::CommandParser`.
- **`DraftingController`**: liga eventos de mouse do viewport ao comando ativo e
  ao `SnapEngine`; desenha pré-visualização elástica, marcadores de snap e grips.
- **Painel de camadas** e barra de status com alternadores (snap, grade, orto, polar).
- **`MeshRunner`**: mesma mecânica do `AnalysisRunner`, para os geradores de malha.
- **`AnalysisRunner`**: executa `core::Analysis` em thread de trabalho; comunica
  progresso/conclusão por sinais Qt (conexão enfileirada). A UI nunca acessa o
  `Model` enquanto o solver o lê — o runner recebe um instantâneo imutável.
- Internacionalização com `tr()` + arquivos `.ts`.

## 6. Arquivos (`io`)

- `GmshReader` — `.msh` 4.1 ASCII e binário (parser próprio, sem SDK do Gmsh).
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
