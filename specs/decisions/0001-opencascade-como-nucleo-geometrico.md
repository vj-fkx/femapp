# ADR 0001 — OpenCASCADE como núcleo geométrico

Status: aceita · Data: 2026-11-09

## Contexto

O femapp precisa de (a) um módulo de desenho no estilo AutoCAD e (b) importação
de CAD (STEP/IGES) já na versão 1.0. Ler STEP/IGES exige um núcleo B-rep
completo; escrever um do zero é inviável. A licença do projeto é MIT.

## Decisão

- Usar **OpenCASCADE (OCCT)** como núcleo geométrico do módulo `geom`: curvas e
  superfícies exatas, sólidos, booleanas, STEP/IGES.
- A **camada de desenho** (entidades, comandos, snaps, camadas, linha de
  comando) é implementação própria sobre o núcleo.
- O OCCT fica totalmente encapsulado em `geom` (pimpl) e é ligado dinamicamente.
- **DXF** é lido/escrito por parser próprio (o OCCT de código aberto não o suporta).
- O gerador de malha continua próprio; os malhadores do OCCT não são usados.

## Consequências

- Booleanas, fillets, offsets e importação de CAD vêm prontos e robustos.
- Dependência grande: build mais lento e pacotes de instalação maiores.
- LGPL 2.1 com exceção é compatível com a licença MIT desde que o link seja dinâmico.
- O `mesher` precisa malhar superfícies paramétricas gerais (`SurfaceMesher`).

## Alternativas consideradas

- **Núcleo B-rep próprio** — sem STEP/IGES em prazo razoável.
- **CGAL** — partes relevantes são GPL; incompatível com MIT.
- **SDK do Gmsh** — GPL; incompatível com MIT.
