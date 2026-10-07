# bash tab-completion for the fibs meta-build-system.
#
# Source this file from your shell rc, e.g.:
#   . /home/dylan/repos/sokol-ink-ribbon/native/scripts/fibs-completion.bash
#
# It completes fibs subcommands, build configs, targets (for `build`, `run`
# and `clean`) and imports (for `link`/`unlink`) by querying the local fibs
# project. Results are cached for a few seconds because invoking fibs runs
# deno and re-evaluates the project.

_fibs_strip_ansi() {
    sed -r 's/\x1B\[[0-9;]*[mK]//g'
}

_fibs_cmd() {
    if [ -x "./fibs" ]; then
        printf './fibs'
    else
        printf 'fibs'
    fi
}

# run a fibs list command, strip ansi, cache for 5 seconds per cwd+key
_fibs_cached_list() {
    local key="$1"
    local fibs
    fibs="$(_fibs_cmd)"
    local cache="/tmp/fibs-complete-$(printf '%s' "$PWD" | cksum | tr -d ' ')-${key}"
    if [ ! -f "$cache" ] || [ -n "$(find "$cache" -mmin +1 2>/dev/null)" ]; then
        "$fibs" list "$key" 2>/dev/null | _fibs_strip_ansi >"$cache"
    fi
    cat "$cache"
}

_fibs_completions() {
    COMPREPLY=()
    local cur="${COMP_WORDS[COMP_CWORD]}"
    local sub="${COMP_WORDS[1]}"
    local cmds="build run config list clean open update diag help emsdk set get unset link unlink webpage reset init"

    if [ "$COMP_CWORD" -eq 1 ]; then
        COMPREPLY=($(compgen -W "$cmds" -- "$cur"))
        return
    fi

    case "$sub" in
        config)
            local cfgs
            cfgs="$(_fibs_cached_list configs)"
            COMPREPLY=($(compgen -W "$cfgs" -- "$cur"))
            ;;
        build | run | clean)
            local targets
            targets="$(_fibs_cached_list targets | sed -n 's/^\([^:]*\):.*/\1/p' | tr -d ' ')"
            COMPREPLY=($(compgen -W "$targets" -- "$cur"))
            ;;
        list)
            COMPREPLY=($(compgen -W "settings configs imports runners openers jobs targets" -- "$cur"))
            ;;
        link | unlink)
            local imports
            imports="$(_fibs_cached_list imports | sed -n 's/^\([A-Za-z0-9_.-]*\):.*/\1/p')"
            COMPREPLY=($(compgen -W "$imports" -- "$cur"))
            ;;
        emsdk)
            COMPREPLY=($(compgen -W "install list uninstall" -- "$cur"))
            ;;
        help)
            COMPREPLY=($(compgen -W "$cmds" -- "$cur"))
            ;;
    esac
}

complete -F _fibs_completions fibs
complete -F _fibs_completions ./fibs
