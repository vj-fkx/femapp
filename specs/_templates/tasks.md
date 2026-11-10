# Tarefas — NNN nome-da-feature

Status: rascunho | aprovado | em andamento | concluído
Design: [design.md](design.md)

Regras:
- Uma tarefa = uma mudança pequena, compilável e testável (idealmente < 2 h).
- Testes vêm antes ou junto da implementação que verificam.
- `[P]` marca tarefas que podem ser feitas em paralelo com a anterior.
- Marque `[x]` somente com build e testes passando.

## Fase 1 — Preparação

- [ ] **T01** <descrição> — arquivos: `…` — cobre: REQ-NNN-xx

## Fase 2 — Núcleo

- [ ] **T02** Escrever teste <…> (deve falhar) — cobre: REQ-NNN-xx
- [ ] **T03** Implementar <…> até o teste T02 passar

## Fase 3 — Integração / UI

- [ ] **T04** …

## Fase 4 — Fechamento

- [ ] **T90** Build e testes verdes nos presets das três plataformas (CI)
- [ ] **T91** `clang-format` e `clang-tidy` sem pendências
- [ ] **T92** Conferir todos os critérios de aceite de `requirements.md`
- [ ] **T93** Atualizar `specs/roadmap.md` (status) e documentação afetada

## Matriz de rastreabilidade

| Requisito | Tarefas | Testes |
|-----------|---------|--------|
| REQ-NNN-01 | T02, T03 | `tests/…` |
