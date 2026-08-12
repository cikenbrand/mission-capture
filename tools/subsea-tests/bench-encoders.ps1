# Mission Capture -- concurrent encoder benchmark (Phase 0 task 0.7).
#
# Answers the question Phase 6's resource guard is built around: how many
# simultaneous 1080p encodes will this machine sustain, per encoder family,
# before session creation fails or throughput falls below realtime?
#
#   .\bench-encoders.ps1
#   .\bench-encoders.ps1 -Encoders h264_nvenc -Sessions 1,2,4 -Seconds 5
#
# CAVEAT, and it is a real one: this drives ffmpeg, not OBS. Driver-level
# session limits are the same, so the "how many can be created" number
# transfers. Throughput does not transfer exactly -- OBS's NVENC and AMF paths
# encode from GPU textures and avoid the CPU round-trip ffmpeg does here, so
# real throughput should be no worse than this and is usually better. Treat
# these as a conservative floor.

[CmdletBinding()]
param(
    # Passed as comma-separated strings rather than native arrays: invoking this
    # via `powershell -File` turns every argument into a string, and "1,2" cast
    # to int[] silently becomes the single value 12.
    [string]$Encoders   = 'h264_nvenc,h264_amf,libx264',
    [string]$Sessions   = '1,2,3,4,6,8',
    [string]$Modes      = '1080p30,1080p60',
    [int]$Seconds       = 10,
    [string]$RecordDrive = 'D:',
    [string]$OutFile    = ''
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib\common.ps1')

$EncoderList = @($Encoders -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
$SessionList = @($Sessions -split ',' | ForEach-Object { [int]$_.Trim() })
$ModeList    = @($Modes    -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })

$scratch = Join-Path $env:TEMP ('mc-bench-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Force -Path $scratch | Out-Null

function Get-EncoderArgs {
    param([string]$Encoder)
    # Settings chosen to resemble what Mission Capture will actually use:
    # quality-targeted rather than bitrate-targeted, realtime-oriented preset.
    switch ($Encoder) {
        'h264_nvenc' { return @('-c:v','h264_nvenc','-preset','p5','-tune','ll','-rc','constqp','-qp','23') }
        'hevc_nvenc' { return @('-c:v','hevc_nvenc','-preset','p5','-tune','ll','-rc','constqp','-qp','25') }
        'h264_amf'   { return @('-c:v','h264_amf','-quality','speed','-rc','cqp','-qp_i','23','-qp_p','23') }
        'hevc_amf'   { return @('-c:v','hevc_amf','-quality','speed','-rc','cqp') }
        'libx264'    { return @('-c:v','libx264','-preset','veryfast','-crf','23') }
        default      { throw "No argument profile for encoder '$Encoder'" }
    }
}

function Test-EncoderAvailable {
    param([string]$Encoder)
    $probe = Join-Path $scratch "probe_$Encoder.mkv"
    $args = @('-hide_banner','-loglevel','error','-f','lavfi','-i','testsrc=size=640x360:rate=30','-frames:v','5') +
            (Get-EncoderArgs $Encoder) + @('-y', $probe)
    & ffmpeg @args 2>&1 | Out-Null
    $ok = (Test-Path $probe)
    if ($ok) { Remove-Item $probe -Force -ErrorAction SilentlyContinue }
    return $ok
}

function Invoke-Batch {
    param([string]$Encoder, [int]$Count, [int]$Width, [int]$Height, [int]$Fps, [int]$DurationSec)

    $frames = $Fps * $DurationSec
    $procs = @()
    $logs  = @()

    $t0 = Get-Date
    for ($i = 0; $i -lt $Count; $i++) {
        $out = Join-Path $scratch ("s{0}.mkv" -f $i)
        $log = Join-Path $scratch ("s{0}.log" -f $i)
        $logs += $log

        $args = @('-hide_banner','-nostdin','-loglevel','info',
                  '-f','lavfi','-i',"testsrc=size=${Width}x${Height}:rate=$Fps",
                  '-frames:v',"$frames") + (Get-EncoderArgs $Encoder) + @('-an','-y',$out)

        $p = Start-Process -FilePath 'ffmpeg' -ArgumentList $args -PassThru -NoNewWindow `
                           -RedirectStandardError $log
        # Touching .Handle caches the process handle. Without it .ExitCode reads
        # as $null after exit and every session looks like a failure.
        $null = $p.Handle
        $procs += $p
    }

    # Sample the Windows video-encode engine counter while they run. Covers both
    # vendors, unlike nvidia-smi.
    $gpuSamples = @()
    while (@($procs | Where-Object { -not $_.HasExited }).Count -gt 0) {
        try {
            $c = Get-Counter '\GPU Engine(*engtype_VideoEncode)\Utilization Percentage' -ErrorAction Stop
            $gpuSamples += (($c.CounterSamples | Measure-Object -Property CookedValue -Sum).Sum)
        } catch { }
        Start-Sleep -Milliseconds 500
    }
    foreach ($p in $procs) { $p.WaitForExit() }
    $wall = ((Get-Date) - $t0).TotalSeconds

    $failed = @($procs | Where-Object { $_.ExitCode -ne 0 }).Count

    # ffmpeg reports "speed=N.NNx" on its final progress line; 1.0x is exactly
    # realtime, which is the threshold that matters for recording.
    $speeds = @()
    foreach ($log in $logs) {
        if (-not (Test-Path $log)) { continue }
        $text = Get-Content $log -Raw
        $m = [regex]::Matches($text, 'speed=\s*([0-9.]+)x')
        if ($m.Count -gt 0) { $speeds += [double]$m[$m.Count - 1].Groups[1].Value }
    }

    $gpuAvg = 0
    if ($gpuSamples.Count -gt 0) { $gpuAvg = [math]::Round((($gpuSamples | Measure-Object -Average).Average), 1) }

    $minSpeed = 0; $avgSpeed = 0
    if ($speeds.Count -gt 0) {
        $minSpeed = [math]::Round((($speeds | Measure-Object -Minimum).Minimum), 2)
        $avgSpeed = [math]::Round((($speeds | Measure-Object -Average).Average), 2)
    }

    Get-ChildItem $scratch -Filter 's*.mkv' | Remove-Item -Force -ErrorAction SilentlyContinue

    return [pscustomobject]@{
        encoder = $Encoder; sessions = $Count; width = $Width; height = $Height; fps = $Fps
        started = $Count - $failed; failed = $failed
        wall_s = [math]::Round($wall, 1)
        min_speed_x = $minSpeed; avg_speed_x = $avgSpeed
        realtime_ok = ($failed -eq 0 -and $minSpeed -ge 1.0)
        gpu_encode_pct = $gpuAvg
    }
}

# --- disk ---------------------------------------------------------------------
function Measure-DiskWrite {
    param([string]$Drive, [int]$SizeMB = 2048)
    $path = Join-Path ($Drive + '\') ('mc-bench-' + [guid]::NewGuid().ToString('N').Substring(0,8) + '.tmp')
    $buf  = New-Object byte[] (8MB)
    (New-Object Random).NextBytes($buf)
    $t0 = Get-Date
    $fs = [System.IO.File]::Create($path)
    try {
        for ($w = 0; $w -lt ($SizeMB / 8); $w++) { $fs.Write($buf, 0, $buf.Length) }
        $fs.Flush($true)
    } finally { $fs.Close(); Remove-Item $path -Force -ErrorAction SilentlyContinue }
    $sec = ((Get-Date) - $t0).TotalSeconds
    return [math]::Round($SizeMB / $sec, 0)
}

# --- run ----------------------------------------------------------------------
Write-Host '=== Encoder availability ===' -ForegroundColor Cyan
$available = @()
foreach ($e in $EncoderList) {
    $ok = Test-EncoderAvailable $e
    Write-Host ("  {0,-12} {1}" -f $e, $(if ($ok) { 'available' } else { 'NOT AVAILABLE - skipped' }))
    if ($ok) { $available += $e }
}

Write-Host "`n=== Disk write throughput ($RecordDrive) ===" -ForegroundColor Cyan
$diskMBs = Measure-DiskWrite -Drive $RecordDrive
Write-Host ("  {0} MB/s sustained (2 GB, flushed)" -f $diskMBs)

$results = @()
foreach ($mode in $ModeList) {
    $w = 1920; $h = 1080; $fps = 30
    if ($mode -eq '1080p60') { $fps = 60 }

    foreach ($e in $available) {
        Write-Host "`n=== $e @ $mode ===" -ForegroundColor Cyan
        foreach ($n in $SessionList) {
            $r = Invoke-Batch -Encoder $e -Count $n -Width $w -Height $h -Fps $fps -DurationSec $Seconds
            $results += $r
            $verdict = 'ok'
            if ($r.failed -gt 0)        { $verdict = "$($r.failed) FAILED TO START" }
            elseif (-not $r.realtime_ok) { $verdict = 'below realtime' }
            Write-Host ("  n={0,-2} started={1,-2} min={2,5}x avg={3,5}x gpu={4,5}%  {5}" -f `
                        $n, $r.started, $r.min_speed_x, $r.avg_speed_x, $r.gpu_encode_pct, $verdict)

            # Once sessions fail outright, higher counts add nothing.
            if ($r.failed -gt 0) { break }
        }
    }
}

Remove-Item $scratch -Recurse -Force -ErrorAction SilentlyContinue

$payload = [pscustomobject]@{
    generated_utc = (Get-Date).ToUniversalTime().ToString('o')
    environment   = Get-ToolVersions
    record_drive  = $RecordDrive
    disk_write_mb_s = $diskMBs
    results = $results
}

if (-not $OutFile) { $OutFile = Join-Path $script:OutRoot ('bench-encoders-' + (Get-Date -Format 'yyyyMMdd_HHmmss') + '.json') }
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutFile) | Out-Null
$payload | ConvertTo-Json -Depth 6 | Out-File -Encoding utf8 $OutFile
Write-Host "`nresults: $OutFile" -ForegroundColor Cyan
