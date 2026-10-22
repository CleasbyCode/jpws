function New-TerminationGuard {
    # PowerShell does not unwind finally on SIGTERM. The callback only sets a flag.
    # Keep this helper embedded: generated jpws images have no companion files.
    if ($PSVersionTable.PSEdition -eq 'Desktop' -or $IsWindows) { return }
    if ($PSVersionTable.PSVersion -lt [version]'7.2') { throw 'Linux/macOS requires PowerShell 7.2 or newer.' }
    if (-not ('Jpws.Termination' -as [type])) {
        Add-Type -ErrorAction Stop -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace Jpws {
    public sealed class Termination : IDisposable {
        public volatile bool Requested;
        private readonly PosixSignalRegistration registration;
        public Termination() {
            registration = PosixSignalRegistration.Create(PosixSignal.SIGTERM,
                context => { context.Cancel = true; Requested = true; });
        }
        public void Dispose() { registration.Dispose(); }
    }
}
'@
    }
    return [Jpws.Termination]::new()
}

function Start-AudioProcess {
    param([string]$Path, [string]$Options)
    $info = [Diagnostics.ProcessStartInfo]::new($Path, $Options)
    $info.UseShellExecute = $false
    $info.RedirectStandardInput = $info.RedirectStandardOutput = $info.RedirectStandardError = $true
    $process = [Diagnostics.Process]::Start($info)
    try {
        # Drain bytes rather than lines: even newline-free output uses bounded memory.
        $null = $process.StandardOutput.BaseStream.CopyToAsync([IO.Stream]::Null)
        $null = $process.StandardError.BaseStream.CopyToAsync([IO.Stream]::Null)
        return $process
    } catch {
        try { if (-not $process.HasExited) { $process.Kill() } } finally { $process.Dispose() }
        throw
    }
}

function Stop-AudioProcess {
    param($Process, [switch]$Drain)
    if ($null -eq $Process) { return }
    try {
        if ($Drain -and -not $Process.HasExited) {
            try { $Process.StandardInput.Close(); $null = $Process.WaitForExit(1500) }
            catch [IO.IOException] { } # A player may close stdin before exiting.
        }
    } finally {
        try {
            if (-not $Process.HasExited) { $Process.Kill(); $null = $Process.WaitForExit(1000) }
        } finally { $Process.Dispose() }
    }
}

function Start-Music {
    param([string]$Url)
    $options = [ordered]@{
        mpv = '--no-terminal --no-config --load-scripts=no --ytdl=no --no-video'
        cvlc = '--ignore-config --no-lua --intf=dummy --extraintf= --no-one-instance --quiet --no-video --play-and-exit'
    }
    foreach ($name in $options.Keys) {
        $command = Get-Command $name -CommandType Application -ErrorAction Ignore | Select-Object -First 1
        if ($command) {
            $process = Start-AudioProcess $command.Source "$($options[$name]) $Url"
            try { $process.StandardInput.Close(); return $process }
            catch { Stop-AudioProcess $process; throw }
        }
    }
    Write-Warning 'Music needs mpv or cvlc on PATH.'
}

# Based on text-sine.ps1 by Darren Shaw / @gierrofo.
# Self-contained for jpws: $args works after its leading "cls;". -Music is opt-in.
if ([Console]::IsInputRedirected -or [Console]::IsOutputRedirected) {
    throw 'text-sine.ps1 requires interactive terminal input and output.'
}
$Music = $args -contains '-Music'
$isWindowsOS = $PSVersionTable.PSEdition -eq 'Desktop' -or $IsWindows
$motif = '.-=:' + [char]0xA7 + '[#]' + [char]0xA7 + ':=-.'
$colours = [ConsoleColor[]]@('DarkGreen', 'DarkCyan', 'DarkYellow', 'Gray', 'DarkGray', 'Green', 'Cyan', 'Red', 'Magenta', 'Yellow', 'White')
$rng = [Random]::new()
$speed = 2
$increment = 1
$runs = 0
$player = $null
$guard = $null
$cursorWasVisible = $null
$originalColour = [Console]::ForegroundColor
try {
    $guard = New-TerminationGuard
    if ($Music -and -not $isWindowsOS) { $player = Start-Music 'https://cleasbycode.co.uk/media/circles.mp3' }
    try { $cursorWasVisible = [Console]::CursorVisible } catch [PlatformNotSupportedException] { }
    if ($null -ne $cursorWasVisible) { [Console]::CursorVisible = $false }
    [Console]::Clear()
    :animation while ($true) {
        for ($i = $colours.Length - 1; $i -gt 0; $i--) {
            $j = $rng.Next($i + 1)
            $swap = $colours[$i]; $colours[$i] = $colours[$j]; $colours[$j] = $swap
        }
        for ($deg = -90; $deg -lt 270; $deg += $speed) {
            if ($null -ne $guard -and $guard.Requested) { break animation }
            if ([Console]::KeyAvailable) { $null = [Console]::ReadKey($true); break animation }
            # Recheck each frame. Shorten the motif and leave one column for wrapping.
            $available = [Math]::Max(0, [Console]::WindowWidth - 1)
            if ($available -gt 0) {
                $text = $motif.Substring(0, [Math]::Min($motif.Length, $available))
                $centre = ($available - $text.Length) / 2
                $length = [int][Math]::Round($centre * (1 + [Math]::Sin($deg * [Math]::PI / 180)) + $text.Length)
                [Console]::ForegroundColor = $colours[[int][Math]::Floor(($deg + 90) * $colours.Length / 360)]
                [Console]::WriteLine($text.PadLeft($length))
            }
            Start-Sleep -Milliseconds 15
        }
        if (++$runs -eq 44) { [Console]::Clear(); $runs = 0 }
        $speed += $increment
        if ($speed -eq 18 -or $speed -eq 2) { $increment = -$increment }
    }
} finally {
    try { Stop-AudioProcess $player } finally {
        try {
            [Console]::ForegroundColor = $originalColour
            if ($null -ne $cursorWasVisible) { [Console]::CursorVisible = $cursorWasVisible }
            [Console]::Clear()
        } finally { if ($null -ne $guard) { $guard.Dispose() } }
    }
}
if ($null -ne $guard -and $guard.Requested) { exit 143 }
