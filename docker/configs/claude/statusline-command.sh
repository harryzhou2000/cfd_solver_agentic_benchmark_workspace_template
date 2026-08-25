#!/bin/bash
# Powerlevel10k-style status line (powerline segments with angled separators).
# Originally derived from ~/.bashrc PS1, now styled to match ~/.p10k.zsh
# (rainbow theme, angled separators) and extended with Claude Code session info:
#   dir | git branch | model | reasoning effort | context window | rate limits

input=$(cat)

cwd=$(echo "$input" | jq -r '.workspace.current_dir')
dir_base=$(basename "$cwd")

model=$(echo "$input" | jq -r '.model.display_name // empty')
effort=$(echo "$input" | jq -r '.effort.level // empty')
ctx_pct=$(echo "$input" | jq -r '.context_window.used_percentage // empty')
five=$(echo "$input" | jq -r '.rate_limits.five_hour.used_percentage // empty')
week=$(echo "$input" | jq -r '.rate_limits.seven_day.used_percentage // empty')

# Powerline angled separator (requires a Nerd Font, matching your p10k config).
SEP=$''
RESET=$'\033[0m'

# segments: array of "bg;fg;text"
segments=()

# --- directory segment (blue bg, like p10k dir) ---
segments+=("39;0;  ${dir_base}")

# --- git branch segment (green if clean, yellow if dirty) ---
branch=$(git --no-optional-locks -C "$cwd" branch --show-current 2>/dev/null)
if [ -n "$branch" ]; then
  if git --no-optional-locks -C "$cwd" diff --quiet --ignore-submodules HEAD 2>/dev/null; then
    segments+=("34;0;  ${branch}")
  else
    segments+=("178;0;  ${branch} ✗")
  fi
fi

# --- model segment (magenta) ---
if [ -n "$model" ]; then
  segments+=("97;255;  ${model}")
fi

# --- reasoning effort segment (cyan) ---
if [ -n "$effort" ]; then
  segments+=("73;0;  ${effort}")
fi

# --- context window usage segment (graded green/yellow/red) ---
if [ -n "$ctx_pct" ]; then
  ctx_int=${ctx_pct%.*}
  if [ "$ctx_int" -ge 80 ]; then
    ctx_bg=160
  elif [ "$ctx_int" -ge 50 ]; then
    ctx_bg=178
  else
    ctx_bg=34
  fi
  segments+=("${ctx_bg};0;  ${ctx_int}%")
fi

# --- rate limit segment (graded, shows 5h and/or 7d) ---
if [ -n "$five" ] || [ -n "$week" ]; then
  rl_out=""
  max_pct=0
  if [ -n "$five" ]; then
    five_int=${five%.*}
    rl_out="5h:${five_int}%"
    [ "$five_int" -gt "$max_pct" ] && max_pct=$five_int
  fi
  if [ -n "$week" ]; then
    week_int=${week%.*}
    [ -n "$rl_out" ] && rl_out="$rl_out "
    rl_out="${rl_out}7d:${week_int}%"
    [ "$week_int" -gt "$max_pct" ] && max_pct=$week_int
  fi
  if [ "$max_pct" -ge 80 ]; then
    rl_bg=160
  elif [ "$max_pct" -ge 50 ]; then
    rl_bg=178
  else
    rl_bg=34
  fi
  segments+=("${rl_bg};0;  ${rl_out}")
fi

# --- render segments as a powerline chain ---
out=""
prev_bg=""
for seg in "${segments[@]}"; do
  bg="${seg%%;*}"
  rest="${seg#*;}"
  fg="${rest%%;*}"
  text="${rest#*;}"

  if [ -n "$prev_bg" ]; then
    out="${out}$(printf '\033[38;5;%sm\033[48;5;%sm%s' "$prev_bg" "$bg" "$SEP")"
  fi
  out="${out}$(printf '\033[48;5;%sm\033[38;5;%sm%s' "$bg" "$fg" "$text")"
  prev_bg="$bg"
done

if [ -n "$prev_bg" ]; then
  out="${out}$(printf '\033[0m\033[38;5;%sm%s' "$prev_bg" "$SEP")"
fi
out="${out}${RESET}"

printf '%s' "$out"
