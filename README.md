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
| `MESH` | `ME` | Gera a malha de barras do desenho (feature 009) |

Pontos digitados: `x,y` ou `x,y,z` (absoluto), `@dx,dy[,dz]` (relativo ao
último ponto), `d<ângulo` e `@d<ângulo` (polar, ângulo em graus a partir de
+X), ou só uma distância, na direção do cursor. Decimal com ponto. Enter ou
Espaço repetem o último comando; Esc ou o botão direito cancelam; Tab
completa o nome; as setas percorrem o histórico. F3 liga o snap a objetos
(extremidade, meio, interseção), F9 o encaixe na grade (passo em Ferramentas
▸ Configurações de desenho) e F8 o modo ortogonal. O mouse marca pontos no
plano Z = 0. Editar ▸ Alvo da seleção escolhe se a seleção sem comando pega o
desenho ou a malha.

O `MESH` pede o tamanho dos elementos (Enter: um elemento por trecho, o que
basta para as vigas exatas) e gera a malha do desenho inteiro fora da thread
da interface. As linhas e polilinhas viram barras, os pontos viram nós, e os
nós coincidentes se fundem. As linhas se dividem onde outra termina no meio
delas (nó em T), onde se cruzam e onde há um ponto sobre elas. Linhas
sobrepostas são recusadas. A malha aparece com o desenho, o relatório vai
para a linha de comando, e uma edição depois da malha avisa que ela ficou
desatualizada. Cada entidade vira um grupo da malha chamado `entity <id>`.
Por ora os textos do `MESH` ficam em inglês. O `bench_line_mesh` mede a
geração de um pórtico com cerca de 100 mil trechos:
`build/release/bin/bench_line_mesh`.

### O modelo (menu Model)

Sobre o desenho ou a malha, o menu **Model** prepara a análise (feature
010). Cada mudança pode ser desfeita com Ctrl+Z, misturada às do desenho.

- **Unidades**: Model ▸ Units… escolhe entre N-m, kN-m, N-mm, kgf-cm e lbf-in.
  O sistema corrente aparece na barra de status. Trocar de sistema converte
  os valores do modelo; o desenho fica como está, lido na nova unidade de
  comprimento.
- **Materiais e seções**: New material… copia um material da biblioteca, por
  norma (Eurocódigos, ABNT NBR 6118 e 8800, ACI 318, AISC 360), ou cria um vazio. New section… cria uma
  seção genérica, paramétrica, de paredes finas ou um polígono maciço a
  partir das polilinhas fechadas selecionadas. O painel Properties mostra as
  propriedades calculadas e o desenho da seção.
- **Atribuições**: Assign bars…, Beam details ▸ … e Supports ▸ … valem para a
  seleção do viewport (linhas e pontos do desenho, ou elementos e nós da
  malha). Sem seleção, o diálogo oferece os grupos da malha. Uma atribuição
  nova sobre o mesmo alvo substitui a anterior.
- **Casos e combinações**: Load cases… cria, ativa e classifica os casos pela
  norma e pela categoria da ação, com os ψ. Add load ▸ … põe uma carga no
  caso ativo. New combination… soma casos com fatores. Uma combinação ativa
  pode ser analisada como um caso.
- **Símbolos**: os apoios e as cargas do caso ativo aparecem no viewport,
  com tamanho constante na tela. Show symbols liga e desliga os símbolos;
  Show local axes mostra os eixos das barras selecionadas ou de todas.
- **Conferência**: Check model lista num dock os problemas e os avisos. Um
  clique seleciona o item na árvore, ou realça os elementos e nós de um
  problema da análise.

A árvore Model, à esquerda, mostra tudo isso e a malha. Um duplo clique ativa
um caso, e o menu de contexto renomeia, duplica, copia cargas e apaga. Os
textos do menu Model ficam em inglês por ora.

### A análise (menu Analysis)

Com o modelo pronto, o menu **Analysis** roda a análise (feature 011):

- **Settings…** escolhe o tipo, para todos os casos: linear, segunda
  ordem (com a tolerância e o máximo de iterações) ou flambagem linear
  (com o número de modos).
- **Run analysis** (F5) analisa o caso ou a combinação ativa. **Run all**
  analisa todos, em ordem.
- **Durante a análise:** a barra de status mostra a etapa e o progresso, e
  a interface continua respondendo. **Cancel**, o botão da barra ou o Esc
  interrompem a análise. As edições feitas enquanto ela roda não a
  atingem.
- **O resultado de cada caso:**
  - o console recebe um resumo com os maiores deslocamentos e esforços, o
    equilíbrio das reações e, na flambagem, os fatores;
  - um caso com erros na conferência não roda: o dock Check abre com os
    problemas;
  - uma falha da análise, como um mecanismo, aparece no console e na
    conferência, com os nós e elementos citados realçados.
- **O dock Results** (Show Results) mostra as tabelas do caso escolhido:
  deslocamentos, reações, esforços nas barras ou modos de flambagem.
  Selecionar linhas realça os nós e as barras no viewport.
- **Atual ou desatualizado:** a árvore mostra, ao lado de cada caso, o
  tipo do resultado e se ele ainda vale ("linear · current"). Qualquer
  edição o deixa "out of date"; desfazer até o estado analisado volta a
  "current".

### Os resultados no viewport

Depois de uma análise, o resultado escolhido no dock Results pode ser
desenhado no lugar da malha (feature 012). Para isso, marque **Show in
viewport** no alto do dock ou use View ▸ **Show Results in Viewport**.

- **Deformada:** cada barra é desenhada curva, como o núcleo a calcula. A
  escala é automática (o maior deslocamento vale 10% do tamanho da
  estrutura), mas pode ser a do usuário (**User**) ou a real (**Real**). A
  forma original pode aparecer por baixo (**Original**).
- **Cores:** a grandeza escolhida colore as barras: deslocamentos (u, v,
  w, |u|), rotações ou esforços (N, Vy, Vz, T, My, Mz), com legenda e
  unidades. As cores podem ser contínuas ou em faixas (**Bands**), e a
  faixa pode ser fixada (**Fixed range**). **None** desliga as cores.
- **Flambagem:** o modo e o seu fator aparecem no alto do viewport. **Play**
  anima o modo, ou a deformada de um caso estático.
- **Valores:** as reações aparecem como setas com os valores, e o máximo e
  o mínimo da grandeza ficam marcados. Um clique numa barra ou num nó
  mostra o valor ali.
- **Desatualizado:** se o modelo mudar depois da análise, um aviso em
  laranja aparece no alto do viewport.

O `bench_result_view` mede a preparação de um resultado com o pórtico de
cerca de 10⁵ barras: `build/release/bin/bench_result_view`.

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
