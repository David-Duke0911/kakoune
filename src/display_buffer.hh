#ifndef display_buffer_hh_INCLUDED
#define display_buffer_hh_INCLUDED

#include <string>
#include <vector>

#include "line_and_column.hh"

#include "buffer.hh"

namespace Kakoune
{

struct DisplayCoord : LineAndColumn<DisplayCoord>
{
    DisplayCoord(int line = 0, int column = 0)
        : LineAndColumn(line, column) {}
};

typedef int Attribute;

enum Attributes
{
    Normal = 0,
    Underline = 1,
    Reverse = 2,
    Blink = 4,
    Bold = 8,
};

enum class Color
{
    Default,
    Black,
    Red,
    Green,
    Yellow,
    Blue,
    Magenta,
    Cyan,
    White
};

struct DisplayAtom
{
    BufferIterator begin;
    BufferIterator end;
    Color          fg_color;
    Color          bg_color;
    Attribute      attribute;
    BufferString   replacement_text;

    DisplayAtom(BufferIterator begin, BufferIterator end,
                Color fg_color = Color::Default,
                Color bg_color = Color::Default,
                Attribute attribute = Attributes::Normal)
        : begin(begin),
          end(end),
          fg_color(fg_color),
          bg_color(bg_color),
          attribute(attribute)
    {}
};

class DisplayBuffer
{
public:
    typedef std::vector<DisplayAtom> AtomList;
    typedef AtomList::iterator iterator;
    typedef AtomList::const_iterator const_iterator;

    DisplayBuffer();

    void clear() { m_atoms.clear(); }
    void append(const DisplayAtom& atom) { m_atoms.push_back(atom); }
    iterator insert(iterator where, const DisplayAtom& atom) { return m_atoms.insert(where, atom); }
    iterator split(iterator atom, const BufferIterator& pos);

    iterator begin() { return m_atoms.begin(); }
    iterator end()   { return m_atoms.end(); }

    const_iterator begin() const { return m_atoms.begin(); }
    const_iterator end()   const { return m_atoms.end(); }

    void check_invariant() const;
private:
    AtomList m_atoms;
};

}

#endif // display_buffer_hh_INCLUDED
