param(
  [string]$Harness = ".\build\tools\vst3_harness\Release\vst3_harness.exe",
  [string]$Plugin  = ".\build\TwoCCompressor_artefacts\Debug\VST3\2C-Compressor.vst3",
  [string]$Dry     = ".\tests\hats_dry.wav",
  [string]$DryKick = ".\tests\kickbass_dry.wav",
  [int]$Sr         = 48000,
  [int]$Bs         = 512,
  [int]$Ch         = 2,
  [int]$Warmup     = 10
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-ParameterIndexMap {
  $lines = & $Harness dump-params --plugin $Plugin
  if ($LASTEXITCODE -ne 0) {
    throw "Failed to dump plugin parameters (exit code $LASTEXITCODE)."
  }

  $map = @{}
  foreach ($line in $lines) {
    if ($line -match '^(\d+)\t(.+?)\t') {
      $index = [int]$matches[1]
      $name = $matches[2].Trim()
      if ($name.Length -gt 0) {
        $map[$name] = $index
      }
    }
  }

  if ($map.Count -eq 0) {
    throw "No parameters parsed from dump-params output."
  }

  return $map
}

function Build-SetParams {
  param(
    [Parameter(Mandatory = $true)][hashtable]$ParameterIndexMap,
    [Parameter(Mandatory = $true)][hashtable]$ValuesByName
  )

  $entries = New-Object System.Collections.Generic.List[object]
  foreach ($entry in $ValuesByName.GetEnumerator()) {
    $name = [string]$entry.Key
    $value = [double]$entry.Value

    if (-not $ParameterIndexMap.ContainsKey($name)) {
      throw "Parameter '$name' not found in dump-params output."
    }

    if ($value -lt 0.0 -or $value -gt 1.0) {
      throw "Normalized value out of range for '$name': $value"
    }

    $index = [int]$ParameterIndexMap[$name]
    $entries.Add([pscustomobject]@{
      Index = $index
      Value = [string]::Format([System.Globalization.CultureInfo]::InvariantCulture, '{0:0.######}', $value)
    })
  }

  $sortedPairs = $entries | Sort-Object -Property Index | ForEach-Object { "$($_.Index)=$($_.Value)" }
  return ($sortedPairs -join ",")
}

function Get-FirstWavOrThrow {
  param(
    [Parameter(Mandatory = $true)][string]$Directory
  )

  $wav = Get-ChildItem $Directory -Filter "*.wav" -ErrorAction Stop | Select-Object -First 1
  if ($null -eq $wav) {
    throw "No .wav file found in directory: $Directory"
  }

  return $wav.FullName
}

function Reset-Directory {
  param(
    [Parameter(Mandatory = $true)][string]$Path
  )

  if (Test-Path $Path) {
    Remove-Item -Path $Path -Recurse -Force
  }

  New-Item -ItemType Directory -Force $Path | Out-Null
}

function Resolve-WetPath {
  param(
    [Parameter(Mandatory = $true)][string]$Directory
  )

  $wetPath = Join-Path $Directory "wet.wav"
  if (Test-Path $wetPath) {
    return $wetPath
  }

  return (Get-FirstWavOrThrow $Directory)
}

function Invoke-RenderCase {
  param(
    [Parameter(Mandatory = $true)][string]$OutDir,
    [Parameter(Mandatory = $true)][string]$SetParams,
    [Parameter(Mandatory = $true)][string]$InputPath
  )

  Reset-Directory $OutDir
  & $Harness render --plugin $Plugin --in $InputPath --outdir $OutDir --sr $Sr --bs $Bs --ch $Ch --warmup $Warmup --set-params $SetParams
  if ($LASTEXITCODE -ne 0) {
    throw "Harness render failed (exit code $LASTEXITCODE): $Harness render --plugin $Plugin --in $InputPath --outdir $OutDir --sr $Sr --bs $Bs --ch $Ch --warmup $Warmup --set-params $SetParams"
  }
}

function Invoke-AnalyzeCase {
  param(
    [Parameter(Mandatory = $true)][string]$DryPath,
    [Parameter(Mandatory = $true)][string]$WetPath,
    [Parameter(Mandatory = $true)][string]$OutDir,
    [switch]$DoNull
  )

  Reset-Directory $OutDir
  if ($DoNull) {
    & $Harness analyze --dry $DryPath --wet $WetPath --outdir $OutDir --auto-align --null
  }
  else {
    & $Harness analyze --dry $DryPath --wet $WetPath --outdir $OutDir --auto-align
  }

  if ($LASTEXITCODE -ne 0) {
    throw "Harness analyze failed (exit code $LASTEXITCODE): $Harness analyze --dry $DryPath --wet $WetPath --outdir $OutDir --auto-align"
  }
}

function Read-Metrics {
  param(
    [Parameter(Mandatory = $true)][string]$AnalysisDir
  )

  $metricsPath = Join-Path $AnalysisDir "metrics.json"
  if (!(Test-Path $metricsPath)) {
    throw "Metrics not found: $metricsPath"
  }

  $m = Get-Content $metricsPath -Raw | ConvertFrom-Json
  return [pscustomobject]@{
    RmsDb    = [double]$m.rms_delta_db
    PeakDb   = [double]$m.peak_delta_db
    RmsDryDb = [double]$m.rms_dry_db
    RmsWetDb = [double]$m.rms_wet_db
  }
}

function Assert-Lt {
  param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][double]$Value,
    [Parameter(Mandatory = $true)][double]$Threshold
  )

  if ($Value -lt $Threshold) {
    Write-Host "PASS $Name ($Value dB < $Threshold dB)" -ForegroundColor Green
  }
  else {
    throw "FAIL $Name ($Value dB >= $Threshold dB)"
  }
}

function Assert-Gt {
  param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][double]$Value,
    [Parameter(Mandatory = $true)][double]$Threshold
  )

  if ($Value -gt $Threshold) {
    Write-Host "PASS $Name ($Value dB > $Threshold dB)" -ForegroundColor Green
  }
  else {
    throw "FAIL $Name ($Value dB <= $Threshold dB)"
  }
}

if (!(Test-Path $Harness)) { throw "Harness not found: $Harness" }
if (!(Test-Path $Plugin))  { throw "Plugin not found: $Plugin" }
if (!(Test-Path $Dry))     { throw "Dry input not found: $Dry" }
if (!(Test-Path $DryKick)) { throw "SC HPF dry input not found: $DryKick" }

$results = New-Object System.Collections.Generic.List[object]
$testResults = New-Object System.Collections.Generic.List[object]
$paramIndexMap = Get-ParameterIndexMap

function Add-TestResult {
  param(
    [Parameter(Mandatory = $true)][string]$Test,
    [Parameter(Mandatory = $true)][bool]$Passed,
    [string]$Message = ""
  )

  $testResults.Add([pscustomobject]@{
    Test = $Test
    Status = if ($Passed) { "PASS" } else { "FAIL" }
    Message = $Message
  })
}

function Invoke-TestCase {
  param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][scriptblock]$Body
  )

  try {
    & $Body
    Add-TestResult -Test $Name -Passed $true -Message ""
  }
  catch {
    $message = $_.Exception.Message
    Write-Host "FAIL ${Name}: $message" -ForegroundColor Red
    Add-TestResult -Test $Name -Passed $false -Message $message
  }
}

Write-Host "=== 2C-Compressor Harness Tests ==="
Write-Host "Harness : $Harness"
Write-Host "Plugin  : $Plugin"
Write-Host "Dry     : $Dry"
Write-Host "DryKick : $DryKick"
Write-Host "SR/BS   : $Sr / $Bs"
Write-Host ""

Invoke-TestCase -Name "P0 bypass null" -Body {
  # -------------------------
  # Test P0: Hard bypass null (deterministic harness/plugin sanity check)
  # -------------------------
  $P0Dir = ".\artifacts\test_p0_clean"
  $P0Params = Build-SetParams -ParameterIndexMap $paramIndexMap -ValuesByName @{
    "Bypass" = 1.0
  }
  Reset-Directory $P0Dir
  & $Harness render --plugin $Plugin --in $Dry --outdir $P0Dir --sr $Sr --bs $Bs --ch $Ch --warmup $Warmup --set-params $P0Params
  if ($LASTEXITCODE -ne 0) {
    throw "Harness render failed in P0 (exit code $LASTEXITCODE)"
  }
  $WetP0 = Resolve-WetPath $P0Dir
  $P0AnalysisDir = Join-Path $P0Dir "analysis"
  Reset-Directory $P0AnalysisDir
  & $Harness analyze --dry $Dry --wet $WetP0 --outdir $P0AnalysisDir --auto-align --null
  if ($LASTEXITCODE -ne 0) {
    throw "Harness analyze failed in P0 (exit code $LASTEXITCODE)"
  }
  $P0Metrics = Read-Metrics $P0AnalysisDir
  $results.Add([pscustomobject]@{ Test = "P0 bypass null"; Rms_dB = $P0Metrics.RmsDb; Peak_dB = $P0Metrics.PeakDb })
  Assert-Lt "P0 RMS" $P0Metrics.RmsDb -120
  Assert-Lt "P0 Peak" $P0Metrics.PeakDb -100
  Write-Host ""
}

Invoke-TestCase -Name "P0b neutral controls" -Body {
  # -------------------------
  # Test P0b: Neutral controls null-ish (informational, no hard fail)
  # -------------------------
  $P0bDir = ".\artifacts\test_p0b_neutral_controls"
  $P0bParams = Build-SetParams -ParameterIndexMap $paramIndexMap -ValuesByName @{
    "Input" = 0.5
    "Threshold" = 1.0
    "Ratio" = 0.0
    "Makeup" = 0.333333
    "Drive" = 0.0
    "Sat Mix" = 0.0
    "Oversampling" = 0.0
    "Mix" = 1.0
    "Output" = 0.5
    "Bypass" = 0.0
  }
  Invoke-RenderCase -OutDir $P0bDir -SetParams $P0bParams -InputPath $Dry
  $WetP0b = Resolve-WetPath $P0bDir
  $P0bAnalysisDir = Join-Path $P0bDir "analysis"
  Invoke-AnalyzeCase -DryPath $Dry -WetPath $WetP0b -OutDir $P0bAnalysisDir -DoNull
  $P0bMetrics = Read-Metrics $P0bAnalysisDir
  $results.Add([pscustomobject]@{ Test = "P0b neutral controls"; Rms_dB = $P0bMetrics.RmsDb; Peak_dB = $P0bMetrics.PeakDb })
  if ($P0bMetrics.RmsDb -lt -70.0 -and $P0bMetrics.PeakDb -lt -40.0) {
    Write-Host "PASS P0b neutral controls null-ish" -ForegroundColor Green
  }
  else {
    Write-Host "WARN P0b neutral-controls null not clean (RMS=$($P0bMetrics.RmsDb) dB, Peak=$($P0bMetrics.PeakDb) dB)" -ForegroundColor Yellow
  }
  Write-Host ""
}

Invoke-TestCase -Name "C1 compress fixed" -Body {
  # -------------------------
  # Test C1: Compression engages (Fixed timing)
  # -------------------------
  $C1Dir = ".\artifacts\test_c1_compress_fixed"
  $C1Params = Build-SetParams -ParameterIndexMap $paramIndexMap -ValuesByName @{
    "Timing" = 1.0
    "Threshold" = 0.2
    "Ratio" = 0.9
    "Drive" = 0.0
    "Sat Mix" = 0.0
    "Oversampling" = 0.0
    "Mix" = 1.0
    "Bypass" = 0.0
  }
  Invoke-RenderCase -OutDir $C1Dir -SetParams $C1Params -InputPath $Dry
  $WetC1 = Resolve-WetPath $C1Dir
  $C1AnalysisDir = Join-Path $C1Dir "analysis"
  Invoke-AnalyzeCase -DryPath $Dry -WetPath $WetC1 -OutDir $C1AnalysisDir -DoNull
  $C1Metrics = Read-Metrics $C1AnalysisDir
  $results.Add([pscustomobject]@{ Test = "C1 compress fixed"; Rms_dB = $C1Metrics.RmsDb; Peak_dB = $C1Metrics.PeakDb })
  Assert-Gt "C1 RMS" $C1Metrics.RmsDb -50
  Write-Host ""
}

Invoke-TestCase -Name "Auto makeup wet RMS lift" -Body {
  # -------------------------
  # Test: under heavy compression, auto makeup ON should raise wet RMS vs OFF.
  # -------------------------
  $AutoBaseParams = @{
    "Timing" = 1.0
    "Threshold" = 0.15
    "Ratio" = 0.95
    "Makeup" = 0.333333
    "Drive" = 0.0
    "Sat Mix" = 0.0
    "Oversampling" = 0.0
    "Mix" = 1.0
    "Bypass" = 0.0
  }

  $AutoOffDir = ".\artifacts\test_auto_makeup_off"
  $AutoOnDir = ".\artifacts\test_auto_makeup_on"

  $AutoOffParams = $AutoBaseParams.Clone()
  $AutoOffParams["Auto Makeup"] = 0.0

  $AutoOnParams = $AutoBaseParams.Clone()
  $AutoOnParams["Auto Makeup"] = 1.0

  Invoke-RenderCase -OutDir $AutoOffDir -SetParams (Build-SetParams -ParameterIndexMap $paramIndexMap -ValuesByName $AutoOffParams) -InputPath $DryKick
  Invoke-RenderCase -OutDir $AutoOnDir -SetParams (Build-SetParams -ParameterIndexMap $paramIndexMap -ValuesByName $AutoOnParams) -InputPath $DryKick

  $WetAutoOff = Resolve-WetPath $AutoOffDir
  $WetAutoOn = Resolve-WetPath $AutoOnDir

  $AutoOffAnalysisDir = Join-Path $AutoOffDir "analysis_vs_dry"
  $AutoOnAnalysisDir = Join-Path $AutoOnDir "analysis_vs_dry"

  Invoke-AnalyzeCase -DryPath $DryKick -WetPath $WetAutoOff -OutDir $AutoOffAnalysisDir
  Invoke-AnalyzeCase -DryPath $DryKick -WetPath $WetAutoOn -OutDir $AutoOnAnalysisDir

  $AutoOffMetrics = Read-Metrics $AutoOffAnalysisDir
  $AutoOnMetrics = Read-Metrics $AutoOnAnalysisDir
  $autoWetLiftDb = $AutoOnMetrics.RmsWetDb - $AutoOffMetrics.RmsWetDb

  $results.Add([pscustomobject]@{ Test = "Auto makeup wet lift"; Rms_dB = $autoWetLiftDb; Peak_dB = 0.25 })
  Assert-Gt "Auto makeup wet lift" $autoWetLiftDb 0.25
  Write-Host ""
}

Invoke-TestCase -Name "Timing manual vs fixed vocal" -Body {
  # -------------------------
  # Test: Manual extremes should differ from Fixed Vocal
  # -------------------------
  $TimingCommon = @{
    "Threshold" = 0.2
    "Ratio" = 0.9
    "Drive" = 0.0
    "Sat Mix" = 0.0
    "Oversampling" = 0.0
    "Mix" = 1.0
    "Bypass" = 0.0
  }

  $TimingManualDir = ".\artifacts\test_timing_manual_extreme"
  $TimingFixedDir = ".\artifacts\test_timing_fixed_vocal"

  $TimingManualParams = $TimingCommon.Clone()
  $TimingManualParams["Timing"] = 0.0
  $TimingManualParams["Attack"] = 0.0
  $TimingManualParams["Release"] = 1.0

  $TimingFixedParams = $TimingCommon.Clone()
  $TimingFixedParams["Timing"] = 1.0
  # Deliberately opposite extremes: fixed mode should ignore these.
  $TimingFixedParams["Attack"] = 1.0
  $TimingFixedParams["Release"] = 0.0

  Invoke-RenderCase -OutDir $TimingManualDir -SetParams (Build-SetParams -ParameterIndexMap $paramIndexMap -ValuesByName $TimingManualParams) -InputPath $DryKick
  Invoke-RenderCase -OutDir $TimingFixedDir -SetParams (Build-SetParams -ParameterIndexMap $paramIndexMap -ValuesByName $TimingFixedParams) -InputPath $DryKick

  $WetTimingManual = Resolve-WetPath $TimingManualDir
  $WetTimingFixed = Resolve-WetPath $TimingFixedDir
  $TimingAnalysisDir = ".\artifacts\test_timing_manual_vs_fixed\analysis"

  Invoke-AnalyzeCase -DryPath $WetTimingManual -WetPath $WetTimingFixed -OutDir $TimingAnalysisDir -DoNull
  $TimingMetrics = Read-Metrics $TimingAnalysisDir

  $results.Add([pscustomobject]@{ Test = "Timing MAN vs VOC"; Rms_dB = $TimingMetrics.RmsDb; Peak_dB = $TimingMetrics.PeakDb })
  Assert-Gt "Timing MAN vs VOC RMS" $TimingMetrics.RmsDb -70
  Write-Host ""
}

Invoke-TestCase -Name "Character clean vs opto" -Body {
  # -------------------------
  # Test: Character switch should alter behavior under identical settings.
  # -------------------------
  $CharacterBase = @{
    "Timing" = 1.0
    "Threshold" = 0.2
    "Ratio" = 0.9
    "Attack" = 0.5
    "Release" = 0.5
    "Drive" = 0.0
    "Sat Mix" = 0.0
    "Oversampling" = 0.0
    "Mix" = 1.0
    "Bypass" = 0.0
  }

  $CharacterCleanDir = ".\artifacts\test_character_clean"
  $CharacterOptoDir = ".\artifacts\test_character_opto"

  $CharacterCleanParams = $CharacterBase.Clone()
  $CharacterCleanParams["Character"] = 0.0
  $CharacterOptoParams = $CharacterBase.Clone()
  $CharacterOptoParams["Character"] = 1.0

  Invoke-RenderCase -OutDir $CharacterCleanDir -SetParams (Build-SetParams -ParameterIndexMap $paramIndexMap -ValuesByName $CharacterCleanParams) -InputPath $DryKick
  Invoke-RenderCase -OutDir $CharacterOptoDir -SetParams (Build-SetParams -ParameterIndexMap $paramIndexMap -ValuesByName $CharacterOptoParams) -InputPath $DryKick

  $WetCharacterClean = Resolve-WetPath $CharacterCleanDir
  $WetCharacterOpto = Resolve-WetPath $CharacterOptoDir
  $CharacterAnalysisDir = ".\artifacts\test_character_clean_vs_opto\analysis"

  Invoke-AnalyzeCase -DryPath $WetCharacterClean -WetPath $WetCharacterOpto -OutDir $CharacterAnalysisDir -DoNull
  $CharacterMetrics = Read-Metrics $CharacterAnalysisDir

  $results.Add([pscustomobject]@{ Test = "Character CLEAN vs OPTO"; Rms_dB = $CharacterMetrics.RmsDb; Peak_dB = $CharacterMetrics.PeakDb })
  Assert-Gt "Character CLEAN vs OPTO RMS" $CharacterMetrics.RmsDb -80
  Write-Host ""
}

Invoke-TestCase -Name "Transfer curve calibration" -Body {
  # -------------------------
  # Test: static transfer curve should match expected gain computer shape.
  # -------------------------
  $TransferScript = ".\tools\transfer_curve_test.py"
  if (!(Test-Path $TransferScript)) {
    throw "Transfer curve script not found: $TransferScript"
  }

  $TransferOutDir = ".\artifacts\test_transfer_curve"
  & python $TransferScript --harness $Harness --plugin $Plugin --outdir $TransferOutDir --sr $Sr --bs $Bs --ch $Ch --max-error-db 0.75
  if ($LASTEXITCODE -ne 0) {
    throw "Transfer curve calibration failed. See $TransferOutDir\transfer_metrics.json"
  }

  $TransferMetricsPath = Join-Path $TransferOutDir "transfer_metrics.json"
  if (!(Test-Path $TransferMetricsPath)) {
    throw "Transfer curve metrics not found: $TransferMetricsPath"
  }

  $TransferMetrics = Get-Content $TransferMetricsPath -Raw | ConvertFrom-Json
  $maxErrorDb = [double]$TransferMetrics.max_error_db
  $results.Add([pscustomobject]@{ Test = "Transfer max error"; Rms_dB = $maxErrorDb; Peak_dB = 0.75 })
  Write-Host ("PASS Transfer max error ({0:N3} dB < 0.75 dB)" -f $maxErrorDb) -ForegroundColor Green
  Write-Host ""
}

Invoke-TestCase -Name "SC HPF on vs off" -Body {
  # -------------------------
  # Test SC HPF On vs Off (use kick+bass source)
  # -------------------------
  $SCBaseParams = @{
    "Timing" = 1.0
    "Threshold" = 0.2
    "Ratio" = 0.9
    "Drive" = 0.0
    "Sat Mix" = 0.0
    "Oversampling" = 0.0
    "Mix" = 1.0
    "Bypass" = 0.0
  }
  $SCOnDir = ".\artifacts\test_sc_hpf_on"
  $SCOffDir = ".\artifacts\test_sc_hpf_off"
  $SCOnParams = $SCBaseParams.Clone()
  $SCOnParams["SC HPF On"] = 1.0
  $SCOffParams = $SCBaseParams.Clone()
  $SCOffParams["SC HPF On"] = 0.0
  Invoke-RenderCase -OutDir $SCOnDir -SetParams (Build-SetParams -ParameterIndexMap $paramIndexMap -ValuesByName $SCOnParams) -InputPath $DryKick
  Invoke-RenderCase -OutDir $SCOffDir -SetParams (Build-SetParams -ParameterIndexMap $paramIndexMap -ValuesByName $SCOffParams) -InputPath $DryKick
  $WetSCOn = Resolve-WetPath $SCOnDir
  $WetSCOff = Resolve-WetPath $SCOffDir
  $SCAnalysisDir = ".\artifacts\test_sc_hpf_diff\analysis"
  Invoke-AnalyzeCase -DryPath $WetSCOff -WetPath $WetSCOn -OutDir $SCAnalysisDir -DoNull
  $SCMetrics = Read-Metrics $SCAnalysisDir
  $results.Add([pscustomobject]@{ Test = "SC HPF on vs off"; Rms_dB = $SCMetrics.RmsDb; Peak_dB = $SCMetrics.PeakDb })
  Assert-Gt "SC HPF RMS" $SCMetrics.RmsDb -80
  Write-Host ""
}

Invoke-TestCase -Name "OS mode differences" -Body {
  # -------------------------
  # Test OS modes differ under saturation stress
  # -------------------------
  $OSBaseParams = @{
    "Drive" = 1.0
    "Sat Mix" = 1.0
    "Mix" = 1.0
    "Bypass" = 0.0
  }
  $OSOffDir = ".\artifacts\test_os_off"
  $OS2xDir = ".\artifacts\test_os_2x"
  $OS4xDir = ".\artifacts\test_os_4x"
  $OSOffParams = $OSBaseParams.Clone()
  $OSOffParams["Oversampling"] = 0.0
  $OS2xParams = $OSBaseParams.Clone()
  $OS2xParams["Oversampling"] = 0.5
  $OS4xParams = $OSBaseParams.Clone()
  $OS4xParams["Oversampling"] = 1.0
  Invoke-RenderCase -OutDir $OSOffDir -SetParams (Build-SetParams -ParameterIndexMap $paramIndexMap -ValuesByName $OSOffParams) -InputPath $Dry
  Invoke-RenderCase -OutDir $OS2xDir -SetParams (Build-SetParams -ParameterIndexMap $paramIndexMap -ValuesByName $OS2xParams) -InputPath $Dry
  Invoke-RenderCase -OutDir $OS4xDir -SetParams (Build-SetParams -ParameterIndexMap $paramIndexMap -ValuesByName $OS4xParams) -InputPath $Dry
  $WetOff = Resolve-WetPath $OSOffDir
  $Wet2x = Resolve-WetPath $OS2xDir
  $Wet4x = Resolve-WetPath $OS4xDir

  $OSOffVs2xAnalysisDir = Join-Path $OS2xDir "analysis_off_vs_2x"
  $OSOffVs4xAnalysisDir = Join-Path $OS4xDir "analysis_off_vs_4x"
  $OS2xVs4xAnalysisDir = ".\artifacts\test_os_2x_vs_4x\analysis"
  Invoke-AnalyzeCase -DryPath $WetOff -WetPath $Wet2x -OutDir $OSOffVs2xAnalysisDir -DoNull
  Invoke-AnalyzeCase -DryPath $WetOff -WetPath $Wet4x -OutDir $OSOffVs4xAnalysisDir -DoNull
  Invoke-AnalyzeCase -DryPath $Wet2x -WetPath $Wet4x -OutDir $OS2xVs4xAnalysisDir -DoNull

  $OSOffVs2xMetrics = Read-Metrics $OSOffVs2xAnalysisDir
  $OSOffVs4xMetrics = Read-Metrics $OSOffVs4xAnalysisDir
  $OS2xVs4xMetrics = Read-Metrics $OS2xVs4xAnalysisDir
  $results.Add([pscustomobject]@{ Test = "OS off vs 2x"; Rms_dB = $OSOffVs2xMetrics.RmsDb; Peak_dB = $OSOffVs2xMetrics.PeakDb })
  $results.Add([pscustomobject]@{ Test = "OS off vs 4x"; Rms_dB = $OSOffVs4xMetrics.RmsDb; Peak_dB = $OSOffVs4xMetrics.PeakDb })
  $results.Add([pscustomobject]@{ Test = "OS 2x vs 4x"; Rms_dB = $OS2xVs4xMetrics.RmsDb; Peak_dB = $OS2xVs4xMetrics.PeakDb })
  Assert-Gt "OS off vs 2x RMS" $OSOffVs2xMetrics.RmsDb -60
  Assert-Gt "OS off vs 4x RMS" $OSOffVs4xMetrics.RmsDb -60
  Assert-Gt "OS 2x vs 4x RMS" $OS2xVs4xMetrics.RmsDb -60

  if ($OSOffVs2xMetrics.RmsDb -gt $OS2xVs4xMetrics.RmsDb) {
    Write-Host "PASS OS ordering (off-vs-2x > 2x-vs-4x)" -ForegroundColor Green
  }
  else {
    Write-Host "WARN OS ordering unexpected (can vary by material): off-vs-2x=$($OSOffVs2xMetrics.RmsDb) dB, 2x-vs-4x=$($OS2xVs4xMetrics.RmsDb) dB" -ForegroundColor Yellow
  }
}

Write-Host ""
Write-Host "=== Metrics Summary ==="
$results | Format-Table -AutoSize
Write-Host ""
Write-Host "=== Test Status Summary ==="
$testResults | Format-Table -AutoSize
Write-Host ""

$totalTests = $testResults.Count
$failedTests = @($testResults | Where-Object { $_.Status -eq "FAIL" }).Count
$passedTests = $totalTests - $failedTests

if ($failedTests -eq 0) {
  Write-Host "CI SUMMARY: PASS ($passedTests/$totalTests tests passed, 0 failed)"
  Write-Host "All tests finished. Check artifacts/ folders for reports."
  exit 0
}
else {
  Write-Host "CI SUMMARY: FAIL ($passedTests/$totalTests tests passed, $failedTests failed)" -ForegroundColor Red
  Write-Host "All tests finished. Check artifacts/ folders for reports."
  exit 1
}
