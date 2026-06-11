$ErrorActionPreference = "Stop"

function Add-BlockOnce {
    param(
        [string]$Path,
        [string]$Marker,
        [string]$Block
    )
    if (!(Test-Path $Path)) {
        Write-Warning "File not found: $Path"
        return
    }
    $content = Get-Content -Path $Path -Raw -Encoding UTF8
    if ($content -match [regex]::Escape($Marker)) {
        Write-Host "Already applied to $Path"
        return
    }
    $append = "`r`n`r`n$Marker`r`n$Block`r`n"
    Add-Content -Path $Path -Value $append -Encoding UTF8
    Write-Host "Updated $Path"
}

$roadmapBlock = @'

## DLM Architectural Laws / Конституция архитектуры ЯРД

DLM теперь фиксирует отдельный набор обязательных архитектурных законов в `docs/DLM_ARCHITECTURAL_LAWS.md`.

Эти законы задают глобальную дисциплину проекта:

```text
1. Разделяй смысловые слои.
2. Управляй операциями через паспорт.
3. Проверяй proof наверху, стирай proof внизу.
4. Держи паспорта на регионах, а не на каждом байте.
5. Делай pure core детерминированным.
6. Все эффекты вводи через явную границу.
7. Capabilities используй как маршрутизатор вычислений.
8. Bridge должен иметь контракт сохранения.
9. Trust только ухудшается или явно доказывается.
10. History полная в audit, компактная в runtime.
11. Checker разбит на passes.
12. После resolution только ID, не String.
13. Каждый смысловой объект имеет Span.
14. Runtime данные плотные.
15. GPU только batch-first.
16. Location — часть паспорта.
17. Materialization — явный bridge.
18. Cost-class — часть модели.
19. Оптимизация должна быть verified.
20. Кэшировать нужно checked смысл.
21. Trusted base всегда видна.
22. Любой результат объясним назад.
23. Архитектура должна быть AI-agent-friendly.
24. Proof kernel должен быть минимальным.
25. Если максимум невозможен — честное понижение статуса.
```

Главная формула:

```text
Смысл должен быть голографическим.
Исполнение должно быть плотным.
Аудит должен объяснять результат назад.
```

Любой будущий патч должен сохранять эти законы. Если MVP временно не может полностью реализовать один из законов, это должно быть явно записано как `Technical Debt`, `Open Obligation` или `Known Incomplete Law Enforcement`.
'@

$readmeBlock = @'

## DLM Architectural Laws

The project now has an explicit architectural constitution: `docs/DLM_ARCHITECTURAL_LAWS.md`.

The laws protect DLM from three major failure modes:

```text
mathematically rich but soundness-unclear;
semantically beautiful but hardware-heavy;
AI-agent-developed but architecturally inconsistent.
```

The core formula is:

```text
Meaning-rich above.
Execution-dense below.
Audit-complete backward.
```

These laws govern proof erasure, passport-governed operations, bridge preservation contracts, trust monotonicity, dense runtime data, batch-first GPU execution, explicit materialization, visible trusted base, minimal proof kernel, and honest status downgrade.
'@

$notesBlock = @'

## DLM Architectural Laws enforcement note

`docs/DLM_ARCHITECTURAL_LAWS.md` is now part of the implementation discipline.

For every future patch, check:

```text
Does it preserve semantic layer separation?
Does it use passports/capabilities/trust rather than raw type-only checks?
Does it avoid carrying full proof/history into hot runtime?
Does it keep trust monotonic?
Does it preserve bridge contracts?
Does it keep effects explicit?
Does it avoid String references after resolution?
Does it preserve Span/source origin?
Does it keep runtime data dense?
Does it keep GPU execution batch-first?
Does it make materialization explicit?
Does it produce audit/explain information?
Does it remain AI-agent-friendly?
Does it honestly downgrade status when full proof is absent?
```
'@

$updateBlock = @'

## Docs update — DLM Architectural Laws

Added `docs/DLM_ARCHITECTURAL_LAWS.md`, a detailed architectural law document that defines 25 global laws for DLM development.

This is a docs-only governance patch. It does not change Rust code, but it changes the standard by which future code patches should be reviewed.
'@

$docsReadmeBlock = @'

### `DLM_ARCHITECTURAL_LAWS.md`

The architectural constitution of DLM / ЯРД. It defines 25 global laws covering semantic layer separation, passport-governed operations, proof erasure, compact runtime data, bridge preservation contracts, trust monotonicity, checker passes, ID-based resolution, Span preservation, batch-first GPU execution, verified optimization, visible trusted base, explainability, AI-agent-friendly development, minimal proof kernel, and honest status downgrade.
'@

Add-BlockOnce -Path "ROADMAP.md" -Marker "<!-- DLM_ARCHITECTURAL_LAWS_BLOCK -->" -Block $roadmapBlock
Add-BlockOnce -Path "README.md" -Marker "<!-- DLM_ARCHITECTURAL_LAWS_BLOCK -->" -Block $readmeBlock
Add-BlockOnce -Path "IMPLEMENTATION_NOTES.md" -Marker "<!-- DLM_ARCHITECTURAL_LAWS_BLOCK -->" -Block $notesBlock
Add-BlockOnce -Path "UPDATE.md" -Marker "<!-- DLM_ARCHITECTURAL_LAWS_BLOCK -->" -Block $updateBlock
Add-BlockOnce -Path "docs\README.md" -Marker "<!-- DLM_ARCHITECTURAL_LAWS_BLOCK -->" -Block $docsReadmeBlock

$manifestPath = "docs\MANIFEST.json"
if (Test-Path $manifestPath) {
    $manifest = Get-Content -Path $manifestPath -Raw -Encoding UTF8
    if ($manifest -notmatch "DLM_ARCHITECTURAL_LAWS.md") {
        if ($manifest.Trim().EndsWith("}")) {
            $manifestBlock = @'

<!-- MANIFEST NOTE: docs/DLM_ARCHITECTURAL_LAWS.md added by docs-only architectural law patch. If your manifest is strict JSON, add this document entry according to the existing schema. -->
'@
            Add-Content -Path $manifestPath -Value $manifestBlock -Encoding UTF8
            Write-Warning "MANIFEST.json exists but schema may vary; appended a manifest note. If strict JSON is required, update the manifest schema manually."
        }
    } else {
        Write-Host "MANIFEST already references DLM_ARCHITECTURAL_LAWS.md"
    }
}

Write-Host "DLM Architectural Laws patch applied."
