$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Add-SectionIfMissing {
    param(
        [Parameter(Mandatory=$true)][string]$Path,
        [Parameter(Mandatory=$true)][string]$Marker,
        [Parameter(Mandatory=$true)][string]$Content
    )

    if (!(Test-Path $Path)) {
        throw "Missing file: $Path"
    }

    $raw = Get-Content -Path $Path -Raw -Encoding UTF8
    if ($raw -notlike "*$Marker*") {
        Add-Content -Path $Path -Value "`r`n$Content`r`n" -Encoding UTF8
    }
}

$roadmap = @'
<!-- DLM_RUNTIME_HARDWARE_LAYERING_PRINCIPLE -->

## Global Architecture Principle — Runtime / Hardware Layering

DLM must separate four layers:

```text
1. Source / mathematical layer
   full meaning, proof, passport, theory, trust, provenance, history, audit.

2. IR / compiler layer
   verification, optimization, bridge policy, proof/passport erasure, audit.

3. Runtime control layer
   compact passport descriptors, capabilities, location, scheduling, runtime witnesses.

4. Hardware execution layer
   raw memory, dense buffers, kernels, SIMD/SIMT, minimal metadata.
```

Main rule:

```text
Meaning should be holographic.
Execution should be dense.
```

DLM must not move full mathematical meaning into every low-level hardware operation. In particular, future runtime/GPU/compiler patches must avoid:

```text
proof checking inside GPU kernels;
history chain per array element;
full passports across PCIe;
dynamic dispatch inside every GPU thread;
branching by trust-level inside SIMD/SIMT lanes;
small GPU tasks instead of batch kernels;
frequent CPU <-> GPU round trips.
```

The strength of DLM is the opposite: passports guide the compiler/runtime scheduler before execution.

```text
Passport is not a GPU burden.
Passport is a CPU/compiler instruction for safe hardware use.
```

This principle is specified in:

```text
docs/RUNTIME_HARDWARE_LAYERING_PRINCIPLE.md
```
'@

$readme = @'
<!-- DLM_RUNTIME_HARDWARE_LAYERING_README -->

## Runtime / Hardware Layering Principle

DLM follows a global performance/soundness rule:

```text
Meaning should be holographic.
Execution should be dense.
```

The language keeps full proof/passport/trust/history semantics at the source and compiler/audit layers, but low-level runtime and hardware execution must use compact descriptors, dense buffers, deterministic kernels, and minimal metadata.

See `docs/RUNTIME_HARDWARE_LAYERING_PRINCIPLE.md`.
'@

$notes = @'
<!-- DLM_RUNTIME_HARDWARE_LAYERING_IMPLEMENTATION_NOTES -->

## Runtime / Hardware Layering Principle

Future compiler/runtime/GPU work must preserve the four-layer separation:

```text
Source / mathematical layer -> IR / compiler layer -> Runtime control layer -> Hardware execution layer
```

Proof, passport, trust and history are full semantic objects at the high level. Hot runtime paths must use proof-erased code, compact passport descriptors, dense buffers, explicit location capabilities, and batch scheduling.

Forbidden direction:

```text
full passport per scalar;
proof checking inside GPU kernel;
history chain per array element;
trust-level branching inside SIMD/SIMT;
implicit CPU/GPU transfer;
implicit materialization.
```
'@

$update = @'
<!-- DLM_RUNTIME_HARDWARE_LAYERING_UPDATE -->

## Runtime / Hardware Layering Principle

Added global roadmap principle:

```text
Meaning should be holographic.
Execution should be dense.
```

This records that passports/proofs/history guide compiler/runtime decisions, but hardware execution must remain dense and minimal.
'@

$docsReadme = @'
<!-- DLM_RUNTIME_HARDWARE_LAYERING_DOCS_README -->

- `RUNTIME_HARDWARE_LAYERING_PRINCIPLE.md` — global rule for separating mathematical meaning, compiler/audit logic, runtime control metadata, and dense hardware execution.
'@

Add-SectionIfMissing -Path "ROADMAP.md" -Marker "DLM_RUNTIME_HARDWARE_LAYERING_PRINCIPLE" -Content $roadmap
Add-SectionIfMissing -Path "README.md" -Marker "DLM_RUNTIME_HARDWARE_LAYERING_README" -Content $readme
Add-SectionIfMissing -Path "IMPLEMENTATION_NOTES.md" -Marker "DLM_RUNTIME_HARDWARE_LAYERING_IMPLEMENTATION_NOTES" -Content $notes
Add-SectionIfMissing -Path "UPDATE.md" -Marker "DLM_RUNTIME_HARDWARE_LAYERING_UPDATE" -Content $update
Add-SectionIfMissing -Path "docs\README.md" -Marker "DLM_RUNTIME_HARDWARE_LAYERING_DOCS_README" -Content $docsReadme

$manifestPath = "docs\MANIFEST.json"
if (Test-Path $manifestPath) {
    $json = Get-Content -Path $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($null -ne $json.docs) {
        $docs = @($json.docs)
        if ($docs -notcontains "RUNTIME_HARDWARE_LAYERING_PRINCIPLE.md") {
            $json.docs = @($docs + "RUNTIME_HARDWARE_LAYERING_PRINCIPLE.md" | Sort-Object)
            $json | ConvertTo-Json -Depth 10 | Set-Content -Path $manifestPath -Encoding UTF8
        }
    }
}

Write-Host "Runtime / Hardware Layering Principle applied."
