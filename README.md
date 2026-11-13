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
| Qt | 6.7 ou mais novo (o CI usa a 6.8.3), componentes Core, Gui, Widgets e Linguist Tools, mais o módulo adicional Qt Shader Tools |

As demais dependências (Eigen, spdlog, Catch2, OpenCASCADE) são baixadas e
compiladas pelo vcpkg na primeira configuração. **A primeira vez demora**: o
OpenCASCADE leva cerca de 40 minutos. Depois disso o vcpkg reutiliza o cache.

### Windows

- Visual Studio 2022 com a carga "Desenvolvimento para desktop com C++" (traz
  MSVC, CMake, Ninja e vcpkg).
- Qt para `msvc2022_64`, pelo instalador oficial ou com
  `aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -m qtshadertools -O C:\Qt`.
- Use o **Developer PowerShell for VS 2022** (ou abra a pasta no Visual Studio /
  VS Code), que já define o compilador e o `VCPKG_ROOT`.
- Clone o repositório em um **caminho curto** (por exemplo `C:\dev\femapp`). O
  vcpkg compila as dependências dentro de `build/`, e caminhos longos estouram
  o limite de 260 caracteres do Windows.

### macOS

- Xcode Command Line Tools, CMake e Ninja (`brew install cmake ninja`).
- vcpkg clonado e `VCPKG_ROOT` definido.
- Qt para macOS (instalador oficial ou `aqt`), com o módulo Qt Shader Tools.

### Linux (Ubuntu 22.04+)

- `sudo apt install build-essential cmake ninja-build git curl zip unzip tar pkg-config`
- Bibliotecas de sistema para o OpenCASCADE e o Qt:
  `sudo apt install libgl1-mesa-dev libx11-dev libxext-dev libxi-dev libxmu-dev libxkbcommon-dev libfontconfig1-dev`
- vcpkg clonado e `VCPKG_ROOT` definido.
- Qt (instalador oficial ou `aqt`), com o módulo Qt Shader Tools.

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

Um dos testes (`femapp_smoke`) abre a janela principal por um instante. Em um
Linux sem tela, rode os testes sob um servidor gráfico virtual:
`xvfb-run -a ctest --preset dev`.

O executável fica em `build/dev/bin/` (no macOS, dentro de `femapp.app`):

```bash
build/dev/bin/femapp
```

Opções de linha de comando:

| Opção | Efeito |
|-------|--------|
| `<arquivo.msh>` | Importa a malha do Gmsh (formato 4.1, ASCII ou binário) ao abrir |
| `--version` | Imprime a versão e sai |
| `--smoke-test` | Abre a janela principal e fecha sozinho (usado nos testes) |

Malhas também podem ser abertas em Arquivo ▸ Importar malha (Ctrl+I) ou
arrastando o arquivo `.msh` para a janela.

Mouse no viewport:

| Ação | Efeito |
|------|--------|
| Botão do meio + arrastar | Desloca a vista (pan) |
| Shift + botão do meio + arrastar | Gira em torno da parte da malha em vista |
| Roda | Zoom em torno do ponto sob o cursor |
| Clique esquerdo | Seleciona um item (Editar ▸ Selecionar escolhe os tipos: elementos, faces, arestas, nós) |
| Arrastar da esquerda para a direita | Janela: seleciona o que está inteiro dentro |
| Arrastar da direita para a esquerda | Cruzamento: seleciona o que está dentro ou toca o retângulo |
| Shift + clique ou arrasto | Retira da seleção |
| Esc ou clique fora da malha | Limpa a seleção (sem comando ativo) |

Desenho (feature 005), pela linha de comando na parte de baixo da janela ou
pelos menus Desenho e Modificar. Os comandos ficam em inglês, como no
AutoCAD:

| Comando | Atalho | Efeito |
|---------|--------|--------|
| `POINT` | `PO` | Pontos |
| `LINE` | `L` | Linhas; opções `U` (desfazer o último trecho) e `C` (fechar) |
| `PLINE` | `PL` | Uma polilinha; opções `U` e `C` |
| `MOVE` | `M` | Move os objetos selecionados (ponto base e segundo ponto) |
| `COPY` | `CO` | Copia, quantas vezes quiser, até Enter |
| `ERASE` | `E` ou Delete | Apaga os objetos selecionados |
| `UNDO` / `REDO` | Ctrl+Z / Ctrl+Y | Desfaz e refaz um comando inteiro |

Pontos digitados: `x,y` ou `x,y,z` (absoluto), `@dx,dy[,dz]` (relativo ao
último ponto), `d<ângulo` e `@d<ângulo` (polar, ângulo em graus a partir de
+X), ou só uma distância, na direção do cursor. Decimal com ponto. Enter ou
Espaço repetem o último comando; Esc ou o botão direito cancelam; Tab
completa o nome; as setas percorrem o histórico. F3 liga o snap a objetos
(extremidade, meio, interseção), F9 o encaixe na grade (passo em Ferramentas
▸ Configurações de desenho) e F8 o modo ortogonal. O mouse marca pontos no
plano Z = 0. Editar ▸ Alvo da seleção escolhe se a seleção sem comando pega o
desenho ou a malha.

Para malhas grandes (centenas de milhares de elementos ou mais), use o build
de Release (`cmake --preset release` e `cmake --build --preset release`): o
build `dev` é de depuração e chega a ser dez vezes mais lento. O
`bench_mesh_scene` mede a importação e a seleção de um arquivo:
`build/release/bin/bench_mesh_scene <arquivo.msh>`.

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
