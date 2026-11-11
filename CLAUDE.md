# CLAUDE.md — femapp

Instruções para agentes (Claude Code) trabalhando neste repositório.

## Projeto

Aplicativo desktop de **análise por elementos finitos** em **C++20**, interface
**Qt 6 (Widgets)** e renderização por **GPU via Qt RHI** (D3D/Metal/Vulkan/OpenGL).
Multiplataforma: Windows, macOS e Linux. Inclui módulo de desenho estilo AutoCAD
sobre OpenCASCADE e gerador de malha próprio. Licença MIT: nunca adicione
dependência GPL/AGPL; Qt e OpenCASCADE sempre por link dinâmico.

## Leia antes de qualquer trabalho

1. `specs/constitution.md` — princípios inegociáveis.
2. `specs/product.md` — visão, escopo e requisitos globais do produto.
3. `specs/architecture.md` — arquitetura, módulos e tecnologias.
4. `specs/roadmap.md` — ordem das features e status.
5. A pasta da feature em andamento em `specs/features/NNN-nome/`.

## Fluxo de trabalho (Spec Driven Development)

Cada feature passa por quatro fases, **com aprovação do usuário entre cada uma**:

| Fase | Comando | Artefato | Saída |
|------|---------|----------|-------|
| 1. Especificar | `/spec <descrição>` | `requirements.md` | requisitos EARS + critérios de aceite |
| 2. Projetar | `/design NNN` | `design.md` | interfaces, dados, decisões, riscos |
| 3. Planejar | `/tasks NNN` | `tasks.md` | checklist de tarefas pequenas e testáveis |
| 4. Implementar | `/implement NNN` | código + testes | uma tarefa por vez, marcando `[x]` |

Regras:
- Nunca pule fases nem implemente algo que não esteja em `tasks.md`.
- Se descobrir que a spec está errada ou incompleta, **pare**, proponha a
  alteração na spec e aguarde aprovação.
- Ao concluir uma tarefa, marque-a em `tasks.md` e cite os `REQ-*` cobertos.
- Atualize o status da feature em `specs/roadmap.md`.
- Decisões arquiteturais relevantes viram ADR em `specs/decisions/NNNN-titulo.md`.

Os templates ficam em `specs/_templates/`.

## Idioma

- Documentação, specs e mensagens ao usuário: **português**.
- Código, identificadores, comentários no código e mensagens de commit: **inglês**.
- Textos da UI: inglês no código, traduzidos via `tr()` / Qt Linguist (pt_BR incluso).

## Comandos de build

```bash
cmake --preset dev            # configura (Debug, testes ligados)
cmake --build --preset dev    # compila
ctest --preset dev            # roda os testes
```

Presets por plataforma: `windows-msvc`, `macos-clang`, `linux-gcc`, `linux-clang`.

No Windows, rode esses comandos no Developer PowerShell do Visual Studio 2022,
com `CMAKE_PREFIX_PATH` apontando para o Qt (nesta máquina:
`C:/Qt/6.8.3/msvc2022_64`, com o módulo Qt Shader Tools). `clang-format` e `clang-tidy` ficam em
`VC/Tools/Llvm/x64/bin` do Visual Studio. Detalhes no `README.md`.

## Convenções de código

- Namespaces: `fem::core`, `fem::geom`, `fem::mesher`, `fem::io`, `fem::render`, `fem::app`.
- Arquivos `snake_case.hpp/.cpp`; classes `PascalCase`; funções e variáveis
  `camelCase`; membros privados `m_` (padrão Qt); constantes `kPascalCase`.
- `core`, `geom` e `mesher` não incluem nenhum header Qt. Verifique antes de concluir uma tarefa.
- Formate com `clang-format` antes de considerar uma tarefa concluída.
- Não introduza dependências novas sem estarem no `design.md` aprovado.
