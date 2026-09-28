[CmdletBinding()]
param(
    [ValidateSet('Full', 'Matrix', 'Module', 'Debug', 'Validators', 'ListTests')]
    [string]$Mode = 'Full',
    [ValidateSet('Debug', 'Release', 'Both')]
    [string]$Configuration = 'Both',
    [string]$Module,
    [string]$TestRegex,
    [ValidateRange(1, 1000)]
    [int]$RepeatUntilFail = 1,
    [ValidateRange(5, 600)]
    [int]$HeartbeatSec = 30,
    [ValidateRange(0, 86400)]
    [int]$TimeoutSec = 3600,
    [switch]$NoCache,
    [switch]$Clean,
    [Alias('h', '?')]
    [switch]$Help
)

$ErrorActionPreference = 'Stop'

function Show-Help {
    @'
Epidemic local CI (AgentEnforcer2 architecture)

Usage:
  ./run.ps1                                      Full Debug+Release, all tests
  ./run.ps1 -Mode Matrix                         Base/Runtime/Full Debug+Release
  ./run.ps1 -Mode Module -Module Simulation      Tests containing "Simulation"
  ./run.ps1 -Mode Module -TestRegex '^Name$'     Exact/custom CTest regex
  ./run.ps1 -Mode Debug -Module Combat           Verbose Debug reproduction
  ./run.ps1 -Mode Debug -TestRegex 'Gameplay' -RepeatUntilFail 20
  ./run.ps1 -Mode Validators                     Freeze/evidence gates only
  ./run.ps1 -Mode ListTests                      List registered Full/Debug tests

Options:
  -Configuration Debug|Release|Both  Module/Full configuration selection
  -HeartbeatSec N                    Progress heartbeat interval (default 30)
  -TimeoutSec N                      Per-command timeout; 0 disables it
  -NoCache                           Ignore trusted validator/self-check stamps
  -Clean                             Remove local CI cache before the run

Every run writes:
  .ci_cache/report.json              Structured latest report
  .ci_cache/logs/                    Complete command output
  .enforcer/Enforcer_last_check.md   Readable latest result
  .enforcer/Enforcer_last_check.log  Machine-readable latest result
  .enforcer/Enforcer_stats.log       Append-only history

New CTest tests are discovered automatically. Update docs/freeze/ctest_manifest.json
when the intended exact test matrix changes.
'@ | Write-Host
}

if ($args.Count -gt 0) {
    Write-Error "Unknown argument(s): $($args -join ', ')"
    exit 2
}

if ($Help) {
    Show-Help
    exit 0
}

if ($Mode -eq 'Module' -and [string]::IsNullOrWhiteSpace($Module) -and
    [string]::IsNullOrWhiteSpace($TestRegex)) {
    Write-Error 'Mode Module requires -Module or -TestRegex.'
    exit 2
}

if ($Mode -eq 'Debug') {
    $Configuration = 'Debug'
}

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
& (Join-Path $root 'build.ps1') @PSBoundParameters
exit $LASTEXITCODE
