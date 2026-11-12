# Roadmap de Features — femapp

Cada linha vira uma pasta `specs/features/NNN-nome/` quando sua especificação começar.

Status: `planejada` → `requisitos` → `design` → `tarefas` → `implementando` → `concluída`

O escopo da 1.0 é amplo, então a entrega é em versões intermediárias, cada uma
utilizável de ponta a ponta. Barras e vigas são os primeiros elementos; os
demais vêm depois da geometria completa.

## v0.1 — Fundação

| Nº | Feature | Entrega | Depende de | Status |
|----|---------|---------|------------|--------|
| 001 | project-skeleton | CMake, presets, vcpkg, módulos vazios, janela Qt mínima, CI nas 3 plataformas, clang-format/tidy, verificação do OpenCASCADE | — | concluída |
| 002 | gpu-viewport | `QRhiWidget`, câmera órbita/pan/zoom, eixos, grade | 001 | concluída |
| 003 | mesh-model | `core::Mesh` (todas as topologias, linear e quadrática), grupos, leitor `.msh` | 001 | concluída |
| 004 | mesh-rendering | Malha no viewport: linhas, faces, arestas, casca externa 3D, seleção | 002, 003 | tarefas |

## v0.2 — Barras e vigas

Primeira versão que analisa: desenhar um pórtico com linhas, atribuir seções e
cargas, resolver e ver diagramas de esforços.

| Nº | Feature | Entrega | Depende de | Status |
|----|---------|---------|------------|--------|
| 005 | drafting-basics | Módulo `geom`: ponto, linha, polilinha (2D e 3D); linha de comando com coordenadas absolutas/relativas/polares; snaps de extremidade, meio e interseção; grade e ortogonal; mover, copiar, apagar | 002 | planejada |
| 006 | fem-kernel | Funções de forma, quadratura, `DofMap` com GDL por nó, montagem paralela, solver direto | 003 | planejada |
| 007 | truss-elements | Treliça 2D/3D, verificação analítica | 006 | planejada |
| 008 | beam-elements | Vigas Euler-Bernoulli e Timoshenko 2D/3D, seções, orientação, rótulas | 007 | planejada |
| 009 | line-meshing | Divisão de linhas em elementos, nós coincidentes fundidos | 005, 003 | planejada |
| 010 | model-setup-ui | Árvore do modelo, propriedades, materiais, seções, apoios, cargas nodais e distribuídas, undo/redo | 004, 005, 008 | planejada |
| 011 | analysis-runner | Execução assíncrona, progresso, cancelamento, validação do modelo | 008, 010 | planejada |
| 012 | results-visualization | Deformada com escala, mapa de cores, legenda, consulta de valores, reações de apoio | 004, 011 | planejada |
| 013 | frame-postprocessing | Diagramas N/V/M, seção extrudada no viewport | 012 | planejada |
| 014 | project-files | Salvar/abrir projeto `.femapp`, exportar `.vtu` | 010, 012 | planejada |

## v0.3 — Geometria CAD completa e importação

| Nº | Feature | Entrega | Depende de | Status |
|----|---------|---------|------------|--------|
| 015 | occt-kernel | OpenCASCADE encapsulado em `geom`; entidades existentes migradas para o núcleo | 005 | planejada |
| 016 | drafting-entities | Círculo, arco, elipse, retângulo, polígono, spline; camadas; snaps de centro, perpendicular, tangente e mais próximo; rastreamento polar | 015 | planejada |
| 017 | drafting-edit | Rotacionar, espelhar, escalar, offset, trim, estender, fillet, chanfro, matriz, explodir, unir; grips; seleção por janela/cruzamento | 016 | planejada |
| 018 | drafting-measure | Distância, ângulo, área, cotas básicas | 016 | planejada |
| 019 | solids-modeling | Planos de trabalho (UCS), regiões, extrusão, revolução, primitivas, booleanas | 017 | planejada |
| 020 | cad-import-export | Importar STEP, IGES e DXF com relatório de entidades ignoradas e reparo; exportar STEP e DXF | 019 | planejada |

## v0.4 — Malha 2D e sólidos 2D

| Nº | Feature | Entrega | Depende de | Status |
|----|---------|---------|------------|--------|
| 021 | mesh-structured-2d | Malha transfinita (quad/tri) em regiões | 019 | planejada |
| 022 | mesh-delaunay-2d | Delaunay restrito + refinamento, controle de tamanho, recombinação em quads | 019 | planejada |
| 023 | mesh-quality | Métricas de qualidade, destaque de elementos ruins, `MeshRunner` | 021, 022 | planejada |
| 024 | elasticity-2d | T3/Q4: estado plano e axissimétrico, tensões, von Mises, benchmarks | 006, 022 | planejada |

## v0.5 — Sólidos 3D

| Nº | Feature | Entrega | Depende de | Status |
|----|---------|---------|------------|--------|
| 025 | elasticity-3d | Tet4, Hex8, Wedge6; solver iterativo (CG) | 024 | planejada |
| 026 | mesh-sweep-3d | Malha por varredura (Hex8/Wedge6) | 022 | planejada |
| 027 | mesh-surface | Malha em superfícies curvas (espaço paramétrico) | 022 | planejada |

## v0.6 — Elementos quadráticos

| Nº | Feature | Entrega | Depende de | Status |
|----|---------|---------|------------|--------|
| 028 | quadratic-solids | T6, Q8, Tet10, Hex20; testes de convergência | 025 | planejada |
| 029 | quadratic-meshing | Elevação de ordem com nós sobre a geometria; render de faces curvas | 023, 028 | planejada |

## v0.7 — Térmica

| Nº | Feature | Entrega | Depende de | Status |
|----|---------|---------|------------|--------|
| 030 | heat-steady | Condução estacionária 2D/3D; temperatura, fluxo, convecção | 025 | planejada |
| 031 | heat-transient | Integração no tempo (θ-method), resultados por passo, animação | 030 | planejada |

## v0.8 — Modal e dinâmica

| Nº | Feature | Entrega | Depende de | Status |
|----|---------|---------|------------|--------|
| 032 | mass-matrices | Massa consistente e concentrada para todos os elementos | 008, 025 | planejada |
| 033 | modal-analysis | Autovalores (Spectra, shift-invert), modos animados | 032 | planejada |
| 034 | transient-dynamics | Newmark e superposição modal, amortecimento de Rayleigh | 033 | planejada |

## v0.9 — Placas e cascas

| Nº | Feature | Entrega | Depende de | Status |
|----|---------|---------|------------|--------|
| 035 | shell-elements | Cascas MITC triangular e quadrilateral, espessura, benchmarks de locking | 008, 027 | planejada |
| 036 | mixed-models | Compatibilização barra + casca + sólido no mesmo modelo | 025, 035 | planejada |

## v1.0 — Malha 3D não estruturada e distribuição

| Nº | Feature | Entrega | Depende de | Status |
|----|---------|---------|------------|--------|
| 037 | mesh-delaunay-3d | Tetraedros por Delaunay 3D com recuperação de fronteira e refinamento | 027 | planejada |
| 038 | performance | Metas REQ-G-20 a REQ-G-22: perfis, paralelismo, solver externo opcional | 025, 037 | planejada |
| 039 | packaging | Instaladores por plataforma (windeployqt, macdeployqt, AppImage) | 014 | planejada |

## Pós-1.0 (ideias, sem ordem)

- Importação de DWG
- Blocos, hachuras e cotas avançadas no módulo de desenho
- Não linearidade geométrica e de material, contato
- Flambagem linear
- Acoplamento termomecânico
- Cortes, isosuperfícies
- Solver em GPU
