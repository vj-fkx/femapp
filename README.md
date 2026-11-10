# femapp

Aplicativo desktop de análise por elementos finitos, em C++20, com interface
Qt 6 e renderização por GPU. Multiplataforma: Windows, macOS e Linux.

Estado atual: esqueleto do projeto (versão 0.1.0). O plano completo está em
[specs/roadmap.md](specs/roadmap.md).

## Pré-requisitos

| Ferramenta | Versão |
|------------|--------|
| Compilador C++20 | MSVC 2022, Apple Clang 15+, GCC 12+ ou Clang 16+ |
| CMake | 3.25 ou mais novo |
| Ninja | qualquer versão recente |
| Git | qualquer versão recente |
| vcpkg | com a variável de ambiente `VCPKG_ROOT` apontando para ele |
| Qt | 6.7 ou mais novo (o CI usa a 6.8.3), componentes Core, Gui, Widgets e Linguist Tools |

As demais dependências (Eigen, spdlog, Catch2, OpenCASCADE) são baixadas e
compiladas pelo vcpkg na primeira configuração. **A primeira vez demora**: o
OpenCASCADE leva cerca de 40 minutos. Depois disso o vcpkg reutiliza o cache.

### Windows

- Visual Studio 2022 com a carga "Desenvolvimento para desktop com C++" (traz
  MSVC, CMake, Ninja e vcpkg).
- Qt para `msvc2022_64`, pelo instalador oficial ou com
  `aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O C:\Qt`.
- Use o **Developer PowerShell for VS 2022** (ou abra a pasta no Visual Studio /
  VS Code), que já define o compilador e o `VCPKG_ROOT`.

### macOS

- Xcode Command Line Tools, CMake e Ninja (`brew install cmake ninja`).
- vcpkg clonado e `VCPKG_ROOT` definido.
- Qt para macOS (instalador oficial ou `aqt`).

### Linux (Ubuntu 22.04+)

- `sudo apt install build-essential cmake ninja-build git curl zip unzip tar pkg-config`
- Bibliotecas de sistema para o OpenCASCADE e o Qt:
  `sudo apt install libgl1-mesa-dev libx11-dev libxext-dev libxi-dev libxmu-dev libxkbcommon-dev libfontconfig1-dev`
- vcpkg clonado e `VCPKG_ROOT` definido.
- Qt (instalador oficial ou `aqt`).

## Compilar, testar e executar

Informe ao CMake onde está o Qt, pela variável de ambiente `CMAKE_PREFIX_PATH`:

```powershell
$env:CMAKE_PREFIX_PATH = "C:\Qt\6.8.3\msvc2022_64"      # Windows (PowerShell)
```

```bash
export CMAKE_PREFIX_PATH=$HOME/Qt/6.8.3/gcc_64          # Linux (macOS: .../macos)
```

Depois:

```bash
cmake --preset dev
```

```bash
cmake --build --preset dev
```

```bash
ctest --preset dev
```

O executável fica em `build/dev/bin/` (no macOS, dentro de `femapp.app`):

```bash
build/dev/bin/femapp
```

Opções de linha de comando:

| Opção | Efeito |
|-------|--------|
| `--version` | Imprime a versão e sai |
| `--smoke-test` | Abre a janela principal e fecha sozinho (usado nos testes) |

## Presets

| Preset | Uso |
|--------|-----|
| `dev` | Desenvolvimento: Debug, compilador padrão da plataforma |
| `release` | Release, compilador padrão da plataforma |
| `windows-msvc`, `macos-clang`, `linux-gcc`, `linux-clang` | Usados no CI: RelWithDebInfo com avisos tratados como erro |

Opções do CMake: `FEMAPP_BUILD_TESTS` (padrão `ON`) e
`FEMAPP_WARNINGS_AS_ERRORS` (padrão `OFF`).

## Formatação e análise estática

O CI usa `clang-format` e `clang-tidy` na **versão 19**; use a mesma versão
localmente para evitar diferenças. No Windows eles vêm com o Visual Studio, em
`VC\Tools\Llvm\x64\bin`.

```bash
clang-format -i $(git ls-files 'src/*.cpp' 'src/*.hpp' 'tests/*.cpp')
```

## Estrutura

| Pasta | Conteúdo |
|-------|----------|
| `src/core` | Núcleo numérico (sem Qt) |
| `src/geom` | Geometria, sobre o OpenCASCADE (sem Qt) |
| `src/mesher` | Geração de malha (sem Qt) |
| `src/io` | Leitura e escrita de arquivos |
| `src/render` | Renderização por GPU (Qt RHI) |
| `src/app` | Interface gráfica e executável |
| `tests` | Testes (Catch2) |
| `specs` | Especificações: constituição, produto, arquitetura, roadmap e features |

## Como o projeto é desenvolvido

O femapp segue desenvolvimento guiado por especificação. Leia
[specs/constitution.md](specs/constitution.md) e [CLAUDE.md](CLAUDE.md) antes de
contribuir.

## Licença

[MIT](LICENSE). Qt (LGPLv3) e OpenCASCADE (LGPL 2.1 com exceção) são ligados
dinamicamente.
