# text_sine2.ps1 - based on text-sine.ps1 created by Darren Shaw / @gierrofo
# Make a command-line sine wave
# Powershell trig functions work on radians
# rad = deg * (pi/180)
# To get value between -1 and 1
# [Math]::Sin($deg * ([Math]::PI / 180))
# Uses [System.Console]::KeyAvailable to check if a key has been pressed, stopping if it has.
# Run with -Music to stream background audio (Linux/macOS only, needs mpv or cvlc).

# NOTE: no param() block. In the jpws JPG-PowerShell polyglot the embedded script
# runs after a leading "cls;", so a param() block would not be the first statement
# (a PowerShell parse error / no binding). Read $args directly instead — this works
# both standalone (pwsh text_sine2.ps1 -Music) and embedded (pwsh image.jpg -Music).
$Music = $args -contains '-Music'

# KeyAvailable throws when console input is redirected, so a real terminal is required
if ([Console]::IsInputRedirected) {
    Write-Error "text_sine2.ps1 must be run from an interactive terminal."
    exit 1
}

# Setup some variables
$displayChar = ".-=:§[#]§:=-."
$dcl = $displayChar.Length
$delay = 15
$continue = $true
$strColours = @("DarkGreen", "DarkCyan", "DarkYellow", "Gray", "DarkGray", "Green", "Cyan", "Red", "Magenta", "Yellow", "White")
$changeColour = 360 / $strColours.Count
$clearScreenAfter = $strColours.Count * 4
$numberOfRuns = 0
$lowestSpeed = 2
$speed = $lowestSpeed
$speedInc = 1
$maxSpeed = 18
$degToRad = [Math]::PI / 180

# Windows PowerShell 5.1 ('Desktop' edition) only runs on Windows and has no $IsWindows
$isWindowsOS = $PSVersionTable.PSEdition -eq 'Desktop' -or $IsWindows

# Background audio is not implemented on Windows
$url = "https://cleasbycode.co.uk/media/circles.mp3"
$player = $null

Clear-Host

try {
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

    while ($continue) {
        # Re-read the width each run so the wave follows window resizes.
        # The right-hand peak stops one column short of the edge to avoid line wrapping.
        $hostWidth = $Host.UI.RawUI.WindowSize.Width
        $centre = ($hostWidth - 1 - $dcl) / 2

        $strColours = @(Get-Random -InputObject $strColours -Count $strColours.Count)  # Shuffle the colour order

        for ($deg = -90; $deg -lt 270; $deg += $speed) {  # Start at -90 to draw at left-hand column
            # Calculate where we're drawing
            $offset = $centre * [Math]::Sin($deg * $degToRad)
            $location = [Math]::Max(0, [Math]::Round($centre + $offset + $dcl))

            # Pad the string to get us to the location
            $displayStr = $displayChar.PadLeft($location, " ")

            # Change the colour at equal intervals based on how many colours we're using
            $strColourNum = [Math]::Truncate(($deg + 90) / $changeColour)  # +90 to start index at 0
            $strColourNum = [Math]::Min($strColourNum, $strColours.Count - 1)  # Prevent index overflow
            $strColour = $strColours[$strColourNum]

            # Write the string
            Write-Host -ForegroundColor $strColour $displayStr

            # Wait a little bit to avoid screen tearing and check for a keypress to exit
            Start-Sleep -Milliseconds $delay

            if ([Console]::KeyAvailable) {
                $null = [Console]::ReadKey($true)  # Clear the key from buffer
                $continue = $false
                break
            }
        }

        # Clear the screen after a set number of runs, stops the screen buffer filling up
        $numberOfRuns++
        if ($numberOfRuns -ge $clearScreenAfter) {
            Clear-Host
            $numberOfRuns = 0
        }

        $speed += $speedInc
        if ($speed -ge $maxSpeed) { $speedInc = -1 }
        if ($speed -le $lowestSpeed) { $speedInc = 1 }
    }
}
finally {
    # Restore cursor visibility on exit
    [Console]::CursorVisible = $true

    # Stop the audio so it doesn't outlive the animation
    if ($null -ne $player -and -not $player.HasExited) {
        $player.Kill()
    }

    Clear-Host
}
