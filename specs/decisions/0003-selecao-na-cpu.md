# ADR 0003 — Seleção no viewport calculada na CPU

Status: proposta · Data: 2026-11-12

## Contexto

A arquitetura previa a seleção (picking) por um passe offscreen na GPU, com o
identificador de cada elemento codificado em cor e lido de volta no pixel do
clique. A feature 004 precisa de:

- clique em elementos e nós, escolhendo o mais próximo da câmera;
- janela e cruzamento que incluem itens escondidos (como no AutoCAD);
- testes automáticos sem GPU;
- resposta em menos de 100 ms (clique) e 500 ms (retângulo) com 1 milhão de
  elementos.

No QRhi, a leitura de uma textura é assíncrona: o resultado chega um ou dois
quadros depois, o que complica o fluxo do clique e não dá para testar sem GPU.
Um passe de IDs também não ajuda na janela e no cruzamento, que precisam de
itens escondidos.

## Decisão

A seleção é calculada na CPU, no módulo `render`, sobre a `MeshScene`:

- clique: raio pelo pixel contra os triângulos visíveis (Möller–Trumbore) e
  distância em tela para linhas, pontos e nós, com teste de oclusão para nós;
- janela e cruzamento: projeção dos vértices na tela e testes 2D.

A GPU só desenha; a seleção não depende dela.

## Consequências

- A seleção é testável sem GPU e igual em todos os backends.
- O custo cresce com o número de triângulos visíveis (força bruta). Até 1
  milhão de triângulos atende às metas; acima disso, uma grade uniforme ou uma
  BVH resolve, sem mudar a interface.
- A cena guarda na CPU uma cópia das posições enviadas à GPU (alguns MB para 1
  milhão de tetraedros; ≈ 48 MB no pior caso de 1 milhão de triângulos 2D).
- `specs/architecture.md` §4 deve trocar "passe offscreen com ID codificado em
  cor" por "cálculo na CPU".

## Alternativas consideradas

- **Passe de IDs na GPU** — leitura assíncrona, sem teste sem GPU e sem ajuda
  para itens escondidos.
- **Passe de IDs só para o clique e CPU para retângulos** — dois mecanismos
  para manter, sem ganho medido.
