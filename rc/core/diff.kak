hook global BufCreate .*\.(diff|patch) %{
    set buffer filetype diff
}

add-highlighter shared/ group diff
add-highlighter shared/diff regex "^\+[^\n]*\n" 0:green,default
add-highlighter shared/diff regex "^-[^\n]*\n" 0:red,default
add-highlighter shared/diff regex "^@@[^\n]*@@" 0:cyan,default

hook -group diff-highlight global WinSetOption filetype=diff %{ add-highlighter window ref diff }
hook -group diff-highlight global WinSetOption filetype=(?!diff).* %{ remove-highlighter window/diff }
