# Conway's Game of Life.
# Usage: game_of_life.ps1 [width] [height] [steps] [-Sound]
# Anything not given (or not valid) on the command line is prompted for.
# -Sound plays each generation's births as notes (needs aplay, paplay or
# play on Linux/macOS; Windows gets a single console beep per generation).
# Settings come from $args rather than a param() block so the script still
# works when it is not the first statement in the file (jpws polyglot images).

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
	$bytes = [byte[]]::new($args[0].Length * 4)
	[Buffer]::BlockCopy($args[0], 0, $bytes, 0, $bytes.Length)
	[Convert]::ToBase64String($sha.ComputeHash($bytes))
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
	$frame = [System.Text.StringBuilder]::new(($Width + 2) * ($Height + 1) * 3 + 80)
	for ($r = 1; $r -le $Height; $r++) {
		$i = $r * $stride + 1
		for ($c = 0; $c -lt $Width; $c++) {
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
		[void]$frame.AppendLine()
	}
	if ($Animate) {
		[void]$frame.Append("$esc[0m")
	}
	$status = "Generation $Generation/$Steps  Alive: $alive"
	[void]$frame.AppendLine($status.PadRight([Math]::Max($Width, 40)))
	if ($Animate) {
		[Console]::SetCursorPosition(0, 0)
	}
	[Console]::Out.Write($frame.ToString())
}

function Start-Player {
	# Starts a player that reads raw 16-bit mono PCM from stdin.
	# Returns nothing if no supported player is installed.
	$rate = $args[0]
	$players = [ordered]@{
		aplay  = "-q -t raw -f S16_LE -c 1 -r $rate -B 100000 -"
		paplay = "--raw --format=s16le --channels=1 --rate=$rate --latency-msec=100"
		play   = "-q -t raw -e signed -b 16 -c 1 -r $rate -"
	}
	foreach ($name in $players.Keys) {
		if (Get-Command $name -CommandType Application -ErrorAction SilentlyContinue) {
			$info = [System.Diagnostics.ProcessStartInfo]::new('sh', "-c `"exec $name $($players[$name]) >/dev/null 2>&1`"")
			$info.RedirectStandardInput = $true
			$info.UseShellExecute = $false
			return [System.Diagnostics.Process]::Start($info)
		}
	}
}

function Write-Notes {
	# Plays this generation's births. Each row maps to a note (top row is the
	# highest) and the four notes with the most births sound together.
	# Windows only beeps the strongest one.
	$Age, $Width, $Height = $args
	$count = $freqs.Length
	$weights = [int[]]::new($count)
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
	$mix = [int16[]]::new($frameSamples)
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
	$bytes = [byte[]]::new($frameSamples * 2)
	[Buffer]::BlockCopy($mix, 0, $bytes, 0, $bytes.Length)
	try {
		$stream = $player.StandardInput.BaseStream
		$stream.Write($bytes, 0, $bytes.Length)
		$stream.Flush()
	}
	catch {
		# The player has gone away (no audio device, for example).
		$script:Sound = $false
	}
}

$Sound = $args -contains '-Sound'
$values = @($args | Where-Object { $_ -ne '-Sound' })

# Keep the board inside the console window so rows never wrap or scroll.
$maxWidth = 200
$maxHeight = 60
$animate = -not [Console]::IsOutputRedirected
if ($animate) {
	try {
		$window = $Host.UI.RawUI.WindowSize
		if ($window.Width -gt 41 -and $window.Height -gt 3) {
			$maxWidth = [Math]::Min($maxWidth, $window.Width - 1)
			$maxHeight = [Math]::Min($maxHeight, $window.Height - 3)
		}
	}
	catch {
		$animate = $false
	}
}

$Width = Read-BoardValue 'board width' 20 1 $maxWidth $values[0]
$Height = Read-BoardValue 'board height' 10 1 $maxHeight $values[1]
$Steps = Read-BoardValue 'number of steps' 50 1 100000 $values[2]
# Only ask about sound when the user is already being prompted.
if (-not $Sound -and $values.Count -lt 3) {
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
if ($Sound) {
	$onWindows = $env:OS -eq 'Windows_NT'
	# A minor pentatonic over two octaves, low to high.
	$freqs = foreach ($semitone in 0, 3, 5, 7, 10, 12, 15, 17, 19, 22, 24) {
		220 * [Math]::Pow(2, $semitone / 12)
	}
	if (-not $onWindows) {
		$rate = 11025
		$frameSamples = [int]($rate * $frameMs / 1000)
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

$cursorWasVisible = $true
if ($animate) {
	try { $cursorWasVisible = [Console]::CursorVisible } catch { }
	Clear-Host
	[Console]::CursorVisible = $false
}
try {
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
		Step-Board $board $scratch $age $Width $Height
		$board, $scratch = $scratch, $board
		# Stop once the board returns to a state it has been in before.
		$key = Get-BoardKey $board
		if ($seen.ContainsKey($key)) {
			$period = $generation - $seen[$key]
			if ($period -eq 1) {
				Write-Host "Board stopped changing after generation $($generation - 1)."
			}
			else {
				Write-Host "Board is repeating every $period generations (since generation $($seen[$key])). Stopping."
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
	if ($player) {
		# Let the last notes finish, then make sure the player is gone.
		try {
			$player.StandardInput.Close()
			if (-not $player.WaitForExit(1500)) {
				$player.Kill()
			}
		}
		catch { }
	}
	if ($animate) {
		[Console]::CursorVisible = $cursorWasVisible
	}
	if ($notice) {
		Write-Host $notice
	}
}
