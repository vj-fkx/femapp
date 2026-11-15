# ADR 0004 — oneTBB para o paralelismo do núcleo

Status: aceita · Data: 2026-11-15

## Contexto

A feature 006 monta matrizes de rigidez com até 1 milhão de elementos e precisa
usar todos os núcleos (REQ-G-22), com ganho de pelo menos 2,5 vezes com 4 ou
mais núcleos (NFR-006-02). A geração de malha (features 021, 022 e 037) terá a
mesma necessidade. A arquitetura já previa o oneTBB, mas ele ainda não estava
instalado.

## Decisão

Usar o oneTBB (pacote `tbb` do vcpkg, licença Apache-2.0) como biblioteca de
paralelismo de `core` e, depois, de `mesher`:

- ligação dinâmica, como as demais dependências de terceiros nos triplets do
  projeto;
- dependência **privada** de cada módulo: nenhum header do oneTBB nas
  interfaces públicas; o número de threads é um `int` nas funções (0 = todos os
  núcleos, 1 = sequencial), controlado internamente com
  `tbb::global_control`/`tbb::task_arena`;
- primeiro uso: `tbb::parallel_for` por cor na montagem (design 006 §5.5).

## Consequências

- Equilíbrio de carga automático e um modelo único de paralelismo para o
  núcleo e a malha.
- Mais uma biblioteca para compilar no CI (cache binário do vcpkg) e para
  empacotar (feature 039).
- O `app` não depende do oneTBB diretamente.

## Alternativas consideradas

- `std::thread`/`std::jthread` com divisão manual — sem equilíbrio de carga,
  mais código; recusado pelo mantenedor.
- Algoritmos paralelos da biblioteca padrão (`std::execution::par`) — suporte
  desigual entre MSVC, libstdc++ (que usa o próprio TBB) e libc++ (sem suporte
  completo no Apple Clang).
- OpenMP — suporte desigual no Apple Clang e integração pior com CMake no
  macOS.
