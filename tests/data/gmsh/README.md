# Arquivos `.msh` de exemplo

Usados pelos testes de `io` (feature 003). Todos no formato Gmsh 4.1; os
gerados pelo Gmsh existem em ASCII (`*.msh`) e binário (`*_bin.msh`).

| Arquivo | Conteúdo |
|---------|----------|
| `all_types` | Um elemento de cada tipo que o Gmsh consegue gravar; o número do elemento é o número do tipo no Gmsh, e os nós são as coordenadas de referência levadas por uma transformação afim conhecida (`all_types_transform` no script) |
| `segment_o1`, `segment_o2` | Segmento de comprimento 2, grupo `segment` |
| `square_*` | Quadrado 2×3 no plano XY em Tri3, Tri6, Quad4, Quad8 e Quad9; grupos `plate` (2D), `left` (1D) e um grupo 1D sem nome |
| `box_hex8`, `box_hex20`, `box_wedge6`, `box_wedge15` | Caixa 1×2×3 extrudada da base; grupos `solid` (3D) e `bottom` (2D) |
| `box_tet4`, `box_tet10` | Caixa 1×2×3 em tetraedros; grupo `solid` (3D) e um grupo 2D sem nome |
| `handwritten/` | Escritos à mão: ids esparsos e coordenadas paramétricas, seções extras com um tipo não suportado, e os arquivos inválidos dos testes de erro |

## Como regenerar

Os arquivos ficam versionados; o build e o CI não precisam do Gmsh. Para
regenerar (por exemplo, com outra versão do Gmsh):

```bash
python -m pip install gmsh
```

```bash
python tests/data/gmsh/generate_test_meshes.py
```

O arquivo grande da medição de desempenho (cerca de 1 milhão de Tet4) não é
versionado; gere-o fora do repositório:

```bash
python tests/data/gmsh/generate_test_meshes.py --large build/perf-data
```

Gerados com o Gmsh 4.15.2. O Gmsh (GPL) é usado só como ferramenta: não é
ligado ao femapp, e os arquivos que ele grava não ficam sob a sua licença.

## Observações

- O Gmsh 4.15.2 não consegue criar elementos de 13 tipos (hexaedros
  incompletos de ordem 4 a 9 e pirâmides incompletas de ordem 3 a 9):
  `addElementsByType` derruba o processo. Por isso eles não estão em
  `all_types`.
- O Gmsh também não cria elementos do tipo 84 ("Line 1"), o único tipo de
  tamanho fixo fora da tabela do femapp; o arquivo
  `handwritten/extra_sections.msh` usa esse tipo para testar o aviso de tipo
  não suportado, e por isso o próprio Gmsh não o abre.
