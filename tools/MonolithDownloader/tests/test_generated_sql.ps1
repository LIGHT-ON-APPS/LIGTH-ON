param(
    [string]$Executable = (Join-Path $PSScriptRoot "..\..\..\MonolithDownloader.exe")
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Assert-Equal {
    param(
        [object]$Actual,
        [object]$Expected,
        [string]$Description
    )

    if ($Actual -ne $Expected) {
        throw "$Description`: expected $Expected, got $Actual"
    }
}

$resolvedExecutable = (Resolve-Path -LiteralPath $Executable).Path
$assembly = [Reflection.Assembly]::LoadFile($resolvedExecutable)
$engineType = $assembly.GetType("LightOn.MonolithDownloader.DownloadEngine", $true)
$method = $engineType.GetMethod(
    "BuildThermistorCsvSql",
    [Reflection.BindingFlags]"NonPublic,Static"
)

if ($null -eq $method) {
    throw "BuildThermistorCsvSql was not found in $resolvedExecutable"
}

$sql = [string]$method.Invoke($null, @("2026-01-02", "2026-01-03"))
$normalizedSql = $sql.Replace("`r`n", "`n")

Assert-Equal ([regex]::Matches($normalizedSql, " AS temp_\d{2}_c").Count) 45 `
    "temperature aliases"
Assert-Equal ([regex]::Matches($normalizedSql, " AS fault_\d{2}").Count) 45 `
    "fault aliases"
Assert-Equal ([regex]::Matches(
        $normalizedSql,
        " AS measurement_device_ts_\d{2}_ms"
    ).Count) 45 "per-sensor device timestamp aliases"
Assert-Equal ($normalizedSql.Contains("interval '35 seconds'")) $true `
    "35-second date-boundary padding"
Assert-Equal ($normalizedSql.Contains("% 4294967296::bigint) > 6000")) $true `
    "wrap-safe source timestamp gap boundary"
Assert-Equal ($normalizedSql.Contains("received_sensor_count = 45")) $true `
    "45-channel completeness check"
Assert-Equal ($normalizedSql.Contains("AS server_delivery_duration_ms")) $true `
    "server delivery duration column"
Assert-Equal ($normalizedSql.Contains("AS measurement_span_ms")) $true `
    "source measurement span column"

<# Count only top-level expressions in the final SELECT. Function arguments
   contain commas, so a plain comma split would over-count the CSV columns. #>
$selectStart = $normalizedSql.LastIndexOf("  SELECT`n    channel,")
$selectEnd = $normalizedSql.IndexOf("`n  FROM scans", $selectStart)
if ($selectStart -lt 0 -or $selectEnd -lt 0) {
    throw "final thermistor SELECT was not found"
}

$selectList = $normalizedSql.Substring(
    $selectStart + "  SELECT`n".Length,
    $selectEnd - ($selectStart + "  SELECT`n".Length)
)
$depth = 0
$inQuote = $false
$topLevelCommas = 0

for ($index = 0; $index -lt $selectList.Length; $index++) {
    $character = $selectList[$index]
    if ($character -eq "'") {
        if ($inQuote -and $index + 1 -lt $selectList.Length `
            -and $selectList[$index + 1] -eq "'") {
            $index++
            continue
        }
        $inQuote = -not $inQuote
        continue
    }

    if ($inQuote) {
        continue
    }

    if ($character -eq "(") {
        $depth++
    }
    elseif ($character -eq ")") {
        $depth--
    }
    elseif ($character -eq "," -and $depth -eq 0) {
        $topLevelCommas++
    }
}

Assert-Equal ($topLevelCommas + 1) 150 "thermistor CSV column count"

Write-Output "MonolithDownloader generated SQL: PASS (150 columns)"
