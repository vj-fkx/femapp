# ADR 0002 — O módulo `render` depende de Qt Widgets e dos headers privados do Qt Gui

Status: aceita · Data: 2026-11-10

## Contexto

A arquitetura define o `Viewport` como um `QRhiWidget` dentro do módulo
`render`, mas lista como dependências desse módulo apenas `core`, `geom` e
Qt Gui. Na prática:

- `QRhiWidget` pertence ao módulo **Qt Widgets**;
- as classes do QRhi (`rhi/qrhi.h`) são API semi-pública e só ficam acessíveis
  ligando o alvo **`Qt6::GuiPrivate`**;
- os shaders são compilados por `qt_add_shaders`, do módulo **Qt Shader Tools**.

## Decisão

1. O módulo `render` passa a depender de `Qt6::Widgets` (público) e de
   `Qt6::GuiPrivate` (privado).
2. Qt Shader Tools passa a ser pré-requisito de build do projeto.
3. Todo uso de `QRhi` continua confinado a `src/render`. Os headers públicos do
   módulo expõem apenas `QRhiWidget` como classe base do `Viewport`; os tipos
   do QRhi ficam atrás de um `Impl`. Uma verificação automática
   (`check_rhi_confined`) falha se outro módulo incluir headers do QRhi.
4. A lógica sem GPU (`Camera`, seleção de backend, espaçamento da grade) não
   usa tipos do Qt, para continuar testável isoladamente.

## Consequências

- O grafo de módulos não muda (`app → render → core`); muda só a lista de
  componentes do Qt usados por `render`. A tabela de `specs/architecture.md`
  será atualizada na implementação da feature 002.
- `render` fica preso à versão do Qt fixada no CI enquanto usar API
  semi-pública; atualizar o Qt exige recompilar e rodar os testes de `render`.
- Quem compila o projeto precisa instalar o módulo Qt Shader Tools.
- `render` não pode ser usado em um processo sem Qt Widgets (por exemplo, um
  executor de análises em linha de comando). Esse executor depende só de
  `core`, então não é afetado.

## Alternativas consideradas

- **`Viewport` no módulo `app` e `render` só com Qt Gui** — manteria `render`
  sem Widgets, mas espalharia o uso de `QRhiWidget` e a tradução de eventos
  pelo `app`, contrariando a arquitetura.
- **`QWindow` com QRhi próprio em `render`, embutido por
  `createWindowContainer`** — dispensa Widgets em `render`, mas traz problemas
  conhecidos de empilhamento e foco entre a janela embutida e os demais
  widgets.
