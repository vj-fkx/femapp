# Constituição do Projeto — femapp

> Princípios inegociáveis do projeto. Toda spec, design, tarefa e linha de código
> deve respeitá-los. Alterações nesta constituição exigem decisão explícita do
> mantenedor e registro em `specs/decisions/` (ADR).

Versão: 1.0.0 · Ratificada em: 2026-11-09

---

## 1. Desenvolvimento guiado por especificação (SDD)

1. **Nenhum código sem spec.** Toda funcionalidade nasce em `specs/features/NNN-nome/`
   com três artefatos, nesta ordem:
   1. `requirements.md` — *o quê* e *por quê* (requisitos em notação EARS, critérios de aceite).
   2. `design.md` — *como* (arquitetura, interfaces, dados, riscos).
   3. `tasks.md` — passos de implementação pequenos, ordenados e verificáveis.
2. **Cada fase é aprovada pelo mantenedor antes da próxima.** O agente não avança de
   requisitos → design → tarefas → implementação sem aprovação explícita.
3. **Rastreabilidade.** Requisitos têm IDs (`REQ-NNN-xx`). Tarefas e testes citam os IDs
   que satisfazem. Um requisito sem teste não está concluído.
4. **A spec é a fonte da verdade.** Se a implementação precisar divergir, a spec é
   atualizada primeiro (e a mudança registrada no histórico da spec).

## 2. Multiplataforma

1. O projeto **deve compilar e rodar** em Windows (MSVC), macOS (Apple Clang) e
   Linux (GCC e Clang). Nenhum merge quebra qualquer uma das três plataformas.
2. Proibido código específico de plataforma fora de camadas isoladas
   (`src/platform/`), sempre atrás de uma interface comum.
3. Caminhos de arquivo via `std::filesystem` / `QDir`; nunca concatenar separadores à mão.
4. Texto é UTF-8 em todo o código e arquivos de dados.

## 3. Renderização por GPU

1. Toda renderização 3D passa pela **Qt RHI** (`QRhi` / `QRhiWidget`), que abstrai
   Direct3D 11/12 (Windows), Metal (macOS), Vulkan e OpenGL (Linux/fallback).
2. Proibido chamar APIs gráficas nativas (GL/Vulkan/Metal/D3D) diretamente fora do
   módulo `render`.
3. Shaders escritos uma vez em GLSL Vulkan-style (`#version 440`) e compilados
   para todos os backends com `qsb` (via `qt_add_shaders` no CMake).
4. A UI deve permanecer responsiva: meta de **≥ 30 FPS** na interação com malhas de
   até 1 milhão de elementos em GPU dedicada de gama média.

## 4. Arquitetura em camadas

1. `core` (núcleo numérico), `geom` (geometria) e `mesher` (malha) **não dependem
   de Qt nem de GPU**. São C++ puro e podem ser usados em linha de comando e
   testes sem interface.
2. Dependências só fluem para baixo, conforme o grafo de `specs/architecture.md`.
   Nunca o contrário.
3. A interface gráfica nunca bloqueia: análises rodam em thread de trabalho com
   progresso e cancelamento.

## 5. Correção numérica primeiro

1. Todo elemento finito novo passa por: **patch test**, teste de convergência
   (taxa esperada em malhas refinadas) e comparação com solução analítica ou
   benchmark de referência (NAFEMS quando aplicável).
2. Unidades: o núcleo é **agnóstico a unidades**, mas a UI exige que o usuário
   escolha um sistema consistente e o exibe sempre.
3. Tolerâncias numéricas são explícitas e documentadas no teste; nada de
   comparações de ponto flutuante com `==`.

## 6. Qualidade de código

1. **C++20**, CMake ≥ 3.25, Qt ≥ 6.7.
2. Compilação limpa com avisos elevados (`/W4`, `-Wall -Wextra -Wpedantic`) e
   `-Werror` no CI.
3. Formatação por `.clang-format`; análise por `clang-tidy`. Ambos rodam no CI.
4. RAII e ponteiros inteligentes; nada de `new`/`delete` cru fora de árvores de
   `QObject` com parent.
5. Testes com Catch2. Cobertura mínima de 80% de linhas no módulo `core`.
6. Commits pequenos, no formato Conventional Commits, citando a feature
   (ex.: `feat(core): elemento Q4 isoparamétrico [003]`).

## 7. Simplicidade

1. Implementar o mínimo que satisfaz os requisitos aprovados (YAGNI).
2. Nova dependência externa exige justificativa no `design.md` e verificação de
   licença compatível (preferência por MIT/BSD/MPL-2.0/LGPL com link dinâmico).
3. Prefira bibliotecas maduras a reinventar: Eigen para álgebra linear, VTK (.vtu)
   para exportação, OpenCASCADE como núcleo geométrico. **Exceção deliberada:**
   o gerador de malha e a camada de desenho estilo CAD são implementação
   própria, por decisão do mantenedor.
4. Geradores de malha passam por testes de conformidade, qualidade mínima e
   casos degenerados antes de serem expostos na interface.

## 8. Licenciamento

1. O femapp é distribuído sob a **licença MIT** (arquivo `LICENSE`).
2. Qt (LGPLv3) e OpenCASCADE (LGPL 2.1 com exceção) são sempre usados por
   **link dinâmico**, nunca estático, para preservar a licença MIT do projeto.
3. Proibido incorporar código ou dependência GPL/AGPL (ex.: SDK do Gmsh, CGAL
   sob GPL, TetGen).
4. Código de terceiros copiado para o repositório fica em `third_party/`, com o
   arquivo de licença original.
