# Design — 001 project-skeleton

Status: aprovado · Data: 2026-11-09
Requisitos: [requirements.md](requirements.md)

## 1. Resumo da abordagem

Um projeto CMake único com um subdiretório por módulo. Cinco bibliotecas
estáticas e um executável Qt, ligados exatamente conforme o grafo da
arquitetura. Dependências C++ pelo vcpkg em modo manifesto com baseline fixado;
Qt por instalação externa. O OpenCASCADE é ligado dinamicamente e de forma
privada a `fem_geom`. Um workflow do GitHub Actions compila, testa e verifica
qualidade em quatro configurações.

## 2. Verificação da constituição

| Princípio | Atende? | Observação |
|-----------|---------|------------|
| Multiplataforma (§2) | Sim | Mesmos presets e CI nas três plataformas; nenhum código específico de plataforma |
| GPU somente via QRhi (§3) | N/A | Sem renderização nesta feature; `fem_render` só liga `Qt6::Gui` |
| Camadas / `core` sem Qt (§4) | Sim | Imposto pelo grafo de alvos e por um teste que varre os includes |
| Correção numérica (§5) | N/A | Sem código numérico |
| Qualidade (§6) | Sim | C++20, avisos altos, `-Werror` no CI, clang-format, clang-tidy, Catch2 |
| Simplicidade / dependências (§7) | Sim | Só dependências já aprovadas; nenhuma nova |
| Licenciamento (§8) | Sim | Qt e OpenCASCADE dinâmicos; nada GPL |

## 3. Módulos e arquivos afetados

Todos os arquivos são novos.

### Raiz e build

| Arquivo | Responsabilidade | Requisito |
|---------|------------------|-----------|
| `CMakeLists.txt` | Projeto `femapp VERSION 0.1.0`, C++20 obrigatório, opções, subdiretórios | 01, 05, 16, 19 |
| `CMakePresets.json` | Presets de configuração, build e teste | 01, 02 |
| `vcpkg.json` | Manifesto: `eigen3`, `catch2`, `spdlog`, `opencascade`; `builtin-baseline` fixado | 03, 27 |
| `cmake/CompilerWarnings.cmake` | Função `femapp_set_warnings(alvo)` | 06 |
| `cmake/FemappModule.cmake` | Função `femapp_add_module(nome …)`: cria a lib, includes, avisos, alias `fem::nome` | 07 |
| `cmake/CheckNoQtIncludes.cmake` | Script que falha se achar `#include <Q…>` ou `"Q…"` em `core`, `geom`, `mesher` | 09 |
| `cmake/triplets/x64-linux-femapp.cmake` | Triplet: estático por padrão, `opencascade` dinâmico | 27 |
| `cmake/triplets/arm64-osx-femapp.cmake` | Idem para macOS | 27 |
| `.clang-format`, `.clang-tidy` | Estilo e análise estática | 20 |
| `.github/workflows/ci.yml` | Integração contínua | 22–25 |
| `README.md` | Descrição, pré-requisitos, build, teste, execução | 26 |

### Código

| Módulo | Arquivos | Conteúdo |
|--------|----------|----------|
| core | `src/core/CMakeLists.txt`, `include/fem/core/version.hpp`, `version.cpp`, `version_config.hpp.in` | Versão do aplicativo (fonte única, gerada pelo CMake) |
| geom | `src/geom/CMakeLists.txt`, `include/fem/geom/kernel_check.hpp`, `kernel_check.cpp` | Verificação do OpenCASCADE |
| mesher | `src/mesher/CMakeLists.txt`, `include/fem/mesher/module.hpp`, `module.cpp` | Função trivial |
| io | `src/io/CMakeLists.txt`, `include/fem/io/module.hpp`, `module.cpp` | Função trivial |
| render | `src/render/CMakeLists.txt`, `include/fem/render/module.hpp`, `module.cpp` | Função trivial (usa `Qt6::Gui`) |
| app | `src/app/CMakeLists.txt`, `main.cpp`, `main_window.hpp/.cpp`, `cli_options.hpp/.cpp`, `i18n/femapp_pt_BR.ts` | Executável |
| testes | `tests/CMakeLists.txt`, `tests/unit/<módulo>/test_<módulo>.cpp` (5 arquivos) | Um executável Catch2 por biblioteca |

Layout de headers: `src/<módulo>/include/fem/<módulo>/…`, incluídos como
`#include <fem/core/version.hpp>`.

## 4. Interfaces públicas

```cpp
// fem/core/version.hpp                                   REQ-001-10, 16
namespace fem::core {
struct Version { int major; int minor; int patch; };
[[nodiscard]] Version version() noexcept;
[[nodiscard]] std::string_view versionString() noexcept;   // "0.1.0"
}

// fem/geom/kernel_check.hpp  (sem headers do OpenCASCADE) REQ-001-10, 28, 29
namespace fem::geom {
// Constrói um bloco dx × dy × dz no núcleo geométrico e devolve seu volume.
[[nodiscard]] double boxVolume(double dx, double dy, double dz);
// Versão do núcleo geométrico, ex.: "7.8.1".
[[nodiscard]] std::string kernelVersion();
}

// fem/mesher/module.hpp, fem/io/module.hpp, fem/render/module.hpp   REQ-001-10
namespace fem::mesher { [[nodiscard]] std::string_view moduleName() noexcept; }
namespace fem::io     { [[nodiscard]] std::string_view moduleName() noexcept; }
namespace fem::render { [[nodiscard]] std::string_view moduleName() noexcept; }

// app/cli_options.hpp  (sem dependência de QApplication)  REQ-001-14, 15
namespace fem::app {
struct CliOptions { bool showVersion = false; bool smokeTest = false; };
[[nodiscard]] CliOptions parseCliOptions(std::span<const char* const> args);
}

// app/main_window.hpp                                     REQ-001-11, 12, 13
namespace fem::app {
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
private slots:
    void showAbout();
};
}
```

## 5. Dados e algoritmos

### 5.1 Grafo de alvos (REQ-001-07, 08)

| Alvo | Tipo | Liga a |
|------|------|--------|
| `fem_core` | STATIC | `Eigen3::Eigen`, `spdlog::spdlog` |
| `fem_geom` | STATIC | `Eigen3::Eigen`; **PRIVATE** OpenCASCADE (`TKernel`, `TKMath`, `TKBRep`, `TKPrim`, `TKTopAlgo`, `TKG3d`, `TKGeomBase`) |
| `fem_mesher` | STATIC | `fem_core`, `fem_geom` |
| `fem_io` | STATIC | `fem_core`, `fem_geom` |
| `fem_render` | STATIC | `fem_core`, `fem_geom`, `Qt6::Gui` |
| `femapp` | executável | todos os anteriores, `Qt6::Widgets` |

O OpenCASCADE como dependência `PRIVATE` faz com que seus diretórios de include
não se propaguem: qualquer outro alvo que tente incluir um header dele não
compila, o que impõe REQ-001-29 pelo próprio build.

### 5.2 Proibição de Qt nos módulos puros (REQ-001-09)

Duas barreiras:
1. `fem_core`, `fem_geom` e `fem_mesher` não ligam nenhum alvo Qt, logo não têm
   os diretórios de include do Qt.
2. O teste CTest `check_no_qt_includes` roda `cmake/CheckNoQtIncludes.cmake`,
   que varre os fontes desses módulos e falha listando arquivo e linha.

### 5.3 Presets (REQ-001-01, 02)

| Preset | Gerador | Compilador | Tipo | Observação |
|--------|---------|------------|------|------------|
| `dev` | Ninja | padrão da plataforma | Debug | Testes ligados; triplet escolhido por condição de SO |
| `release` | Ninja | padrão da plataforma | Release | |
| `windows-msvc` | Ninja | `cl` | RelWithDebInfo | Triplet `x64-windows` |
| `macos-clang` | Ninja | Apple Clang | RelWithDebInfo | Triplet `arm64-osx-femapp` |
| `linux-gcc` | Ninja | `g++` | RelWithDebInfo | Triplet `x64-linux-femapp` |
| `linux-clang` | Ninja | `clang++` | RelWithDebInfo | Idem |

Comum a todos (preset oculto `base`): `CMAKE_TOOLCHAIN_FILE` =
`$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake`, `VCPKG_OVERLAY_TRIPLETS` =
`cmake/triplets`, `CMAKE_EXPORT_COMPILE_COMMANDS=ON`, pasta `build/<preset>`.
Os presets de plataforma ligam `FEMAPP_WARNINGS_AS_ERRORS`. No Windows, o Ninja
exige o ambiente do MSVC (Developer PowerShell, Visual Studio ou VS Code).

Opções CMake: `FEMAPP_BUILD_TESTS` (padrão ON), `FEMAPP_WARNINGS_AS_ERRORS`
(padrão OFF).

### 5.4 Ligação dinâmica e localização em tempo de execução (REQ-001-27, 30)

| Plataforma | OpenCASCADE | Como o executável acha as bibliotecas |
|------------|-------------|----------------------------------------|
| Windows | Triplet `x64-windows` (tudo dinâmico) | Todos os executáveis vão para `build/<preset>/bin`; o vcpkg copia suas DLLs para lá; um passo pós-build roda `windeployqt` nessa pasta |
| Linux | Triplet próprio: estático, exceto `opencascade` dinâmico | RPATH da árvore de build, gerado pelo CMake |
| macOS | Idem | RPATH da árvore de build |

O port `opencascade` é pedido sem recursos opcionais (sem VTK, TBB, FreeImage)
para reduzir o tempo de compilação.

### 5.5 Versão (REQ-001-16)

`project(femapp VERSION 0.1.0)` → `configure_file` gera `version_config.hpp` no
diretório de build de `fem_core` → `fem::core::version()`. O aplicativo e a
caixa "Sobre" leem dali.

### 5.6 Tradução (REQ-001-17)

`qt_add_translations` compila `femapp_pt_BR.ts` e o embute como recurso em
`:/i18n`. No início, `QTranslator::load(QLocale(), "femapp", "_", ":/i18n")`.
Textos-fonte em inglês.

### 5.7 Integração contínua (REQ-001-22 a 25)

Arquivo `.github/workflows/ci.yml`, disparado em `push` e `pull_request`.

| Job | Executor | Preset | Passos |
|-----|----------|--------|--------|
| `windows` | `windows-2022` | `windows-msvc` | configurar, compilar, `ctest` |
| `macos` | `macos-14` (arm64) | `macos-clang` | idem |
| `linux-gcc` | `ubuntu-24.04` | `linux-gcc` | idem |
| `linux-clang` | `ubuntu-24.04` | `linux-clang` | idem + `run-clang-tidy` sobre `compile_commands.json` |
| `format` | `ubuntu-24.04` | — | `clang-format --dry-run --Werror` em `src/` e `tests/` |

- Qt: ação `jurplel/install-qt-action`, versão fixada **6.8.3**, com cache.
- vcpkg: checkout no commit do baseline; cache binário em pasta local
  (`VCPKG_DEFAULT_BINARY_CACHE`) guardada com `actions/cache`, chave derivada
  de `vcpkg.json` e dos triplets.
- Smoke test e testes de `fem_render` rodam com `QT_QPA_PLATFORM=offscreen`.
- Linux: pacotes de sistema para compilar o OpenCASCADE e rodar o Qt (OpenGL,
  X11, xkbcommon, fontconfig) instalados por `apt`.
- `clang-format` e `clang-tidy` fixados na versão 19 no CI (igual à do Visual Studio 2022 local).

## 6. Fluxo

Inicialização do aplicativo (`main.cpp`):

1. `parseCliOptions(argv)` — antes de criar `QApplication`.
2. Se `showVersion`: imprime `femapp <versão>` em stdout e retorna 0. Não cria
   `QApplication`, então funciona sem servidor gráfico.
3. Cria `QApplication`, define nome e versão, carrega o tradutor.
4. Cria e mostra `MainWindow`.
5. Se `smokeTest`: `QTimer::singleShot(0, &app, &QApplication::quit)`.
6. `return app.exec()`.

## 7. Tratamento de erros

| Situação | Comportamento | Requisito |
|----------|---------------|-----------|
| Qt ausente ou < 6.7 | `find_package(Qt6 6.7 …)` falha; `message(FATAL_ERROR)` cita a versão mínima e `CMAKE_PREFIX_PATH` | 04 |
| Compilador sem C++20 | `CMAKE_CXX_STANDARD_REQUIRED ON` + `target_compile_features(cxx_std_20)` interrompem a configuração | 05 |
| `VCPKG_ROOT` não definido | `FATAL_ERROR` com instrução de como definir | 01, 03 |
| Argumento de linha de comando desconhecido | Ignorado (repasse ao Qt) | — |
| Tradução não encontrada | Interface em inglês, sem erro | 17 |

## 8. Estratégia de testes

| Teste | Tipo | Requisito | Verificação |
|-------|------|-----------|-------------|
| `test_core`: `versionString()` igual a `PROJECT_VERSION` | unitário | 10, 16 | Igualdade de texto |
| `test_geom`: `boxVolume(2, 3, 4)` | unitário | 10, 28 | `== 24` com tolerância relativa 1e-12 |
| `test_geom`: `kernelVersion()` | unitário | 27 | Versão ≥ 7.8 (o baseline traz a 8.0.1) |
| `test_mesher`, `test_io`, `test_render`: `moduleName()` | unitário | 10, 18 | Igualdade de texto |
| `check_no_qt_includes` | CTest (script) | 09 | Falha se houver include do Qt |
| `femapp_version`: `femapp --version` | CTest | 14, 16 | Saída casa com `femapp 0\.1\.0`, código 0 |
| `femapp_smoke`: `femapp --smoke-test` | CTest | 11, 15, 30 | Código 0, tempo limite 30 s |
| Include de Qt em `core` quebra o build | manual (AC-4) | 09 | Feito uma vez, não fica no repositório |
| Menus, "Sobre", "Sair", idioma | manual (AC-5, AC-7) | 11–13, 17 | Roteiro no `tasks.md` |
| Ligação dinâmica do OpenCASCADE | CI | 27 | `dumpbin /dependents`, `ldd`, `otool -L` listam as bibliotecas `TK*` |
| CI falha com aviso, má formatação ou diagnóstico | manual (AC-9) | 06, 24 | Um PR de teste, descartado depois |

### Ajuste ao REQ-001-18 (aprovado em 2026-11-09)

O módulo `app`, que é o próprio executável, é testado por `femapp_version` e
`femapp_smoke` em vez de um executável Catch2 separado. O texto do requisito
foi atualizado em `requirements.md`.

## 9. Alternativas consideradas

| Alternativa | Por que foi descartada |
|-------------|------------------------|
| Qt pelo vcpkg | Compilar o Qt do zero leva horas e estoura o cache do CI |
| Gerador "Visual Studio" no Windows | Presets diferentes por plataforma; Ninja é igual nas três e mais rápido |
| Triplets comunitários `*-dynamic` (tudo dinâmico) | Só o OpenCASCADE precisa ser dinâmico; o triplet próprio mantém o resto estático e simples |
| Bibliotecas dos módulos como SHARED | Exigiria macros de exportação sem benefício agora |
| `clang-tidy` durante o build (`CMAKE_CXX_CLANG_TIDY`) | Deixa todo build local lento; roda só no CI |
| Biblioteca separada `fem_app_ui` para testar a janela com Catch2 | Sem lógica a testar ainda; o smoke test cobre |

## 10. Riscos e mitigação

- **OpenCASCADE demora a compilar (dezenas de minutos por plataforma) na
  primeira execução** → cache binário do vcpkg; port sem recursos opcionais.
- **O port do OpenCASCADE pode falhar em algum triplet (principalmente macOS
  arm64 com triplet próprio)** → é exatamente o que esta feature quer descobrir;
  plano B: triplet comunitário `arm64-osx-dynamic`.
- **Versões diferentes de `clang-format` local e no CI formatam diferente** →
  versão 19 fixada no CI e documentada no README.
- **`femapp --version` não imprime em um console interativo do Windows**
  (aplicativo de subsistema gráfico); funciona com saída redirecionada, que é o
  caso do CTest e do CI → corrigir exigiria código específico de plataforma;
  adiado para a feature 039 (empacotamento).
- **Limite de 10 GB de cache do GitHub Actions** → uma chave por job, sem cache
  de pastas de build.

## 11. Novas dependências

Nenhuma além das já aprovadas na arquitetura e nos requisitos.

| Dependência | Versão | Licença | Origem |
|-------------|--------|---------|--------|
| Qt (Core, Gui, Widgets, LinguistTools) | 6.8.3 no CI; mínimo 6.7 | LGPLv3 | Instalador / `install-qt-action` |
| OpenCASCADE | ≥ 7.8 (a do baseline) | LGPL 2.1 + exceção | vcpkg, dinâmico |
| Eigen | 3.4 | MPL-2.0 | vcpkg |
| spdlog | a do baseline | MIT | vcpkg |
| Catch2 | 3.x | BSL-1.0 | vcpkg (só testes) |

## 12. Ambiente local do mantenedor (verificado em 2026-11-09)

| Item | Situação |
|------|----------|
| Visual Studio 2022 Community, MSVC 14.44 | Instalado |
| CMake, Ninja, vcpkg, clang-format, clang-tidy | Presentes dentro do Visual Studio; fora do `PATH` de um terminal comum |
| Git 2.39 | Instalado |
| Qt 6 | **Não instalado** — necessário antes da implementação |
| `gh` (GitHub CLI) | Não instalado — o repositório remoto terá de ser criado pelo site ou após instalar o `gh` |

A implementação local usará o Developer PowerShell do Visual Studio, com
`VCPKG_ROOT` apontando para o vcpkg que acompanha o Visual Studio.
