---
description: Fase 3 do SDD — quebra o design aprovado em tarefas de implementação
argument-hint: <número da feature>
---

Gere o plano de tarefas da feature $ARGUMENTS.

1. Confirme que `requirements.md` e `design.md` da feature estão com status `aprovado`.
   Caso contrário, pare e avise.
2. Crie `tasks.md` na pasta da feature a partir de `specs/_templates/tasks.md`:
   - tarefas pequenas, ordenadas por dependência, cada uma compilável e testável;
   - testes antes ou junto da implementação;
   - cada tarefa cita arquivos e os `REQ-*` cobertos;
   - preencha a matriz de rastreabilidade — nenhum requisito pode ficar sem tarefa e teste.
3. Atualize o status no roadmap para `tarefas`.
4. **Não escreva código.** Apresente o plano e peça aprovação.
