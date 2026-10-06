# matrix_rain1.ps1 - Matrix Rain
#
# Half-width Katakana, alphanumeric, and special characters.
# Colour gradient trail on a black background: bright white head, green body,
# dark green tail that smears into shade blocks before it is erased.
#   - Per-stream variable speed and random gaps between streams
#   - Buffered frame output (one write per frame)
#   - Follows terminal resizes
#   - Shimmer effect: trail characters randomly change each frame
#   - Blur effect: mid-trail characters occasionally smear into a shade block for a frame
#   - Bleed effect: a faint copy of the head character flashes in a neighbouring column
# Press any key to exit.
# Run with -Music to stream background audio (Linux/macOS only, needs mpv or cvlc).

# NOTE: no param() block. In the jpws JPG-PowerShell polyglot the embedded script
# runs after a leading "cls;", so a param() block would not be the first statement.
# Read $args directly instead; this works both standalone (pwsh matrix_rain1.ps1 -Music)
# and embedded (pwsh image.jpg -Music).
$Music = $args -contains '-Music'

# KeyAvailable throws when console input is redirected, so a real terminal is required
if ([Console]::IsInputRedirected -or [Console]::IsOutputRedirected) {
	Write-Error "matrix_rain1.ps1 must be run from an interactive terminal."
	exit 1
}

# Matrix character set. The Katakana are built from code points (U+FF66..U+FF9D)
# so this file stays plain ASCII whatever encoding it is read with.
$chars = @(0xFF66..0xFF9D | ForEach-Object { [char]$_ })
$chars += '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz'.ToCharArray()
$chars += '!@#$%^&*()+=<>?/{}[]|:;~'.ToCharArray()
$nchars = $chars.Count

# Trail colour gradient (head to tail) and the chance (1 in N) that each one
# is repainted on a given frame. The 1-in-2 and 1-in-3 entries give the shimmer.
$esc = [char]27
$colours = @(
	"$esc[1;97m"       # bold bright white  (leading character)
	"$esc[1;92m"       # bold bright green  (just behind head)
	"$esc[38;5;46m"    # bright green       (upper trail)
	"$esc[38;5;34m"    # medium green       (mid trail)
	"$esc[38;5;22m"    # dark green         (lower trail)
	"$esc[38;5;22m"    # dark green         (tail smear, three stages)
	"$esc[38;5;22m"
	"$esc[38;5;22m"
)
$odds = 1, 1, 2, 3, 3, 1, 1, 1
$stages = $colours.Count

# Shade blocks: light, medium, dark.
$shades = [char]0x2591, [char]0x2592, [char]0x2593

# Tail smear: the trail ends in six cells of shade blocks (two dark, two medium,
# two light). $null means a random character is drawn for that stage.
$glyphs = $null, $null, $null, $null, $null, $shades[2], $shades[1], $shades[0]
$smear = 5   # rows the smear runs past the trail length before the erase

# Blur: each frame a stream has a 1 in $blurOdds chance of smearing one random
# mid-trail character into a shade block for one frame.
$blurOdds = 6

# Bleed: each frame a stream has a 1 in $bleedOdds chance of flashing a faint
# copy of its head character in the column to its left or right for one frame.
# It only happens where that neighbouring cell is known to be empty, and only
# on even rows so that it never wipes out a ghost (see below).
$bleedOdds = 4

# Ghosts: a fast stream leaves a near-black character behind on half the odd rows
# instead of erasing it. They stay in the background until the next stream
# comes down that column, which gives the rain some depth.
$ghost = "$esc[38;5;234m"
$black = "$esc[40m"

# Rows behind the head at which each colour is drawn, for every trail length.
$minLen = 6
$maxLen = 19
$offsets = @{}
for ($l = $minLen; $l -le $maxLen; $l++) {
	$offsets[$l] = [int[]]@(0, 1, [Math]::Floor($l / 4), [Math]::Floor($l / 2), [Math]::Floor(3 * $l / 4), ($l - 1), ($l + 1), ($l + 3))
}

$rng = [System.Random]::new()
$frameMs = 50

# Windows PowerShell 5.1 ('Desktop' edition) only runs on Windows and has no $IsWindows
$isWindowsOS = $PSVersionTable.PSEdition -eq 'Desktop' -or $IsWindows

# Background audio is not implemented on Windows
$url = "https://cleasbycode.co.uk/media/matrix2.webm"
$player = $null

Clear-Host

try {
	# The Katakana need UTF-8 output (already the default outside Windows)
	try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }

	if ($Music -and -not $isWindowsOS) {
		# Try mpv first, then vlc. Started as a child process so it can be stopped on exit.
		if (Get-Command mpv -CommandType Application -ErrorAction Ignore) {
			# --no-terminal: don't write over the animation or read keypresses
			# --no-config:   ignore user mpv config and scripts
			# --ytdl=no:     don't hand the URL to yt-dlp
			$player = Start-Process mpv -PassThru -ArgumentList '--no-terminal', '--no-config', '--ytdl=no', '--no-video', $url
		} elseif (Get-Command cvlc -CommandType Application -ErrorAction Ignore) {
			$player = Start-Process cvlc -PassThru -RedirectStandardError /dev/null -ArgumentList '--quiet', '--no-video', '--play-and-exit', $url
		}
	}

	# Hide cursor for cleaner display
	[Console]::CursorVisible = $false

	$cols = 0
	$rows = 0
	$frame = [System.Text.StringBuilder]::new()
	$undo = [System.Text.StringBuilder]::new()   # puts back last frame's blur and bleed cells
	$clock = [System.Diagnostics.Stopwatch]::StartNew()

	while ($true) {
		# One stream per column. Start over whenever the window size changes.
		if ([Console]::WindowWidth -ne $cols -or [Console]::WindowHeight -ne $rows) {
			$cols = [Math]::Max(1, [Console]::WindowWidth)
			$rows = [Math]::Max(1, [Console]::WindowHeight)
			$head = [int[]]::new($cols)   # row of the leading character
			$len = [int[]]::new($cols)    # trail length
			$spd = [int[]]::new($cols)    # rows moved per frame
			$gap = [int[]]::new($cols)    # frames left before restarting
			for ($i = 0; $i -lt $cols; $i++) {
				$gap[$i] = 1
			}
			# Paint the whole window black. Spaces are written explicitly
			# because not every terminal clears to the background colour.
			[void]$frame.Append("$esc[0m$black$esc[2J")
			$blank = ' ' * $cols
			for ($r = 1; $r -le $rows; $r++) {
				[void]$frame.Append("$esc[$r;1H").Append($blank)
			}
			[void]$undo.Clear()
		}
		[void]$frame.Append($undo.ToString())
		[void]$undo.Clear()

		for ($i = 0; $i -lt $cols; $i++) {
			# Handle inactive streams (gap countdown)
			if ($gap[$i] -gt 0) {
				if (--$gap[$i] -eq 0) {
					$head[$i] = -$rng.Next([int]($rows / 2) + 1)   # start above screen
					$len[$i] = $rng.Next($minLen, $maxLen + 1)
					$spd[$i] = $rng.Next(1, 3)                     # 1-2 rows/frame
				}
				continue
			}

			$c = $i + 1   # 1-based terminal column
			$l = $len[$i]
			$offset = $offsets[$l]

			# Move one row at a time, even for fast streams, so every cell
			# passes through the whole gradient and is erased at the end.
			for ($step = 0; $step -lt $spd[$i]; $step++) {
				$h = $head[$i]
				for ($k = 0; $k -lt $stages; $k++) {
					$p = $h - $offset[$k]
					if ($p -ge 1 -and $p -le $rows -and ($odds[$k] -eq 1 -or $rng.Next($odds[$k]) -eq 0)) {
						$glyph = $glyphs[$k]
						if ($null -eq $glyph) {
							$glyph = $chars[$rng.Next($nchars)]
						}
						[void]$frame.Append("$esc[$p;${c}H").Append($colours[$k]).Append($glyph)
					}
				}

				# Erase: clear the end of the smear, or leave a ghost behind
				$p = $h - $l - $smear
				if ($p -ge 1 -and $p -le $rows) {
					if ($spd[$i] -eq 2 -and $p % 2 -eq 1 -and $rng.Next(2) -eq 0) {
						[void]$frame.Append("$esc[$p;${c}H$ghost").Append($chars[$rng.Next($nchars)])
					}
					else {
						[void]$frame.Append("$esc[$p;${c}H ")
					}
				}
				$head[$i]++
			}

			# Blur: smear one character between the head and the fading tail,
			# in the colour that part of the trail normally has
			if ($rng.Next($blurOdds) -eq 0) {
				$d = $rng.Next(2, $l - 2)
				$p = $head[$i] - 1 - $d
				if ($p -ge 1 -and $p -le $rows) {
					$k = 4
					if ($d -lt $offset[2]) { $k = 1 } elseif ($d -lt $offset[3]) { $k = 2 } elseif ($d -lt $offset[4]) { $k = 3 }
					[void]$frame.Append("$esc[$p;${c}H").Append($colours[$k]).Append($shades[$rng.Next(3)])
					[void]$undo.Append("$esc[$p;${c}H").Append($colours[$k]).Append($chars[$rng.Next($nchars)])
				}
			}

			# Bleed: the neighbouring cell is empty if that stream is resting,
			# has not reached this row yet, or has already erased it
			$j = $i + 2 * $rng.Next(2) - 1
			$p = $head[$i] - 1
			if ($rng.Next($bleedOdds) -eq 0 -and $p % 2 -eq 0 -and $j -ge 0 -and $j -lt $cols -and $p -ge 1 -and $p -le $rows -and
				($gap[$j] -gt 0 -or $p -gt $head[$j] + 2 -or $p -lt $head[$j] - $len[$j] - $smear - 1)) {
				$glyph = $chars[$rng.Next($nchars)]
				$at = "$esc[$p;$($j + 1)H"
				[void]$frame.Append("$esc[$p;${c}H").Append($colours[0]).Append($glyph).Append($at).Append($colours[4]).Append($glyph)
				[void]$undo.Append($at).Append(' ')
			}

			# Deactivate when fully off-screen
			if ($head[$i] - $l - $smear -gt $rows) {
				$gap[$i] = $rng.Next(5, 45)   # pause 5-44 frames before restart
			}
		}

		[Console]::Out.Write($frame.ToString())
		[void]$frame.Clear()

		# Exit on any key press
		if ([Console]::KeyAvailable) {
			# Clear the whole key sequence from the buffer (arrow keys send several bytes)
			while ([Console]::KeyAvailable) {
				$null = [Console]::ReadKey($true)
			}
			break
		}

		# Pace against a clock so the frame rate does not depend on window size
		$wait = $frameMs - $clock.ElapsedMilliseconds
		if ($wait -gt 0) {
			Start-Sleep -Milliseconds $wait
		}
		$clock.Restart()
	}
}
finally {
	# Restore colours and cursor visibility on exit
	[Console]::Out.Write("$esc[0m")
	[Console]::CursorVisible = $true

	# Stop the audio so it doesn't outlive the animation
	if ($null -ne $player -and -not $player.HasExited) {
		$player.Kill()
	}

	Clear-Host
}
