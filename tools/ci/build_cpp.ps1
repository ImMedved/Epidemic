Set-StrictMode -Version Latest

function ConvertTo-Ae2CommandText {
    param([string]$FilePath, [string[]]$Arguments)
    $quoted = foreach ($argument in $Arguments) {
        if ($argument -match '[\s"]') { '"' + ($argument -replace '"', '\"') + '"' } else { $argument }
    }
    return ((@($FilePath) + @($quoted)) -join ' ').Trim()
}

function Invoke-Ae2Process {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string]$FilePath,
        [string[]]$Arguments = @(),
        [Parameter(Mandatory)] [string]$Label,
        [Parameter(Mandatory)] [string]$LogPath,
        [Parameter(Mandatory)] [string]$WorkingDirectory,
        [int]$HeartbeatSec = 30,
        [int]$TimeoutSec = 3600,
        [switch]$VerboseOutput
    )

    $logDirectory = Split-Path -Parent $LogPath
    New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null
    $commandText = ConvertTo-Ae2CommandText -FilePath $FilePath -Arguments $Arguments
    Write-Host "[run] $commandText" -ForegroundColor DarkGray

    $start = Get-Date
    $info = [System.Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $FilePath
    $info.WorkingDirectory = $WorkingDirectory
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    foreach ($argument in $Arguments) { [void]$info.ArgumentList.Add($argument) }

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $info
    if (-not $process.Start()) { throw "Failed to start $FilePath" }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $nextHeartbeat = $start.AddSeconds($HeartbeatSec)
    $timedOut = $false

    while (-not $process.HasExited) {
        Start-Sleep -Milliseconds 250
        $now = Get-Date
        if ($now -ge $nextHeartbeat) {
            $elapsed = $now - $start
            Write-Host ("[heartbeat] {0} alive t+{1:hh\:mm\:ss} pid={2} log={3}" -f
                $Label, $elapsed, $process.Id, $LogPath) -ForegroundColor DarkCyan
            $nextHeartbeat = $now.AddSeconds($HeartbeatSec)
        }
        if ($TimeoutSec -gt 0 -and ($now - $start).TotalSeconds -gt $TimeoutSec) {
            $timedOut = $true
            try { $process.Kill($true) } catch { }
            break
        }
    }

    $process.WaitForExit()
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    $finished = Get-Date
    $exitCode = if ($timedOut) { 124 } else { $process.ExitCode }
    $log = @(
        "command: $commandText",
        "started_at: $($start.ToUniversalTime().ToString('o'))",
        "finished_at: $($finished.ToUniversalTime().ToString('o'))",
        "exit_code: $exitCode",
        "timed_out: $timedOut",
        '',
        '--- stdout ---',
        $stdout,
        '--- stderr ---',
        $stderr
    ) -join [Environment]::NewLine
    [System.IO.File]::WriteAllText($LogPath, $log, [System.Text.UTF8Encoding]::new($false))

    $combined = (($stdout, $stderr) -join [Environment]::NewLine).Trim()
    $lines = @($combined -split '\r?\n')
    $limit = if ($VerboseOutput -or $exitCode -ne 0) { 100 } else { 12 }
    if ($lines.Count -gt 0 -and -not [string]::IsNullOrWhiteSpace($combined)) {
        $lines | Select-Object -Last $limit | ForEach-Object { Write-Host $_ }
    }
    if ($lines.Count -gt $limit) {
        Write-Host "[output clamped] Full output: $LogPath" -ForegroundColor DarkGray
    }
    if ($timedOut) {
        Write-Host "[timeout] $Label exceeded ${TimeoutSec}s" -ForegroundColor Red
    }

    [pscustomobject]@{
        Label = $Label
        ExitCode = $exitCode
        TimedOut = $timedOut
        DurationMs = [int]($finished - $start).TotalMilliseconds
        LogPath = $LogPath
        Output = $combined
    }
}

function Get-EpidemicProfile {
    param([Parameter(Mandatory)] [string]$Name)
    $parts = $Name.Split('-')
    if ($parts.Count -ne 2) { throw "Invalid profile: $Name" }
    $layer = $parts[0]
    $configuration = if ($parts[1] -eq 'debug') { 'Debug' } else { 'Release' }
    [pscustomobject]@{
        Name = $Name
        Configuration = $configuration
        Runtime = if ($layer -eq 'base') { 'OFF' } else { 'ON' }
        Framework = if ($layer -eq 'full') { 'ON' } else { 'OFF' }
    }
}

function Get-EpidemicProfiles {
    param([string]$Mode, [string]$Configuration)
    $names = switch ($Mode) {
        'Matrix' { @('base-debug','base-release','runtime-debug','runtime-release','full-debug','full-release') }
        default {
            switch ($Configuration) {
                'Debug' { @('full-debug') }
                'Release' { @('full-release') }
                default { @('full-debug','full-release') }
            }
        }
    }
    @($names | ForEach-Object { Get-EpidemicProfile -Name $_ })
}

function Invoke-EpidemicConfigure {
    param($Profile, [string]$Root, [string]$BuildDir, [string]$LogDir,
          [int]$HeartbeatSec, [int]$TimeoutSec)
    Invoke-Ae2Process -FilePath 'cmake' -Arguments @(
        '-S', $Root, '-B', $BuildDir,
        '-G', 'Visual Studio 18 2026', '-A', 'x64',
        '-DBUILD_TESTING=ON',
        "-DEPIDEMIC_BUILD_RUNTIME=$($Profile.Runtime)",
        "-DEPIDEMIC_BUILD_FRAMEWORK=$($Profile.Framework)",
        '-DEPIDEMIC_WARNINGS_AS_ERRORS=ON',
        '-DEPIDEMIC_ARCHITECTURE_FREEZE_CHECKS=ON'
    ) -Label "configure:$($Profile.Name)" -LogPath (Join-Path $LogDir "configure-$($Profile.Name).log") `
      -WorkingDirectory $Root -HeartbeatSec $HeartbeatSec -TimeoutSec $TimeoutSec
}

function Invoke-EpidemicBuild {
    param($Profile, [string]$Root, [string]$BuildDir, [string]$LogDir,
          [int]$HeartbeatSec, [int]$TimeoutSec, [string]$Target)
    $arguments = @('--build', $BuildDir, '--config', $Profile.Configuration, '--parallel')
    if (-not [string]::IsNullOrWhiteSpace($Target)) { $arguments += @('--target', $Target) }
    $suffix = if ($Target) { "-$Target" } else { '' }
    Invoke-Ae2Process -FilePath 'cmake' -Arguments $arguments -Label "build:$($Profile.Name)$suffix" `
      -LogPath (Join-Path $LogDir "build-$($Profile.Name)$suffix.log") -WorkingDirectory $Root `
      -HeartbeatSec $HeartbeatSec -TimeoutSec $TimeoutSec
}

function Get-EpidemicRegisteredTests {
    param($Profile, [string]$Root, [string]$BuildDir, [string]$LogDir,
          [int]$HeartbeatSec, [int]$TimeoutSec)
    $result = Invoke-Ae2Process -FilePath 'ctest' -Arguments @(
        '--test-dir', $BuildDir, '-C', $Profile.Configuration, '-N'
    ) -Label "list-tests:$($Profile.Name)" -LogPath (Join-Path $LogDir "list-tests-$($Profile.Name).log") `
      -WorkingDirectory $Root -HeartbeatSec $HeartbeatSec -TimeoutSec $TimeoutSec
    if ($result.ExitCode -ne 0) { throw "CTest discovery failed. Log: $($result.LogPath)" }
    @($result.Output -split '\r?\n' | ForEach-Object {
        if ($_ -match '^\s*Test\s+#\d+:\s+(.+?)\s*$') { $Matches[1] }
    } | Where-Object { $_ })
}

function Resolve-EpidemicTestRegex {
    param([string[]]$TestNames, [string]$Module, [string]$TestRegex)
    if (-not [string]::IsNullOrWhiteSpace($TestRegex)) { return $TestRegex }
    if ([string]::IsNullOrWhiteSpace($Module)) { return $null }
    $matches = @($TestNames | Where-Object { $_ -match [regex]::Escape($Module) })
    if ($matches.Count -eq 0) {
        throw "No CTest test name contains '$Module'. Use -Mode ListTests to inspect names."
    }
    $escaped = $matches | ForEach-Object { [regex]::Escape($_) }
    '^(' + ($escaped -join '|') + ')$'
}

function Invoke-EpidemicManifestCheck {
    param($Profile, [string]$Root, [string]$BuildDir, [string]$LogDir,
          [int]$HeartbeatSec, [int]$TimeoutSec)
    Invoke-Ae2Process -FilePath 'python' -Arguments @(
        'docs/freeze/ctest_manifest.py', '--check-profile', $Profile.Name,
        '--build-dir', $BuildDir, '--configuration', $Profile.Configuration
    ) -Label "manifest:$($Profile.Name)" -LogPath (Join-Path $LogDir "manifest-$($Profile.Name).log") `
      -WorkingDirectory $Root -HeartbeatSec $HeartbeatSec -TimeoutSec $TimeoutSec
}

function Invoke-EpidemicTests {
    param($Profile, [string]$Root, [string]$BuildDir, [string]$LogDir,
          [int]$HeartbeatSec, [int]$TimeoutSec, [string]$Regex,
          [int]$RepeatUntilFail = 1, [switch]$VerboseOutput)
    $arguments = @('--test-dir', $BuildDir, '-C', $Profile.Configuration,
                   '--output-on-failure', '--timeout', '300')
    if (-not [string]::IsNullOrWhiteSpace($Regex)) { $arguments += @('-R', $Regex) }
    if ($RepeatUntilFail -gt 1) { $arguments += @('--repeat', "until-fail:$RepeatUntilFail") }
    if ($VerboseOutput) { $arguments += '--verbose' }
    $result = Invoke-Ae2Process -FilePath 'ctest' -Arguments $arguments -Label "test:$($Profile.Name)" `
      -LogPath (Join-Path $LogDir "test-$($Profile.Name).log") -WorkingDirectory $Root `
      -HeartbeatSec $HeartbeatSec -TimeoutSec $TimeoutSec -VerboseOutput:$VerboseOutput
    $total = 0; $failed = 0; $passed = 0
    if ($result.Output -match '(?m)(\d+)% tests passed, (\d+) tests failed out of (\d+)') {
        $failed = [int]$Matches[2]; $total = [int]$Matches[3]; $passed = $total - $failed
    }
    $result | Add-Member -NotePropertyName TestTotal -NotePropertyValue $total
    $result | Add-Member -NotePropertyName TestPassed -NotePropertyValue $passed
    $result | Add-Member -NotePropertyName TestFailed -NotePropertyValue $failed
    $result
}
