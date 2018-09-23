hook global BufCreate .*(COMMIT_EDITMSG|MERGE_MSG) %{
    set-option buffer filetype git-commit
}

hook global BufCreate .*(\.gitconfig|git/config) %{
    set-option buffer filetype ini
}

hook -group git-commit-highlight global WinSetOption filetype=git-commit %{
    add-highlighter window/git-commit-highlight regions
    add-highlighter window/git-commit-highlight/diff region '^diff --git' '^(?=diff --git)' ref diff # highlight potential diffs from the -v option
    add-highlighter window/git-commit-highlight/comments region '^\h*#' '$' group 
    add-highlighter window/git-commit-highlight/comments/ fill cyan,default
    add-highlighter window/git-commit-highlight/comments/ regex "\b(?:(modified)|(deleted)|(new file)|(renamed|copied)):([^\n]*)$" 1:yellow 2:red 3:green 4:blue 5:magenta
}

hook -group git-commit-highlight global WinSetOption filetype=(?!git-commit).* %{ remove-highlighter window/git-commit-highlight }

hook global BufCreate .*git-rebase-todo %{
    set-option buffer filetype git-rebase
}

hook -group git-rebase-highlight global WinSetOption filetype=git-rebase %{
    add-highlighter window/git-rebase-highlight group
    add-highlighter window/git-rebase-highlight/ regex "#[^\n]*\n" 0:cyan,default
    add-highlighter window/git-rebase-highlight/ regex "^(pick|edit|reword|squash|fixup|exec|drop|label|reset|merge|[persfxdltm]) (\w+)" 1:green 2:magenta
}

hook -group git-rebase-highlight global WinSetOption filetype=(?!git-rebase).* %{ remove-highlighter window/git-rebase-highlight }
