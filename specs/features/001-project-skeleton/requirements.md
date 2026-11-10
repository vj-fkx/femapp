# Requisitos — 001 project-skeleton

Status: aprovado · Data: 2026-11-09

## 1. Contexto e objetivo

O repositório ainda não tem código. Esta feature cria o esqueleto sobre o qual
todas as outras serão construídas: sistema de build, gerenciamento de
dependências, os seis módulos da arquitetura (vazios, mas compiláveis), uma
janela Qt mínima, infraestrutura de testes, verificações de qualidade e
integração contínua nas três plataformas. O objetivo é que, a partir daqui,
qualquer feature possa ser implementada e verificada em Windows, macOS e Linux
sem trabalho adicional de infraestrutura.

## 2. Histórias de usuário

- **US-1** Como desenvolvedor, quero clonar o repositório e compilar com dois
  comandos em qualquer das três plataformas, para começar a trabalhar sem
  configuração manual.
- **US-2** Como desenvolvedor, quero que a estrutura de módulos e suas
  dependências permitidas sejam impostas pelo build, para que violações de
  arquitetura apareçam como erro e não em revisão.
- **US-3** Como mantenedor, quero que cada alteração seja compilada, testada e
  verificada automaticamente nas três plataformas, para que nada quebre sem aviso.
- **US-4** Como usuário, quero abrir o femapp e ver uma janela principal, para
  confirmar que o aplicativo roda na minha máquina.

## 3. Requisitos funcionais (EARS)

### Build e dependências

| ID | Requisito | História |
|----|-----------|----------|
| REQ-001-01 | O sistema DEVE ser configurado e compilado apenas com `cmake --preset <nome>` e `cmake --build --preset <nome>`, sem passos manuais além da instalação dos pré-requisitos documentados. | US-1 |
| REQ-001-02 | O sistema DEVE oferecer os presets `dev` (Debug, testes ligados, compilador padrão da plataforma), `release`, `windows-msvc`, `macos-clang`, `linux-gcc` e `linux-clang`. | US-1 |
| REQ-001-03 | O sistema DEVE obter Eigen, Catch2 e spdlog automaticamente via vcpkg em modo manifesto, com versões fixadas (baseline). | US-1 |
| REQ-001-04 | SE o Qt (≥ 6.7) não for encontrado na configuração, ENTÃO O sistema DEVE interromper com mensagem indicando a versão mínima e como informar o caminho da instalação. | US-1 |
| REQ-001-05 | SE o compilador não suportar C++20, ENTÃO O sistema DEVE interromper a configuração com mensagem clara. | US-1 |
| REQ-001-06 | O sistema DEVE compilar sem avisos com `/W4` (MSVC) e `-Wall -Wextra -Wpedantic` (GCC/Clang), e tratar avisos como erro quando a opção `FEMAPP_WARNINGS_AS_ERRORS` estiver ligada (padrão no CI). | US-3 |

### Estrutura de módulos

| ID | Requisito | História |
|----|-----------|----------|
| REQ-001-07 | O sistema DEVE definir os alvos de biblioteca `fem_core`, `fem_geom`, `fem_mesher`, `fem_io` e `fem_render` e o executável `femapp`, cada um em `src/<módulo>/` com seu namespace (`fem::core` etc.). | US-2 |
| REQ-001-08 | O sistema DEVE declarar entre os alvos somente as dependências permitidas pelo grafo de `specs/architecture.md`. | US-2 |
| REQ-001-09 | SE um arquivo de `core`, `geom` ou `mesher` incluir um header do Qt, ENTÃO a compilação DEVE falhar. | US-2 |
| REQ-001-10 | Cada módulo DEVE conter ao menos uma função pública trivial (ex.: versão do módulo) usada por um teste, para provar que compila, liga e é testável. | US-2 |

### Verificação antecipada do OpenCASCADE

| ID | Requisito | História |
|----|-----------|----------|
| REQ-001-27 | O sistema DEVE obter o OpenCASCADE (≥ 7.8) via vcpkg, com versão fixada, e ligá-lo **dinamicamente** apenas ao alvo `fem_geom`. | US-1 |
| REQ-001-28 | O teste de `fem_geom` DEVE executar uma operação real do OpenCASCADE (criar um bloco e conferir seu volume) nas três plataformas. | US-3 |
| REQ-001-29 | Os headers públicos de `fem_geom` NÃO DEVEM incluir headers do OpenCASCADE. | US-2 |
| REQ-001-30 | O executável `femapp` e os testes DEVEM localizar as bibliotecas dinâmicas do OpenCASCADE e do Qt ao rodar a partir da pasta de build, sem configuração manual de `PATH`. | US-1 |

### Aplicativo mínimo

| ID | Requisito | História |
|----|-----------|----------|
| REQ-001-11 | QUANDO o usuário executar `femapp`, O sistema DEVE exibir uma janela principal com título "femapp", barra de menus (Arquivo ▸ Sair; Ajuda ▸ Sobre) e barra de status. | US-4 |
| REQ-001-12 | QUANDO o usuário escolher Ajuda ▸ Sobre, O sistema DEVE exibir nome, versão do aplicativo, versão do Qt e licença (MIT). | US-4 |
| REQ-001-13 | QUANDO o usuário escolher Arquivo ▸ Sair ou fechar a janela, O sistema DEVE encerrar com código de saída 0. | US-4 |
| REQ-001-14 | QUANDO executado com `--version`, O sistema DEVE imprimir a versão e encerrar com código 0 sem abrir janela. | US-3 |
| REQ-001-15 | QUANDO executado com `--smoke-test`, O sistema DEVE criar a janela principal, processar eventos e encerrar sozinho com código 0, para uso no CI. | US-3 |
| REQ-001-16 | O sistema DEVE definir a versão do aplicativo em um único lugar (CMake) e propagá-la ao código. | US-4 |
| REQ-001-17 | Os textos da interface DEVEM passar por `tr()`, com tradução pt_BR carregada conforme o idioma do sistema. | US-4 |

### Testes

| ID | Requisito | História |
|----|-----------|----------|
| REQ-001-18 | O sistema DEVE executar todos os testes com `ctest --preset <nome>`, usando Catch2, com um executável de teste por biblioteca; o executável `femapp` é testado por `--version` e `--smoke-test`. *(Alterado em 2026-11-09, na aprovação do design.)* | US-3 |
| REQ-001-19 | O sistema DEVE permitir desligar a compilação dos testes pela opção `FEMAPP_BUILD_TESTS`. | US-1 |

### Qualidade e integração contínua

| ID | Requisito | História |
|----|-----------|----------|
| REQ-001-20 | O repositório DEVE conter `.clang-format` e `.clang-tidy` coerentes com as convenções de `CLAUDE.md`. | US-3 |
| REQ-001-21 | O repositório git DEVE ser inicializado com ramo `main`, e o `.gitignore` DEVE excluir todos os artefatos de build. | US-1 |
| REQ-001-22 | QUANDO houver push ou pull request, o CI DEVE configurar, compilar e testar em Windows (MSVC), macOS (Apple Clang) e Linux (GCC e Clang), com avisos como erro. | US-3 |
| REQ-001-23 | O CI DEVE executar o `--smoke-test` do aplicativo em cada plataforma. | US-3 |
| REQ-001-24 | O CI DEVE falhar se houver arquivo fora do padrão do `clang-format` ou diagnóstico do `clang-tidy`. | US-3 |
| REQ-001-25 | O CI DEVE reaproveitar em cache os pacotes do vcpkg e a instalação do Qt entre execuções. | US-3 |

### Documentação

| ID | Requisito | História |
|----|-----------|----------|
| REQ-001-26 | O repositório DEVE conter um `README.md` com descrição do projeto, pré-requisitos por plataforma e instruções de build, teste e execução. | US-1 |

## 4. Requisitos não funcionais

| ID | Requisito | Como medir |
|----|-----------|------------|
| NFR-001-01 | Build limpo do esqueleto (com dependências em cache, inclusive o OpenCASCADE) em menos de 5 min em cada executor do CI. A primeira execução, sem cache, pode levar bem mais. | Tempo do job no CI |
| NFR-001-02 | O aplicativo abre a janela em menos de 2 s em desktop comum. | Medição manual |
| NFR-001-03 | Pré-requisitos limitados a: compilador C++20, CMake ≥ 3.25, Ninja, Git, vcpkg e Qt ≥ 6.7. | Revisão do README |

## 5. Critérios de aceite

- [ ] **AC-1** (REQ-001-01, 02, 03) Dado um clone limpo em uma máquina com os
  pré-requisitos, quando executo `cmake --preset dev` e `cmake --build --preset dev`,
  então o build termina sem erros nem avisos.
- [ ] **AC-2** (REQ-001-04) Dado um ambiente sem Qt, quando configuro, então a
  configuração falha com mensagem que cita a versão mínima e `CMAKE_PREFIX_PATH`.
- [ ] **AC-3** (REQ-001-07, 08, 10, 18) Quando executo `ctest --preset dev`, então
  há um teste por módulo e todos passam.
- [ ] **AC-4** (REQ-001-09) Dado um `#include <QString>` acrescentado a um arquivo
  de `core`, quando compilo, então a compilação falha.
- [ ] **AC-5** (REQ-001-11, 12, 13) Quando executo `femapp`, então vejo a janela
  com menus e barra de status; "Sobre" mostra versão, Qt e licença; "Sair"
  encerra com código 0.
- [ ] **AC-6** (REQ-001-14, 15, 16) `femapp --version` imprime a versão definida no
  CMake; `femapp --smoke-test` encerra sozinho com código 0.
- [ ] **AC-7** (REQ-001-17) Com o sistema em português, os menus aparecem em
  português; em outro idioma, em inglês.
- [ ] **AC-8** (REQ-001-22, 23, 25) Um push dispara o CI e os quatro jobs
  (Windows, macOS, Linux GCC, Linux Clang) terminam verdes, incluindo o smoke test.
- [ ] **AC-9** (REQ-001-06, 24) Um commit com aviso de compilação, má formatação
  ou diagnóstico de `clang-tidy` faz o CI falhar.
- [ ] **AC-11** (REQ-001-27, 28, 30) O teste de `fem_geom` cria um bloco com o
  OpenCASCADE, confere o volume e passa nos quatro jobs do CI, rodando direto da
  pasta de build.
- [ ] **AC-12** (REQ-001-27, 29) Nenhum header público de `fem_geom` inclui o
  OpenCASCADE, e nenhum outro alvo o liga diretamente; o OpenCASCADE é ligado
  como biblioteca dinâmica.
- [ ] **AC-10** (REQ-001-26) Seguindo apenas o `README.md`, é possível compilar e
  executar o aplicativo.

## 6. Fora de escopo

- Viewport 3D e qualquer uso de QRhi (feature 002).
- Docks da interface: árvore do modelo, propriedades, console (feature 010).
- Qualquer funcionalidade de geometria sobre o OpenCASCADE além do teste de
  verificação (o encapsulamento completo é a feature 015).
- Spectra, oneTBB e nlohmann/json: cada dependência entra na feature que
  primeiro a utiliza.
- Qualquer lógica de elementos finitos, geometria ou malha.
- Instaladores e empacotamento (feature 039).
- Medição de cobertura de testes (entra com a feature 006, quando houver código a cobrir).

## 7. Requisitos globais relacionados

REQ-G-01 (compilar nas três plataformas com CMake e presets). Prepara o terreno
para REQ-G-03 (janela principal).

## 8. Questões em aberto

- [x] **Hospedagem e CI:** decidido em 2026-11-09 — GitHub, com CI em GitHub Actions.
- [x] **Smoke test do OpenCASCADE:** decidido em 2026-11-09 — antecipar para esta
  feature (REQ-001-27 a 30).
- [x] **Versão inicial do aplicativo:** decidido em 2026-11-09 — `0.1.0`.
