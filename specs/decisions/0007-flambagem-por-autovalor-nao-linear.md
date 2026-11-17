# ADR 0007 — Flambagem por autovalor não linear com contagem de Sturm

Status: aceita · Data: 2026-11-17

## Contexto

A feature 008c pede o fator de carga crítica exato na barra, com um
elemento por vão (REQ-008c-10). O esforço normal entra na viga pelo motor
exato (funções de estabilidade), e a rigidez passa a depender de λ de forma
transcendental, não linear. Por isso, a flambagem deixa de ser o autovalor
linear clássico K φ = −λ K_g φ e vira o problema não linear K(λ) φ = 0.

`architecture.md` previa o Spectra para autovalores (análise modal), mas ele
resolve só problemas lineares e ainda não está no projeto.

## Decisão

- Resolver K(λ) φ = 0 por **iteração com deslocamento**: em σ, fatorar
  K(σ) e fazer iteração de subespaço sobre o problema linearizado
  K(σ) φ = −μ K'(σ) φ (Rayleigh-Ritz com os solvers densos do Eigen), com
  K' pela diferença central no início e depois pela secante; repetir com
  σ ← σ + μ até convergir.
- Garantir que nenhum fator falte pela **contagem de Wittrick-Williams**:
  pivôs negativos da LDLᵀ global (sem pivoteamento, que dá a inércia) mais
  os pivôs negativos das condensações internas de cada barra, com as partes
  do motor curtas o bastante para não ter autovalor próprio abaixo de λ.
  Bissecção quando a contagem acusar falta.
- O `SparseCholesky` ganha a fatoração indefinida com a inércia e o reuso da
  análise simbólica.
- Nenhuma dependência nova. O Spectra fica para a feature modal, quando
  essa decisão for revista.

## Consequências

- Fatores exatos com um elemento por vão, inclusive com ligações, base
  elástica e Timoshenko.
- A contagem também detecta a instabilidade na análise de segunda ordem.
- Custo: algumas fatorações por fator (deslocamento), mais as das
  contagens. É mais que um autovalor linear de um subespaço, e o NFR-008c-02
  mede.
- A LDLᵀ indefinida sem pivoteamento é sensível perto de pivôs quase nulos:
  os pontos de contagem ficam afastados dos fatores.
- A análise modal (dinâmica) pode reaproveitar a iteração de subespaço e a
  contagem, ou adotar o Spectra; a decisão fica para aquela feature.

## Alternativas consideradas

- Autovalor linear com K_g polinomial (Spectra ou subespaço) — não é exato
  com um elemento por vão e exige subdividir as barras.
- Só bissecção de Wittrick-Williams — exata, mas 30 a 40 fatorações por
  fator.
- Determinante de K(λ) com busca de raízes — perde raízes múltiplas e muda
  de sinal sem controle em barras com pivôs internos.
