# Tarefas — 001 project-skeleton

Status: em andamento
Design: [design.md](design.md) · Requisitos: [requirements.md](requirements.md)

Regras:
- Uma tarefa = uma mudança pequena, compilável e testável.
- Testes vêm antes ou junto da implementação que verificam.
- `[P]` marca tarefas que podem ser feitas em paralelo com a anterior.
- `[M]` marca tarefas que dependem de ação do mantenedor.
- Marque `[x]` somente com build e testes passando.
- Os IDs abaixo omitem o prefixo: `01` significa `REQ-001-01`.

## Fase 0 — Pré-requisitos do ambiente local

- [x] **T00** `[M]` Instalar o Qt 6.8.3 para MSVC 2022 64-bit (instalador oficial
  ou `aqtinstall`) e anotar o caminho (ex.: `C:\Qt\6.8.3\msvc2019_64`).
- [x] **T01** Confirmar que, no Developer PowerShell do Visual Studio, `cmake`,
  `ninja`, `cl`, `clang-format` e `clang-tidy` respondem e que `VCPKG_ROOT`
  aponta para um vcpkg válido; registrar as versões no README (T22).

## Fase 1 — Repositório e build vazio

- [x] **T02** Inicializar o git com ramo `main`; revisar o `.gitignore` para
  cobrir `build/` e `vcpkg_installed/`; primeiro commit com as specs —
  cobre: 21
- [x] **T03** `[P]` Criar `.clang-format` e `.clang-tidy` segundo as convenções
  do `CLAUDE.md` — arquivos: `.clang-format`, `.clang-tidy` — cobre: 20
- [x] **T04** Criar o `CMakeLists.txt` raiz: `project(femapp VERSION 0.1.0)`,
  C++20 obrigatório, opções `FEMAPP_BUILD_TESTS` e `FEMAPP_WARNINGS_AS_ERRORS`,
  saída dos executáveis em `bin/`; criar `cmake/CompilerWarnings.cmake` —
  cobre: 01, 05, 06, 16, 19
- [x] **T05** Criar `vcpkg.json` (`eigen3`, `catch2`, `spdlog`, baseline fixado),
  os triplets `cmake/triplets/x64-linux-femapp.cmake` e
  `arm64-osx-femapp.cmake`, e `CMakePresets.json` com os seis presets de
  configuração, build e teste; erro claro se `VCPKG_ROOT` faltar.
  Verificar: `cmake --preset dev` configura o projeto ainda vazio —
  cobre: 01, 02, 03

## Fase 2 — Módulos de biblioteca

- [x] **T06** Criar `cmake/FemappModule.cmake` (`femapp_add_module`) e
  `tests/CMakeLists.txt` (Catch2, `catch_discover_tests`, respeitando
  `FEMAPP_BUILD_TESTS`) — cobre: 07, 18, 19
- [x] **T07** Escrever `tests/unit/core/test_core.cpp`: `versionString()` igual
  à versão do projeto (deve falhar ao compilar) — cobre: 10, 16
- [x] **T08** Implementar `fem_core`: `version.hpp`, `version.cpp`,
  `version_config.hpp.in` até T07 passar — arquivos: `src/core/…` —
  cobre: 07, 10, 16
- [x] **T09** Acrescentar `opencascade` (sem recursos opcionais) ao `vcpkg.json`
  e reconfigurar. Primeira compilação longa; registrar o tempo — cobre: 27
- [x] **T10** Escrever `tests/unit/geom/test_geom.cpp`: `boxVolume(2,3,4) == 24`
  (tolerância relativa 1e-12) e `kernelVersion()` ≥ 7.8 (deve falhar) —
  cobre: 10, 28
- [x] **T11** Implementar `fem_geom`: `kernel_check.hpp` sem headers do
  OpenCASCADE, `kernel_check.cpp` com OpenCASCADE ligado como `PRIVATE`, até
  T10 passar — arquivos: `src/geom/…` — cobre: 07, 08, 27, 28, 29
- [x] **T12** Verificar manualmente que um `#include <gp_Pnt.hxx>` em `fem_core`
  ou em um teste não compila; desfazer — cobre: 29
- [x] **T13** `[P]` Criar `fem_mesher` e `fem_io` com `moduleName()` e seus
  testes (teste primeiro) — arquivos: `src/mesher/…`, `src/io/…`,
  `tests/unit/mesher/…`, `tests/unit/io/…` — cobre: 07, 08, 10, 18
- [x] **T14** Acrescentar `find_package(Qt6 6.7 …)` com mensagem de erro que cita
  a versão mínima e `CMAKE_PREFIX_PATH`; verificar a mensagem configurando sem
  o caminho do Qt — cobre: 04
- [x] **T15** Criar `fem_render` (liga `Qt6::Gui`) com `moduleName()` e seu
  teste (teste primeiro) — arquivos: `src/render/…`, `tests/unit/render/…` —
  cobre: 07, 08, 10, 18
- [x] **T16** Criar `cmake/CheckNoQtIncludes.cmake` e o teste CTest
  `check_no_qt_includes`; verificar manualmente que um `#include <QString>` em
  `core` faz o teste falhar e o build quebrar; desfazer — cobre: 09

## Fase 3 — Aplicativo

- [x] **T17** Registrar o teste CTest `femapp_version` (saída casa com
  `femapp 0\.1\.0`, código 0); deve falhar por não existir o executável —
  cobre: 14, 16
- [x] **T18** Criar o alvo `femapp` com `cli_options.hpp/.cpp` e `main.cpp`
  tratando `--version` antes de criar `QApplication`, até T17 passar —
  arquivos: `src/app/…` — cobre: 07, 08, 14, 16
- [x] **T19** No Windows, passo pós-build com `windeployqt` na pasta `bin/`;
  conferir que `femapp` e todos os testes rodam direto da pasta de build, sem
  ajustar o `PATH` — cobre: 30
- [x] **T20** Registrar o teste CTest `femapp_smoke` (`--smoke-test`, código 0,
  limite 30 s, `QT_QPA_PLATFORM=offscreen`); deve falhar — cobre: 15
- [x] **T21** Implementar `MainWindow` (título, menus Arquivo ▸ Sair e
  Ajuda ▸ Sobre, barra de status, caixa "Sobre" com versão, Qt e licença) e o
  modo `--smoke-test`, até T20 passar — arquivos: `src/app/main_window.*`,
  `main.cpp` — cobre: 11, 12, 13, 15
- [x] **T22** Tradução: todos os textos em `tr()`, `i18n/femapp_pt_BR.ts`,
  `qt_add_translations`, carga do `QTranslator` pelo idioma do sistema —
  cobre: 17
- [ ] **T23** `[M]` Roteiro manual: abrir o aplicativo, conferir menus, "Sobre",
  "Sair" (código 0) e o idioma em português e em inglês (AC-5, AC-7); medir o
  tempo de abertura (NFR-02) — cobre: 11, 12, 13, 17

## Fase 4 — Documentação e integração contínua

- [x] **T24** Escrever o `README.md`: descrição, pré-requisitos por plataforma,
  build, teste, execução, versão fixada do `clang-format` — cobre: 26, NFR-03
- [x] **T25** `[M]` Criar o repositório no GitHub e informar a URL; adicionar o
  remoto e fazer o primeiro push (com sua confirmação) — cobre: 22
- [ ] **T26** Criar `.github/workflows/ci.yml` com os jobs `windows`, `macos`,
  `linux-gcc` e `linux-clang`: Qt 6.8.3 com cache, vcpkg no commit do baseline
  com cache binário, pacotes `apt` no Linux, configurar, compilar, `ctest`
  (inclui o smoke test) — cobre: 22, 23, 25
- [ ] **T27** Corrigir o que os quatro jobs revelarem até ficarem verdes. Se o
  triplet próprio falhar no macOS, aplicar o plano B do design e registrar a
  mudança no `design.md` — cobre: 01, 22, 27, 28, 30
- [ ] **T28** Acrescentar ao CI a verificação de ligação dinâmica do
  OpenCASCADE (`dumpbin /dependents`, `ldd`, `otool -L`) — cobre: 27
- [ ] **T29** Acrescentar o job `format` (`clang-format --dry-run --Werror`) e o
  passo `run-clang-tidy` no job `linux-clang`; corrigir os apontamentos —
  cobre: 24
- [ ] **T30** Abrir um PR de teste com um aviso de compilação, um arquivo mal
  formatado e um diagnóstico de `clang-tidy`; confirmar que o CI falha nos três
  casos; fechar o PR sem mesclar (AC-9) — cobre: 06, 24
- [ ] **T31** Medir o tempo dos jobs com cache e comparar com a meta de 5 min
  (NFR-01); registrar o resultado aqui

## Fase 5 — Fechamento

- [ ] **T90** Build e testes verdes nos quatro jobs do CI
- [ ] **T91** `clang-format` e `clang-tidy` sem pendências
- [ ] **T92** Conferir os doze critérios de aceite de `requirements.md`,
  incluindo AC-10 (seguir só o README em um clone limpo)
- [ ] **T93** Atualizar `specs/roadmap.md` para `concluída` e o `CLAUDE.md` se
  os comandos de build tiverem mudado

## Matriz de rastreabilidade

| Requisito | Tarefas | Verificação |
|-----------|---------|-------------|
| REQ-001-01 | T04, T05, T27 | Build pelos presets, local e CI (AC-1) |
| REQ-001-02 | T05 | Seis presets configuram; quatro deles no CI |
| REQ-001-03 | T05 | Configuração baixa as dependências (AC-1) |
| REQ-001-04 | T14 | Configuração sem Qt, manual (AC-2) |
| REQ-001-05 | T04 | Revisão do CMake (`CXX_STANDARD_REQUIRED`) |
| REQ-001-06 | T04, T30 | PR de teste com aviso falha no CI (AC-9) |
| REQ-001-07 | T06, T08, T11, T13, T15, T18 | Todos os alvos compilam (AC-3) |
| REQ-001-08 | T11, T13, T15, T18 | Revisão do grafo de alvos contra a arquitetura |
| REQ-001-09 | T16 | `check_no_qt_includes` + verificação manual (AC-4) |
| REQ-001-10 | T07, T08, T10, T11, T13, T15 | `test_core`, `test_geom`, `test_mesher`, `test_io`, `test_render` |
| REQ-001-11 | T21, T23 | `femapp_smoke` + roteiro manual (AC-5) |
| REQ-001-12 | T21, T23 | Roteiro manual (AC-5) |
| REQ-001-13 | T21, T23 | `femapp_smoke` (código 0) + roteiro manual (AC-5) |
| REQ-001-14 | T17, T18 | `femapp_version` (AC-6) |
| REQ-001-15 | T20, T21 | `femapp_smoke` (AC-6) |
| REQ-001-16 | T04, T07, T08, T17 | `test_core`, `femapp_version` |
| REQ-001-17 | T22, T23 | Roteiro manual (AC-7) |
| REQ-001-18 | T06, T13, T15 | `ctest --preset dev` lista e passa todos (AC-3) |
| REQ-001-19 | T04, T06 | Configurar com `-DFEMAPP_BUILD_TESTS=OFF` |
| REQ-001-20 | T03 | Arquivos presentes; job `format` |
| REQ-001-21 | T02 | `git status` limpo após um build |
| REQ-001-22 | T25, T26, T27 | Quatro jobs verdes (AC-8) |
| REQ-001-23 | T26 | `femapp_smoke` nos quatro jobs (AC-8) |
| REQ-001-24 | T29, T30 | PR de teste falha (AC-9) |
| REQ-001-25 | T26, T31 | Segunda execução do CI restaura os caches |
| REQ-001-26 | T24 | AC-10 em T92 |
| REQ-001-27 | T09, T11, T27, T28 | `test_geom` + verificação de ligação dinâmica (AC-11, AC-12) |
| REQ-001-28 | T10, T11, T27 | `test_geom` nos quatro jobs (AC-11) |
| REQ-001-29 | T11, T12 | Verificação manual; dependência `PRIVATE` (AC-12) |
| REQ-001-30 | T19, T27 | Testes rodam da pasta de build, local e CI (AC-11) |
| NFR-001-01 | T31 | Tempo dos jobs |
| NFR-001-02 | T23 | Medição manual |
| NFR-001-03 | T24 | Revisão do README |

## Notas de implementação

**2026-11-09 — Fases 0 a 2 (parcial), verificado localmente no Windows com o preset `dev`:**

- T01: CMake 3.31.6, Ninja 1.12.1, MSVC 19.44, clang-format/clang-tidy 19.1.5,
  vcpkg do Visual Studio (2025-11-19). `clang-format` e `clang-tidy` ficam em
  `VC\Tools\Llvm\x64\bin`, fora do `PATH` do Developer PowerShell.
- T02: repositório inicializado em `main`, `.gitignore` conferido (após um build,
  `build/` não aparece em `git status`) e `.gitattributes` acrescentado para
  fixar fins de linha LF. **Pendente: o primeiro commit**, porque o git não tem
  `user.name`/`user.email` configurados nesta máquina.
- T05: baseline do vcpkg = tag `2026.10.16`
  (`9eec002df1cd109f8f104cd966a763daf9751489`). O triplet padrão por sistema é
  escolhido no `CMakeLists.txt` raiz, para que `dev` e `release` funcionem nas
  três plataformas sem presets condicionais.
- T07/T10/T13: os testes foram escritos antes das implementações, mas compilados
  junto com elas; a falha inicial não foi observada separadamente.
- T09: o baseline traz o **OpenCASCADE 8.0.1**. A primeira compilação levou
  **38 min** (40 min no total da instalação). Reconfigurações seguintes usam o
  cache binário e levam segundos.
- T11: além das bibliotecas listadas no design, foi preciso ligar `TKGeomBase`
  (onde está `GProp_GProps`). O CMake de `geom` recusa a configuração se alguma
  biblioteca do OpenCASCADE não for dinâmica.
- T12: `#include <gp_Pnt.hxx>` em `test_mesher.cpp` falhou com C1083, como esperado.
- T16: script e teste CTest criados; com `#include <QString>` em
  `src/core/version.cpp` o teste falhou apontando arquivo e linha. **Falta
  repetir a parte "o build quebra" com o Qt instalado**, por isso a tarefa
  continua aberta.
- Resultado: 6 de 6 testes passam, zero avisos com `/W4`, `clang-format` limpo.

**Desvios do design a registrar:** `clang-format`/`clang-tidy` 19 no lugar de 18
(para igualar a versão local); `TKGeomBase` na lista de bibliotecas; teste de
versão do núcleo aceita 8.x.

**Bloqueio:** T14 em diante exigem o Qt instalado (T00).

**2026-11-10 — Fases 2 (restante) e 3, e T24, verificado localmente no Windows com o preset `dev`:**

- T00: Qt 6.8.3 `msvc2022_64` instalado em `C:\Qt\6.8.3\msvc2022_64` com o
  `aqtinstall` (2,0 GB). O CMake o encontra pela variável de ambiente
  `CMAKE_PREFIX_PATH`.
- T14: sem o caminho do Qt, a configuração para com a mensagem que cita a versão
  mínima 6.7 e `CMAKE_PREFIX_PATH` (AC-2).
- T16: concluída. Com `#include <QString>` em `src/core/version.cpp`, o build
  falha com C1083 (AC-4).
- T17–T18, T20–T21: o alvo `femapp` e os testes foram escritos juntos; a falha
  inicial dos testes `femapp_version` e `femapp_smoke` não foi observada antes
  da implementação.
- T19: `windeployqt` roda após o build de `femapp`. Problema encontrado e
  corrigido: ele não copiava o plugin `qoffscreen`, e o smoke test (que usa
  `QT_QPA_PLATFORM=offscreen`) falhava após 20 s; resolvido com
  `--include-plugins qoffscreen`. `femapp --version` roda de um terminal comum,
  sem ambiente do Visual Studio nem Qt no `PATH`.
- A descoberta dos testes Catch2 passou a `DISCOVERY_MODE PRE_TEST`, para não
  depender das DLLs durante o build.
- T22: 7 textos traduzidos e compilados pelo `lrelease`. **A exibição em
  português na janela não foi conferida visualmente** — fica para a T23.
- T24: `README.md` escrito. **As instruções de macOS e Linux não foram testadas**
  (AC-10 será conferido na T92, com o CI funcionando).
- Resultado: 9 de 9 testes passam, zero avisos com `/W4`, `clang-format` limpo.

**Pendentes que dependem do mantenedor:** T02 (primeiro commit: falta identidade
do git), T23 (roteiro manual da janela), T25 (repositório no GitHub).

**2026-11-10 — Workflow do CI escrito, ainda não executado:**

- T26, T28, T29: `.github/workflows/ci.yml` escrito com os jobs `windows`,
  `macos`, `linux-gcc`, `linux-clang` (com `clang-tidy`) e `format`, incluindo a
  verificação de ligação dinâmica do OpenCASCADE. O YAML é válido, mas **o
  workflow nunca rodou**: as três tarefas continuam abertas até o primeiro push
  (T25) e serão fechadas junto com a T27.
- `clang-tidy` 19 rodado localmente pela primeira vez sobre `src/` e `tests/`.
  Corrigido: inicializadores designados em `version()`, constantes `constexpr`
  no lugar de macros em `version_config.hpp.in`, `main` que não deixa escapar
  exceções, e `char**` no lugar de array C. Desligado
  `clang-analyzer-cplusplus.NewDelete`, que acusa falso positivo dentro dos
  handles do OpenCASCADE. Resultado: zero diagnósticos.
- Pontos do workflow com maior chance de precisar de ajuste na T27: pacote
  `clang-tidy-19` no `apt` do Ubuntu 24.04; triplet próprio no macOS; lista de
  pacotes de sistema para o OpenCASCADE no Linux; tempo da primeira execução
  (o vcpkg compila o OpenCASCADE em Debug e Release).

**2026-11-10 — Repositório remoto criado (T25, parcial):**

- Repositório público e vazio criado em `https://github.com/vj-fkx/femapp` e
  configurado como `origin`. **O push ainda não foi feito**: aguarda confirmação
  do mantenedor, por isso a T25 continua aberta.

**2026-11-10 — Primeiro push (T25 concluída):**

- Os commits foram reescritos com o e-mail `noreply` do GitHub antes do envio.
- `main` enviado para `origin` com o Git Credential Manager (login pelo
  navegador, feito pelo mantenedor). A execução nº 1 do CI começou no commit
  `d86e743`. Resultado ainda desconhecido; as correções ficam na T27.

**2026-11-10 — Execução nº 1 do CI (T27, em andamento):**

- `format`: passou em 8 s.
- `macos`: não iniciou — "The configuration 'macos-14-arm64' is not supported".
  A imagem `macos-14` foi descontinuada; trocada por `macos-15` no workflow e
  no design.
- Aviso de Node.js 20 obsoleto em `actions/checkout@v4`; atualizado para `v5`.
- `windows`, `linux-gcc` e `linux-clang`: ainda rodando quando esta nota foi
  escrita. A correção acima está commitada localmente e **não foi enviada**,
  porque um novo push cancelaria a execução em curso e perderia o cache do
  OpenCASCADE que ela está construindo.
