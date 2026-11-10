---
description: Fase 4 do SDD — implementa as tarefas aprovadas de uma feature
argument-hint: <número da feature> [tarefa, ex.: T03]
---

Implemente a feature $ARGUMENTS.

1. Confirme que `tasks.md` da feature está com status `aprovado` ou `em andamento`.
   Caso contrário, pare e avise.
2. Atualize o status no roadmap para `implementando`.
3. Execute a próxima tarefa não marcada (ou a tarefa indicada), uma por vez:
   - siga o `design.md`; não implemente nada fora de `tasks.md`;
   - compile e rode os testes (`cmake --build --preset dev && ctest --preset dev`);
   - marque `[x]` em `tasks.md` somente com tudo verde.
4. Se a spec ou o design se mostrarem errados ou incompletos, **pare**, explique o
   problema e proponha a alteração no documento correspondente antes de continuar.
5. Ao final de cada fase de tarefas, resuma o que foi feito, o que foi verificado e o
   que não pôde ser verificado localmente (ex.: outras plataformas — dependem do CI).
6. Concluídas todas as tarefas e critérios de aceite, mude o status no roadmap para
   `concluída`.
