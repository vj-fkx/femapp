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

- Resolver K(λ) φ = 0 **reaproveitando fatorações** (alterado em
  2026-11-17, após a medida da T03: uma fatoração custa quase uma análise
  linear, uma solução com ela 1/150 disso): estimativas por iteração de
  subespaço sobre o problema linearizado K₀ φ = −μ K'(0) φ com a fatoração
  da análise linear (Rayleigh-Ritz com os solvers densos do Eigen); uma
  fatoração em σ₁ abaixo da primeira estimativa; refinamento de cada fator
  pela iteração inversa residual (Neumaier) com essa fatoração.
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
- Custo: no caso típico, três fatorações (a da análise linear, a de σ₁ e
  a da contagem final), mais produtos K(λ)x baratos; fatores espalhados ou
  perdidos pedem fatorações extras. O NFR-008c-02 mede.
- A mesma ideia serve à segunda ordem: a fatoração da análise linear
  pré-condiciona o gradiente conjugado das iterações, e uma fatoração final
  dá a contagem.
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

## Adendo (2026-11-19, feature 008d, T18b)

Com a flexo-torção, K(λ) deixa de ser quase linear em λ (a flambagem
lateral vai com λ²: os momentos na seção girada), e as estimativas do
pencil linear K0 − λG erram os fatores por dezenas de vezes. Duas mudanças:

- **Estimativas pelo pencil quadrático** K0 − λG + λ²K2, usado quando o
  termo em λ² pesa na primeira estimativa linear: Rayleigh-Ritz na base de
  K0⁻¹Gx e K0⁻¹K2x, resolvido na forma companheira; K2 é reajustado a cada
  iteração para que o quadrático passe também por K(λ) na primeira
  estimativa (as barras exatas não são quadráticas em λ).
- **Fatores perdidos**: a bissecção na contagem global vai só até 10⁻³;
  depois, iteração inversa e refinamento a partir da fatoração na ponta de
  baixo (fator nodal), ou bissecção da contagem interna de uma barra
  (modo dentro dela), confirmada pela contagem global; só sem nenhum dos
  dois a bissecção global vai até o fim.

A garantia de Wittrick-Williams continua a mesma: nenhum fator é aceito
sem a contagem global. A meta de custo da flambagem com flexo-torção
passou a ≤ 400 vezes a linear (NFR-008d-02): uma contagem custa cerca de
3 vezes a análise linear no benchmark.
