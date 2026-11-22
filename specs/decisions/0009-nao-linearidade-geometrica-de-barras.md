# ADR 0009 — Não linearidade geométrica de barras: corrotacional com a barra exata e barra geometricamente exata na isoparamétrica

Status: proposta (feature 008e adiada para depois da 1.0) · Data: 2026-11-22

## Contexto

A feature 008e traz grandes deslocamentos e rotações para treliças e vigas
retas, curvas e com empenamento (REQ-008e-01 a 05). Ela precisa de:

- pontos críticos com a deformação antes da flambagem (Kirby e Nethercot);
- cargas seguidoras;
- coincidir com a 008c/008d no limite de carga pequena.

O `core` já tem a barra exata de espaço de estados (008b a 008d). Ela dá o
P-δ e a flexo-torção exatos com um elemento por barra, a partir de um perfil
de referência dos seis esforços. Tem também a contagem de Sturm com as
contagens internas das barras (ADR 0007) e a energia de barra da
isoparamétrica (ADR 0008).

As formulações conhecidas para barras com grandes rotações são três:

- a corrotacional (Crisfield; Battini e Pacoste, com empenamento);
- a lagrangiana total;
- a barra geometricamente exata (Simo e Reissner; Simo e Vu-Quoc, com
  empenamento).

## Decisão

- **Elementos de dois nós e o arco exato: corrotacionais.**
  - O referencial é o de Battini e Pacoste (2002), com o empenamento
    aditivo.
  - O elemento local é a barra exata da 008c/008d, com o perfil de
    referência tirado dos esforços locais correntes.
  - O ponto fixo do perfil é resolvido pelo próprio Newton global.
  - A tangente local é consistente: a variação do perfil com os
    deslocamentos locais entra por diferenças finitas nos seis esforços de
    início.
- **Isoparamétrica de três nós: barra geometricamente exata.**
  - As deformações são as de Simo e Reissner, com a interpolação objetiva
    das rotações de Crisfield e Jelenić.
  - Wagner e monossimetria vêm da deformação axial da fibra com ½·r²·K₁².
  - O empenamento de Vlasov fica por penalidade, como na 008d.
- **Rotações nodais:** vetores de rotação totais como incógnitas, com
  orientações em quatérnio, atualização multiplicativa e Tₛ(θ) na tangente.
  - Os momentos mortos são conjugados ao vetor de rotação: conservativos,
    semitangenciais até a segunda ordem.
  - Sem cargas seguidoras, a tangente é simétrica e a contagem de Sturm do
    ADR 0007 continua valendo.
- **Tangentes não simétricas** (cargas seguidoras): montagem completa e
  `Eigen::SparseLU`. Os pontos críticos vêm do sinal do determinante.

## Consequências

- **O que se mantém da 008c/008d:**
  - uma barra por vão continua exata para o P-δ e a flexo-torção sob
    rotações moderadas;
  - grandes rotações pedem tantos elementos quanto a curvatura do caminho
    exigir;
  - os pontos críticos a partir de reto coincidem com a 008c/008d;
  - a flambagem a partir de um estado reaproveita a busca do ADR 0007.
- **Verificação:** duas formulações independentes (o arco corrotacional e a
  isoparamétrica exata) concordam com o refinamento.
- **Custo:** cada iteração avalia o motor exato sete vezes por elemento. Na
  medida da §5.12 do design, isso dá 1,0 a 1,4 vez a análise linear por
  iteração, porque a fatoração domina.
- **Momentos aplicados:** em 3D geral, um momento morto é conservativo pelo
  vetor de rotação. A reescala em |θ| > π e o trabalho em ciclo fechado são
  verificados por teste.
- **Com cargas seguidoras:** a contagem de Sturm vira paridade, e a
  flambagem linearizada a partir de um estado não é oferecida. O flutter
  fica fora.

## Alternativas consideradas

- **Barra geometricamente exata em tudo:** os `Line2` lineares travam, e
  perde-se a exatidão por barra da 008c/008d.
- **Corrotacional com elemento local linear ou cúbico raso:** pede 4 a 8
  elementos por barra para o P-δ e a flexo-torção.
- **Lagrangiana total:** mesmo trabalho com as rotações finitas, sem
  reaproveitar o elemento exato.
- **Tangente local sem a variação do perfil:** Newton linear e pontos
  críticos deslocados.
- **Momentos pela rotação espacial (spin):** não conservativos, e a
  tangente fica não simétrica mesmo com cargas mortas.
