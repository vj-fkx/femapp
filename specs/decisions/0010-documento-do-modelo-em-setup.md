# ADR 0010 — O documento do modelo num módulo próprio, `setup`

Status: aceita · Data: 2026-11-23

## Contexto

A feature 010 cria o documento que o usuário edita para preparar uma
análise: unidades, materiais, seções, atribuições, apoios, casos de carga e
combinações. Esses dados são guardados nas entidades do desenho (IDs do
`geom`), em grupos e em seleções da malha. A partir deles se monta o
`core::Model` de um caso ou de uma combinação, pelo mapa malha → geometria
da 009 (`mesher`).

O documento precisa ser testável sem Qt (REQ-010-01) e será usado pela
análise assíncrona (011) e pelo arquivo de projeto (014). O grafo de
dependências da arquitetura não tem lugar para ele:

- o `core` não conhece o `geom` nem o `mesher`;
- o `mesher` é geração de malha;
- o `app` é Qt.

## Decisão

- Um módulo novo, **`setup`** (`fem::setup`, biblioteca estática, sem Qt),
  depende de `core`, `geom` e `mesher`. O `app` passa a depender dele.
  Nenhum outro módulo depende do `setup`. O `render` recebe os símbolos
  como dados simples (`render::SymbolSet`).
- No `setup` ficam: as unidades, o documento e as suas edições
  reversíveis, a conversão dos tipos do `core` entre sistemas, a
  biblioteca de materiais e a montagem do `core::Model` com a conferência.
- O documento reusa os tipos do `core` como dados dos itens, com o alvo
  trocado por `setup::Targets`.
- `check_no_qt_includes` e a cobertura de linhas (≥ 80%) passam a cobrir
  o `setup`.

Grafo:

```
app ──► setup ──► mesher ──► geom
          │         │
          └─────────┴──────► core
```

## Consequências

- **Testes:** o documento e a montagem são testados como o `core`, sem
  interface. A 011 recebe uma montagem pronta, e a 014 serializa o
  `setup::Document`.
- **Grafo e build:** a tabela de módulos da arquitetura ganha uma linha, o
  grafo ganha uma seta, e o CI cobre mais um módulo.
- **Dependência do `core`:** uma mudança nos tipos do `core` (um campo
  novo) pede o campo na conversão de unidades do `setup`. Um teste de
  conversão com todos os campos não nulos acusa o esquecimento.

## Alternativas consideradas

- **No `app`:** não seria testável sem Qt, e a 011 e a 014 dependeriam da
  interface.
- **No `core`:** inverteria o grafo, porque o `core` passaria a depender de
  `geom` e de `mesher`.
- **No `mesher`:** misturaria a preparação do modelo com a geração de
  malha, e o `mesher` ganharia dependências do `core` além da `Mesh`.
