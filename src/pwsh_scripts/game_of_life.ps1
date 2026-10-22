# Conway's Game of Life.
# Usage: game_of_life.ps1 [width] [height] [steps] [-Sound]
# Anything not given (or not valid) on the command line is prompted for.
# -Sound plays each generation's births as notes (needs aplay, paplay or
# play on Linux/macOS; Windows gets a single console beep per generation).
# Settings come from $args rather than a param() block so the script still
# works when it is not the first statement in the file (jpws polyglot images).

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

function Read-BoardValue {
	# $Given is an optional command-line value; fall back to prompting.
	$Prompt, $Default, $Min, $Max, $Given = $args
	$Default = [Math]::Max($Min, [Math]::Min($Default, $Max))
	$value = 0
	if ($null -ne $Given) {
		if ([int]::TryParse([string]$Given, [ref]$value) -and $value -ge $Min -and $value -le $Max) {
			return $value
		}
		Write-Host "Ignoring '$Given': $Prompt must be a whole number from $Min to $Max."
	}
	if ([Console]::IsInputRedirected) { throw "Supply a valid $Prompt when input is redirected." }
	while ($true) {
		$text = Read-Host "Enter the $Prompt ($Min-$Max, default $Default)"
		if ([string]::IsNullOrWhiteSpace($text)) {
			return $Default
		}
		if ([int]::TryParse($text, [ref]$value) -and $value -ge $Min -and $value -le $Max) {
			return $value
		}
		Write-Host "Please enter a whole number from $Min to $Max."
	}
}

function Step-Board {
	# Computes the next generation of $Cur into $Next. Both are flat int arrays
	# with a one-cell dead border, so no bounds checks are needed.
	# $Age counts how many generations each live cell has survived.
	$Cur, $Next, $Age, $Width, $Height = $args
	$stride = $Width + 2
	for ($r = 1; $r -le $Height; $r++) {
		$i = $r * $stride + 1
		$up = $i - $stride
		$down = $i + $stride
		for ($c = 0; $c -lt $Width; $c++) {
			$n = $Cur[$up - 1] + $Cur[$up] + $Cur[$up + 1] +
				 $Cur[$i - 1] + $Cur[$i + 1] +
				 $Cur[$down - 1] + $Cur[$down] + $Cur[$down + 1]
			$cell = [int]($n -eq 3 -or ($n -eq 2 -and $Cur[$i] -eq 1))
			$Next[$i] = $cell
			if ($cell -eq 1) {
				$Age[$i]++
			}
			else {
				$Age[$i] = 0
			}
			$i++
			$up++
			$down++
		}
	}
}

function Get-BoardKey {
	# A short fingerprint of the board, used to spot repeating patterns.
	[Buffer]::BlockCopy($args[0], 0, $hashBytes, 0, $hashBytes.Length)
	[Convert]::ToBase64String($sha.ComputeHash($hashBytes))
}

function Show-Board {
	# Builds the whole frame as one string and writes it in a single call.
	# With $Animate set, cells are coloured by age: newborn bright green,
	# then green, cyan and bright red as they get older; dead cells are light grey.
	$Board, $Age, $Width, $Height, $Generation, $Steps, $Animate = $args
	$stride = $Width + 2
	$alive = 0
	$esc = [char]27
	$current = 0
	[void]$frame.Clear()
	$viewWidth, $viewHeight = $Width, $Height
	$statusWidth = [Math]::Max($Width, 40)
	if ($Animate) {
		$statusWidth = [Math]::Max(0, [Console]::WindowWidth - 1)
		$viewWidth = [Math]::Min($Width, $statusWidth)
		$viewHeight = [Math]::Min($Height, [Math]::Max(0, [Console]::WindowHeight - 2))
		if ($statusWidth -eq 0 -or [Console]::WindowHeight -lt 2) { return }
		[void]$frame.Append("$esc[H")
	}
	for ($r = 1; $r -le $Height; $r++) {
		$i = $r * $stride + 1
		for ($c = 0; $c -lt $Width; $c++) {
			if ($r -gt $viewHeight -or $c -ge $viewWidth) { $alive += $Board[$i]; $i++; continue }
			$cellAge = $Age[$i]
			if ($Board[$i] -eq 1) {
				$glyph = '#'
				$alive++
				if ($cellAge -le 1) { $colour = 92 }
				elseif ($cellAge -le 4) { $colour = 32 }
				elseif ($cellAge -le 9) { $colour = 36 }
				else { $colour = 91 }
			}
			else {
				$glyph = '.'
				$colour = 37
			}
			# Only emit an escape sequence when the colour actually changes.
			if ($Animate -and $colour -ne $current) {
				[void]$frame.Append("$esc[${colour}m")
				$current = $colour
			}
			[void]$frame.Append($glyph)
			$i++
		}
		if ($r -le $viewHeight) { [void]$frame.AppendLine() }
	}
	if ($Animate) {
		[void]$frame.Append("$esc[0m")
	}
	$status = "Generation $Generation/$Steps  Alive: $alive"
	if ($viewWidth -lt $Width -or $viewHeight -lt $Height) { $status += ' (clipped)' }
	[void]$frame.AppendLine($status.Substring(0, [Math]::Min($status.Length, $statusWidth)).PadRight($statusWidth))
	if ($Animate) { [void]$frame.Append("$esc[J") }
	[Console]::Out.Write($frame.ToString())
}

function Start-Player {
	# Starts a player that reads raw 16-bit mono PCM from stdin.
	# Returns nothing if no supported player is installed.
	$rate = $args[0]
	$players = [ordered]@{
		aplay  = "-q -t raw -f S16_LE -c 1 -r $rate -B 100000 -"
		paplay = "--raw --format=s16le --channels=1 --rate=$rate --latency-msec=100"
		play   = "-q -t raw -e signed -L -b 16 -c 1 -r $rate -"
	}
	foreach ($name in $players.Keys) {
		$command = Get-Command $name -CommandType Application -ErrorAction Ignore | Select-Object -First 1
		if ($command) { return Start-AudioProcess $command.Source $players[$name] }
	}
}

function Write-Notes {
	# Plays this generation's births. Each row maps to a note (top row is the
	# highest) and the four notes with the most births sound together.
	# Windows only beeps the strongest one.
	$Age, $Width, $Height = $args
	$count = $freqs.Length
	[Array]::Clear($weights, 0, $weights.Length)
	for ($r = 1; $r -le $Height; $r++) {
		$i = $r * ($Width + 2) + 1
		$note = [int][Math]::Floor(($Height - $r) * $count / $Height)
		for ($c = 0; $c -lt $Width; $c++) {
			if ($Age[$i] -eq 1) {
				$weights[$note]++
			}
			$i++
		}
	}
	if (-not $onWindows) { [Array]::Clear($mix, 0, $mix.Length) }
	for ($voice = 0; $voice -lt 4; $voice++) {
		$best = 0
		for ($n = 1; $n -lt $count; $n++) {
			if ($weights[$n] -gt $weights[$best]) {
				$best = $n
			}
		}
		if ($weights[$best] -eq 0) {
			break
		}
		$weights[$best] = 0
		if ($onWindows) {
			[Console]::Beep([int]$freqs[$best], 120)
			return
		}
		$table = $tables[$best]
		for ($j = 0; $j -lt $frameSamples; $j++) {
			$mix[$j] += $table[$j]
		}
	}
	if ($onWindows) {
		return
	}
	[Buffer]::BlockCopy($mix, 0, $audioBytes, 0, $audioBytes.Length)
	try {
		$stream = $player.StandardInput.BaseStream
		$write = $stream.WriteAsync($audioBytes, 0, $audioBytes.Length)
		if (-not $write.Wait($frameMs)) { throw 'Audio player stopped consuming PCM.' }
	}
	catch {
		# Never queue more audio or reuse the buffer while a write is pending.
		$script:Sound = $false
		$script:notice = 'Audio stopped because the player failed or stalled.'
		try { Stop-AudioProcess $player } finally { $script:player = $null }
	}
}

$Sound = $args -contains '-Sound'
$values = @($args | Where-Object { $_ -ne '-Sound' })

# Keep simulation dimensions stable; Show-Board clips to the current viewport.
$maxWidth = 200
$maxHeight = 60
$animate = -not [Console]::IsOutputRedirected -and $Host.UI.SupportsVirtualTerminal
$Width = Read-BoardValue 'board width' 20 1 $maxWidth $values[0]
$Height = Read-BoardValue 'board height' 10 1 $maxHeight $values[1]
$Steps = Read-BoardValue 'number of steps' 50 1 100000 $values[2]
# Only ask about sound when the user is already being prompted.
if (-not $Sound -and $values.Count -lt 3 -and -not [Console]::IsInputRedirected) {
	$Sound = (Read-Host 'Enable sound? (Y/N, default N)') -match '^\s*y'
}

$stride = $Width + 2
$board = [int[]]::new($stride * ($Height + 2))
$scratch = [int[]]::new($board.Length)
$age = [int[]]::new($board.Length)
$rng = [System.Random]::new()
for ($r = 1; $r -le $Height; $r++) {
	$i = $r * $stride + 1
	for ($c = 0; $c -lt $Width; $c++) {
		if ($rng.NextDouble() -lt 0.2) {
			$board[$i] = 1
			$age[$i] = 1
		}
		$i++
	}
}

$frameMs = 200
$notice = ''
$player = $null
$guard = $null
$sha = $null
$cursorWasVisible = $null
$frame = [Text.StringBuilder]::new(($Width + 2) * ($Height + 1) * 8 + 80)
$hashBytes = [byte[]]::new($board.Length * 4)
try {
	$guard = New-TerminationGuard
	if ($Sound) {
		$onWindows = $env:OS -eq 'Windows_NT'
		# A minor pentatonic over two octaves, low to high.
		$freqs = foreach ($semitone in 0, 3, 5, 7, 10, 12, 15, 17, 19, 22, 24) {
			220 * [Math]::Pow(2, $semitone / 12)
		}
		$weights = [int[]]::new($freqs.Length)
		if (-not $onWindows) {
			$rate = 11025
			$frameSamples = [int]($rate * $frameMs / 1000)
			$mix = [int16[]]::new($frameSamples)
			$audioBytes = [byte[]]::new($frameSamples * 2)
			$player = Start-Player $rate
			if ($null -eq $player) {
				$Sound = $false
				$notice = 'No audio player found (aplay, paplay or play), so there was no sound.'
			}
			else {
				# One frame-long, bell-like tone per note, fading to silence
				# so consecutive frames join without clicks.
				$tables = foreach ($freq in $freqs) {
					$table = [int16[]]::new($frameSamples)
					for ($j = 0; $j -lt $frameSamples; $j++) {
						$time = $j / $rate
						$envelope = [Math]::Min(1, $time / 0.01) * [Math]::Exp(-12 * $time) *
									[Math]::Min(1, ($frameSamples - $j) / ($rate * 0.02))
						$angle = 2 * [Math]::PI * $freq * $time
						$table[$j] = 5000 * $envelope * ([Math]::Sin($angle) + 0.3 * [Math]::Sin(2 * $angle))
					}
					, $table
				}
			}
		}
	}

	if ($animate) {
		# Some platforms cannot report cursor visibility; leave it untouched there.
		try { $cursorWasVisible = [Console]::CursorVisible } catch [PlatformNotSupportedException] { }
		if ($null -ne $cursorWasVisible) { [Console]::CursorVisible = $false }
		[Console]::Clear()
	}
	Show-Board $board $age $Width $Height 0 $Steps $animate
	$clock = [System.Diagnostics.Stopwatch]::StartNew()
	$sha = [System.Security.Cryptography.SHA256]::Create()
	$seen = @{ (Get-BoardKey $board) = 0 }
	for ($generation = 1; $generation -le $Steps; $generation++) {
		# Pace against a clock so frames (and the audio) do not drift.
		$wait = $generation * $frameMs - $clock.ElapsedMilliseconds
		if ($wait -gt 0) {
			Start-Sleep -Milliseconds $wait
		}
		if ($null -ne $guard -and $guard.Requested) { break }
		Step-Board $board $scratch $age $Width $Height
		$board, $scratch = $scratch, $board
		# Stop once the board returns to a state it has been in before.
		$key = Get-BoardKey $board
		if ($seen.ContainsKey($key)) {
			$period = $generation - $seen[$key]
			if ($period -eq 1) {
				$notice = "Board stopped changing after generation $($generation - 1)."
			}
			else {
				$notice = "Board is repeating every $period generations (since generation $($seen[$key])). Stopping."
			}
			break
		}
		$seen[$key] = $generation
		Show-Board $board $age $Width $Height $generation $Steps $animate
		if ($Sound) {
			Write-Notes $age $Width $Height
		}
	}
}
finally {
    try { Stop-AudioProcess $player -Drain } finally {
        try { if ($null -ne $cursorWasVisible) { [Console]::CursorVisible = $cursorWasVisible } } finally {
            if ($null -ne $sha) { $sha.Dispose() }
            if ($null -ne $guard) { $guard.Dispose() }
        }
    }
}
if ($null -ne $guard -and $guard.Requested) { exit 143 }
if ($notice) {
    if ($animate) {
        $limit = [Math]::Max(0, [Console]::WindowWidth - 1)
        [Console]::WriteLine($notice.Substring(0, [Math]::Min($notice.Length, $limit)))
    } else { Write-Host $notice }
}
