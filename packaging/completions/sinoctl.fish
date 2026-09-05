# fish completion for sinoctl / sinodragon.
#
#   mkdir -p ~/.config/fish/completions
#   cp sinoctl.fish ~/.config/fish/completions/sinoctl.fish
#
# Profile and game names come from the running daemon.

function __sinoctl_ask
    sinoctl complete $argv[1] 2>/dev/null
end

function __sinoctl_no_command
    set -l tokens (commandline -opc)
    set -e tokens[1]
    # Drop global options so the first bare word is the command.
    while set -q tokens[1]
        switch $tokens[1]
            case -s --socket
                set -e tokens[1]; set -e tokens[1]
            case '-*'
                set -e tokens[1]
            case '*'
                return 1
        end
    end
    return 0
end

function __sinoctl_using
    set -l tokens (commandline -opc)
    contains -- $argv[1] $tokens
end

complete -c sinoctl -f
complete -c sinoctl -n __sinoctl_no_command -a "(__sinoctl_ask commands)"
complete -c sinoctl -s s -l socket -r -F -d "Control socket"
complete -c sinoctl -s h -l help -d "Show help"

complete -c sinoctl -n "__sinoctl_using profile" -a "(__sinoctl_ask profiles)"
complete -c sinoctl -n "__sinoctl_using profile" -a "for" -d "Hold for a duration"
complete -c sinoctl -n "__sinoctl_using for" -a "10s 30s 1m 5m 30m 1h"
complete -c sinoctl -n "__sinoctl_using game" -a "(__sinoctl_ask games) list stop"
complete -c sinoctl -n "__sinoctl_using game" -a "start stop"
complete -c sinoctl -n "__sinoctl_using state" -a "ok warn fail busy off"
complete -c sinoctl -n "__sinoctl_using watch" -a "on off"
complete -c sinoctl -n "__sinoctl_using brightness" -a "0 25 50 75 100"
complete -c sinoctl -n "__sinoctl_using complete" -a "commands profiles games"
complete -c sinoctl -n "__sinoctl_using pomodoro" -a "start pause reset skip status"

complete -c sinodragon -s c -l config -r -F -d "Config file"
complete -c sinodragon -s d -l daemon -d "Run without the interactive prompt"
complete -c sinodragon -s p -l preview -d "Draw frames in the terminal"
complete -c sinodragon -s s -l socket -r -F -d "Control socket"
complete -c sinodragon -l no-socket -d "Do not listen for control commands"
complete -c sinodragon -l lock -r -F -d "Single-instance lock file"
complete -c sinodragon -l no-lock -d "Allow more than one instance"
complete -c sinodragon -s h -l help -d "Show help"
complete -c sinodragon -s v -l version -d "Show the version"
