---
description: Fase 1 do SDD — cria os requisitos de uma nova feature
argument-hint: <número ou descrição da feature>
---

Inicie a especificação da feature: $ARGUMENTS

1. Leia `specs/constitution.md`, `specs/product.md`, `specs/architecture.md` e `specs/roadmap.md`.
2. Identifique a feature no roadmap (ou proponha número e nome `NNN-kebab-case` se for nova).
3. Se houver ambiguidades que mudem o escopo, pergunte ao usuário antes de escrever.
4. Crie `specs/features/NNN-nome/requirements.md` a partir de `specs/_templates/requirements.md`:
   requisitos em notação EARS com IDs `REQ-NNN-xx`, critérios de aceite verificáveis,
   fora de escopo e questões em aberto.
5. Atualize o status da feature em `specs/roadmap.md` para `requisitos`.
6. **Não escreva design nem código.** Apresente um resumo e peça aprovação.
