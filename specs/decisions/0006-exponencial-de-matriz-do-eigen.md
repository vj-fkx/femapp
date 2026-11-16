# ADR 0006 — Exponencial de matriz pelo módulo `MatrixFunctions` do Eigen

Status: aceita · Data: 2026-11-16

## Contexto

A feature 008b resolve a viga reta com empenamento e a viga curva em arco
de forma exata por espaço de estados (design 008b §5.3): a rigidez, as
cargas equivalentes e os esforços ao longo da barra saem da exponencial de
uma matriz constante de até 19×19 (com os estados aumentados das cargas).
A exponencial precisa ser precisa até o arredondamento, sem alocação no
`compute` e rápida (uma por parte de cada viga, dentro da montagem
paralela).

## Decisão

Usar `Eigen::MatrixExponential` do módulo `unsupported/Eigen/MatrixFunctions`,
que vem no mesmo pacote do Eigen já usado pelo projeto (MPL-2.0), com
aproximantes de Padé de grau 13, escala e quadratura (Higham, 2005):

- atrás de uma única função de `core` (`matrixExponential`, em
  `beam_state.cpp`), sem o header nas interfaces públicas;
- em matrizes de tamanho máximo fixo, para não alocar;
- o argumento mantido com norma pequena pela subdivisão do design (§5.3).

## Consequências

- Nenhuma dependência nova nem código numérico delicado próprio.
- O diretório `unsupported` não tem a mesma garantia de API do núcleo do
  Eigen. Se a API mudar, a função de `core` passa a usar um Padé 13 próprio
  (cerca de 60 linhas, com os testes da exponencial já existentes).
- O teste de alocação (`test_allocations`) passa a cobrir o caminho da
  exponencial.

## Alternativas consideradas

- Padé próprio desde já — reimplementa o que o Eigen já oferece
  (constituição §7.3).
- Fórmulas fechadas por caso (senh/cosh, Roark) — uma fórmula por
  combinação de pontas, cargas e curvatura; descartada no design.
- Integração numérica da EDO (Runge-Kutta) — não é exata e precisa de passo
  controlado.
