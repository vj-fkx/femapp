# Design — NNN nome-da-feature

Status: rascunho | aprovado · Data: AAAA-MM-DD
Requisitos: [requirements.md](requirements.md)

## 1. Resumo da abordagem

<Em poucas frases: como a feature será construída.>

## 2. Verificação da constituição

| Princípio | Atende? | Observação |
|-----------|---------|------------|
| Multiplataforma (§2) | | |
| GPU somente via QRhi (§3) | | |
| Camadas / `core` sem Qt (§4) | | |
| Correção numérica (§5) | | |
| Simplicidade / dependências (§7) | | |

## 3. Módulos e arquivos afetados

| Módulo | Arquivo | Novo/Alterado | Responsabilidade |
|--------|---------|---------------|------------------|
| core | `src/core/…` | novo | … |

## 4. Interfaces públicas

```cpp
// Assinaturas das classes/funções novas ou alteradas.
```

## 5. Dados e algoritmos

<Estruturas de dados, formulação matemática (para elementos: funções de forma,
quadratura, matriz B), complexidade, formato de arquivos.>

## 6. Fluxo

<Sequência de chamadas / threads envolvidas. Diagrama em texto se ajudar.>

## 7. Tratamento de erros

| Situação | Comportamento | Requisito |
|----------|---------------|-----------|
| … | … | REQ-NNN-xx |

## 8. Estratégia de testes

| Teste | Tipo | Requisito | Referência/tolerância |
|-------|------|-----------|-----------------------|
| … | unitário / verificação / manual | REQ-NNN-xx | … |

## 9. Alternativas consideradas

| Alternativa | Por que foi descartada |
|-------------|------------------------|
| … | … |

## 10. Riscos e mitigação

- <risco> → <mitigação>

## 11. Novas dependências

<Nenhuma | nome, versão, licença, justificativa.>
