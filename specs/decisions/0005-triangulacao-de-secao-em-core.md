# ADR 0005 — Triangulação de seção dentro de `core`

Status: aceita · Data: 2026-11-16

## Contexto

A feature 008b pede a seção poligonal maciça com J, centro de cisalhamento,
Iw e áreas de cisalhamento calculados numericamente (REQ-008b-02), o que
exige uma malha 2D da seção. O gerador de malha do projeto mora em `mesher`
(implementação própria, constituição §7.3), mas o gerador 2D geral só vem na
feature 022, e `core` não pode depender de `mesher` (o grafo de
`architecture.md` vai de `mesher` para `core`). As seções são parte do
`Model`, em `core`, e precisam das propriedades sem interface e sem `mesher`.

## Decisão

`core` ganha uma triangulação pequena e própria, restrita às seções de
viga, em `src/core/detail/section_mesh.hpp` (não pública):

- polígonos com furos, ligados por pontes e triangulados por orelhas;
- trocas de Lawson até o Delaunay restrito;
- refinamento por bissecção da aresta mais longa (Rivara) até um tamanho
  máximo de aresta;
- triângulos de 6 nós para os problemas de empenamento e cisalhamento.

Ela não é exposta como gerador de malha nem usada fora das seções.

## Consequências

- A seção poligonal funciona já na 008b, com as propriedades em `core` e
  sem dependências novas.
- Há duas triangulações no projeto quando a 022 chegar. A 022 decide se a
  seção passa a usar o gerador de `mesher` (o que exigiria mover o cálculo
  da seção para uma camada acima de `core` ou inverter a dependência) ou se
  as duas convivem, com a da seção limitada a este uso.
- A triangulação da seção segue a §7.4 da constituição: testes de
  conformidade, qualidade mínima e casos degenerados.

## Alternativas consideradas

- Esperar a 022 e adiar a seção poligonal — rejeitada pelo mantenedor na
  aprovação dos requisitos da 008b.
- Fazer `core` depender de `mesher` — inverte o grafo de camadas.
- Calcular as propriedades da seção em `mesher` — o `Model` e a análise, em
  `core`, precisariam de `mesher`.
- Biblioteca externa (Triangle, CGAL) — Triangle não é livre para uso
  comercial; o CGAL de triangulação é GPL (constituição §8.3).
