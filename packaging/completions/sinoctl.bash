# bash completion for sinoctl / sinodragon.
#
#   sudo cp sinoctl.bash /usr/share/bash-completion/completions/sinoctl
# or, without root:
#   mkdir -p ~/.local/share/bash-completion/completions
#   cp sinoctl.bash ~/.local/share/bash-completion/completions/sinoctl
#
# Profile and game names come from the running daemon, so they always match
# the config that is actually loaded. With no daemon up, only static words
# are offered.

_sinoctl_ask() {
    # $1: what to complete (commands|profiles|games). Silence failure so a
    # stopped daemon degrades to no suggestions rather than an error.
    sinoctl complete "$1" 2>/dev/null
}

_sinoctl() {
    local cur prev words cword
    _init_completion 2>/dev/null || {
        cur="${COMP_WORDS[COMP_CWORD]}"
        prev="${COMP_WORDS[COMP_CWORD-1]}"
    }

    # Find the command word, skipping the global options.
    local i cmd="" cmd_index=0
    for (( i=1; i < COMP_CWORD; i++ )); do
        case "${COMP_WORDS[i]}" in
            -s|--socket) (( i++ )) ;;
            -h|--help) ;;
            -*) ;;
            *) cmd="${COMP_WORDS[i]}"; cmd_index=$i; break ;;
        esac
    done

    if [[ -z $cmd ]]; then
        COMPREPLY=( $(compgen -W "$(_sinoctl_ask commands) --socket --help" -- "$cur") )
        return
    fi

    local argno=$(( COMP_CWORD - cmd_index ))

    case "$cmd" in
        profile)
            if (( argno == 1 )); then
                COMPREPLY=( $(compgen -W "$(_sinoctl_ask profiles)" -- "$cur") )
            elif (( argno == 2 )); then
                COMPREPLY=( $(compgen -W "for" -- "$cur") )
            elif (( argno == 3 )); then
                COMPREPLY=( $(compgen -W "10s 30s 1m 5m 30m 1h" -- "$cur") )
            fi
            ;;
        game)
            if (( argno == 1 )); then
                COMPREPLY=( $(compgen -W "$(_sinoctl_ask games) list stop" -- "$cur") )
            elif (( argno == 2 )); then
                COMPREPLY=( $(compgen -W "start stop" -- "$cur") )
            fi
            ;;
        state)
            (( argno == 2 )) && COMPREPLY=( $(compgen -W "ok warn fail busy off" -- "$cur") )
            ;;
        watch)
            (( argno == 1 )) && COMPREPLY=( $(compgen -W "on off" -- "$cur") )
            ;;
        brightness)
            (( argno == 1 )) && COMPREPLY=( $(compgen -W "0 10 25 40 50 75 100" -- "$cur") )
            ;;
        complete)
            (( argno == 1 )) && COMPREPLY=( $(compgen -W "commands profiles games" -- "$cur") )
            ;;
        pomodoro)
            (( argno == 1 )) && COMPREPLY=( $(compgen -W "start pause reset skip status" -- "$cur") )
            ;;
        -s|--socket)
            _filedir 2>/dev/null
            ;;
    esac
}
complete -F _sinoctl sinoctl

_sinodragon() {
    local cur="${COMP_WORDS[COMP_CWORD]}"
    local prev="${COMP_WORDS[COMP_CWORD-1]}"
    case "$prev" in
        -c|--config|-s|--socket|--lock)
            _filedir 2>/dev/null || COMPREPLY=( $(compgen -f -- "$cur") )
            return ;;
    esac
    COMPREPLY=( $(compgen -W "-c --config -d --daemon -p --preview -s --socket \
        --no-socket --lock --no-lock -h --help -v --version" -- "$cur") )
}
complete -F _sinodragon sinodragon
