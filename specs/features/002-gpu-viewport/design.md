# Design — 002 gpu-viewport

Status: aprovado · Data: 2026-11-10
Requisitos: [requirements.md](requirements.md)

## 1. Resumo da abordagem

O módulo `render` ganha quatro peças:

1. **`Camera`** — matemática pura em precisão dupla (Eigen), sem Qt e sem GPU.
2. **`SceneRenderer`** — dono dos recursos de GPU (pipelines, buffers, shaders).
   Recebe um `QRhi` e um alvo de renderização; não sabe nada de widgets. Pode
   ser exercitado em teste com o backend `Null` do QRhi.
3. **`Viewport`** — subclasse fina de `QRhiWidget`: escolhe o backend, traduz
   eventos do mouse em chamadas da `Camera` e delega o desenho ao
   `SceneRenderer`.
4. **Seleção de backend** — funções puras que interpretam `FEM_RHI_BACKEND` e
   montam a lista de tentativas por plataforma.

A grade é um único quadrilátero com as linhas geradas no fragment shader; o
indicador de eixos é um segundo passe em uma área fixa do canto. O `app` coloca
o `Viewport` como widget central, cria o menu "Exibir" e trata a falha de GPU.

## 2. Verificação da constituição

| Princípio | Atende? | Observação |
|-----------|---------|------------|
| Multiplataforma (§2) | Sim | Um código de render; backend por plataforma escolhido em tempo de execução; CI nos quatro jobs |
| GPU somente via QRhi (§3) | Sim | Nenhuma chamada a API nativa. Shaders em GLSL `#version 440` compilados por `qsb` (`qt_add_shaders`). `QRhi` confinado a `src/render`, com verificação automática |
| Camadas / `core` sem Qt (§4) | Sim | Nada muda em `core`, `geom` e `mesher`. `render` passa a depender de Qt Widgets (ver ADR 0002) |
| Correção numérica (§5) | Sim | Câmera em `double`; para a GPU vão matrizes `float` relativas ao ponto de foco. Testes com tolerância explícita |
| Simplicidade / dependências (§7) | Sim | Sem bibliotecas novas; apenas dois módulos do próprio Qt (Shader Tools e os headers privados do Gui exigidos pelo QRhi) |

## 3. Módulos e arquivos afetados

| Módulo | Arquivo | Novo/Alterado | Responsabilidade |
|--------|---------|---------------|------------------|
| render | `include/fem/render/camera.hpp`, `camera.cpp` | novo | Câmera: órbita, pan, zoom, vistas, enquadramento, matrizes |
| render | `include/fem/render/backend.hpp`, `backend.cpp` | novo | Enum `Backend`, leitura de `FEM_RHI_BACKEND`, lista de tentativas |
| render | `include/fem/render/grid_spacing.hpp`, `grid_spacing.cpp` | novo | Cálculo do espaçamento e da mistura de níveis da grade |
| render | `include/fem/render/viewport.hpp`, `viewport.cpp` | novo | Widget `Viewport` (`QRhiWidget`) |
| render | `scene_renderer.hpp`, `scene_renderer.cpp` (privados) | novo | Recursos de GPU e passes de desenho |
| render | `axis_gizmo_mesh.hpp/.cpp`, `axis_labels.hpp/.cpp` (privados) | novo | Geometria das setas; atlas com as letras X, Y, Z |
| render | `shaders/grid.vert/.frag`, `gizmo.vert/.frag`, `label.vert/.frag` | novo | Shaders |
| render | `module.hpp`, `module.cpp` | removido | Substituto provisório da 001 |
| render | `CMakeLists.txt` | alterado | Fontes, `AUTOMOC`, `qt_add_shaders`, dependências |
| app | `main_window.hpp/.cpp` | alterado | Viewport central, menu "Exibir", mensagem de falha, backend no "Sobre" |
| app | `main.cpp` | alterado | `--smoke-test` espera o primeiro quadro (ou a falha) |
| app | `i18n/femapp_pt_BR.ts` | alterado | Textos novos |
| raiz | `CMakeLists.txt` | alterado | `find_package(Qt6 … ShaderTools)` |
| cmake | `CheckRhiConfined.cmake` | novo | Falha se arquivo fora de `src/render` incluir `rhi/…` ou `QRhi…` |
| tests | `unit/render/test_camera.cpp`, `test_backend.cpp`, `test_grid_spacing.cpp`, `test_scene_renderer.cpp` | novo | Testes (substituem `test_render.cpp`) |
| tests | `CMakeLists.txt` | alterado | Novos testes, variáveis de ambiente do smoke test |
| CI | `.github/workflows/ci.yml` | alterado | Módulo `qtshadertools` na instalação do Qt |
| docs | `README.md`, `specs/architecture.md` | alterado | Pré-requisito novo; dependências do `render` |

## 4. Interfaces públicas

```cpp
// fem/render/camera.hpp — no Qt, no GPU. REQ-002-11..19, 28; NFR-002-03
namespace fem::render {

using Vec2 = Eigen::Vector2d;
using Vec3 = Eigen::Vector3d;
using Mat4 = Eigen::Matrix4d;

enum class Projection { Perspective, Orthographic };
enum class StandardView { Front, Back, Left, Right, Top, Bottom, Isometric };

struct Aabb { Vec3 min; Vec3 max; };
struct Ray  { Vec3 origin; Vec3 direction; };

class Camera
{
public:
    void setViewportSize(int widthPx, int heightPx);

    void orbit(double deltaXPx, double deltaYPx);            // REQ-13
    void pan(const Vec2& fromPx, const Vec2& toPx);          // REQ-12
    void zoomAt(const Vec2& cursorPx, double wheelSteps);    // REQ-14, 15
    void setStandardView(StandardView view);                 // REQ-17
    void setProjection(Projection projection);               // REQ-16
    void fit(const Aabb& bounds);                            // REQ-18

    [[nodiscard]] Projection projection() const noexcept;
    [[nodiscard]] Vec3 target() const noexcept;
    [[nodiscard]] double distance() const noexcept;
    [[nodiscard]] Vec3 position() const;
    [[nodiscard]] Vec3 right() const;
    [[nodiscard]] Vec3 up() const;
    [[nodiscard]] Vec3 forward() const;

    // View matrix with the target at the origin: safe to convert to float.
    [[nodiscard]] Mat4 viewMatrixRelativeToTarget() const;
    [[nodiscard]] Mat4 viewMatrix() const;
    [[nodiscard]] Mat4 projectionMatrix() const;             // depth range [0, 1]
    [[nodiscard]] Ray rayThrough(const Vec2& pixel) const;
};

} // namespace fem::render
```

```cpp
// fem/render/backend.hpp — REQ-002-02..05
namespace fem::render {

enum class Backend { Direct3D11, Direct3D12, Metal, Vulkan, OpenGL, Null };
enum class Platform { Windows, MacOS, Linux };

[[nodiscard]] std::optional<Backend> parseBackendName(std::string_view name);
[[nodiscard]] std::string_view backendDisplayName(Backend backend);
[[nodiscard]] bool isBackendSupported(Backend backend, Platform platform);

struct BackendPlan
{
    std::vector<Backend> attempts;   // in order
    std::string warning;             // empty when the override was accepted
};

// `overrideName` is the value of FEM_RHI_BACKEND (empty when unset).
[[nodiscard]] BackendPlan planBackends(Platform platform, std::string_view overrideName);

} // namespace fem::render
```

```cpp
// fem/render/grid_spacing.hpp — REQ-002-21
namespace fem::render {

struct GridSpacing
{
    double minor;      // world units, a power of 10
    double major;      // 10 * minor
    double minorFade;  // 0..1: opacity of the minor lines at this zoom
};

[[nodiscard]] GridSpacing gridSpacing(double worldUnitsPerPixel);

} // namespace fem::render
```

```cpp
// fem/render/viewport.hpp — REQ-002-01, 06..10, 26
namespace fem::render {

class Viewport : public QRhiWidget
{
    Q_OBJECT

public:
    explicit Viewport(QWidget* parent = nullptr);
    ~Viewport() override;

    [[nodiscard]] QString backendName() const;        // REQ-07
    [[nodiscard]] quint64 frameCount() const;         // REQ-09, AC-11
    [[nodiscard]] bool isGridVisible() const;
    [[nodiscard]] bool isAxesVisible() const;
    [[nodiscard]] Projection projection() const;

public slots:
    void setGridVisible(bool visible);                // REQ-26
    void setAxesVisible(bool visible);
    void setStandardView(fem::render::StandardView view);
    void setProjection(fem::render::Projection projection);
    void fitAll();                                    // REQ-18

signals:
    void frameRendered();                             // used by --smoke-test
    void backendChanged(const QString& name);
    void backendUnavailable(const QString& reason);   // REQ-06

protected:
    void initialize(QRhiCommandBuffer* cb) override;
    void render(QRhiCommandBuffer* cb) override;
    void releaseResources() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;                     // hides SceneRenderer / QRhi types
};

} // namespace fem::render
```

`SceneRenderer` é privado do módulo (não fica em `include/`):

```cpp
struct FrameParams
{
    const Camera* camera;
    QSize pixelSize;
    double devicePixelRatio;
    bool showGrid;
    bool showAxes;
};

class SceneRenderer
{
public:
    void initialize(QRhi* rhi, QRhiRenderPassDescriptor* rp, int sampleCount);
    void render(QRhiCommandBuffer* cb, QRhiRenderTarget* rt, const FrameParams& params);
    void release();
};
```

## 5. Dados e algoritmos

### 5.1 Câmera

Estado: ponto de foco `t` (double), azimute `ψ`, elevação `θ ∈ [−90°, 90°]`,
distância `d`, projeção, campo de visão vertical `fov = 45°`, tamanho em pixels.

A base é escrita diretamente em função dos ângulos, sem produto vetorial com um
"up" fixo. Assim ela continua definida nas vistas superior e inferior, e o eixo
Z fica sempre vertical na tela (REQ-002-11, 13):

```
f = (−cosθ·cosψ, −cosθ·sinψ, −sinθ)     direção de visada
r = (−sinψ, cosψ, 0)                    direita
u = r × f                               para cima na tela
posição = t − d·f
```

- **Órbita:** `ψ −= k·Δx`, `θ += k·Δy`, com `k = 0,5°/pixel`; `θ` é limitado a
  `[−90°, 90°]`.
- **Pan:** os dois pixels são projetados no plano que passa por `t` e é
  perpendicular a `f`; `t −= (P_to − P_from)`. O ponto sob o cursor acompanha o
  cursor (REQ-002-12).
- **Zoom:** `s = 1,15^(−passos)`; `d' = clamp(d·s, d_min, d_max)`; com `P` o
  ponto sob o cursor no plano de foco, `t' = P + (t − P)·(d'/d)`, que mantém `P`
  fixo na tela (REQ-002-14). Limites `d_min = 1e−6`, `d_max = 1e12`
  (REQ-002-15).
- **Projeção:** perspectiva com `fov`; ortográfica com meia-altura
  `h = d·tan(fov/2)`. Como `h` deriva de `d`, trocar de projeção preserva o
  foco e o tamanho aparente no plano de foco (REQ-002-16). Planos de corte:
  `near = d/100`, `far = 100·d` (perspectiva); `[−100·d, 100·d]` (ortográfica).
- **Vistas padrão** (`ψ`, `θ`): Frontal (−90°, 0), Posterior (90°, 0), Direita
  (0, 0), Esquerda (180°, 0), Superior (−90°, 90°), Inferior (−90°, −90°),
  Isométrica (−45°, 35,264°). Na frontal a câmera fica em −Y olhando para +Y,
  com X para a direita e Z para cima.
- **Enquadrar:** `t` = centro da caixa; `R` = metade da diagonal;
  `d = 1,1·R / sin(min(fov_v, fov_h)/2)`. Região padrão sem conteúdo: caixa de
  `(−10, −10, 0)` a `(10, 10, 0)`.
- **Precisão:** tudo em `double`. Para a GPU vai `projeção × vista relativa ao
  foco`, convertida para `float`; os objetos são desenhados com a translação
  `(origem − t)` calculada em `double` (NFR-002-03).

### 5.2 Grade

Um quadrilátero no plano Z = 0, centrado sob o ponto de foco, com lado
proporcional a `d`. O fragment shader calcula as linhas a partir da coordenada
do mundo, com antisserrilhamento por derivadas de tela (`fwidth`):

- Espaçamento (em CPU, `gridSpacing`): com `w` = unidades do mundo por pixel no
  foco, `minor = 10^ceil(log10(w·P))`, `P` = 12 pixels mínimos entre linhas;
  `major = 10·minor`. A parte fracionária do logaritmo vira `minorFade`, de modo
  que as linhas secundárias surgem e somem gradualmente (REQ-002-21).
- Para não perder precisão longe da origem, o shader recebe a posição do foco
  **já reduzida módulo `major`** (calculada em `double`), e não a coordenada
  absoluta.
- Linhas `x = 0` e `y = 0` desenhadas em verde e vermelho — o eixo X é a linha
  `y = 0` (REQ-002-22). A distância até elas também é calculada em `double` na
  CPU e passada como uniforme.
- Opacidade multiplicada por um fator de distância ao foco e por um fator de
  ângulo rasante (derivada grande ⇒ linha mais fraca), o que elimina a
  cintilação no horizonte (REQ-002-23).
- Pipeline: mistura alfa, teste de profundidade ligado, escrita de profundidade
  desligada.

### 5.3 Indicador de eixos

Segundo passe, em uma área de 96×96 pixels lógicos no canto inferior esquerdo
(`96 × devicePixelRatio` pixels físicos, REQ-002-25), com projeção ortográfica
própria e apenas a rotação da câmera:

- Três setas (cilindro + cone, 12 lados) geradas uma vez em CPU, cor por
  vértice: X vermelho, Y verde, Z azul (REQ-002-24).
- Rótulos "X", "Y", "Z": um atlas pequeno desenhado com `QPainter` em uma
  `QImage` e enviado como textura; três quadriláteros voltados para a câmera
  nas pontas das setas. O atlas é refeito quando o `devicePixelRatio` muda.
- A profundidade é limpa só nessa área, para o indicador ficar sempre na frente.

### 5.4 Seleção de backend

`planBackends` devolve a lista de tentativas:

| Plataforma | Sem variável | Com `FEM_RHI_BACKEND` válido |
|------------|--------------|------------------------------|
| Windows | D3D11, OpenGL | o pedido, depois a lista padrão |
| macOS | Metal, OpenGL | idem |
| Linux | Vulkan, OpenGL | idem |

Valor desconhecido ou não suportado na plataforma (ex.: `metal` no Windows) ⇒
lista padrão e `warning` preenchido (REQ-002-05). `null` é aceito em todas as
plataformas. Para testar a falha total existe o valor interno `fail`, não
documentado ao usuário, que produz uma lista vazia.

### 5.5 Redesenho e alta densidade

`QRhiWidget` só redesenha quando `update()` é chamado; o `Viewport` chama
`update()` apenas nos eventos de mouse que mudam a câmera, nos slots e no
redimensionamento (REQ-002-09). O tamanho em pixels físicos vem do alvo de
renderização a cada quadro, então redimensionar ou trocar de tela não exige
código especial (REQ-002-08). Antisserrilhamento: `setSampleCount(4)`.

## 6. Fluxo

Tudo roda na thread da interface; não há threads novas.

```
MainWindow::MainWindow
  └─ Viewport()                     planBackends(...) → setApi(primeira tentativa)
show()
  └─ QRhiWidget cria o QRhi
       ├─ sucesso → initialize(cb) → SceneRenderer::initialize → backendChanged
       └─ falha (sinal renderFailed)
            ├─ há próxima tentativa → setApi(próxima)
            └─ lista esgotada → backendUnavailable(reason)
                                   └─ MainWindow troca o viewport pela mensagem

mouse/roda → Camera::{pan, orbit, zoomAt} → update()
render(cb) → SceneRenderer::render(grade → eixos) → ++frameCount → frameRendered
```

`--smoke-test`: o `main` encerra com código 0 ao receber o primeiro
`frameRendered` ou `backendUnavailable`, e com código 1 se nenhum dos dois
chegar em 10 s.

Menu "Exibir" (textos em inglês no código, traduzidos):

| Item | Atalho |
|------|--------|
| Front / Back / Left / Right / Top / Bottom / Isometric | Ctrl+1 … Ctrl+7 |
| Perspective ⇄ Orthographic (marcável) | Ctrl+Shift+O |
| Fit All | Home |
| Grid (marcável) | F7 |
| Axes (marcável) | — |

Os atalhos usam modificador ou tecla de função para não colidir com a linha de
comando da feature 005, que receberá as teclas simples.

## 7. Tratamento de erros

| Situação | Comportamento | Requisito |
|----------|---------------|-----------|
| Backend padrão não inicializa | Tenta o próximo da lista (OpenGL) | REQ-002-03 |
| `FEM_RHI_BACKEND` desconhecido ou não suportado | Aviso no log (`qCWarning`, categoria `fem.render`) e lista padrão | REQ-002-05 |
| Nenhum backend inicializa | `backendUnavailable`; a janela mostra a mensagem com o motivo e permanece aberta; itens do menu "Exibir" desabilitados | REQ-002-06 |
| Shader ou pipeline não cria (erro de build/driver) | Mesmo caminho de `backendUnavailable`, com o nome do recurso no motivo | REQ-002-06 |
| Viewport com largura ou altura zero | `Camera` ignora o tamanho; nada é desenhado | REQ-002-08 |
| Zoom além dos limites | Distância fica presa ao limite; o foco não se move | REQ-002-15 |
| Caixa vazia ou degenerada em `fit` | Usa a região padrão da grade | REQ-002-18 |

## 8. Estratégia de testes

| Teste | Tipo | Requisito | Referência/tolerância |
|-------|------|-----------|-----------------------|
| Base ortonormal e `u.z ≥ 0` após sequências aleatórias de órbita | unitário | REQ-002-11, 13 | 1e−12 |
| Elevação limitada a ±90°; base definida nos polos | unitário | REQ-002-13 | exato |
| Pan: o ponto sob `from` passa a projetar em `to` | unitário | REQ-002-12 | 1e−6 px |
| Zoom: o ponto sob o cursor continua no mesmo pixel; limites respeitados | unitário | REQ-002-14, 15 | 1e−6 px |
| Troca de projeção preserva foco e altura visível no plano de foco | unitário | REQ-002-16 | 1e−12 relativo |
| Direção e "para cima" de cada vista padrão | unitário | REQ-002-17 | 1e−12 |
| `fit`: os 8 cantos de uma caixa conhecida ficam dentro do quadro, com folga ≤ 25 % | unitário | REQ-002-18, 19 | — |
| Foco em (1e6, 1e6, 0): matriz relativa não perde precisão ao virar `float` | unitário | NFR-002-03 | erro < 1e−3 px |
| `parseBackendName`, `planBackends` para as 3 plataformas, valores inválidos, `fail` | unitário | REQ-002-02..05 | exato |
| `gridSpacing`: potências de 10, monotonicidade, `minorFade ∈ [0, 1]` | unitário | REQ-002-21 | exato |
| `SceneRenderer` com `QRhi` `Null`: inicializa, desenha um quadro em alvo offscreen, libera | unitário (sem GPU) | REQ-002-29, 30 | sem erro |
| `femapp_smoke` com `FEM_RHI_BACKEND=null`, na plataforma nativa (`xvfb` no CI de Linux) | CTest | REQ-002-29 | código 0 |
| `femapp_smoke_no_gpu` com `FEM_RHI_BACKEND=fail` | CTest | REQ-002-06 | código 0 e mensagem de falha no log |
| `check_rhi_confined` | CTest | REQ-002-10 | — |
| Roteiro manual: navegação, grade, eixos, menus, redimensionar, backend no "Sobre", `FEM_RHI_BACKEND=opengl` | manual | REQ-002-07, 08, 12–14, 20–27 | AC-2, 3, 5, 8–11, 14 |
| Quadros por segundo e repouso | manual | NFR-002-01, 02 | ≥ 60 FPS; contador parado |

Não há teste de imagem (comparação de pixels) nesta feature; fica para a 004,
quando houver conteúdo que justifique.

## 9. Alternativas consideradas

| Alternativa | Por que foi descartada |
|-------------|------------------------|
| `QWindow` + `QRhi` próprio, embutido com `createWindowContainer` | Mais controle, mas traz problemas de empilhamento e foco com widgets; `QRhiWidget` é a via prevista na arquitetura |
| Câmera com `QMatrix4x4`/`QVector3D` | São `float`: não atendem a NFR-002-03 |
| Arcball livre (trackball) | Não mantém Z vertical; desorienta em modelos de estruturas |
| Grade como malha de linhas | Largura de linha > 1 px não é portável no QRhi e a densidade adaptativa exigiria refazer buffers a cada zoom |
| Rótulos dos eixos como `QLabel` filhos | Simples, mas tremem ao orbitar e não respeitam a profundidade das setas |
| Toda a lógica dentro do `Viewport` | Impediria testar a renderização sem janela |
| `--smoke-test` só com `QTimer` (como na 001) | Não garantiria que um quadro foi renderizado |

## 10. Riscos e mitigação

- **`QRhiWidget` não funciona na plataforma `offscreen` do Qt** (confirmado
  na implementação, em 2026-11-10: nem o backend `null` inicializa) →
  `femapp_smoke` roda na plataforma nativa, com `FEM_RHI_BACKEND=null`; no CI
  de Linux o `ctest` roda sob `xvfb-run`. `femapp_smoke_no_gpu` continua em
  `offscreen`. O teste de `SceneRenderer` com `QRhi` `Null` não depende de
  janela. Ajuste aprovado pelo mantenedor; o texto do REQ-002-29 não mudou.
- **Trocar de backend depois da janela visível** recria a janela nativa e pode
  piscar → a primeira tentativa é definida antes do `show()`; a troca só ocorre
  no caminho de falha.
- **Vulkan no Linux** depende de driver e de uma instância Vulkan que o
  `QRhiWidget` cria por conta própria; não consigo testar aqui → fallback para
  OpenGL já previsto; conferência manual em Linux fica pendente de alguém com a
  plataforma (será registrada como ressalva, como na 001).
- **`qt_add_shaders` em biblioteca estática**: os recursos podem não ser
  ligados ao executável → é o padrão suportado pelo Qt (bibliotecas de objeto);
  o teste de `SceneRenderer` falha se os shaders não estiverem embutidos.
- **API do QRhi é semi-pública** (exige `Qt6::GuiPrivate`) e pode mudar entre
  versões menores do Qt → versão fixada no CI (6.8.3); uso confinado a
  `src/render`.
- **macOS não testável localmente** (Metal, telas Retina) → só o CI compila e
  roda o smoke test; AC-2 e AC-10 no macOS ficam como ressalva.
- **Máquina local sem o módulo Qt Shader Tools** → precisa ser instalado antes
  da implementação (download de cerca de 10 MB pelo `aqt`, com sua confirmação).

## 11. Novas dependências

Nenhuma biblioteca de terceiros. Do próprio Qt (LGPLv3, ligação dinâmica):

| Componente | Uso | Observação |
|------------|-----|------------|
| Qt Shader Tools (`qtshadertools`) | `qt_add_shaders` / `qsb` em tempo de build | Módulo adicional do instalador; entra no CI e no README. Já previsto na constituição §3.3 |
| `Qt6::GuiPrivate` | Headers `rhi/qrhi.h` | Exigido pelo Qt para usar o QRhi |
| `Qt6::Widgets` no módulo `render` | `QRhiWidget` | Muda a dependência do `render` descrita na arquitetura — ver [ADR 0002](../../decisions/0002-render-depende-de-qt-widgets.md) |

Eigen (já no `vcpkg.json`) passa a ser usado pelo `render` para a câmera.
