# text-sine.ps1 created by Darren Shaw / @gierrofo
# Make a command-line sine wave
# Powershell trig functions work on radians
# rad = deg * (pi/180)
# To get value between 0 and -1
# [math]::sin($deg * ([math]::pi/180))
# Uses [system.console]::keyavailable to check if a key has been pressed, stopping if it has.

# Setup some variables
$hostwidth = $Host.UI.RawUI.WindowSize.Width
$display_char= ".-=:§[#]§:=-."
$dcl = $display_char.length
$hostwidth = ($hostwidth - (2 * $dcl)) / 2
$centre = $hostwidth + ($dcl / 2)
$delay = 15
$continue = $true
$str_colours = "DarkGreen", "DarkCyan", "DarkYellow", "Gray", "DarkGray", "Green", "Cyan", "Red", "Magenta", "Yellow", "White"
$change_colour = 360 / $str_colours.count
$clear_screen_after = $str_colours.count * 4
$number_of_runs = 0
$lowest_speed = 2
$speed = $lowest_speed
$speed_inc = 1
$max_speed = 18

Clear-Host 

While ($continue -eq $true) {
	$str_colours = $str_colours | Sort-Object {Get-Random}	# Shuffle the colour order

	For ($deg = -90; $deg -lt 270; $deg = $deg + $speed) {		# Start at -90 to draw at left-hand column
		# Calcualte where we're drawing
		$offset_delta = [math]::sin($deg * ([math]::pi/180))
		$offset = ($centre * $offset_delta)
		$location = [math]::round($centre + $offset + $dcl)

		# Pad the string to get us to the location
		$display_str = $display_char.PadLeft($location, " ")

		# Change the colour at equal time based on how many colours we're using
		$str_colour_num = [math]::truncate(($deg / $change_colour))
		$str_colour = $str_colours[$str_colour_num]

		# Write the string
		Write-Host -ForegroundColor ${str_colour} "${display_str}"

		# Wait a little bit to avoid screen tearing and check for a keypress to exit
		Start-Sleep -Milliseconds $delay
		If ([system.console]::keyavailable) {
			$continue = $false
			break
		}
	}
	# Clear the screen after a set number of runs, stops the screen buffer filling up
	$number_of_runs++
	If ($number_of_runs -gt $clear_screen_after) { 
		Clear-Host 
		$number_of_runs = 0
	}
	
	$speed = $speed + $speed_inc
	If ($speed -eq $max_speed) { $speed_inc = -1 }
	If ($speed -eq $lowest_speed) { $speed_inc = 1 }
}