Write-Host "Enter the board width (default 20):"
$widthInput = Read-Host
if ([string]::IsNullOrWhiteSpace($widthInput)) {
    $Width = 20
}
else {
    $Width = [int]$widthInput
}

Write-Host "Enter the board height (default 10):"
$heightInput = Read-Host
if ([string]::IsNullOrWhiteSpace($heightInput)) {
    $Height = 10
}
else {
    $Height = [int]$heightInput
}
Write-Host "Enter the number of steps (default 50):"
$stepsInput = Read-Host
if ([string]::IsNullOrWhiteSpace($stepsInput)) {
    $Steps = 50
}
else {
    $Steps = [int]$stepsInput
}
$board = @()
for ($r = 0; $r -lt $Height; $r++) {
    $row = @()
    for ($c = 0; $c -lt $Width; $c++) {
        $randVal = Get-Random -Minimum 0.0 -Maximum 1.0
        if ($randVal -lt 0.2) {
            $row += $true
        }
        else {
            $row += $false
        }
    }
    $board += , $row
}
function Draw-Board {
    param([object[]]$board)
    Clear-Host
    for ($r = 0; $r -lt $board.Count; $r++) {
        $rowStr = ""
        for ($c = 0; $c -lt $board[$r].Count; $c++) {
            if ($board[$r][$c]) {
                $rowStr += "#"
            }
            else {
                $rowStr += "."
            }
        }
        Write-Host $rowStr
    }
}
function Get-NeighborCount {
    param(
        [object[]]$board,
        [int]$r,
        [int]$c
    )
    $height = $board.Count
    $width  = $board[0].Count
    $count  = 0
    for ($dr = -1; $dr -le 1; $dr++) {
        for ($dc = -1; $dc -le 1; $dc++) {
            if ($dr -eq 0 -and $dc -eq 0) {
                continue
            }
            $nr = $r + $dr
            $nc = $c + $dc
            if ($nr -ge 0 -and $nr -lt $height -and
                $nc -ge 0 -and $nc -lt $width) {
                if ($board[$nr][$nc]) {
                    $count++
                }
            }
        }
    }
    return $count
}
function Next-Board {
    param([object[]]$board)
    $height = $board.Count
    $width  = $board[0].Count
    $newBoard = @()
    for ($r = 0; $r -lt $height; $r++) {
        $newRow = @()
        for ($c = 0; $c -lt $width; $c++) {
            $alive     = $board[$r][$c]
            $neighbors = Get-NeighborCount -board $board -r $r -c $c
            if ($alive) {
                if ($neighbors -lt 2 -or $neighbors -gt 3) {
                    $newRow += $false
                }
                else {
                    $newRow += $true
                }
            }
            else {
                if ($neighbors -eq 3) {
                    $newRow += $true
                }
                else {
                    $newRow += $false
                }
            }
        }
        $newBoard += , $newRow
    }
    return $newBoard
}
for ($i = 1; $i -le $Steps; $i++) {
    Draw-Board -board $board
    $board = Next-Board -board $board
    Start-Sleep -Milliseconds 200
}
