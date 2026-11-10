---
description: Fase 2 do SDD — cria o design técnico de uma feature com requisitos aprovados
argument-hint: <número da feature>
---

Crie o design da feature $ARGUMENTS.

1. Confirme que `specs/features/$ARGUMENTS-*/requirements.md` existe, está com status
   `aprovado` e não tem questões em aberto. Caso contrário, pare e avise.
2. Leia `specs/constitution.md` e `specs/architecture.md`; inspecione o código existente
   que será afetado.
3. Crie `design.md` na pasta da feature a partir de `specs/_templates/design.md`.
   Preencha a verificação da constituição; toda interface e todo teste deve apontar
   para os `REQ-*` que atende.
4. Se o design exigir mudança estrutural ou nova dependência, crie um ADR em
   `specs/decisions/` (template `specs/_templates/adr.md`).
5. Atualize o status no roadmap para `design`.
6. **Não escreva código.** Apresente um resumo das decisões e peça aprovação.
