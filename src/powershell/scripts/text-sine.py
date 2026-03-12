#!/usr/bin/env python3
"""
text-sine.py - converted from PowerShell by Claude
Original: text-sine.ps1 created by Darren Shaw / @gierrofo
Make a command-line sine wave
"""

import math
import os
import sys
import time
import random
import shutil
import subprocess
import threading
import platform

# Cross-platform keyboard input handling
if platform.system() == 'Windows':
    import msvcrt
    def key_available():
        return msvcrt.kbhit()
    def read_key():
        return msvcrt.getch()
else:
    import select
    import tty
    import termios
    def key_available():
        return select.select([sys.stdin], [], [], 0)[0] != []
    def read_key():
        return sys.stdin.read(1)

# ANSI color codes mapping to PowerShell color names
COLORS = {
    "DarkGreen": "\033[32m",
    "DarkCyan": "\033[36m",
    "DarkYellow": "\033[33m",
    "Gray": "\033[37m",
    "DarkGray": "\033[90m",
    "Green": "\033[92m",
    "Cyan": "\033[96m",
    "Red": "\033[91m",
    "Magenta": "\033[95m",
    "Yellow": "\033[93m",
    "White": "\033[97m",
}
RESET = "\033[0m"

def clear_screen():
    os.system('cls' if platform.system() == 'Windows' else 'clear')

def hide_cursor():
    print("\033[?25l", end="", flush=True)

def show_cursor():
    print("\033[?25h", end="", flush=True)

def play_audio_background(url):
    """Try to play audio in background using mpv or vlc"""
    def play():
        try:
            # Try mpv first
            subprocess.run(
                ["mpv", "--no-video", url],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL
            )
        except FileNotFoundError:
            try:
                # Try cvlc as fallback
                subprocess.run(
                    ["cvlc", "--no-video", "--play-and-exit", url],
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL
                )
            except FileNotFoundError:
                pass  # No audio player available
    
    thread = threading.Thread(target=play, daemon=True)
    thread.start()

def main():
    clear_screen()
    
    # Setup variables
    terminal_size = shutil.get_terminal_size()
    host_width = terminal_size.columns
    display_char = ".-=:§[#]§:=-."
    dcl = len(display_char)
    host_width = (host_width - (2 * dcl)) / 2
    centre = host_width + (dcl / 2)
    delay = 15 / 1000  # Convert milliseconds to seconds
    continue_loop = True
    
    str_colours = ["DarkGreen", "DarkCyan", "DarkYellow", "Gray", "DarkGray", 
                   "Green", "Cyan", "Red", "Magenta", "Yellow", "White"]
    change_colour = 360 / len(str_colours)
    clear_screen_after = len(str_colours) * 4
    number_of_runs = 0
    lowest_speed = 2
    speed = lowest_speed
    speed_inc = 1
    max_speed = 18
    
    # Detect OS and play audio on non-Windows
    is_windows = platform.system() == 'Windows'
    url = "https://cleasbycode.co.uk/media/circles.mp3"
    
    if not is_windows:
        play_audio_background(url)
    
    clear_screen()
    hide_cursor()
    
    # Set up terminal for non-blocking input on Unix
    if not is_windows:
        old_settings = termios.tcgetattr(sys.stdin)
        tty.setcbreak(sys.stdin.fileno())
    
    try:
        while continue_loop:
            # Shuffle the colour order
            random.shuffle(str_colours)
            
            deg = -90  # Start at -90 to draw at left-hand column
            while deg < 270:
                # Calculate where we're drawing
                offset_delta = math.sin(math.radians(deg))
                offset = centre * offset_delta
                location = round(centre + offset + dcl)
                
                # Pad the string to get us to the location
                display_str = display_char.rjust(location)
                
                # Change the colour at equal intervals based on how many colours we're using
                str_colour_num = int((deg + 90) / change_colour)  # +90 to start index at 0
                str_colour_num = min(str_colour_num, len(str_colours) - 1)  # Prevent index overflow
                str_colour = str_colours[str_colour_num]
                
                # Write the string with color
                color_code = COLORS.get(str_colour, RESET)
                print(f"{color_code}{display_str}{RESET}")
                
                # Wait a little bit and check for keypress to exit
                time.sleep(delay)
                
                if key_available():
                    read_key()  # Clear the key from buffer
                    continue_loop = False
                    break
                
                deg += speed
            
            # Clear the screen after a set number of runs
            number_of_runs += 1
            if number_of_runs > clear_screen_after:
                clear_screen()
                number_of_runs = 0
            
            speed += speed_inc
            if speed >= max_speed:
                speed_inc = -1
            if speed <= lowest_speed:
                speed_inc = 1
    
    finally:
        # Restore terminal settings and cursor visibility on exit
        if not is_windows:
            termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old_settings)
        show_cursor()
        clear_screen()

if __name__ == "__main__":
    main()
