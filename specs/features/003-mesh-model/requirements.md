# Requisitos — 003 mesh-model

Status: rascunho · Data: 2026-11-11

## 1. Contexto e objetivo

Toda a análise, a visualização e a geração de malha do femapp giram em torno de
uma estrutura de malha comum. Esta feature cria essa estrutura no núcleo
(`core::Mesh`): nós, elementos de todos os tipos da biblioteca do produto
(lineares e quadráticos, de 0 a 3 dimensões, misturados no mesmo modelo),
mais a linha de 3 nós, que aparece no contorno das malhas quadráticas, e
grupos nomeados. Também entrega o leitor de malhas do Gmsh (`.msh` 4.1, ASCII e
binário), que fornece malhas reais para a exibição (feature 004) e para a
verificação numérica (features 006 em diante) antes de o gerador de malha
próprio existir. Não há interface nesta feature: tudo é biblioteca, verificada
por testes automáticos.

## 2. Histórias de usuário

- **US-1** Como desenvolvedor das features de análise e de exibição, quero uma
  estrutura de malha única, com todos os tipos de elemento e grupos, para não
  ter de adaptar cada módulo a um formato diferente.
- **US-2** Como engenheiro, quero importar malhas geradas no Gmsh, com os grupos
  físicos que defini lá, para analisar modelos no femapp antes de o gerador
  próprio cobrir o meu caso.
- **US-3** Como engenheiro, quero mensagens claras quando um arquivo de malha
  estiver corrompido ou usar recursos não suportados, para saber o que corrigir
  sem ficar com uma malha incompleta sem perceber.
- **US-4** Como desenvolvedor, quero que malhas grandes caibam na memória e
  sejam lidas rapidamente, para que o objetivo de 1 milhão de elementos do
  produto seja alcançável.

## 3. Requisitos funcionais (EARS)

### Tipos de elemento

| ID | Requisito | História |
|----|-----------|----------|
| REQ-003-01 | O sistema DEVE suportar os tipos de elemento: ponto (1 nó), linha de 2 e de 3 nós, triângulo de 3 e de 6 nós, quadrilátero de 4 e de 8 nós, tetraedro de 4 e de 10 nós, hexaedro de 8 e de 20 nós e cunha de 6 nós. | US-1 |
| REQ-003-02 | O sistema DEVE informar, para cada tipo, a dimensão topológica (0 a 3), o número de nós, o número de vértices e a ordem (linear ou quadrática). | US-1 |
| REQ-003-03 | O sistema DEVE definir e documentar, para cada tipo, a ordem local dos nós, incluindo a posição dos nós de meio de aresta dos tipos quadráticos. | US-1 |

### Malha

| ID | Requisito | História |
|----|-----------|----------|
| REQ-003-04 | O sistema DEVE armazenar os nós com coordenadas 3D em precisão dupla, cada um com um identificador externo (o número do nó no arquivo de origem) e um índice interno contíguo a partir de 0. | US-1 |
| REQ-003-05 | O sistema DEVE armazenar elementos de tipos diferentes na mesma malha, cada um com identificador externo, tipo e lista de nós. | US-1 |
| REQ-003-06 | O sistema DEVE permitir consultar o número de nós, o número de elementos por tipo, os nós de um elemento, as coordenadas de um nó e a conversão entre identificador externo e índice interno, de nós e de elementos. | US-1 |
| REQ-003-07 | O sistema DEVE calcular a caixa envolvente da malha. | US-1 |
| REQ-003-08 | O sistema DEVE calcular a medida de cada elemento — comprimento, área ou volume, conforme a dimensão — a partir dos seus vértices, com sinal para os elementos de dimensão igual à da malha que permita detectar elementos invertidos. | US-1, US-3 |
| REQ-003-09 | SE um elemento referenciar um nó inexistente, tiver número de nós diferente do exigido pelo seu tipo, ou repetir um nó, ENTÃO O sistema DEVE recusar o elemento com uma mensagem que identifique o elemento e o problema. | US-3 |
| REQ-003-10 | SE dois nós ou dois elementos tiverem o mesmo identificador externo, ENTÃO O sistema DEVE recusar o segundo com uma mensagem que identifique o identificador repetido. | US-3 |

### Grupos

| ID | Requisito | História |
|----|-----------|----------|
| REQ-003-11 | O sistema DEVE permitir definir grupos nomeados de elementos, cada um com dimensão (0 a 3) e com elementos dessa dimensão. | US-1, US-2 |
| REQ-003-12 | O sistema DEVE permitir que um elemento pertença a vários grupos ou a nenhum. | US-2 |
| REQ-003-13 | O sistema DEVE permitir obter um grupo pelo nome e obter o conjunto de nós usados pelos elementos de um grupo. | US-1 |
| REQ-003-14 | SE um grupo for criado com um nome já usado na mesma dimensão, ou com elementos de outra dimensão, ENTÃO O sistema DEVE recusar o grupo com uma mensagem que identifique o grupo. | US-3 |

### Leitor `.msh`

| ID | Requisito | História |
|----|-----------|----------|
| REQ-003-15 | QUANDO receber um arquivo `.msh` na versão 4.1, em ASCII ou binário, O sistema DEVE carregar seus nós e elementos numa malha. | US-2 |
| REQ-003-16 | QUANDO o arquivo tiver grupos físicos, O sistema DEVE criar um grupo por grupo físico, com o nome definido no arquivo ou, na falta dele, um nome gerado a partir da dimensão e do número do grupo. | US-2 |
| REQ-003-17 | O sistema DEVE aceitar números de nós e de elementos não contíguos e em qualquer ordem, e coordenadas paramétricas nos nós (que são ignoradas). | US-2 |
| REQ-003-18 | O sistema DEVE ignorar seções do arquivo que não descrevem a malha (por exemplo, dados de resultados e periodicidade). | US-2 |
| REQ-003-19 | SE o arquivo tiver elementos de tipos não suportados (por exemplo, pirâmide ou cunha de 15 nós), ENTÃO O sistema DEVE ignorá-los e informar, num aviso, cada tipo ignorado e quantos elementos dele havia. | US-3 |
| REQ-003-20 | SE, depois de ignorar os tipos não suportados, não restar nenhum elemento, ENTÃO O sistema DEVE recusar o arquivo. | US-3 |
| REQ-003-21 | SE o arquivo estiver em outra versão do formato, for uma malha particionada ou um arquivo binário com ordem de bytes diferente da do computador, ENTÃO O sistema DEVE recusar o arquivo com uma mensagem que diga qual dessas situações ocorreu. | US-3 |
| REQ-003-22 | SE o arquivo estiver malformado (seção incompleta, número inválido, fim inesperado, referência a nó inexistente), ENTÃO O sistema DEVE recusar o arquivo com uma mensagem que indique o problema e a linha (ASCII) ou a posição em bytes (binário). | US-3 |
| REQ-003-23 | SE a leitura falhar, ENTÃO O sistema NÃO DEVE devolver malha parcial. | US-3 |
| REQ-003-24 | O sistema DEVE ler arquivos cujo caminho tenha caracteres não ASCII (por exemplo, acentos), em todas as plataformas. | US-2 |

### Camadas e testes

| ID | Requisito | História |
|----|-----------|----------|
| REQ-003-25 | O sistema DEVE manter a malha no módulo `core` e o leitor no módulo `io`, nenhum dos dois dependente de Qt. | US-1 |
| REQ-003-26 | O sistema DEVE ter testes automáticos com arquivos `.msh` de exemplo, em ASCII e em binário, que cubram cada tipo de elemento suportado, grupos físicos e os casos de erro dos requisitos acima. | US-1, US-3 |

## 4. Requisitos não funcionais

| ID | Requisito | Como medir |
|----|-----------|------------|
| NFR-003-01 | Uma malha de 1 milhão de tetraedros de 4 nós DEVE ocupar no máximo 1,5 vez o tamanho dos dados brutos (coordenadas em `double` e conectividade em inteiros de 32 bits). | Teste de memória com malha sintética, em build Release |
| NFR-003-02 | A leitura de um `.msh` binário com 1 milhão de tetraedros de 4 nós DEVE levar menos de 5 s; a de um ASCII equivalente, menos de 30 s. | Medição em build Release, na máquina do mantenedor |
| NFR-003-03 | Os testes unitários de `core` e `io` desta feature DEVEM rodar em menos de 5 s, sem os arquivos grandes de NFR-003-02. | Tempo no `ctest` |

## 5. Critérios de aceite

- [ ] **AC-1** (REQ-003-01, 02, 03) Os testes conferem, para cada um dos doze
  tipos, dimensão, número de nós e de vértices, ordem, e que a ordem local dos
  nós documentada produz medida positiva no elemento de referência.
- [ ] **AC-2** (REQ-003-04 a 07) Uma malha montada em teste com tipos misturados
  (ponto, linha, triângulo, hexaedro) responde corretamente às consultas de
  quantidade, nós, coordenadas, conversão de identificadores e caixa
  envolvente.
- [ ] **AC-3** (REQ-003-08) A soma das medidas dos elementos reproduz o
  comprimento, a área e o volume conhecidos de malhas de referência; um
  elemento invertido tem medida negativa.
- [ ] **AC-4** (REQ-003-09, 10, 14) Cada situação de erro de malha e de grupo é
  recusada com mensagem que identifica o item.
- [ ] **AC-5** (REQ-003-11, 12, 13) Grupos com elementos compartilhados e
  elementos sem grupo são consultados corretamente por nome, e o conjunto de nós
  de um grupo está correto.
- [ ] **AC-6** (REQ-003-15, 16, 17) Arquivos de exemplo ASCII e binários com o
  mesmo conteúdo produzem malhas idênticas, com os grupos físicos e seus nomes.
- [ ] **AC-7** (REQ-003-15) Para cada tipo suportado há um arquivo de exemplo
  cuja leitura reproduz a medida conhecida e mostra todos os elementos com
  medida positiva (a ordem dos nós foi interpretada corretamente).
- [ ] **AC-8** (REQ-003-18, 19, 20) Um arquivo com seções extras e com um tipo
  não suportado é lido com aviso que nomeia o tipo e a quantidade; um arquivo
  só com tipos não suportados é recusado.
- [ ] **AC-9** (REQ-003-21, 22, 23) Cada caso de arquivo inválido é recusado
  com a mensagem esperada (com linha ou posição) e sem malha parcial.
- [ ] **AC-10** (REQ-003-24) Um arquivo num caminho com acentos é lido nas três
  plataformas do CI.
- [ ] **AC-11** (REQ-003-25) `check_no_qt_includes` passa, e o módulo `io` não
  liga Qt.
- [ ] **AC-12** (NFR-003-01, 02) Medições de memória e de tempo registradas em
  `tasks.md`, dentro das metas.

## 6. Fora de escopo

- Menu de importação e exibição da malha na interface — feature 004.
- Escrita de `.msh` e formatos `.msh` 2.2 e 4.0; outros formatos de malha.
- Tipos de elemento fora da biblioteca do produto, exceto a linha de 3 nós
  (cunha de 15 nós, quadrilátero de 9 nós, hexaedro de 27 nós, pirâmide etc.).
- Formulação de elemento para a linha de 3 nós: aqui ela é só um tipo de
  malha; o uso em cargas e contornos quadráticos vem nas features de elementos.
- Malhas particionadas, periódicas e dados de resultados do `.msh`.
- Geração de malha, fusão de nós coincidentes (feature 009) e métricas de
  qualidade (feature 023).
- Funções de forma, quadratura e `DofMap` (feature 006).
- Ligação dos grupos às entidades de geometria (features de geração de malha).
- Edição da malha (remover ou mover nós e elementos).

## 7. Requisitos globais relacionados

REQ-G-11 (importação de `.msh` com nós, elementos e grupos físicos — aqui a
biblioteca; a ação na interface vem na 004), REQ-G-13 (tipos de elemento
misturados no mesmo modelo), REQ-G-20 (malhas de até 1 milhão de elementos,
quanto a memória e leitura).

## 8. Questões em aberto

- [x] **Manter a importação de `.msh`?** Decidido em 2026-11-11: sim, versão 4.1
  ASCII e binário (questão em aberto de `specs/product.md`).
- [x] **Tipos de elemento:** decidido em 2026-11-11 — só os da biblioteca do
  produto, mais o ponto.
- [x] **Menu de importação:** decidido em 2026-11-11 — fica para a feature 004.
- [x] **Linha de 3 nós:** decidido em 2026-11-11 — entra como tipo suportado,
  para que malhas quadráticas do Gmsh com contornos possam ser importadas.
  Os demais tipos fora da biblioteca são ignorados com aviso (REQ-003-19).
- [x] **Arquivos de exemplo para os testes:** decidido em 2026-11-11 — gerados
  com o Gmsh (programa GPL usado só como ferramenta, na máquina do mantenedor;
  os arquivos gerados não ficam sob GPL) e versionados junto com os scripts
  `.geo` que os produziram.
