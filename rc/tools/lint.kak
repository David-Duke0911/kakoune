declare-option \
    -docstring %{
        The shell command used by lint-buffer and lint-selections.

        It will be given the path to a file containing the text to be
        linted, and must produce output in the format:

            {filename}:{line}:{column}: {kind}: {message}

        If the 'kind' field contains 'error', the message is treated
        as an error, otherwise it is assumed to be a warning.
    } \
    str lintcmd

declare-option -hidden line-specs lint_flags
declare-option -hidden line-specs lint_messages
declare-option -hidden int lint_error_count
declare-option -hidden int lint_warning_count

define-command \
    -hidden \
    -params 1 \
    -docstring %{
        lint-cleaned-selections <linter>: Check each selection with <linter>.

        Assumes selections all have anchor before cursor, and that
        %val{selections} and %val{selections_desc} are in the same order.
    } \
    lint-cleaned-selections \
%{
    # Clear the current contents of the various options.
    set-option buffer lint_flags %val{timestamp}
    set-option buffer lint_messages %val{timestamp}
    set-option buffer lint_error_count 0
    set-option buffer lint_warning_count 0

    # Create a temporary directory to keep all our state.
    evaluate-commands %sh{
        # This is going to come in handy later.
        kakquote() { printf "%s" "$*" | sed "s/'/''/g; 1s/^/'/; \$s/\$/'/"; }

        # Before we clobber our arguments,
        # let's record the lintcmd we were given.
        lintcmd="$1"

        # Some linters care about the name or extension
        # of the file being linted, so we'll store the text we want to lint
        # in a file with the same name as the original buffer.
        filename="${kak_buffile##*/}"

        # A directory to keep all our temporary data.
        dir=$(mktemp -d "${TMPDIR:-/tmp}"/kak-lint.XXXXXXXX)

        # A fifo to send the results back to a Kakoune buffer.
        # FIXME: Should we put the lint output in toolsclient?
        mkfifo "$dir"/fifo
        printf '%s\n' "evaluate-commands -draft %{
                  edit! -fifo $(kakquote "$dir/fifo") -debug *lint-output*
                  set-option buffer filetype make
                  set-option buffer make_current_error_line 0
              }"

        # Write all the selection descriptions to files.
        eval set -- "$kak_selections_desc"
        i=0
        for desc; do
            mkdir -p "$dir"/sel-"$i"
            printf "%s" "$desc" > "$dir"/sel-$i/desc
            i=$(( i + 1 ))
        done

        # Write all the selection contents to files.
        eval set -- "$kak_quoted_selections"
        i=0
        for text; do
            # The selection text needs to be stored in a subdirectory,
            # so we can be sure the filename won't clash with one of ours.
            mkdir -p "$dir"/sel-"$i"/text/
            printf "%s" "$text" > "$dir"/sel-$i/text/"$filename"
            i=$(( i + 1 ))
        done

        # We do redirection trickiness to record stderr from
        # this background task and route it back to Kakoune,
        # but shellcheck isn't a fan.
        # shellcheck disable=SC2094
        ({ # do the parsing in the background and when ready send to the session

        for selpath in "$dir"/sel-*; do
            # Read in the line and column offset of this selection.
            IFS=".," read -r start_line start_byte _ < "$selpath"/desc

            # Run the linter, and record the exit-code.
            eval "$lintcmd '$selpath/text/$filename'" |
                    sort -t: -k2,2 -n |
                    awk \
                        -v line_offset=$(( start_line - 1 )) \
                        -v first_line_byte_offset=$(( start_byte - 1 )) \
                    '
                        BEGIN { OFS=":"; FS=":" }

                        /:[1-9][0-9]*:[1-9][0-9]*:/ {
                            $1 = ENVIRON["kak_bufname"]
                            if ( $2 == 1 ) {
                                $3 += first_line_byte_offset
                            }
                            $2 += line_offset
                            print $0
                        }
                    ' >>"$dir"/result
        done

        # Load all the linter messages into Kakoune options.
        # Inside this block, shellcheck warns us that the shell doesn't
        # need backslash-continuation chars in a single-quoted string,
        # but awk still needs them.
        # shellcheck disable=SC1004
        awk -v file="$kak_buffile" -v client="$kak_client" '
            function kakquote(text) {
                # \x27 is apostrophe, escaped for shell-quoting reasons.
                gsub(/\x27/, "\x27\x27", text)
                return "\x27" text "\x27"
            }

            BEGIN {
                OFS=":"
                FS=":"
                error_count = 0
                warning_count = 0
            }

            /:[1-9][0-9]*:[1-9][0-9]*:/ {
                # Remember that an error or a warning occurs on this line..
                if ($4 ~ /[Ee]rror/) {
                    # We definitely have an error on this line.
                    flags_by_line[$2] = "{Error}!"
                    error_count++
                } else if (flags_by_line[$2] ~ /Error/) {
                    # We have a warning on this line,
                    # but we already have an error, so do nothing.
                    warning_count++
                } else {
                    # We have a warning on this line,
                    # and no previous error.
                    flags_by_line[$2] = "{Information}?"
                    warning_count++
                }

                # The message starts with the severity indicator.
                msg = substr($4, 2)

                # fix case where $5 is not the last field
                # because of extra colons in the message
                for (i=5; i<=NF; i++) msg = msg ":" $i

                # Mention the column where this problem occurs,
                # so that information is not lost.
                msg = msg "(col " $3 ")"

                # Messages will be stored in a line-specs option,
                # and each record in the option uses "|"
                # as a field delimiter, so we need to escape them.
                gsub(/\|/, "\\|", msg)

                if ($2 in messages_by_line) {
                    # We already have a message on this line,
                    # so append our new message.
                    messages_by_line[$2] = messages_by_line[$2] "\n" msg
                } else {
                    # A brand-new message on this line.
                    messages_by_line[$2] = msg
                }
            }

            END {
                for (line in flags_by_line) {
                    flag = flags_by_line[line]

                    print "set-option -add " \
                        kakquote("buffer=" file) " " \
                        "lint_flags " \
                        kakquote(line "|" flag)
                }

                for (line in messages_by_line) {
                    msg = messages_by_line[line]

                    print "set-option -add " \
                        kakquote("buffer=" file) " " \
                        "lint_messages " \
                        kakquote(line "|" msg)
                }

                print "set-option " \
                    kakquote("buffer=" file) " " \
                    "lint_error_count " \
                    error_count
                print "set-option " \
                    kakquote("buffer=" file) " " \
                    "lint_warning_count " \
                    warning_count
            }
        ' "$dir"/result | kak -p "$kak_session"

        # Send any linting errors to the debug buffer,
        # for visibility.
        if [ -s "$dir"/stderr ]; then
            # Errors were detected!"
            printf "echo -debug Linter errors: <<<\n"
            while read -r LINE; do
                printf "echo -debug %s\n" "$(kakquote "  $LINE")"
            done < "$dir"/stderr
            printf "echo -debug >>>\n"
            # FIXME: When #3254 is fixed, this can become a "fail"
            printf "eval -client %s echo -markup {Error}%s\n" \
                "$kak_client" \
                "lint failed, see *debug* for details"
        else
            # No errors detected, show the results.
            printf "eval -client %s lint-show-counters" \
                "$kak_client"
        fi | kak -p "$kak_session"

        # We are done here. Send the results to Kakoune,
        # and clean up.
        cat "$dir"/result > "$dir"/fifo
        rm -rf "$dir"

        } & ) >"$dir"/stderr 2>&1 </dev/null
    }
}

define-command \
    -params 0..2 \
    -docstring %{
        lint-selections [<switches>]: Check each selection with a linter.

        Switches:
            -command <cmd>      Use the given linter.
                                If not given, the lintcmd option is used.
    } \
    lint-selections \
%{
    evaluate-commands -draft %{
        # Make sure all the selections are "forward" (anchor before cursor)
        execute-keys <a-:>

        # Make sure the selections are in document order.
        evaluate-commands %sh{
            printf "select "
            printf "%s\n" "$kak_selections_desc" |
                tr ' ' '\n' |
                sort -n -t. |
                tr '\n' ' '
        }

        evaluate-commands %sh{
            # This is going to come in handy later.
            kakquote() { printf "%s" "$*" | sed "s/'/''/g; 1s/^/'/; \$s/\$/'/"; }

            if [ "$1" = "-command" ]; then
                if [ -z "$2" ]; then
                    echo 'fail -- -command option requires a value'
                    exit 1
                fi
                lintcmd="$2"
            elif [ -n "$1" ]; then
                echo "fail -- Unrecognised parameter $(kakquote "$1")"
                exit 1
            elif [ -z "${kak_opt_lintcmd}" ]; then
                echo 'fail The lintcmd option is not set'
                exit 1
            else
                lintcmd="$kak_opt_lintcmd"
            fi

            printf 'lint-cleaned-selections %s\n' "$(kakquote "$lintcmd")"
        }
    }
}

define-command \
    -docstring %{
        lint-buffer: Check the current buffer with a linter.

        Set the lintcmd option to control which linter is used.
    } \
    lint-buffer \
%{
    evaluate-commands -draft %{
        execute-keys '%'
        lint-cleaned-selections %opt{lintcmd}
    }
}

alias global lint lint-buffer

define-command -hidden lint-show %{
    update-option buffer lint_messages
    evaluate-commands %sh{
        # This is going to come in handy later.
        kakquote() { printf "%s" "$*" | sed "s/'/''/g; 1s/^/'/; \$s/\$/'/"; }

        eval set -- "${kak_quoted_opt_lint_messages}"
        shift # skip the timestamp

        while [ $# -gt 0 ]; do
            lineno=${1%%|*}
            msg=${1#*|}

            if [ "$lineno" -eq "$kak_cursor_line" ]; then
                printf "info -anchor %d.%d %s\n" \
                    "$kak_cursor_line" \
                    "$kak_cursor_column" \
                    "$(kakquote "$msg")"
                break
            fi
            shift
        done
    }
}

define-command -hidden lint-show-counters %{
    echo -markup "linting results: {Error} %opt{lint_error_count} error(s) {Information} %opt{lint_warning_count} warning(s) "
}

define-command lint-enable -docstring "Activate automatic diagnostics of the code" %{
    add-highlighter window/lint flag-lines default lint_flags
    hook window -group lint-diagnostics NormalIdle .* %{ lint-show }
    hook window -group lint-diagnostics WinSetOption lint_flags=.* %{ info; lint-show }
}

define-command lint-disable -docstring "Disable automatic diagnostics of the code" %{
    remove-highlighter window/lint
    remove-hooks window lint-diagnostics
}

# FIXME: Is there some way we can re-use make-next-error
# instead of re-implementing it?
define-command \
    -docstring "Jump to the next line that contains a lint message" \
    lint-next-message \
%{
    update-option buffer lint_messages

    evaluate-commands %sh{
        # This is going to come in handy later.
        kakquote() { printf "%s" "$*" | sed "s/'/''/g; 1s/^/'/; \$s/\$/'/"; }

        eval "set -- ${kak_quoted_opt_lint_messages}"
        shift

        if [ "$#" -eq 0 ]; then
            printf 'fail no lint messages'
            exit
        fi

        for lint_message; do
            lineno="${lint_message%%|*}"
            msg="${lint_message#*|}"

            if [ "$lineno" -gt "$kak_cursor_line" ]; then
                printf "execute-keys %dg\n" "$lineno"
                printf "info -anchor %d.%d %s\n" \
                    "$lineno" "1" "$(kakquote "$msg")"
                break
            fi
        done

        # FIXME: should we wrap around like make-next-error?
    }
}

# FIXME: Is there some way we can re-use make-previous-error
# instead of re-implementing it?
define-command \
    -docstring "Jump to the previous line that contains a lint message" \
    lint-previous-message \
%{
    update-option buffer lint_messages

    evaluate-commands %sh{
        # This is going to come in handy later.
        kakquote() { printf "%s" "$*" | sed "s/'/''/g; 1s/^/'/; \$s/\$/'/"; }

        eval "set -- ${kak_quoted_opt_lint_messages}"
        shift

        if [ "$#" -eq 0 ]; then
            printf 'fail no lint messages'
            exit
        fi

        last_lineno="$kak_cursor_line"
        last_msg="no previous message"

        for lint_message; do
            lineno="${lint_message%%|*}"
            msg="${lint_message#*|}"

            if [ "$lineno" -ge "${kak_cursor_line}" ]; then
                printf "execute-keys %dg\n" "$last_lineno"
                printf "info -anchor %d.%d %s\n" \
                    "$lineno" "1" "$(kakquote "$last_msg")"
                break
            fi

            last_lineno="$last_lineno"
            last_msg="$msg"
        done

        # FIXME: should we wrap around like make-previous-error?
    }
}
