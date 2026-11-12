# Tarefas — 003 mesh-model

Status: em andamento
Design: [design.md](design.md) · Requisitos: [requirements.md](requirements.md)

Regras:
- Uma tarefa = uma mudança pequena, compilável e testável.
- Testes vêm antes ou junto da implementação que verificam.
- `[P]` marca tarefas que podem ser feitas em paralelo com a anterior.
- `[M]` marca tarefas que dependem de ação do mantenedor.
- Marque `[x]` somente com build e testes passando.
- Os IDs abaixo omitem o prefixo: `01` significa `REQ-003-01`; `N01` significa
  `NFR-003-01`.

## Fase 0 — Preparação

- [x] **T00** Acrescentar `tl-expected` ao `vcpkg.json` e ao `core`; criar
  `expected.hpp` (`Error`, `Expected<T>`, `makeError`) com um teste mínimo de
  valor e de erro — arquivos: `vcpkg.json`, `src/core/CMakeLists.txt`,
  `src/core/include/fem/core/expected.hpp`, `tests/unit/core/test_expected.cpp`
  — cobre: 09, 22 (base)
- [x] **T01** `[P]` Incluir `io` em `CheckNoQtIncludes.cmake`; tirar
  `fem::geom` das dependências de `io`; dar aos testes de `io` a definição
  `FEMAPP_TEST_DATA_DIR` — arquivos: `cmake/CheckNoQtIncludes.cmake`,
  `src/io/CMakeLists.txt`, `tests/CMakeLists.txt` — cobre: 25, 26

## Fase 1 — Tipos de elemento

- [x] **T02** Escrever `tools/generate_element_types.py`: consulta a API do
  Gmsh para os tipos 1 a 999, filtra os 90 de número fixo de nós que o Gmsh cria (design
  §5.1) e grava `src/core/element_type_table.inc` (tabela e coordenadas de
  referência) e a lista do enum, com a versão do Gmsh no cabeçalho. Rodar e
  versionar a saída — cobre: 01, 02, 03
- [x] **T03** Testes da tabela: 90 tipos; contagem por família (design §5.1);
  números do Gmsh únicos; `nodeCount` igual ao tamanho das coordenadas de
  referência; vértices primeiro e iguais entre os tipos de cada família; ponto
  com ordem 0 — arquivo: `tests/unit/core/test_element_type.cpp` — cobre: 01,
  02, 03
- [x] **T04** Implementar `element_type.hpp/.cpp` (`elementTypeInfo`,
  `allElementTypes`, `referenceNodeCoordinates`) até a T03 passar — cobre: 01,
  02, 03

## Fase 2 — Malha

- [x] **T05** Testes e implementação de `IdRanges` (privado): faixas contíguas,
  ids soltos, busca, detecção de repetidos, ordem de inserção qualquer —
  arquivos: `src/core/id_ranges.hpp/.cpp`, `tests/unit/core/test_id_ranges.cpp`
  — cobre: 04, 06, 10
- [x] **T06** Testes de `MeshBuilder`/`Mesh` sem grupos: malha mista (ponto,
  linha, triângulo, hexaedro), ids não contíguos, consultas de quantidade, nós,
  coordenadas, tipo, conversão de ids, blocos, dimensão e caixa envolvente
  (também vazia) — arquivo: `tests/unit/core/test_mesh.cpp` — cobre: 04, 05,
  06, 07
- [x] **T07** Implementar `Mesh` e `MeshBuilder` (nós, `finishNodes`,
  elementos, `build`) até a T06 passar — arquivos: `mesh.hpp/.cpp`,
  `mesh_builder.hpp/.cpp` — cobre: 04, 05, 06, 07
- [x] **T08** Testes e implementação das validações do design §5.4 (número de
  nós, nó inexistente, nó repetido, ids repetidos de nós e de elementos,
  limite de 2³¹ − 1), com a mensagem citando o item — cobre: 09, 10
- [x] **T09** Testes e implementação dos grupos: elemento em dois grupos e em
  nenhum, `findGroup`, `findGroups`, `groupNodes`, nome repetido na mesma
  dimensão, elemento de outra dimensão — cobre: 11, 12, 13, 14
- [x] **T10** Testes e implementação de `elementMeasure`: elemento de referência
  de cada um dos 90 tipos com a medida esperada; elemento invertido negativo;
  área com sinal no plano XY e positiva fora dele; ponto igual a 1 — arquivos:
  `mesh_measure.hpp/.cpp`, `tests/unit/core/test_mesh_measure.cpp` — cobre: 03,
  08
- [x] **T11** `Mesh::memoryUsage` e teste com malha sintética de 200 mil Tet4 e
  um grupo cobrindo tudo: razão ≤ 1,5 sobre os dados brutos — cobre: N01

## Fase 3 — Arquivos de exemplo

- [x] **T12** Escrever `tests/data/gmsh/generate_test_meshes.py` (API do Gmsh)
  e os `.geo`: arquivo com um elemento de cada um dos 90 tipos (transformação
  afim conhecida por tipo); malhas reais de ordem 1 e 2 (segmento, quadrado
  2×3, caixa 1×2×3) com grupos com e sem nome; ids esparsos; nós com
  coordenadas paramétricas; seções extras; tipo não suportado. Gravar ASCII e
  binário, rodar, versionar, com `README.md` explicando como regenerar —
  cobre: 15, 16, 17, 18, 19, 26
- [x] **T13** `[P]` Arquivos de erro escritos à mão: versão 2.2, particionado,
  só tipos não suportados, truncado, número inválido, nó inexistente — cobre:
  20, 21, 22, 26

## Fase 4 — Leitor `.msh`

- [x] **T14** Testes e implementação do cursor (ASCII com linha, binário com
  byte): tokens, inteiros, reais sem depender do locale (com o sistema em
  pt_BR), fim inesperado e número inválido com a posição na mensagem —
  arquivos: `src/io/gmsh_cursor.hpp/.cpp`, `tests/unit/io/test_gmsh_cursor.cpp`
  — cobre: 22
- [x] **T15** `$MeshFormat`: versão 4.1, ASCII/binário, `size_t` de 8 bytes,
  ordem dos bytes; testes com versão 2.2 e com a ordem de bytes trocada em
  memória — arquivos: `gmsh_reader.hpp/.cpp`, `tests/unit/io/test_gmsh_reader.cpp`
  — cobre: 15, 21
- [x] **T16** `$Nodes` e `$Elements` em ASCII: o arquivo com os 90 tipos é
  lido com cada nó na posição dada pelas coordenadas de referência; ids
  esparsos e coordenadas paramétricas — cobre: 15, 17
- [x] **T17** Mesmo caminho em binário: o arquivo binário com os 90 tipos
  produz malha idêntica à do ASCII — cobre: 15
- [x] **T18** `$PhysicalNames` e `$Entities`: grupos com nome e com nome gerado,
  elemento em mais de um grupo, ASCII e binário iguais — cobre: 16
- [x] **T19** Seções ignoradas, tipos não suportados com aviso (tipo e
  quantidade) e arquivo só com tipos não suportados recusado — cobre: 18, 19,
  20
- [x] **T20** Erros: particionado, truncado, número inválido e nó inexistente,
  em ASCII (linha) e binário (byte); nenhum resultado parcial — cobre: 21, 22,
  23
- [x] **T21** `readGmsh(path)`: arquivo inexistente; arquivo copiado pelo teste
  para um caminho com acentos e lido — cobre: 22, 24
- [x] **T22** Malhas reais de ordem 1 e 2: soma das medidas igual à da
  geometria e todas positivas — cobre: 08, 15
- [x] **T23** Remover `module.hpp/.cpp` e `test_io.cpp` de `io`; conferir que
  `check_no_qt_includes` cobre `io` e passa — cobre: 25

## Fase 5 — Desempenho e documentação

- [ ] **T24** Criar `tests/perf/bench_gmsh_read.cpp` (fora do `ctest`) e a
  opção do script de exemplos que gera uma caixa com cerca de 1 milhão de Tet4
  em ASCII e binário, fora do repositório — cobre: N02
- [ ] **T25** `[M]` Rodar o `bench_gmsh_read` em build Release e registrar aqui
  os tempos e a memória (máquina do mantenedor) — cobre: N01, N02
- [ ] **T26** Conferir o tempo dos testes de `core` e `io` (< 5 s) — cobre: N03
- [ ] **T27** Atualizar `specs/architecture.md`: dependências de `core`
  (`tl-expected`) e de `io`; `Mesh` com blocos por tipo e ordem de nós do Gmsh;
  `GmshReader` como `readGmsh` — cobre: 25

## Fase 6 — Fechamento

- [ ] **T90** Build e testes verdes nos quatro jobs do CI (push com sua
  confirmação) — cobre: 24, 26
- [ ] **T91** `clang-format` e `clang-tidy` sem pendências
- [ ] **T92** Conferir os doze critérios de aceite de `requirements.md`
- [ ] **T93** Atualizar `specs/roadmap.md` para `concluída` e o `README.md` se
  algo do build tiver mudado

## Matriz de rastreabilidade

| Requisito | Tarefas | Testes |
|-----------|---------|--------|
| REQ-003-01 | T02, T03, T04 | `test_element_type` (AC-1) |
| REQ-003-02 | T02, T03, T04 | `test_element_type` (AC-1) |
| REQ-003-03 | T02, T03, T04, T10 | `test_element_type`, `test_mesh_measure` (AC-1) |
| REQ-003-04 | T05, T06, T07 | `test_id_ranges`, `test_mesh` (AC-2) |
| REQ-003-05 | T06, T07 | `test_mesh` (AC-2) |
| REQ-003-06 | T05, T06, T07 | `test_id_ranges`, `test_mesh` (AC-2) |
| REQ-003-07 | T06, T07 | `test_mesh` (AC-2) |
| REQ-003-08 | T10, T22 | `test_mesh_measure`, `test_gmsh_reader` (AC-3) |
| REQ-003-09 | T00, T08 | `test_mesh` (AC-4) |
| REQ-003-10 | T05, T08 | `test_id_ranges`, `test_mesh` (AC-4) |
| REQ-003-11 | T09 | `test_mesh` (AC-5) |
| REQ-003-12 | T09 | `test_mesh` (AC-5) |
| REQ-003-13 | T09 | `test_mesh` (AC-5) |
| REQ-003-14 | T09 | `test_mesh` (AC-4) |
| REQ-003-15 | T12, T15, T16, T17, T22 | `test_gmsh_reader` (AC-6, AC-7) |
| REQ-003-16 | T12, T18 | `test_gmsh_reader` (AC-6) |
| REQ-003-17 | T12, T16 | `test_gmsh_reader` (AC-6) |
| REQ-003-18 | T12, T19 | `test_gmsh_reader` (AC-8) |
| REQ-003-19 | T12, T19 | `test_gmsh_reader` (AC-8) |
| REQ-003-20 | T13, T19 | `test_gmsh_reader` (AC-8) |
| REQ-003-21 | T13, T15, T20 | `test_gmsh_reader` (AC-9) |
| REQ-003-22 | T00, T13, T14, T20, T21 | `test_gmsh_cursor`, `test_gmsh_reader` (AC-9) |
| REQ-003-23 | T20 | `test_gmsh_reader` (AC-9) |
| REQ-003-24 | T21, T90 | `test_gmsh_reader` nas três plataformas (AC-10) |
| REQ-003-25 | T01, T23, T27 | `check_no_qt_includes` (AC-11) |
| REQ-003-26 | T01, T12, T13, T90 | Todos os testes de `io` usam os arquivos de exemplo |
| NFR-003-01 | T11, T25 | `test_mesh` (razão de memória); medição registrada (AC-12) |
| NFR-003-02 | T24, T25 | `bench_gmsh_read`, medição registrada (AC-12) |
| NFR-003-03 | T26 | Tempo no `ctest` |

## Notas de implementação

**2026-11-11 — Fases 0 a 2 (T00–T11):**

- Testes e implementação compilados juntos, e não em dois passos (teste
  falhando, depois implementação). Mesmo desvio das features anteriores.
- 63 testes passando; o teste de memória (196 608 Tet4 com um grupo) roda em
  menos de 1 s em Debug.
- Desvios do design §4: `IdRanges` ficou em `include/fem/core/detail/` (e não
  como arquivo privado), porque `Mesh` o guarda como membro; `Mesh` ganhou
  `liesInXYPlane()`, calculado uma vez na construção, para o sinal das áreas
  sem percorrer a malha a cada elemento.
- A lista de tipos é um X-macro gerado (`element_types.inc`), usado no enum
  público e na tabela; as coordenadas de referência ficam num arquivo privado
  (`element_reference_coordinates.inc`, cerca de 320 kB).

**2026-11-11 — Fase 3 (T12 parcial, T13):**

- `generate_test_meshes.py` grava os exemplos pela API do Gmsh 4.15.2 (sem
  arquivos `.geo`: a geometria fica no próprio script). Total de 1,2 MB, mais
  que as "poucas centenas de kB" do design, por causa dos elementos de ordem
  alta em `all_types` (465 kB em ASCII).
- `.msh` marcados como binários no `.gitattributes`, para o git não converter
  fins de linha dos arquivos binários.
- **Problema:** o Gmsh 4.15.2 derruba o processo ao criar elementos de 13 dos
  103 tipos (hexaedros incompletos de ordem 4 a 9, tipos 100–105; pirâmides
  incompletas de ordem 3 a 9, tipos 125–131). Eles ficaram fora de
  `all_types`, e o design (§8) pede que o arquivo com todos os tipos seja
  gravado pelo Gmsh. Decisão do mantenedor (2026-11-11): tirar os 13 tipos.
  Requisitos, design, gerador da tabela e testes atualizados para 90 tipos;
  os 13 passam a ser ignorados com aviso na leitura.
- O tipo 84 ("Line 1"), usado para testar o aviso de tipo não suportado,
  também não é aceito pelo Gmsh; `extra_sections.msh` foi escrito à mão.

**2026-11-11 — Fase 4 (T14–T23):**

- 80 testes passando no Windows, build sem avisos, `clang-format` e
  `clang-tidy` limpos (o `clang-tidy` exigiu trocar algoritmos clássicos por
  `std::ranges` e anotar a macro de propagação de erros do leitor).
- `all_types` em ASCII e binário: os 90 tipos lidos com cada nó na posição
  dada pelas coordenadas de referência (AC-7), o que confirma a ordem dos nós
  para todos os tipos. Malhas reais de ordem 1 e 2 reproduzem comprimento 2,
  área 6 e volume 6, todas as medidas positivas.
- **Desvio do design §8:** "ASCII e binário produzem malhas iguais" não vale
  bit a bit para as coordenadas: o Gmsh grava ASCII com 16 algarismos
  significativos, um a menos que o necessário para reproduzir o `double`
  exato. O teste compara coordenadas com tolerância de 1e−15 relativa; ids,
  tipos, conectividade e grupos são comparados exatamente.
- O teste de caminho com acentos usa uma pasta temporária com letras
  portuguesas e chinesas; passou no Windows. Linux e macOS dependem do CI.
- O teste de números independentes do locale troca o `LC_NUMERIC` para
  pt_BR quando a máquina o tem; no Windows a leitura usa `std::from_chars`,
  no macOS deve usar o caminho alternativo (`strtod_l`), que só o CI exercita.
