#!/bin/bash
#
# Matrix Rain
#
# Half-width Katakana, alphanumeric, and special characters
# Color gradient trail: bright white head → green body → dark green tail → erased
#   - Per-stream variable speed and random gaps between streams
#   - Buffered frame output (single printf per frame, no subshells in hot loop)
#   - Terminal resize handling (SIGWINCH)
#   - Shimmer effect: trail characters randomly change each frame

set -u

MUSIC_PID=""

url="https://cleasbycode.co.uk/media/matrix2.webm"

  if command -v mpv &>/dev/null; then                                                               
      mpv --no-video "$url" &>/dev/null &
      MUSIC_PID=$!
  elif command -v cvlc &>/dev/null; then                                                            
      cvlc --no-video --play-and-exit "$url" &>/dev/null &
      MUSIC_PID=$!
  fi   

# -- Setup --
printf '\e[?25l\e[2J\e[H'

# Put terminal in raw mode so key presses are immediate (no Enter needed)
old_tty=$(stty -g)
stty -echo -icanon min 0 time 0

cleanup() {
      [[ -n "$MUSIC_PID" ]] && kill "$MUSIC_PID" 2>/dev/null
      stty "$old_tty"
      printf '\e[?25h\e[0m\e[2J\e[H'
      exit 0
  }
  trap cleanup INT TERM EXIT

cols=$(tput cols)
rows=$(tput lines)

recalc() {
    cols=$(tput cols)
    rows=$(tput lines)
    num_streams=$cols
    ((num_streams < 1)) && num_streams=1
    printf '\e[2J'
}
trap recalc WINCH

# Single-cell characters — one stream per column
cell_w=1
num_streams=$cols
((num_streams < 1)) && num_streams=1

# Matrix character set: half-width Katakana + digits + Latin + punctuation/symbols
chars=(
    # Half-width Katakana (the iconic Matrix look)
    ｦ ｧ ｨ ｩ ｪ ｫ ｬ ｭ ｮ ｯ ｰ ｱ ｲ ｳ ｴ ｵ ｶ ｷ ｸ ｹ ｺ ｻ ｼ ｽ ｾ ｿ
    ﾀ ﾁ ﾂ ﾃ ﾄ ﾅ ﾆ ﾇ ﾈ ﾉ ﾊ ﾋ ﾌ ﾍ ﾎ ﾏ ﾐ ﾑ ﾒ ﾓ ﾔ ﾕ ﾖ ﾗ ﾘ ﾙ ﾚ ﾛ ﾜ ﾝ
    # Digits
    0 1 2 3 4 5 6 7 8 9
    # Latin
    A B C D E F G H I J K L M N O P Q R S T U V W X Y Z
    a b c d e f g h i j k l m n o p q r s t u v w x y z
    # Symbols / special characters
    '!' '@' '#' '$' '%' '^' '&' '*' '(' ')' '+' '=' '<' '>' '?' '/'
    '{' '}' '[' ']' '|' ':' ';' '~'
)
nchars=${#chars[@]}

# Trail color gradient (head to tail)
C_HEAD='\e[1;97m'       # bold bright white  (leading character)
C_NEAR='\e[1;92m'       # bold bright green  (just behind head)
C_BODY='\e[38;5;46m'    # bright green       (upper trail)
C_MID='\e[38;5;34m'     # medium green       (mid trail)
C_TAIL='\e[38;5;22m'    # dark green         (lower trail)
C_DIM='\e[38;5;236m'    # near-black         (fading out)

# -- Per-stream state arrays --
declare -a s_row s_len s_spd s_gap s_on

reset_stream() {
    local i=$1
    s_row[$i]=$(( -(RANDOM % (rows / 2 + 1)) ))   # start above screen
    s_len[$i]=$(( RANDOM % 14 + 6 ))               # trail length 6-19
    s_spd[$i]=$(( RANDOM % 2 + 1 ))                # speed 1-2 rows/frame
    s_on[$i]=1
}

for ((i = 0; i < num_streams; i++)); do
    reset_stream "$i"
done

# -- Main loop --
while true; do
    buf=''

    for ((i = 0; i < num_streams; i++)); do
        # Handle inactive streams (gap countdown)
        if (( !s_on[i] )); then
            (( --s_gap[i] <= 0 )) && reset_stream "$i"
            continue
        fi

        c=$((i * cell_w + 1))   # 1-based terminal column
        h=${s_row[i]}
        l=${s_len[i]}

        # -- Draw the gradient trail from head to tail --

        # Head: bright white (the leading drop)
        if (( h >= 1 && h <= rows )); then
            buf+="\e[${h};${c}H${C_HEAD}${chars[RANDOM % nchars]}"
        fi

        # 1 behind: bold bright green
        (( p = h - 1 ))
        if (( p >= 1 && p <= rows )); then
            buf+="\e[${p};${c}H${C_NEAR}${chars[RANDOM % nchars]}"
        fi

        # Upper trail body: bright green (shimmer)
        (( p = h - l / 4 ))
        if (( p >= 1 && p <= rows && RANDOM % 2 == 0 )); then
            buf+="\e[${p};${c}H${C_BODY}${chars[RANDOM % nchars]}"
        fi

        # Mid trail: medium green (shimmer)
        (( p = h - l / 2 ))
        if (( p >= 1 && p <= rows && RANDOM % 3 == 0 )); then
            buf+="\e[${p};${c}H${C_MID}${chars[RANDOM % nchars]}"
        fi

        # Lower trail: dark green (shimmer)
        (( p = h - 3 * l / 4 ))
        if (( p >= 1 && p <= rows && RANDOM % 3 == 0 )); then
            buf+="\e[${p};${c}H${C_TAIL}${chars[RANDOM % nchars]}"
        fi

        # Fading out: near-black just before erase
        (( p = h - l + 1 ))
        if (( p >= 1 && p <= rows )); then
            buf+="\e[${p};${c}H${C_DIM}${chars[RANDOM % nchars]}"
        fi

        # Erase: clear the tail end
        (( p = h - l ))
        if (( p >= 1 && p <= rows )); then
            buf+="\e[${p};${c}H "
        fi

        # Advance the stream
        (( s_row[i] += s_spd[i] ))

        # Deactivate when fully off-screen
        if (( h - l > rows )); then
            s_on[$i]=0
            s_gap[$i]=$(( RANDOM % 40 + 5 ))   # pause 5-44 frames before restart
        fi
    done

    printf '%b' "$buf"

    # Exit on any key press
    if read -rsn1 -t 0.035 _; then
        break
    fi
done
