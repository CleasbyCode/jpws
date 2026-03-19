# Matrix Rain — half-width Katakana, alphanumeric, and special characters.
# Press any key to exit.

# -- Character set --
$Chars = (
    # Half-width Katakana
    'ｦｧｨｩｪｫｬｭｮｯｰｱｲｳｴｵｶｷｸｹｺｻｼｽｾｿ' +
    'ﾀﾁﾂﾃﾄﾅﾆﾇﾈﾉﾊﾋﾌﾍﾎﾏﾐﾑﾒﾓﾔﾕﾖﾗﾘﾙﾚﾛﾜﾝ' +
    # Digits + Latin + symbols
    '0123456789' +
    'ABCDEFGHIJKLMNOPQRSTUVWXYZ' +
    'abcdefghijklmnopqrstuvwxyz' +
    '!@#$%^&*()+=<>?/{}[]|:;~'
).ToCharArray()

$NChars = $Chars.Length
$Rng = [System.Random]::new()

function Get-RandChar { $Chars[$Rng.Next($NChars)] }

# -- Trail color gradient (ANSI escape codes) --
$C_HEAD = "`e[1;97m"       # bold bright white
$C_NEAR = "`e[1;92m"       # bold bright green
$C_BODY = "`e[38;5;46m"    # bright green
$C_MID  = "`e[38;5;34m"    # medium green
$C_TAIL = "`e[38;5;22m"    # dark green
$C_DIM  = "`e[38;5;236m"   # near-black

# -- Stream class --
class RainStream {
    [int]$Row
    [int]$Length
    [int]$Speed
    [bool]$Active
    [int]$Gap

    Reset([int]$rows, [System.Random]$rng) {
        $this.Row    = -$rng.Next(0, [Math]::Max($rows / 2, 1) + 1)
        $this.Length = $rng.Next(6, 20)
        $this.Speed  = $rng.Next(1, 3)
        $this.Active = $true
        $this.Gap    = 0
    }

    Deactivate([System.Random]$rng) {
        $this.Active = $false
        $this.Gap    = $rng.Next(5, 45)
    }
}

# -- Setup --
$OrigCursorVisible = [Console]::CursorVisible
[Console]::CursorVisible = $false
[Console]::Clear()

$Cols = [Console]::WindowWidth
$Rows = [Console]::WindowHeight

# Initialize streams
$Streams = [System.Collections.Generic.List[RainStream]]::new($Cols)
for ($i = 0; $i -lt $Cols; $i++) {
    $s = [RainStream]::new()
    $s.Reset($Rows, $Rng)
    $Streams.Add($s)
}

$Buf = [System.Text.StringBuilder]::new(8192)

try {
    while ($true) {
        # Handle terminal resize
        $NewCols = [Console]::WindowWidth
        $NewRows = [Console]::WindowHeight
        if ($NewCols -ne $Cols -or $NewRows -ne $Rows) {
            if ($NewCols -gt $Cols) {
                for ($i = $Cols; $i -lt $NewCols; $i++) {
                    $s = [RainStream]::new()
                    $s.Reset($NewRows, $Rng)
                    $Streams.Add($s)
                }
            } elseif ($NewCols -lt $Cols) {
                $Streams.RemoveRange($NewCols, $Cols - $NewCols)
            }
            $Cols = $NewCols
            $Rows = $NewRows
            [Console]::Clear()
        }

        [void]$Buf.Clear()

        for ($i = 0; $i -lt $Streams.Count; $i++) {
            $s = $Streams[$i]
            $c = $i + 1  # 1-based column

            if (-not $s.Active) {
                $s.Gap--
                if ($s.Gap -le 0) { $s.Reset($Rows, $Rng) }
                continue
            }

            $h = $s.Row
            $len = $s.Length

            # Head: bright white
            if ($h -ge 1 -and $h -le $Rows) {
                [void]$Buf.Append("`e[${h};${c}H${C_HEAD}$(Get-RandChar)")
            }

            # 1 behind: bold bright green
            $p = $h - 1
            if ($p -ge 1 -and $p -le $Rows) {
                [void]$Buf.Append("`e[${p};${c}H${C_NEAR}$(Get-RandChar)")
            }

            # Upper trail: bright green (shimmer)
            $p = $h - [Math]::Floor($len / 4)
            if ($p -ge 1 -and $p -le $Rows -and $Rng.Next(2) -eq 0) {
                [void]$Buf.Append("`e[${p};${c}H${C_BODY}$(Get-RandChar)")
            }

            # Mid trail: medium green (shimmer)
            $p = $h - [Math]::Floor($len / 2)
            if ($p -ge 1 -and $p -le $Rows -and $Rng.Next(3) -eq 0) {
                [void]$Buf.Append("`e[${p};${c}H${C_MID}$(Get-RandChar)")
            }

            # Lower trail: dark green (shimmer)
            $p = $h - [Math]::Floor(3 * $len / 4)
            if ($p -ge 1 -and $p -le $Rows -and $Rng.Next(3) -eq 0) {
                [void]$Buf.Append("`e[${p};${c}H${C_TAIL}$(Get-RandChar)")
            }

            # Fading out: near-black
            $p = $h - $len + 1
            if ($p -ge 1 -and $p -le $Rows) {
                [void]$Buf.Append("`e[${p};${c}H${C_DIM}$(Get-RandChar)")
            }

            # Erase tail
            $p = $h - $len
            if ($p -ge 1 -and $p -le $Rows) {
                [void]$Buf.Append("`e[${p};${c}H ")
            }

            # Advance
            $s.Row += $s.Speed

            # Deactivate when off-screen
            if (($h - $len) -gt $Rows) {
                $s.Deactivate($Rng)
            }
        }

        [Console]::Write($Buf.ToString())

        # Exit on any key press (sleep serves as frame delay when no key)
        if ([Console]::KeyAvailable) {
            [void][Console]::ReadKey($true)
            break
        }
        Start-Sleep -Milliseconds 35
    }
}
finally {
    # Cleanup: restore cursor, reset colors, clear screen
    [Console]::CursorVisible = $OrigCursorVisible
    Write-Host "`e[0m`e[2J`e[H" -NoNewline
}
