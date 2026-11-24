# ADR 0011 — Controle de execução das análises no `core`

Status: proposta · Data: 2026-11-24

## Contexto

A constituição (§4.3) e o REQ-G-05 pedem que as análises rodem fora da thread
da interface, com progresso e cancelamento. As análises do `core` (estática
linear, segunda ordem e flambagem, features 007 a 008d) são funções que vão
até o fim. Elas não informam o andamento nem aceitam parar. Na 011, o
mantenedor escolheu o cancelamento de verdade, que para o cálculo, e não
apenas descartar o resultado.

O `core` não pode depender de Qt (§4.1), e a montagem é paralela (oneTBB, ADR
0004): o progresso nasce em várias threads ao mesmo tempo.

## Decisão

Todas as análises recebem, nas opções (`StaticOptions::control`, também
presente em `SecondOrderOptions::base` e `BucklingOptions::base`), um
`core::RunControl`:
- `const std::atomic<bool>* cancel`: lido entre as etapas, por bloco de
  elementos na montagem e na recuperação, e a cada iteração da segunda
  ordem e da flambagem;
- `std::function<void(const RunProgress&)> progress`: chamado de qualquer
  thread com a etapa, a fração e a iteração. Deve ser barato e seguro entre
  threads.

Ao ver o cancelamento, a análise devolve `Error` com `cancelled = true`. Sem
controle (o padrão), nada muda: as operações numéricas são as mesmas, na
mesma ordem.

A interface não recebe um sinal por chamada. Ela grava os números em
atômicos e os lê num timer.

## Consequências

- O cancelamento passa a ser parte do contrato de toda análise nova (modal,
  térmica, dinâmica): cada uma deve verificar o controle entre as suas
  etapas.
- Uma fatoração em andamento não para: o solver esparso não tem ponto de
  parada. O tempo até cancelar é, no pior caso, o de uma fatoração.
- O `Error` do `core` ganha um campo. Quem só lê a mensagem não muda.
- Testes garantem que sem controle os resultados são idênticos bit a bit, e
  um benchmark limita o custo do controle a 2%.

## Alternativas consideradas

- **Descartar o resultado na interface, sem tocar o `core`.** Recusada pelo
  mantenedor: a CPU segue ocupada, e o Run all não acelera.
- **`std::stop_token`/`std::jthread`.** Prende o chamador a `std::jthread`
  de ponta a ponta. Um ponteiro para atômico é mais simples e serve a
  `QThread`, a TBB e aos testes.
- **Um objeto de progresso com fila de mensagens.** Enche a fila em modelos
  grandes. Os atômicos lidos por amostragem custam nada.
- **Interromper a thread ou o solver à força.** Vaza recursos e deixa o
  estado indefinido.
