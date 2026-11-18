# ADR 0008 — Flexo-torção pela segunda variação da energia

Status: aceita · Data: 2026-11-18

## Contexto

A feature 008d acrescenta à segunda ordem e à flambagem da 008c os efeitos
de flexo-torção: o acoplamento do giro com os momentos fletores, o termo de
Wagner, a monossimetria e a altura da carga (REQ-008d-10 a 14).

A 008c pôs o esforço normal no motor exato linearizando o equilíbrio da
barra na configuração deformada (M' = … − u'×(N·e₁)). Com momentos de
referência, a mesma linearização acrescenta θ×M₀ aos esforços locais e dá
uma rigidez **não simétrica**. É o resultado clássico dos momentos "axiais"
descritos pelo vetor de rotação (Argyris, 1979). Todo o resto da 008c
supõe K simétrica: a LDLᵀ com a inércia, a contagem de Sturm de
Wittrick-Williams (ADR 0007), o gradiente conjugado da segunda ordem e o
Rayleigh-Ritz da flambagem.

## Decisão

- Os termos de flexo-torção vêm da **segunda variação de um funcional
  quadrático** de Vlasov-Trahair. Ele tem os termos elásticos da 008b, o de
  N (com Engesser, como na 008c), o de Wagner N·r0², os de monossimetria
  βy·My e βz·Mz, o acoplamento 2·Mz·φ·w'' − 2·My·φ·v'' e a altura da
  carga −(q·d)·φ² (Trahair, 1993; Yang e McGuire, 1986).
- As equações de Euler-Lagrange desse funcional entram na matriz A do motor
  exato, e a mesma segunda variação dá a rigidez geométrica da
  isoparamétrica. A rigidez de toda barra fica simétrica.
- Os **momentos nodais aplicados são semitangenciais**: é o tipo de momento
  conservativo que esse funcional pressupõe nas pontas, e ele não pede
  rigidez de carga. A convenção fica documentada em `NodalForce`.
- Só com N, o funcional reproduz a 008c: os números dela não mudam.

## Consequências

- A LDLᵀ, a contagem de Sturm, o gradiente conjugado e a flambagem da 008c
  servem sem mudança de algoritmo.
- O significado de um momento aplicado num nó que gira passa a ser o
  semitangencial na segunda ordem e na flambagem. Na análise linear nada
  muda. Um teste com o pórtico em L de Argyris confere o equilíbrio dos
  momentos nas juntas de barras em ângulo.
- Os sinais dos termos dependem da convenção de esforços da 008 e são
  fixados pelos casos fechados (momento crítico com momento uniforme, Pcr
  de torção e de flexo-torção) antes dos casos tabelados.
- Um momento "axial" ou seguidor (que acompanha a rotação), se for pedido
  um dia, entra como rigidez de carga própria, fora desta feature.

## Alternativas consideradas

- Linearização no equilíbrio deformado, como a 008c fez com N — rigidez
  não simétrica com momentos. Isso pede LU no lugar da LDLᵀ e perde a
  contagem de Sturm, a garantia de nenhum fator perdido.
- Simetrizar a rigidez não simétrica, tomando a parte simétrica — mudaria
  a física sem uma convenção de momento que a justifique; os casos fechados
  deixariam de bater.
- Momentos quase tangenciais com momentos induzidos nas juntas (Yang e
  McGuire, 1986) — também é simétrica, mas pede termos de junta à parte no
  elemento. O semitangencial sai direto do funcional.
