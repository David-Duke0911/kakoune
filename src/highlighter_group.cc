#include "highlighter_group.hh"

namespace Kakoune
{

void HighlighterGroup::highlight(const Context& context, HighlightFlags flags,
                                 DisplayBuffer& display_buffer)
{
    for (auto& hl : m_highlighters)
       hl.second->highlight(context, flags, display_buffer);
}

void HighlighterGroup::add_child(HighlighterAndId&& hl)
{
    if (m_highlighters.contains(hl.first))
        throw runtime_error("duplicate id: " + hl.first);

    m_highlighters.append(std::move(hl));
}

void HighlighterGroup::remove_child(StringView id)
{
    m_highlighters.remove(id);
}

Highlighter& HighlighterGroup::get_child(StringView path)
{
    auto sep_it = find(path, '/');
    StringView id(path.begin(), sep_it);
    auto it = m_highlighters.find(id);
    if (it == m_highlighters.end())
        throw child_not_found("no such id: "_str + id);
    if (sep_it == path.end())
        return *it->second;
    else
        return it->second->get_child({sep_it+1, path.end()});
}

Completions HighlighterGroup::complete_child(StringView path, ByteCount cursor_pos, bool group) const
{
    auto sep_it = find(path, '/');
    if (sep_it != path.end())
    {
        ByteCount offset = sep_it+1 - path.begin();
        Highlighter& hl = const_cast<HighlighterGroup*>(this)->get_child({path.begin(), sep_it});
        return offset_pos(hl.complete_child(path.substr(offset), cursor_pos - offset, group), offset);
    }

    auto condition = [=](const HighlighterMap::value_type& hl)
    {
        return not group || hl.second->has_children();
    };
    return { 0, 0, m_highlighters.complete_id_if(path, cursor_pos, condition) };
}

}
