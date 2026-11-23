# Design — 009 line-meshing

Status: aprovado · Data: 2026-11-22
Requisitos: [requirements.md](requirements.md)

## 1. Resumo da abordagem

`fem::mesher::meshLines` gera a malha de barras do desenho em seis etapas:

1. coleta os trechos retos e os pontos das entidades (`geom::segmentsOf`),
   descartando os de comprimento menor que a tolerância;
2. acha os contatos entre trechos e entre pontos e trechos com uma **BVH**
   das caixas dos trechos (consultas em paralelo com oneTBB, pares
   ordenados para o resultado não depender das threads);
3. classifica cada contato pela distância entre segmentos 3D (sobreposição
   → erro; nó em T, cruzamento, ponto sobre trecho → divisão);
4. funde todos os nós candidatos (pontas, vértices, pontos, divisões) por
   uma grade de hash com célula igual à tolerância e union-find,
   representante o primeiro em ordem;
5. divide cada trecho entre nós obrigatórios conforme a contagem, o
   comprimento alvo e a graduação da entidade, em `Line2` ou `Line3`;
6. numera nós e elementos na ordem das entidades e monta a `core::Mesh` com
   um grupo por entidade, junto com o mapa para a geometria e o relatório.

Erros e relatório saem como dados (IDs, entidades, posições). O `app` os
escreve com `tr()`, como a 005 faz com as mensagens dos comandos.

No `app`:

- o `DraftingController` reconhece `MESH`/`ME` antes de `geom::findCommand`
  e conduz um pedido de tamanho;
- um `LineMeshJob` (como o `MeshImport` da 004) copia o desenho, gera a
  malha e a cena numa thread de trabalho e devolve o resultado na thread
  da interface;
- o viewport mostra a malha com o desenho, e o console recebe o relatório;
- a revisão do desenho no momento da malha permite avisar que ela ficou
  desatualizada.

## 2. Verificação da constituição

| Princípio | Atende? | Observação |
|-----------|---------|------------|
| Multiplataforma (§2) | Sim | C++20, Eigen e oneTBB; nada de plataforma. Resultado determinístico também entre plataformas: as decisões geométricas usam comparações com a tolerância, nunca igualdade exata. |
| GPU somente via QRhi (§3) | Sim | A malha chega ao viewport pela `MeshScene` existente (004). |
| Camadas / `core` sem Qt (§4) | Sim | `mesher` sem Qt, dependendo de `core` e `geom` (REQ-009-17); `check_no_qt_includes` já cobre `mesher`. O comando fica no `app`, a geração roda fora da thread da interface (§4.3). |
| Correção numérica (§5) | Sim (malha) | §7.4 da constituição: testes de conformidade (nós compartilhados, sem duplicados), casos degenerados (trechos nulos, colineares, quase tocando, reversas, translação e escala) e o pórtico analisado igual ao montado à mão. |
| Simplicidade / dependências (§7) | Sim | Nenhuma dependência nova. A BVH é pequena e própria (cerca de 150 linhas), não uma biblioteca de geometria. O OCCT não é usado: o desenho ainda é reto (015 traz as curvas). |

## 3. Módulos e arquivos afetados

| Módulo | Arquivo | Novo/Alterado | Responsabilidade |
|--------|---------|---------------|------------------|
| mesher | `include/fem/mesher/line_mesher.hpp` | novo | `LineSizing`, `LineMeshOptions`, `LineMeshResult`, mapa, relatório, erro; `meshLines`, `entityGroupName` (REQ-009-01 a 11) |
| mesher | `line_mesher.cpp` | novo | As seis etapas da §1 |
| mesher | `detail/segment_bvh.hpp`, `segment_bvh.cpp` | novo | BVH estática de caixas, construção por mediana, consulta de pares (NFR-009-01) |
| mesher | `detail/segment_geometry.hpp`, `segment_geometry.cpp` | novo | Distância entre segmentos 3D, ponto-segmento, classificação do contato (REQ-009-03, 04; NFR-009-02) |
| mesher | `module.cpp`, `include/fem/mesher/module.hpp` | removido | O marcador de lugar da 001 sai, com o seu teste |
| mesher | `CMakeLists.txt` | alterado | Fontes novas; oneTBB privado (ADR 0004) |
| app | `line_mesh_job.hpp`, `line_mesh_job.cpp` | novo | Geração e cena numa thread de trabalho; sinais `finished`/`failed` (REQ-009-13) |
| app | `drafting_controller.hpp`, `drafting_controller.cpp` | alterado | `MESH`/`ME`, o pedido de tamanho, Esc, o aviso de desatualizada, o Tab completando MESH (REQ-009-12, 15) |
| app | `main_window.hpp`, `main_window.cpp` | alterado | Mostrar a malha gerada (mesma rota do `meshImported`), o relatório e as falhas no console (REQ-009-13, 14) |
| app | `i18n/femapp_pt_BR.ts` | adiado | Tradução dos textos novos (REQ-009-16): adiada pelo mantenedor em 2026-11-22; os textos ficam em inglês por `tr()` |
| tests | `tests/unit/mesher/test_line_mesher.cpp`, `test_segment_geometry.cpp`, `test_segment_bvh.cpp` | novo | AC-1 a AC-5, NFR-009-02 |
| tests | `tests/CMakeLists.txt` | alterado | `femapp_add_test(mesher ...)` |
| tests | `tests/perf/bench_line_mesh.cpp` | novo | NFR-009-01 |
| ci | `.github/workflows/ci.yml` | alterado | Cobertura de `src/mesher/` ≥ 80%, num segundo `gcovr` (NFR-009-03); `cmake/CheckNoQtIncludes.cmake` já cobre `mesher` |

## 4. Interfaces públicas

```cpp
// line_mesher.hpp
#pragma once
#include <fem/core/mesh.hpp>
#include <fem/geom/drawing.hpp>
#include <tl/expected.hpp>

namespace fem::mesher {

// REQ-009-06: where a geometric grading makes the elements grow.
enum class Grading : std::uint8_t {
    TowardEnd, // the last element is `ratio` times the first (from start to end)
    TowardBoth // symmetric: the middle element is `ratio` times the end ones
};

// REQ-009-05 to 07: how the pieces of an entity are divided. Without
// divisions or length: one element per piece.
struct LineSizing
{
    std::optional<int> divisions;   // per piece, >= 1
    std::optional<double> length;   // target length, > 0 (divisions win if both)
    double ratio = 1.0;             // > 0; 1 is uniform
    Grading grading = Grading::TowardEnd;
    bool quadratic = false;         // REQ-009-07: Line3
};

struct LineMeshOptions
{
    LineSizing defaults{};
    // REQ-009-06: per entity, over the defaults (by the value of the id).
    std::unordered_map<std::uint64_t, LineSizing> sizing;
    // REQ-009-01: only these entities (all when empty).
    std::vector<geom::EntityId> entities;
    int threads = 0; // 0: all cores
};

// REQ-009-09: "entity <id>", the name of the group of an entity.
[[nodiscard]] std::string entityGroupName(geom::EntityId id);

// REQ-009-10
struct ElementOrigin
{
    geom::EntityId entity{};
    int segment = 0; // index in geom::segmentsOf(entity); 0 for a point
};
struct NodeOrigin
{
    std::vector<geom::EntityId> entities; // through the node, in drawing order
    // The vertex (geom::verticesOf index) or the point it comes from.
    std::optional<std::pair<geom::EntityId, int>> vertex;
};
struct LineMeshMap
{
    std::vector<ElementOrigin> elements; // per element (internal index)
    std::vector<NodeOrigin> nodes;       // per node
};

// REQ-009-11
enum class SplitKind : std::uint8_t { TJoint, Crossing, PointOnLine };
struct Split
{
    SplitKind kind = SplitKind::Crossing;
    geom::Vec3 position = geom::Vec3::Zero();
    std::vector<geom::EntityId> entities; // the ones split, then the one causing it
};
struct LineMeshReport
{
    int nodes = 0;
    int elements = 0;
    std::vector<Split> splits;
    int mergedNodes = 0; // candidates merged into another
    std::vector<std::pair<geom::EntityId, int>> ignored; // (entity, segment) shorter than tol
};

struct LineMeshResult
{
    core::Mesh mesh;
    LineMeshMap map;
    LineMeshReport report;
};

// REQ-009-04, 14
struct Overlap
{
    geom::EntityId first{};
    geom::EntityId second{};
    geom::Vec3 from = geom::Vec3::Zero();
    geom::Vec3 to = geom::Vec3::Zero();
};
struct LineMeshError
{
    enum class Kind : std::uint8_t { NoLines, Overlaps, InvalidSizing, UnknownEntity };
    Kind kind = Kind::NoLines;
    std::vector<Overlap> overlaps;      // Overlaps: all of them
    std::vector<geom::EntityId> items;  // InvalidSizing, UnknownEntity
};

// REQ-009-01 to 11.
[[nodiscard]] tl::expected<LineMeshResult, LineMeshError>
meshLines(const geom::Drawing& drawing, const LineMeshOptions& options = {});

} // namespace fem::mesher
```

```cpp
// app/line_mesh_job.hpp
class LineMeshJob : public QObject
{
    Q_OBJECT
public:
    // Copies the drawing; ignored while another job runs.
    void start(const geom::Drawing& drawing, mesher::LineMeshOptions options);
    [[nodiscard]] bool isRunning() const noexcept;
signals:
    void finished(std::shared_ptr<const render::MeshScene> scene, mesher::LineMeshReport report,
                  std::uint64_t revision);
    void failed(mesher::LineMeshError error);
};
```

## 5. Dados e algoritmos

### 5.1 Coleta (REQ-009-01, 04)

- As entidades são percorridas na ordem do desenho, ou só as dadas: um id
  inexistente é erro `UnknownEntity`.
- Cada trecho guarda (entidade, índice, a, b, comprimento). Um trecho com
  ‖b − a‖ ≤ tol vai para `ignored`, mas as suas pontas continuam como nós
  candidatos, porque um vértice continua sendo nó.
- Os pontos guardam (entidade, posição).
- **Sem trechos:** erro `NoLines`, mesmo com pontos.
- **`LineSizing` inválido** (divisões < 1, comprimento ≤ 0, razão ≤ 0 ou
  não finita): erro `InvalidSizing` com as entidades.

### 5.2 Busca de contatos (NFR-009-01)

- **Construção da BVH:**
  - folhas com até 4 trechos; caixas inflando cada trecho por tol;
  - divisão pela mediana dos centros no maior eixo, com
    `std::nth_element`;
  - construção sequencial, O(n log n), cerca de 20 ms para 100 mil
    trechos.
- **Consulta:** cada trecho i consulta a árvore e guarda os j > i cujas
  caixas tocam a sua. As consultas rodam em paralelo (`tbb::parallel_for`)
  em listas por trecho, juntadas e ordenadas por (i, j), então o resultado
  não depende das threads (REQ-009-08).
- **Pontos:** consultam a mesma árvore com a caixa do ponto.
- **Pares de trechos vizinhos da mesma polilinha** (que compartilham um
  vértice) passam pela classificação como os outros: o toque nas pontas
  não divide nada, e uma volta colinear sobre si mesma é sobreposição.

### 5.3 Classificação (REQ-009-03, 04; NFR-009-02)

- **Contas em coordenadas relativas:** todos os cálculos de um par são
  feitos a partir de a₁ (os pontos subtraídos de a₁ antes de qualquer
  produto). Assim, a precisão não depende da distância à origem.
- **Segmentos paralelos:** quando |d₁ × d₂| ≤ tol·max(‖d₁‖, ‖d₂‖), os
  segmentos são paralelos.
  - Se a distância entre as retas for ≤ tol e as projeções se sobrepuserem
    por mais que tol: `Overlap` com o pedaço comum.
  - Senão, só toques de pontas, já cobertos pela fusão.
- **Não paralelos:** os pontos mais próximos p₁ = a₁ + s·d₁ e
  p₂ = a₂ + t·d₂, com s e t presos a [0, 1] (Ericson, "Real-Time Collision
  Detection", §5.1.9). Se ‖p₁ − p₂‖ ≤ tol, há contato:
  - **interior de ambos** (s·L₁ > tol, (1 − s)·L₁ > tol, idem t): cruzamento;
    o nó fica em ½(p₁ + p₂), e os dois trechos se dividem em s e em t;
  - **ponta de um no interior do outro:** nó em T; o trecho atingido se
    divide no parâmetro da projeção, e o nó é a própria ponta (a fusão
    junta);
  - **pontas com pontas:** nada, a fusão cuida.
- **Ponto e trecho:** se a projeção do ponto cair no interior e a distância
  for ≤ tol, o trecho se divide e o nó é o ponto.
- **Retas reversas** a uma distância maior que tol não se ligam (AC-2).
- **Tolerância:** `Drawing::tolerance()` = 1e−9·max(1, diagonal).
  - Com a translação, a diagonal e a tolerância não mudam.
  - Com uma escala, a tolerância acompanha enquanto a diagonal for ≥ 1.
  - NFR-009-02 é verificado com fatores 1, 1e3 e 1e6 sobre um desenho de
    diagonal ≥ 1, e com a translação de (1e6, −1e6, 1e6). Abaixo de
    diagonal 1, a tolerância é absoluta: isso fica documentado no teste.

### 5.4 Fusão de nós (REQ-009-02)

- **Candidatos, em ordem:** para cada entidade na ordem do desenho, as
  pontas e os vértices (ou o ponto); depois, as divisões da §5.3, em ordem
  do par.
- **Fusão:**
  - grade de hash com célula = tol;
  - cada candidato procura nas 27 células vizinhas um representante a
    distância ≤ tol;
  - o union-find une, e o representante é o candidato de menor ordem, com a
    posição dele (a desenhada, não uma média);
  - `mergedNodes` conta os candidatos unidos a outro, fora o representante.
- **Cadeias:** a fusão é transitiva. Três candidatos em fila, cada um a
  menos de tol do vizinho, viram um só. Com tol de 1e−9 da diagonal, isso
  só acontece com pontos que o usuário quis no mesmo lugar.

### 5.5 Trechos e divisão (REQ-009-05 a 07)

- **Trechos entre nós obrigatórios:** os parâmetros de divisão de cada
  trecho (0, 1 e as divisões) são ordenados, e cada um é levado ao seu nó
  pela fusão. Parâmetros cujos nós se fundiram viram um só. Os trechos
  ficam entre nós obrigatórios consecutivos.
- **Número de elementos:**
  - `divisions` ou, sem ele, n = max(1, ⌈L/h − 1e−9⌉) para o comprimento
    alvo h; o −1e−9 faz L/h = 3 + 1e−12 dar 3;
  - sem nenhum dos dois, n = 1.
- **Comprimentos, com razão r:**
  - **TowardEnd:** q = r^(1/(n−1)) e ℓₖ ∝ qᵏ, k = 0…n−1, de modo que
    ℓ_{n−1}/ℓ₀ = r;
  - **TowardBoth:** os elementos crescem das duas pontas para o meio
    (n par: duas metades espelhadas de n/2; n ímpar: o do meio é o maior),
    com a razão entre o maior e o menor igual a r;
  - os comprimentos são normalizados, e a última posição é a ponta exata,
    então a soma dá o trecho em 1e−12 relativo.
- **Direção:** a graduação segue a direção da entidade (do primeiro ao
  último vértice), não a do trecho dividido. TowardEnd numa linha cortada
  ao meio continua crescendo para a ponta final em cada parte.
- **`Line3`:** o nó do meio fica no ponto médio do elemento sobre o trecho
  e não se funde com nada (é interior).

### 5.6 Numeração, grupos e mapa (REQ-009-08 a 10)

- **Ordem:** nós e elementos recebem IDs externos 1, 2, 3, … na ordem em
  que aparecem ao percorrer as entidades na ordem do desenho, trecho a
  trecho e do início ao fim. Um nó já numerado não ganha outro id. Um
  ponto gera um elemento `Point1`, na posição dele na ordem.
- **Construção:** a malha é montada pelo `MeshBuilder` (nós, depois
  elementos, depois grupos).
- **Grupos:** um por entidade, `entityGroupName(id)` = "entity <id>".
  Linhas e polilinhas têm dimensão 1; pontos, dimensão 0. O nome só
  depende do id, então mover a entidade mantém o nome (AC-5).
- **Mapa:** `elements[e]` dá a entidade e o trecho; `nodes[n]` dá as
  entidades em ordem e o vértice ou ponto de origem, se houver (as
  divisões internas e os nós do meio não têm).

### 5.7 Comando MESH (REQ-009-12 a 16)

- **`DraftingController::enterText`:** sem sessão ativa, `MESH` ou `ME`
  (qualquer caixa) inicia o estado `meshing` do controlador, antes de
  `geom::findCommand`. Nenhum comando da 005 usa esses nomes (`M` é MOVE).
  - O pedido é "Element size or <one per piece>:".
  - Um número > 0 vira o comprimento alvo global; Enter, o padrão.
  - Esc cancela. Outra entrada mostra "Invalid size." e repete o pedido.
- **Tab:** completa MESH junto com os nomes da 005.
- **Execução:** o `LineMeshJob` copia o `geom::Drawing` (tipo valor) e a
  revisão. Numa thread de trabalho (`QThreadPool`, como o `MeshImport`),
  chama `meshLines` e `render::buildMeshScene` e emite `finished` ou
  `failed` na thread do objeto. Um segundo MESH durante a geração mostra
  "Meshing is already running.".
- **`finished`:**
  - `m_viewport->setScene(scene)`, com o desenho continuando visível;
  - o rótulo de informação da malha, como na 004;
  - o relatório no console: "Mesh: N nodes, E elements; S splits (…);
    M nodes merged; I pieces ignored", e uma linha por divisão até 20,
    com "… and K more";
  - guarda a revisão.
- **`failed`:** o console recebe "Could not mesh the drawing: …", com cada
  sobreposição ("entities 12 and 15 overlap from (x, y, z) to (x, y, z)")
  ou "the drawing has no lines". A malha mostrada não muda.
- **Desatualizada:** no sinal de mudança do desenho, se a revisão difere da
  guardada e o aviso ainda não foi dado, o console recebe "The mesh shown
  is out of date: run MESH again.". O aviso volta a valer depois de outro
  MESH.
- **Desfazer:** o MESH não gera `DrawingEdit`, então o desfazer não o vê.
- **Textos:** todos por `tr()` no `app`, em inglês por ora (a tradução
  pt_BR foi adiada pelo mantenedor em 2026-11-22). Os números
  saem com o ponto decimal, como as coordenadas da 005.

## 6. Fluxo

```
usuário: MESH ⏎  → DraftingController (estado meshing) → pedido de tamanho
usuário: 0.5 ⏎  → LineMeshJob::start(cópia do desenho, opções)
                      thread de trabalho:
                        meshLines → coleta → BVH (TBB) → classificação
                                  → fusão → divisão → numeração → Mesh
                        buildMeshScene(mesh)
                  ← finished(scene, report, revision) (thread da interface)
MainWindow: viewport->setScene; console: relatório
edição do desenho → DraftingController: revisão ≠ → console: desatualizada (uma vez)
```

## 7. Tratamento de erros

| Situação | Comportamento | Requisito |
|----------|---------------|-----------|
| Desenho sem linhas (ou subconjunto sem linhas) | `NoLines`; console "Could not mesh the drawing: the drawing has no lines."; malha anterior mantida | REQ-009-14 |
| Trechos sobrepostos | `Overlaps` com todos os pares e pedaços; console lista cada um | REQ-009-04, 14 |
| Trecho menor que a tolerância | Ignorado, listado no relatório | REQ-009-04 |
| `LineSizing` inválido (API) | `InvalidSizing` com as entidades | REQ-009-06 |
| Id do subconjunto inexistente (API) | `UnknownEntity` com os ids | REQ-009-01 |
| Tamanho digitado inválido (≤ 0, texto) | "Invalid size." e o pedido de novo | REQ-009-12 |
| MESH durante uma geração | "Meshing is already running." | REQ-009-13 |
| `MeshBuilder` recusando algo | Erro de programação (os dados vêm do gerador): `assert` em Debug e a mensagem do builder no console | — |

## 8. Estratégia de testes

| Teste | Tipo | Requisito | Referência/tolerância |
|-------|------|-----------|-----------------------|
| Distância entre segmentos 3D: cruzamento, T, paralelos, colineares com e sem pedaço comum, reversas, degenerados | unitário | REQ-009-03, 04 | contra as contas à mão, 1e−12 relativo |
| Translação (1e6) e escala (1, 1e3, 1e6) dos casos acima | unitário | NFR-009-02 | mesma classificação e mesmos nós (relativos) |
| BVH: pares iguais aos da força bruta em nuvens aleatórias (semente fixa) com 1, 2 e todas as threads | unitário | NFR-009-01, REQ-009-08 | iguais |
| Pórtico 2×2: contagens, IDs, nós fundidos; análise linear com as atribuições por grupo igual ao pórtico montado à mão | verificação | REQ-009-01, 02, 05, 08, 09 | deslocamentos em 1e−12 relativo |
| Ligações (T, cruzamento, vértice, ponto sobre viga, cruzamento 3D, reversas) e o relatório | unitário | REQ-009-03, 11 | nós e divisões exatos |
| Sobreposição, trecho nulo, sem linhas, sizing inválido, id inexistente | unitário | REQ-009-04, 06, 14 | erros com os itens |
| Divisões, comprimento alvo (L/h inteiro e 3 + 1e−12), graduação TowardEnd e TowardBoth (n par e ímpar), padrão sobreposto por entidade, `Line3` | unitário | REQ-009-05 a 07 | razões e soma em 1e−12 |
| Grupos e mapa; mover uma entidade e gerar de novo | unitário | REQ-009-09, 10 | nomes iguais |
| Determinismo: 1 thread contra todas, malha inteira comparada | unitário | REQ-009-08 | idênticas |
| Roteiro manual do MESH (passos em `tasks.md`) | manual | REQ-009-12 a 16 | o roteiro |
| `bench_line_mesh`: grade 3D de pilares, vigas e diagonais com cruzamentos, cerca de 100 mil trechos | benchmark | NFR-009-01 | ≤ 2 s Release |

## 9. Alternativas consideradas

| Alternativa | Por que foi descartada |
|-------------|------------------------|
| Grade uniforme para os pares | Simples, mas sofre com desenhos de escalas misturadas (barras longas com detalhes curtos): a célula boa para uns é ruim para os outros. A BVH não depende da distribuição. |
| Varredura (sweep and prune) num eixo | Degrada em pórticos com muitas barras alinhadas no eixo da varredura (todos os pilares têm o mesmo intervalo em x e y). |
| Interseções pelo OCCT (`BRepAlgoAPI_Section`, general fuse) | Pesado para segmentos retos, ainda não ligado ao desenho (015) e sem a tolerância do desenho. Vale reconsiderar com as curvas na 016. |
| Predicados exatos (Shewchuk) | Decidem orientação no plano, não "a menos de tol" em 3D. A decisão aqui é com tolerância, e as contas relativas bastam (NFR-009-02). |
| Posição do nó fundido como média | Mudaria as coordenadas desenhadas e dependeria da ordem de fusão. O representante de menor ordem é determinístico e preserva o desenho. |
| MESH como `CommandName` em `geom` | `geom` não pode depender de `mesher` (ciclo). O comando é do `app`, como pede o REQ-009-17. |
| Recusar o MESH com malha importada na tela | A malha mostrada é só uma, e o MESH a substitui como uma nova importação substituiria; não há conflito de dados. |

## 10. Riscos e mitigação

- **Divisões em cadeia** (um cruzamento perto de uma ponta, a menos de tol
  de ser T) → a classificação usa as mesmas distâncias com tol em todos os
  testes de interior. A fusão final junta qualquer nó a menos de tol de
  outro, então a malha continua conforme. Há testes de quase-T.
- **Muitos contatos num ponto** (vários pilares e vigas num nó) → cada par
  gera no máximo um nó candidato, e a fusão os reduz a um. O custo é
  O(pares), limitado pela BVH.
- **Tamanho do relatório** em desenhos grandes → o relatório completo fica
  na API, e o console mostra 20 linhas e o total.
- **Cobertura de `mesher` no CI** passa a ser exigida → o módulo só tem
  este código, e todos os ramos de erro têm teste.

## 11. Novas dependências

Nenhuma. oneTBB já está aprovado para `mesher` (arquitetura, tabela de
módulos; ADR 0004), e Eigen e `tl::expected` já são usados pelos módulos
de que ele depende.
