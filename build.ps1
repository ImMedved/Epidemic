[CmdletBinding()]
param(
    [ValidateSet('Full', 'Matrix', 'Module', 'Debug', 'Validators', 'ListTests')]
    [string]$Mode = 'Full',
    [ValidateSet('Debug', 'Release', 'Both')]
    [string]$Configuration = 'Both',
    [string]$Module,
    [string]$TestRegex,
    [int]$RepeatUntilFail = 1,
    [int]$HeartbeatSec = 30,
    [int]$TimeoutSec = 3600,
    [switch]$NoCache,
    [switch]$Clean,
    [switch]$Help
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Root
. (Join-Path $Root 'tools/ci/build_cpp.ps1')

$CacheDir = Join-Path $Root '.ci_cache'
$LogDir = Join-Path $CacheDir 'logs'
$StampDir = Join-Path $CacheDir 'stamps'
$EnforcerDir = Join-Path $Root '.enforcer'
$ReportPath = Join-Path $CacheDir 'report.json'
$LastJsonPath = Join-Path $EnforcerDir 'Enforcer_last_check.log'
$LastMarkdownPath = Join-Path $EnforcerDir 'Enforcer_last_check.md'
$StatsPath = Join-Path $EnforcerDir 'Enforcer_stats.log'

if ($Clean -and (Test-Path $CacheDir)) { Remove-Item -LiteralPath $CacheDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $CacheDir,$LogDir,$StampDir,$EnforcerDir | Out-Null

$script:StartedAt = Get-Date
$script:Stages = @()
$script:Issues = @()
$script:Metrics = @{ test_counts = @{ total = 0; passed = 0; failed = 0; skipped = 0 } }
$script:Failed = $false

function Add-Stage {
    param([string]$Name, [string]$Status, [string]$Note, [int]$DurationMs = 0,
          [hashtable]$Details = @{})
    $script:Stages += [ordered]@{ name=$Name; status=$Status; note=$Note;
        duration_ms=$DurationMs; details=$Details }
}

function Add-Issue {
    param([string]$Tool, [string]$Rule, [string]$Message, [string]$LogPath)
    $script:Issues += [ordered]@{ language='cpp'; tool=$Tool; rule=$Rule; count=1;
        message=$Message; log_path=$LogPath }
}

function Invoke-Stage {
    param([string]$Name, [scriptblock]$Action, [switch]$Critical)
    Write-Host "`n==================== $Name ====================" -ForegroundColor Cyan
    $start = Get-Date
    try {
        $result = & $Action
        $note = if ($null -ne $result -and $result.PSObject.Properties['Note']) { $result.Note } else { '' }
        $details = if ($null -ne $result -and $result.PSObject.Properties['Details']) { $result.Details } else { @{} }
        Add-Stage -Name $Name -Status 'ok' -Note $note -DurationMs ([int]((Get-Date)-$start).TotalMilliseconds) -Details $details
        return $result
    } catch {
        $script:Failed = $true
        $message = $_.Exception.Message
        Add-Stage -Name $Name -Status 'fail' -Note $message -DurationMs ([int]((Get-Date)-$start).TotalMilliseconds)
        Add-Issue -Tool $Name -Rule 'stage_failed' -Message $message -LogPath ''
        Write-Host $message -ForegroundColor Red
        if ($Critical) { throw }
    }
}

function Add-SkippedStage {
    param([string]$Name, [string]$Reason)
    Add-Stage -Name $Name -Status 'skip' -Note $Reason
}

function Get-TrackedHash {
    $lines = & git ls-files --cached --others --exclude-standard 2>$null
    if ($LASTEXITCODE -ne 0) { return '' }
    $builder = [System.Text.StringBuilder]::new()
    foreach ($relative in @($lines | Sort-Object)) {
        if ($relative -match '^(?:Deltas|build|\.ci_cache|\.enforcer|\.ae2-reference)/') { continue }
        $path = Join-Path $Root $relative
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { continue }
        [void]$builder.AppendLine($relative)
        [void]$builder.AppendLine((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash)
    }
    $bytes = [Text.Encoding]::UTF8.GetBytes($builder.ToString())
    [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
}

function Test-TrustedStamp {
    param([string]$Name, [string]$Hash)
    if ($NoCache -or [string]::IsNullOrWhiteSpace($Hash)) { return $false }
    $hashPath = Join-Path $StampDir "$Name.sha256"
    $trustPath = Join-Path $StampDir "$Name.trusted"
    (Test-Path $hashPath) -and (Test-Path $trustPath) -and
        ((Get-Content -LiteralPath $hashPath -Raw).Trim() -eq $Hash)
}

function Write-TrustedStamp {
    param([string]$Name, [string]$Hash)
    if ([string]::IsNullOrWhiteSpace($Hash)) { return }
    Set-Content -LiteralPath (Join-Path $StampDir "$Name.sha256") -Value $Hash -Encoding utf8
    New-Item -ItemType File -Force -Path (Join-Path $StampDir "$Name.trusted") | Out-Null
}

function Assert-ProcessSuccess {
    param($Result, [string]$Tool)
    if ($Result.ExitCode -ne 0) {
        Add-Issue -Tool $Tool -Rule (if ($Result.TimedOut) { 'timeout' } else { 'exit_code' }) `
            -Message "$($Result.Label) failed with exit code $($Result.ExitCode)" -LogPath $Result.LogPath
        throw "$($Result.Label) failed. Full log: $($Result.LogPath)"
    }
}

function Invoke-Validators {
    $commands = @(
        @('python', @('docs/freeze/goal4_evidence_quality.py','--check'), 'goal4-quality-check'),
        @('python', @('docs/freeze/goal4_evidence_quality.py','--self-test'), 'goal4-quality-self-test'),
        @('python', @('docs/freeze/module_dossiers.py','--check'), 'dossiers-check'),
        @('python', @('docs/freeze/module_dossiers.py','--self-test'), 'dossiers-self-test'),
        @('python', @('docs/freeze/architecture_ownership.py','--check'), 'architecture-check'),
        @('python', @('docs/freeze/architecture_ownership.py','--self-test'), 'architecture-self-test'),
        @('python', @('docs/freeze/coverage_manifests.py','--check'), 'coverage-check'),
        @('python', @('docs/freeze/coverage_manifests.py','--self-test'), 'coverage-self-test'),
        @('python', @('docs/freeze/ctest_manifest.py','--check'), 'ctest-manifest-check'),
        @('python', @('docs/freeze/ctest_manifest.py','--self-test'), 'ctest-manifest-self-test'),
        @('python', @('docs/freeze/local_ready_contract.py','--check'), 'local-ready-check'),
        @('python', @('docs/freeze/local_ready_contract.py','--self-test'), 'local-ready-self-test'),
        @('python', @('docs/freeze/public_api_inventory.py','--check'), 'public-api-check'),
        @('python', @('docs/freeze/public_api_inventory.py','--self-test'), 'public-api-self-test'),
        @('python', @('docs/freeze/public_surface_manifest.py','--check'), 'public-surface-check'),
        @('python', @('docs/freeze/public_surface_manifest.py','--self-test'), 'public-surface-self-test'),
        @('python', @('docs/freeze/ci_gate_contract.py','--check'), 'ci-contract-check'),
        @('python', @('docs/freeze/ci_gate_contract.py','--self-test'), 'ci-contract-self-test'),
        @('cmake', @('-P','cmake/ArchitectureFreezeSelfTest.cmake'), 'cmake-architecture-self-test')
    )
    foreach ($command in $commands) {
        $result = Invoke-Ae2Process -FilePath $command[0] -Arguments $command[1] -Label $command[2] `
          -LogPath (Join-Path $LogDir "$($command[2]).log") -WorkingDirectory $Root `
          -HeartbeatSec $HeartbeatSec -TimeoutSec $TimeoutSec
        Assert-ProcessSuccess -Result $result -Tool $command[2]
    }
    [pscustomobject]@{ Note='All freeze/evidence validators and negative fixtures passed'; Details=@{} }
}

function Write-Reports {
    $finished = Get-Date
    $status = if ($script:Stages.status -contains 'fail') { 'fail' }
              elseif ($script:Stages.status -contains 'warn') { 'warn' } else { 'ok' }
    $report = [ordered]@{
        schema_version=1
        started_at_utc=$script:StartedAt.ToUniversalTime().ToString('o')
        finished_at_utc=$finished.ToUniversalTime().ToString('o')
        duration_ms=[int]($finished-$script:StartedAt).TotalMilliseconds
        status=$status
        mode=$Mode
        stages=$script:Stages
        issues=$script:Issues
        metrics=$script:Metrics
    }
    $json = $report | ConvertTo-Json -Depth 12
    Set-Content -LiteralPath $ReportPath -Value $json -Encoding utf8
    Set-Content -LiteralPath $LastJsonPath -Value $json -Encoding utf8

    $markdown = @("# Epidemic local CI", '', "- Status: **$($status.ToUpperInvariant())**",
        "- Mode: $Mode", "- Started: $($report.started_at_utc)",
        "- Duration: $($report.duration_ms) ms", '', '## Stages', '')
    foreach ($stage in $script:Stages) {
        $markdown += "- **$($stage.status.ToUpperInvariant())** $($stage.name): $($stage.note)"
    }
    $markdown += @('', '## Issues', '')
    if ($script:Issues.Count -eq 0) { $markdown += '- None' }
    foreach ($issue in $script:Issues) {
        $suffix = if ($issue.log_path) { " Log: $($issue.log_path)" } else { '' }
        $markdown += "- [$($issue.tool)/$($issue.rule)] $($issue.message).$suffix"
    }
    $markdown += @('', "Structured report: $ReportPath", "Full command logs: $LogDir")
    Set-Content -LiteralPath $LastMarkdownPath -Value $markdown -Encoding utf8

    $history = [ordered]@{ timestamp_utc=$finished.ToUniversalTime().ToString('o');
        status=$status; mode=$Mode; duration_ms=$report.duration_ms; issues=$script:Issues }
    Add-Content -LiteralPath $StatsPath -Value ($history | ConvertTo-Json -Depth 8 -Compress) -Encoding utf8

    Write-Host "`n==================== SUMMARY ====================" -ForegroundColor Cyan
    foreach ($stage in $script:Stages) {
        $color = switch ($stage.status) { 'ok' {'Green'} 'fail' {'Red'} 'warn' {'Yellow'}
            'cached' {'Cyan'} default {'DarkGray'} }
        Write-Host ("{0,-34} {1,-7} {2}" -f $stage.name,$stage.status.ToUpperInvariant(),$stage.note) -ForegroundColor $color
    }
    Write-Host "Report: $LastMarkdownPath" -ForegroundColor DarkGray
}

try {
    Invoke-Stage -Name 'self-check' -Critical -Action {
        $required = @('run.ps1','build.ps1','tools/ci/build_cpp.ps1','.ci/config.json','.ci/ci_report.schema.json')
        foreach ($path in $required) { if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing $path" } }
        foreach ($tool in @('git','cmake','ctest','python')) {
            if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "Required tool is not in PATH: $tool" }
        }
        foreach ($scriptPath in @('run.ps1','build.ps1','tools/ci/build_cpp.ps1')) {
            $tokens=$null; $errors=$null
            [void][Management.Automation.Language.Parser]::ParseFile((Join-Path $Root $scriptPath),[ref]$tokens,[ref]$errors)
            if ($errors.Count -gt 0) { throw "$scriptPath parser error: $($errors[0].Message)" }
        }
        Get-Content -LiteralPath '.ci/config.json' -Raw | ConvertFrom-Json | Out-Null
        Get-Content -LiteralPath '.ci/ci_report.schema.json' -Raw | ConvertFrom-Json | Out-Null
        [pscustomobject]@{ Note='Runner, orchestrator, config and required tools are valid'; Details=@{} }
    } | Out-Null

    Add-SkippedStage -Name 'fmt' -Reason 'No repository-wide C++ formatter policy is adopted yet; tracked in CI_TODO.md'
    Add-SkippedStage -Name 'line-limits' -Reason 'No enforceable baseline has been approved; tracked in CI_TODO.md'

    $sourceHash = Get-TrackedHash
    if (Test-TrustedStamp -Name 'validators' -Hash $sourceHash) {
        Add-Stage -Name 'lint:freeze-validators' -Status 'cached' -Note 'Trusted source hash'
    } else {
        Invoke-Stage -Name 'lint:freeze-validators' -Critical -Action { Invoke-Validators } | Out-Null
        Write-TrustedStamp -Name 'validators' -Hash $sourceHash
    }

    if ($Mode -notin @('Validators')) {
        $profiles = if ($Mode -eq 'Matrix') {
            Get-EpidemicProfiles -Mode 'Matrix' -Configuration 'Both'
        } elseif ($Mode -in @('Debug','ListTests')) {
            @(Get-EpidemicProfile -Name 'full-debug')
        } else {
            Get-EpidemicProfiles -Mode $Mode -Configuration $Configuration
        }

        foreach ($profile in $profiles) {
            $buildDir = Join-Path $Root "build/ae2/$($profile.Name)"
            Invoke-Stage -Name "build:$($profile.Name)" -Critical -Action {
                $configure = Invoke-EpidemicConfigure -Profile $profile -Root $Root -BuildDir $buildDir -LogDir $LogDir `
                    -HeartbeatSec $HeartbeatSec -TimeoutSec $TimeoutSec
                Assert-ProcessSuccess $configure 'cmake-configure'
                if ($Mode -eq 'ListTests') {
                    return [pscustomobject]@{ Note='Configured for test discovery'; Details=@{} }
                }
                $build = Invoke-EpidemicBuild -Profile $profile -Root $Root -BuildDir $buildDir -LogDir $LogDir `
                    -HeartbeatSec $HeartbeatSec -TimeoutSec $TimeoutSec
                Assert-ProcessSuccess $build 'cmake-build'
                $headers = Invoke-EpidemicBuild -Profile $profile -Root $Root -BuildDir $buildDir -LogDir $LogDir `
                    -HeartbeatSec $HeartbeatSec -TimeoutSec $TimeoutSec -Target 'EpidemicPublicHeaderSelfContainment'
                Assert-ProcessSuccess $headers 'public-header-self-containment'
                [pscustomobject]@{ Note='/W4 /WX build and public-header self-containment passed'; Details=@{} }
            } | Out-Null

            if ($Mode -eq 'ListTests') {
                $names = Get-EpidemicRegisteredTests -Profile $profile -Root $Root -BuildDir $buildDir -LogDir $LogDir `
                    -HeartbeatSec $HeartbeatSec -TimeoutSec $TimeoutSec
                $names | ForEach-Object { Write-Host $_ }
                Add-Stage -Name 'list-tests' -Status 'ok' -Note "$($names.Count) tests registered"
                continue
            }

            Invoke-Stage -Name "test:$($profile.Name)" -Critical -Action {
                $manifest = Invoke-EpidemicManifestCheck -Profile $profile -Root $Root -BuildDir $buildDir -LogDir $LogDir `
                    -HeartbeatSec $HeartbeatSec -TimeoutSec $TimeoutSec
                Assert-ProcessSuccess $manifest 'ctest-manifest'
                $names = Get-EpidemicRegisteredTests -Profile $profile -Root $Root -BuildDir $buildDir -LogDir $LogDir `
                    -HeartbeatSec $HeartbeatSec -TimeoutSec $TimeoutSec
                $regex = Resolve-EpidemicTestRegex -TestNames $names -Module $Module -TestRegex $TestRegex
                $tests = Invoke-EpidemicTests -Profile $profile -Root $Root -BuildDir $buildDir -LogDir $LogDir `
                    -HeartbeatSec $HeartbeatSec -TimeoutSec $TimeoutSec -Regex $regex `
                    -RepeatUntilFail $RepeatUntilFail -VerboseOutput:($Mode -eq 'Debug')
                Assert-ProcessSuccess $tests 'ctest'
                $script:Metrics.test_counts.total += $tests.TestTotal
                $script:Metrics.test_counts.passed += $tests.TestPassed
                $script:Metrics.test_counts.failed += $tests.TestFailed
                [pscustomobject]@{ Note="$($tests.TestPassed)/$($tests.TestTotal) tests passed";
                    Details=@{ profile=$profile.Name; regex=$regex; log=$tests.LogPath } }
            } | Out-Null
        }
    }

    Add-SkippedStage -Name 'coverage' -Reason 'No Windows C++ coverage collector is configured; stage retained and enablement tracked in CI_TODO.md'
    Add-SkippedStage -Name 'security' -Reason 'No package manifest/SAST baseline is present; Architecture Freeze remains enforced'
    Add-SkippedStage -Name 'archive' -Reason 'Testing entry point does not create release archives'
}
catch {
    $script:Failed = $true
}
finally {
    Write-Reports
}

if ($script:Failed -or ($script:Stages.status -contains 'fail')) { exit 1 }
exit 0
