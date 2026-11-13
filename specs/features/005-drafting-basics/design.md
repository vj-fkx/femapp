# Design — 005 drafting-basics

Status: aprovado · Data: 2026-11-12
Requisitos: [requirements.md](requirements.md)

## 1. Resumo da abordagem

Três camadas, como na arquitetura (§3a e §5):

1. **`geom` (sem Qt, sem GPU):** o documento `Drawing` com entidades de ID
   estável; `DrawingEdit`, um registro reversível de cada operação (base do
   desfazer); a interpretação da linha de comando (`parseInput`); o
   `PointResolver` (snaps, grade e ortogonal, a partir de uma projeção de tela
   passada como matriz, sem depender de `render`); e as máquinas de estado dos
   comandos (`Command` e `CommandSession`). Tudo testável sem interface.
2. **`render`:** `DrawingGpu` desenha as entidades, o realce e a
   pré-visualização com os shaders de linha e ponto da 004; `DrawingPicker`
   faz clique, janela e cruzamento sobre as entidades; o `Viewport` ganha o
   alvo da seleção (Desenho/Malha) e um modo "pedir ponto", em que o mouse
   alimenta o comando em vez de selecionar.
3. **`app`:** `CommandLineWidget` (dock com histórico e entrada),
   `DraftingController` (liga linha de comando, viewport, `CommandSession`,
   barra de status e `QUndoStack`) e `DraftingSettingsDialog`.

O OpenCASCADE não é usado nesta feature (entra na 015); as entidades são
segmentos retos em `double`, simples o bastante para a geometria própria.

## 2. Verificação da constituição

| Princípio | Atende? | Observação |
|-----------|---------|------------|
| Multiplataforma (§2) | Sim | Só C++20, Eigen e Qt Widgets; atalhos F3/F8/F9 e Ctrl+Z/Y existem nas três plataformas (no macOS, Cmd+Z pelo `QKeySequence::Undo`) |
| GPU somente via QRhi (§3) | Sim | `DrawingGpu` usa os pipelines de linha e ponto já existentes, em `render` |
| Camadas / `core` sem Qt (§4) | Sim | Documento, comandos, interpretação e snaps em `geom`, sem Qt; os pedidos são IDs traduzidos em `app` com `tr()` |
| Correção numérica (§5) | Sim | Coordenadas em `double`; GPU com posições relativas a uma origem e deslocamento calculado em `double` (como na 004); tolerância de interseção relativa ao tamanho do desenho, documentada nos testes |
| Simplicidade / dependências (§7) | Sim | Nenhuma dependência nova; busca de snap por força bruta (meta medida); `QUndoStack` do Qt Gui |

## 3. Módulos e arquivos afetados

| Módulo | Arquivo | Novo/Alterado | Responsabilidade |
|--------|---------|---------------|------------------|
| geom | `include/fem/geom/drawing.hpp`, `drawing.cpp` | novo | `EntityId`, entidades, `Drawing`, `DrawingEdit` |
| geom | `include/fem/geom/command_input.hpp`, `command_input.cpp` | novo | `parseInput`: comandos, opções, coordenadas, números |
| geom | `include/fem/geom/point_resolver.hpp`, `point_resolver.cpp` | novo | Snaps, grade, ortogonal |
| geom | `include/fem/geom/commands.hpp`, `commands.cpp` | novo | `Command`, POINT, LINE, PLINE, MOVE, COPY, ERASE, `CommandSession` |
| render | `include/fem/render/drawing_picker.hpp`, `drawing_picker.cpp` | novo | Clique, janela e cruzamento sobre entidades |
| render | `drawing_gpu.hpp/.cpp` (privados) | novo | Buffers de entidades, realce e pré-visualização |
| render | `pick_util.hpp` (privado) | novo | `Projector` e auxiliares 2D, extraídos de `mesh_picker.cpp` |
| render | `viewport.hpp/.cpp`, `scene_renderer.cpp`, `selection_overlay.hpp/.cpp` | alterado | Desenho na cena, alvo da seleção, modo pedir ponto, marcador de snap |
| app | `command_line_widget.hpp/.cpp`, `drafting_controller.hpp/.cpp`, `drafting_settings_dialog.hpp/.cpp` | novo | Linha de comando, controle do desenho, configurações |
| app | `main_window.hpp/.cpp`, `i18n/femapp_pt_BR.ts` | alterado | Dock, menus, barra de status, atalhos, textos |
| tests | `unit/geom/test_drawing.cpp`, `test_command_input.cpp`, `test_point_resolver.cpp`, `test_commands.cpp`; `unit/render/test_drawing_picker.cpp`; `perf/bench_drafting.cpp` | novo | Testes sem GPU e benchmark |

## 4. Interfaces públicas

```cpp
// fem/geom/drawing.hpp — REQ-005-01, 02, 31, 32
namespace fem::geom {

using Vec3 = Eigen::Vector3d;

enum class EntityId : std::uint64_t {}; // 0 is never used

struct PointEntity    { Vec3 position; };
struct LineEntity     { Vec3 start; Vec3 end; };
struct PolylineEntity { std::vector<Vec3> vertices; bool closed = false; }; // >= 2 vertices
using EntityShape = std::variant<PointEntity, LineEntity, PolylineEntity>;

struct Entity
{
    EntityId id;
    EntityShape shape;
};

struct Segment { Vec3 a; Vec3 b; EntityId entity; }; // straight pieces of an entity

// The drawing document. Entities keep their id for ever; ids are never reused.
class Drawing
{
public:
    [[nodiscard]] EntityId nextId() noexcept;                // reserves a fresh id
    [[nodiscard]] const Entity* find(EntityId id) const;
    [[nodiscard]] std::span<const Entity> entities() const;  // in insertion order
    [[nodiscard]] std::uint64_t revision() const noexcept;   // changes on every edit
    [[nodiscard]] std::optional<core::BoundingBox> bounds() const;
    [[nodiscard]] double tolerance() const;                  // 1e-9 x max(1, diagonal)

    void apply(const DrawingEdit& edit);   // removes then inserts
    void revert(const DrawingEdit& edit);  // the inverse
};

// One reversible change: entities taken out and entities put in (an entity
// moved is taken out with its old shape and put in with the new one, same id).
struct DrawingEdit
{
    std::vector<Entity> removed;
    std::vector<Entity> added;
    [[nodiscard]] bool empty() const noexcept;
    void append(const DrawingEdit& next); // merges, for one undo step per command
};

[[nodiscard]] std::vector<Segment> segmentsOf(const Entity& entity);
[[nodiscard]] Entity translated(const Entity& entity, const Vec3& by, EntityId id);

} // namespace fem::geom
```

```cpp
// fem/geom/command_input.hpp — REQ-005-05, 08, 10 a 13
namespace fem::geom {

enum class CommandName : std::uint8_t { Point, Line, Pline, Move, Copy, Erase, Undo, Redo };

struct AbsolutePoint { Vec3 point; };               // x,y[,z]
struct RelativePoint { Vec3 delta; };               // @dx,dy[,dz]
struct PolarPoint    { double distance; double angleDeg; bool relative; }; // [@]d<a
struct Distance      { double value; };             // direct distance entry
struct Keyword       { std::string text; };         // option or command candidate
struct Empty {};                                    // Enter / Space on an empty line
using ParsedInput = std::variant<Empty, AbsolutePoint, RelativePoint, PolarPoint, Distance,
                                 Keyword>;

// Text typed on the command line; std::nullopt if malformed (REQ-005-08).
[[nodiscard]] std::optional<ParsedInput> parseInput(std::string_view text);
// Name or alias, any case (REQ-005-05).
[[nodiscard]] std::optional<CommandName> findCommand(std::string_view text);
// Completion for Tab: the first command whose name starts with `prefix`.
[[nodiscard]] std::optional<std::string_view> completeCommand(std::string_view prefix);
// Turns a parsed point (or a distance along `direction`) into a point.
[[nodiscard]] std::optional<Vec3> resolvePoint(const ParsedInput& input,
                                               const std::optional<Vec3>& last,
                                               const Vec3& direction);

} // namespace fem::geom
```

```cpp
// fem/geom/point_resolver.hpp — REQ-005-14, 19, 22, 23
namespace fem::geom {

enum class SnapMode : std::uint8_t { Endpoint, Midpoint, Intersection };

struct DraftingSettings
{
    bool objectSnap = true;                        // F3
    std::array<bool, 3> modes = {true, true, true}; // per SnapMode
    bool gridSnap = false;                         // F9
    double gridStep = 1.0;
    bool ortho = false;                            // F8
    double aperturePx = 10.0;
};

// What the resolver needs from the screen, without depending on render.
struct ScreenView
{
    Eigen::Matrix4d viewProjection; // world -> clip (OpenGL convention)
    double width;
    double height;
    Vec3 rayOrigin;                 // the cursor ray
    Vec3 rayDirection;
    Eigen::Vector2d cursor;         // pixels
};

struct ResolvedPoint
{
    Vec3 point;
    std::optional<SnapMode> snap; // set when an object snap was taken
};

class PointResolver
{
public:
    // Segments of the drawing are cached per drawing revision.
    [[nodiscard]] std::optional<ResolvedPoint>
    resolve(const Drawing& drawing, const ScreenView& view, const DraftingSettings& settings,
            const std::optional<Vec3>& last) const;
};

} // namespace fem::geom
```

```cpp
// fem/geom/commands.hpp — REQ-005-06, 07, 15 a 18, 27 a 31
namespace fem::geom {

enum class PromptId : std::uint8_t {
    Command, FirstPoint, NextPoint, PointOrEnter, SelectObjects, BasePoint,
    SecondPoint, SecondPointOrEnter
};
enum class OptionId : std::uint8_t { Undo, Close };            // keywords U, C
enum class MessageId : std::uint8_t {
    InvalidInput, UnknownCommand, NothingSelected, NeedTwoSegments, NothingToUndo,
    EntitiesCreated, EntitiesMoved, EntitiesCopied, EntitiesErased
};

struct Prompt { PromptId id; std::vector<OptionId> options; };
struct Message { MessageId id; int count = 0; };

// Elastic line and ghost entities to show while a command runs (REQ-005-18).
struct Preview
{
    std::optional<std::pair<Vec3, Vec3>> rubberBand; // from the last point to the cursor
    std::vector<Entity> ghosts;                       // moved or copied entities
};

// Drives one command at a time. Commands change `drawing` as they go and
// report each finished command as one DrawingEdit (one undo step).
class CommandSession
{
public:
    explicit CommandSession(Drawing& drawing);

    [[nodiscard]] bool isActive() const noexcept;
    [[nodiscard]] Prompt prompt() const;
    [[nodiscard]] bool wantsPoint() const noexcept;      // viewport in point mode
    [[nodiscard]] bool wantsSelection() const noexcept;  // "Select objects"
    [[nodiscard]] std::optional<Vec3> lastPoint() const;

    void start(CommandName name, std::span<const EntityId> preselected);
    void feedText(std::string_view text, const Vec3& cursorDirection); // command line
    void feedPoint(const Vec3& point);                                 // mouse
    void feedSelection(std::span<const EntityId> ids);                 // picked objects
    void enter();                                                      // Enter/Space
    void cancel();                                                     // Esc
    [[nodiscard]] Preview preview(const std::optional<Vec3>& cursor) const;

    // Drained by the caller after every call.
    [[nodiscard]] std::vector<Message> takeMessages();
    [[nodiscard]] std::vector<DrawingEdit> takeFinishedEdits(); // to the undo stack
    [[nodiscard]] std::optional<CommandName> takeUndoRequest();  // UNDO / REDO typed
};

} // namespace fem::geom
```

```cpp
// fem/render/drawing_picker.hpp — REQ-005-25
namespace fem::render {

class DrawingPicker
{
public:
    [[nodiscard]] std::optional<geom::EntityId> pick(const geom::Drawing& drawing,
                                                     const Camera& camera, const Vec2& pixel,
                                                     double tolerancePx) const;
    [[nodiscard]] std::vector<geom::EntityId> pickRect(const geom::Drawing& drawing,
                                                       const Camera& camera,
                                                       const PickRect& rect) const;
};

} // namespace fem::render
```

```cpp
// Viewport (alterações) — REQ-005-14, 18, 20, 25, 26, 33, 34
enum class SelectionTarget : std::uint8_t { Drawing, Mesh };
enum class MouseMode : std::uint8_t { Select, PickPoint };

class Viewport
{
public:
    void setDrawing(std::shared_ptr<const geom::Drawing> drawing); // redrawn on revision
    void drawingChanged();                                          // after an edit
    void setDrawingPreview(geom::Preview preview);
    void setSnapMarker(std::optional<std::pair<Vec3, geom::SnapMode>> marker);
    void setMouseMode(MouseMode mode);
    [[nodiscard]] const std::vector<geom::EntityId>& drawingSelection() const;
    void setDrawingSelection(std::vector<geom::EntityId> ids);

public slots:
    void setSelectionTarget(fem::render::SelectionTarget target);

signals:
    void cursorMoved(fem::render::Vec2 pixel);     // PickPoint mode: every move
    void pointRequested(fem::render::Vec2 pixel);  // PickPoint mode: left click
    void drawingSelectionChanged();
};
```

`app`: `CommandLineWidget` (sinais `textEntered(QString)`, `escapePressed()`,
completar com Tab, histórico com as setas, Espaço como Enter, slot
`showPrompt(QString)`, `appendHistory(QString)`); `DraftingController` (dono
do `Drawing`, da `CommandSession`, do `PointResolver`, das
`DraftingSettings` e do `QUndoStack`); `DraftingSettingsDialog` (passo da
grade e abertura do snap).

## 5. Dados e algoritmos

### 5.1 Documento e desfazer (REQ-005-01, 02, 31, 32)

- Entidades num `std::vector<Entity>` em ordem de inserção, mais um índice
  `std::unordered_map<EntityId, std::size_t>`; remover troca com a última e
  atualiza o índice (ordem estável não é requisito). `nextId()` só cresce.
- `DrawingEdit` guarda entidades inteiras (com o ID): desfazer é
  `revert` = tirar `added` e repor `removed`. Mover gera `removed` (forma
  antiga) e `added` (forma nova, mesmo ID); copiar só `added` com IDs novos;
  apagar só `removed`. Como `revert` repõe as entidades com seus IDs, eles não
  mudam no desfazer (REQ-005-32), e `nextId()` não volta atrás (REQ-005-02).
- No `app`, cada edição concluída vira um `QUndoCommand` que guarda o
  `DrawingEdit`: `undo()` chama `revert`, `redo()` chama `apply` (o primeiro
  `redo()` é ignorado, porque o comando já aplicou a edição ao desenhar). Assim
  a lógica de desfazer é testada em `geom`, e o `QUndoStack` dá os textos dos
  menus e o estado habilitado.

### 5.2 Linha de comando (REQ-005-05, 08, 10 a 13)

Gramática (espaços nas pontas ignorados; decimal com ponto; expoente aceito):

```
entrada  = vazio | ponto | polar | número | palavra
ponto    = ["@"] num "," num ["," num]
polar    = ["@"] num "<" num
número   = num
palavra  = letras
```

- `findCommand`: `POINT`/`PO`, `LINE`/`L`, `PLINE`/`PL`, `MOVE`/`M`,
  `COPY`/`CO`/`CP`, `ERASE`/`E`, `UNDO`/`U`, `REDO`, sem diferenciar
  maiúsculas. Sem comando ativo, `U` é UNDO; dentro da LINE e PLINE, `U` é a
  opção Desfazer (o pedido ativo decide).
- `resolvePoint`: relativo e polar relativo precisam do último ponto
  (senão erro "nenhum ponto anterior"); polar: `p = ref + d·(cos a, sin a, 0)`,
  com `ref` a origem (absoluto) ou o último ponto (relativo) e `z = ref.z`;
  número: `last + d · dir/|dir|`, com `dir` a direção do cursor já restringida
  pelo ortogonal (vinda do `PointResolver`).

### 5.3 Pontos do mouse (REQ-005-14, 19, 22, 23)

`PointResolver::resolve`, na ordem:

1. **Snap a objetos** (se ligado): projeta com `viewProjection` os pontos
   candidatos de cada modo ligado — extremidades (vértices e pontos), meios de
   cada segmento — e fica com o mais próximo do cursor dentro da abertura;
   **interseção**: escolhe os segmentos cuja projeção passa a até a abertura do
   cursor; para cada par, os pontos mais próximos entre as duas retas
   (fechado, com parâmetros em [0, 1]); se a distância entre eles for até a
   tolerância do documento, o ponto médio é candidato. Vence o candidato mais
   próximo do cursor na tela; em empate, extremidade > interseção > meio.
2. **Plano:** o raio do cursor contra o plano `z = 0`, ou `z = último.z` com o
   ortogonal ligado e ponto anterior.
3. **Grade:** com encaixe ligado, x e y arredondados para múltiplos do passo
   (z fica 0).
4. **Ortogonal:** com ponto anterior, `p = último + (dx, 0, 0)` ou
   `(0, dy, 0)`, o eixo com maior |componente| de `p − último`.

Segmentos e candidatos ficam em cache por `revision()`. Força bruta: 100 mil
trechos são 300 mil projeções 4×4, cerca de 2 ms em Release; a meta é 5 ms
(NFR-005-01). Uma grade uniforme na tela fica como alternativa se a medição
pedir.

### 5.4 Comandos (REQ-005-06, 07, 15 a 18, 27 a 30)

Máquinas de estado pequenas, todas atrás de `Command` (interface privada em
`commands.cpp`):

| Comando | Passos | Ao concluir |
|---------|--------|-------------|
| POINT | ponto* (Enter/Esc encerram) | um ponto por entrada |
| LINE | 1º ponto → próximo* [U, C] | uma linha por trecho; C exige ≥ 2 trechos |
| PLINE | 1º ponto → próximo* [U, C] | uma polilinha (≥ 2 vértices; C fecha) |
| MOVE | objetos → base → 2º ponto | entidades transladadas (mesmo ID) |
| COPY | objetos → base → 2º ponto* | uma cópia por 2º ponto (IDs novos) |
| ERASE | objetos | entidades removidas |

- A LINE e o POINT aplicam cada entidade ao desenho assim que criada (ela
  aparece na hora) e acumulam o `DrawingEdit`; U reverte só o último trecho;
  Esc e Enter fecham o passo de desfazer com o que já foi feito (REQ-005-07).
  A PLINE mostra a polilinha em construção como pré-visualização e só a cria
  ao encerrar.
- "Selecione objetos" (REQ-005-27): com pré-seleção, o comando pula esse
  passo; senão, o `Viewport` fica no modo de seleção com alvo Desenho, e cada
  clique, janela ou cruzamento acrescenta (Shift retira); Enter conclui; sem
  nada selecionado, mensagem e fim.
- `CommandSession` guarda o último comando concluído para Enter/Espaço
  (REQ-005-06) e o último ponto entre comandos (para `@` no início de um novo
  comando, como no AutoCAD).

### 5.5 Desenho na tela (REQ-005-18, 20, 33)

- `DrawingGpu` monta, a cada nova `revision()`, um buffer de segmentos
  (posições `float` relativas à origem do desenho, o centro da caixa no
  momento da montagem) e um de pontos (instâncias); o deslocamento até o alvo
  da câmera é calculado em `double` por quadro, como na malha (REQ-004-18).
  Cor branco-gelo `(232, 236, 240)`; selecionadas com o bit de realce. Usa os
  pipelines de linha e de ponto da 004 (mesmo layout de uniformes).
- Pré-visualização (trecho elástico, polilinha em construção, fantasmas de
  MOVE/COPY): buffer dinâmico pequeno, refeito quando a pré-visualização muda,
  em cor mais apagada.
- Marcador e nome do snap (REQ-005-20): desenhados em 2D pelo
  `SelectionOverlay` (o widget de `QPainter` do retângulo), na posição
  projetada do ponto — quadrado, triângulo ou X de 10 px, em amarelo.
- Repouso (NFR-005-03): só se pede quadro novo quando o cursor muda o ponto
  resolvido ou a pré-visualização.

### 5.6 Seleção (REQ-005-25, 26)

- `DrawingPicker`: clique — distância do cursor a cada segmento projetado e a
  cada ponto, até a tolerância, ficando com o mais próximo da câmera; janela —
  todos os vértices dentro; cruzamento — algum vértice dentro ou algum
  segmento cruzando o retângulo (`segmentTouchesBox`). Os auxiliares
  `Projector`, `distanceToSegment`, `segmentTouchesBox` saem de
  `mesh_picker.cpp` para `pick_util.hpp`, usados pelos dois.
- `Viewport`: alvo da seleção Desenho (padrão) ou Malha (menu Editar ▸ Alvo
  da seleção); o gesto do botão esquerdo vai para o `DrawingPicker` ou para o
  `MeshPicker` conforme o alvo. A seleção de malha da 004 não muda. Clique
  fora com alvo Desenho limpa a seleção do desenho (REQ-004-25 vale para os
  dois).
- No modo "pedir ponto", o clique esquerdo emite `pointRequested` em vez de
  selecionar, e cada movimento emite `cursorMoved`.

### 5.7 Interface (REQ-005-04, 09, 21, 24, 35)

- Dock inferior "Linha de comando" (`QDockWidget`), com histórico
  (`QPlainTextEdit` só leitura, 500 linhas) e entrada (`QLineEdit`). A
  entrada recebe o foco ao digitar com o viewport em foco (teclas imprimíveis
  repassadas), como no AutoCAD.
- Barra de status: coordenadas `x, y, z` do ponto resolvido e três botões
  marcáveis — SNAP (F3), GRADE (F9), ORTO (F8).
- Menus: Desenho ▸ Ponto, Linha, Polilinha; Modificar ▸ Mover, Copiar,
  Apagar; Editar ▸ Desfazer, Refazer, Alvo da seleção, Snap a objetos ▸
  modos; Ferramentas ▸ Configurações de desenho.
- Textos: pedidos e mensagens chegam do `geom` como `PromptId`, `OptionId` e
  `MessageId`; o `app` os traduz com `tr()` (por exemplo, "Especifique o
  próximo ponto ou [Desfazer (U)/Fechar (C)]:"). Os nomes dos comandos e as
  letras das opções ficam em inglês.

## 6. Fluxo

```
digitar "L" ─→ CommandLineWidget ─→ DraftingController
  └─ findCommand → session.start(Line, seleção atual)
       └─ prompt FirstPoint → tr() → linha de comando; viewport em PickPoint

mouse move ─→ Viewport::cursorMoved(px)
  └─ DraftingController: ScreenView da câmera → PointResolver::resolve
       ├─ barra de status (x, y, z); setSnapMarker
       └─ session.preview(ponto) → Viewport::setDrawingPreview → update()

clique ─→ Viewport::pointRequested(px) → resolve → session.feedPoint(p)
digitar "@3,0" ─→ session.feedText → parseInput → resolvePoint → feedPoint

a cada chamada: takeMessages → histórico; takeFinishedEdits → QUndoStack;
                drawing.revision mudou → Viewport::drawingChanged → DrawingGpu
Ctrl+Z ─→ QUndoStack::undo → DrawingEdit revert → drawingChanged
```

## 7. Tratamento de erros

| Situação | Comportamento | Requisito |
|----------|---------------|-----------|
| Texto mal formado ou opção que não existe no passo | Mensagem "Entrada inválida." no histórico; o comando continua no mesmo passo | REQ-005-08 |
| Palavra desconhecida sem comando ativo | "Comando desconhecido: X." | REQ-005-08 |
| `@` ou distância sem ponto anterior | "Não há ponto anterior." | REQ-005-08, 11, 13 |
| Fechar (C) com menos de 2 trechos | "São necessários pelo menos dois segmentos." | REQ-005-16, 17 |
| MOVE/COPY/ERASE sem objetos | "Nenhum objeto selecionado."; comando encerra | REQ-005-27 |
| Ponto pedido com o cursor sem plano (raio paralelo a z = 0) e sem snap | Nenhum ponto: o clique é ignorado e as coordenadas da barra ficam vazias | REQ-005-14 |
| UNDO sem nada para desfazer | "Nada para desfazer." | REQ-005-31 |
| Comando digitado com outro ativo | O ativo é cancelado (como Esc) e o novo começa | REQ-005-07 |

## 8. Estratégia de testes

| Teste | Tipo | Requisito | Referência/tolerância |
|-------|------|-----------|-----------------------|
| `Drawing`: inserir, achar, remover; IDs crescentes e nunca reutilizados; `apply`/`revert` de cada tipo de edição restauram o estado e os IDs; `append` junta edições; `bounds` e `tolerance` | unitário | REQ-005-01, 02, 31, 32 | exato |
| `segmentsOf`: ponto (nenhum), linha (1), polilinha aberta (n−1) e fechada (n) | unitário | REQ-005-01 | exato |
| `parseInput` e `resolvePoint`: absolutos 2D/3D, relativos, polares (0°, 90°, 225°, negativos), número com direção, expoentes, espaços, entradas inválidas (`1,`, `@`, `3<`, `a,b`) | unitário | REQ-005-08, 10 a 13 | 1e−12 |
| `findCommand` e `completeCommand`: nomes, atalhos, maiúsculas, prefixos | unitário | REQ-005-05 | exato |
| `PointResolver`: extremidade, meio e interseção dentro e fora da abertura; interseção de segmentos 3D que se cruzam e que passam perto sem se tocar; prioridade snap > grade > plano; grade com passo 0,5; ortogonal escolhe X ou Y; plano em z do último ponto | unitário | REQ-005-14, 19, 22, 23 | 1e−9 |
| `CommandSession`: POINT; LINE com U, C, Enter, Esc; PLINE aberta e fechada; MOVE e COPY com e sem pré-seleção; ERASE; repetição com Enter vazio; comando novo cancela o ativo; edições finais (um passo por comando); pré-visualização em cada passo; mensagens de erro | unitário | REQ-005-06, 07, 15 a 18, 27 a 31 | exato |
| `DrawingPicker`: clique em linha e ponto dentro e fora da tolerância; janela e cruzamento sobre um pórtico em vista superior | unitário | REQ-005-25 | exato |
| `SceneRenderer` com backend `Null`: desenho com linhas, pontos, seleção e pré-visualização | unitário | REQ-005-33 | sem erro |
| `bench_drafting`: 100 mil trechos — resolução de snap por movimento | manual, Release | NFR-005-01 | < 5 ms |
| Roteiro manual: pórtico plano e espacial pelo mouse e pela linha de comando; snaps, grade, ortogonal; MOVE/COPY/ERASE; desfazer/refazer; alvo da seleção; desenho com malha; textos em pt_BR; FPS e repouso | manual | REQ-005-04 a 35, NFR-005-02, 03 | AC-5 a AC-10 |

## 9. Alternativas consideradas

| Alternativa | Por que foi descartada |
|-------------|------------------------|
| Usar o OpenCASCADE já nas entidades | Só há segmentos retos nesta fatia; o roadmap migra o desenho para o núcleo na 015, com círculos e arcos |
| Desfazer só com `QUndoStack` e comandos Qt | A lógica de desfazer ficaria fora de `geom` e sem teste sem Qt; o `DrawingEdit` é testável e o `QUndoStack` só o embrulha |
| `PointResolver` dependente de `render::Camera` | `geom` não pode depender de `render`; a matriz de projeção e o raio do cursor bastam |
| Pedidos montados em `geom` como texto | `geom` não tem Qt nem `tr()`; IDs de pedido e mensagem permitem a tradução em `app` |
| Índice espacial (BVH ou grade) para o snap já agora | Força bruta atende à meta estimada; o índice entra só se a medição pedir (§7 da constituição) |
| Marcadores de snap na GPU | O `SelectionOverlay` com `QPainter` já existe, desenha em pixels e não exige pipeline novo |

## 10. Riscos e mitigação

- **Viewport acumulando papéis** (malha, desenho, modos do mouse) → o
  `Viewport` só encaminha eventos e guarda estado de exibição; a lógica fica no
  `DraftingController` e em `geom`.
- **Foco do teclado** entre o viewport e a linha de comando (Espaço, Delete,
  Esc) → atalhos no `MainWindow` com contexto de janela e teclas imprimíveis
  repassadas à entrada; roteiro manual nas três plataformas pelo CI só cobre o
  build (ressalva).
- **Snap de interseção em desenhos densos** (muitos segmentos sob o cursor) →
  pares limitados aos segmentos dentro da abertura; medir no benchmark.
- **Força bruta acima de 5 ms** → grade uniforme na tela por revisão, só se a
  medição pedir.

## 11. Novas dependências

Nenhuma. `QUndoStack`, `QDockWidget`, `QPlainTextEdit` e `QLineEdit` vêm de Qt
Gui e Widgets, já usados.
