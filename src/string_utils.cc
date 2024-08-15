#include "string_utils.hh"

#include "exception.hh"
#include "utf8_iterator.hh"
#include "unit_tests.hh"

namespace Kakoune
{

String trim_indent(StringView str)
{
    if (str.empty())
        return {};

    if (str[0_byte] == '\n')
        str = str.substr(1_byte);
    while (not str.empty() and is_blank(str.back()))
        str = str.substr(0, str.length() - 1);

    utf8::iterator it{str.begin(), str};
    while (it != str.end() and is_horizontal_blank(*it))
        ++it;

    const StringView indent{str.begin(), it.base()};
    return accumulate(str | split_after<StringView>('\n') | transform([&](auto&& line) {
            if (line == "\n")
                return line;
            else if (not prefix_match(line, indent))
                throw runtime_error("inconsistent indentation in the string");

            return line.substr(indent.length());
        }), String{}, [](String s, StringView l) { return s += l; });
}

String escape(StringView str, StringView characters, char escape)
{
    String res;
    res.reserve(str.length());
    auto cbeg = characters.begin(), cend = characters.end();
    for (auto it = str.begin(), end = str.end(); it != end; )
    {
        auto next = std::find_first_of(it, end, cbeg, cend);
        if (next != end)
        {
            res += StringView{it, next+1};
            res.back() = escape;
            res += *next;
            it = next+1;
        }
        else
        {
            res += StringView{it, next};
            break;
        }
    }
    return res;
}

String unescape(StringView str, StringView characters, char escape)
{
    String res;
    res.reserve(str.length());
    for (auto it = str.begin(), end = str.end(); it != end; )
    {
        auto next = std::find(it, end, escape);
        if (next != end and next+1 != end and contains(characters, *(next+1)))
        {
            res += StringView{it, next+1};
            res.back() = *(next+1);
            it = next + 2;
        }
        else
        {
            res += StringView{it, next == end ? next : next + 1};
            it = next == end ? next : next + 1;
        }
    }
    return res;
}

String indent(StringView str, StringView indent)
{
    String res;
    res.reserve(str.length());
    bool was_eol = true;
    for (ByteCount i = 0; i < str.length(); ++i)
    {
        if (was_eol)
            res += indent;
        res += str[i];
        was_eol = is_eol(str[i]);
    }
    return res;
}

String replace(StringView str, StringView substr, StringView replacement)
{
    String res;
    for (auto it = str.begin(); it != str.end(); )
    {
        auto match = std::search(it, str.end(), substr.begin(), substr.end());
        res += StringView{it, match};
        if (match == str.end())
            break;

        res += replacement;
        it = match + substr.length();
    }
    return res;
}

String left_pad(StringView str, ColumnCount size, Codepoint c)
{
    return String(c, std::max(0_col, size - str.column_length())) + str.substr(0, size);
}

String right_pad(StringView str, ColumnCount size, Codepoint c)
{
    return str.substr(0, size) + String(c, std::max(0_col, size - str.column_length()));
}

Optional<int> str_to_int_ifp(StringView str)
{
    bool negative = not str.empty() and str[0] == '-';
    if (negative)
        str = str.substr(1_byte);
    if (str.empty())
        return {};

    unsigned int res = 0;
    for (auto c : str)
    {
        if (c < '0' or c > '9')
            return {};
        res = res * 10 + c - '0';
    }
    return negative ? -res : res;
}

int str_to_int(StringView str)
{
    if (auto val = str_to_int_ifp(str))
        return *val;
    throw runtime_error{str + " is not a number"};
}

bool subsequence_match(StringView str, StringView subseq)
{
    auto it = str.begin();
    for (auto& c : subseq)
    {
        if (it == str.end())
            return false;
        while (*it != c)
        {
            if (++it == str.end())
                return false;
        }
        ++it;
    }
    return true;
}

String expand_tabs(StringView line, ColumnCount tabstop, ColumnCount col)
{
    String res;
    res.reserve(line.length());
    for (auto it = line.begin(), end = line.end(); it != end; )
    {
        if (*it == '\t')
        {
            ColumnCount end_col = (col / tabstop + 1) * tabstop;
            res += String{' ', end_col - col};
            col = end_col;
            ++it;
        }
        else
        {
            auto char_beg = it;
            auto cp = utf8::read_codepoint(it, end);
            res += {char_beg, it};
            col += codepoint_width(cp);
        }
    }
    return res;
}

WrapView::Iterator::Iterator(StringView text, ColumnCount max_width)
  : m_remaining{text}, m_max_width{max_width}
{
    if (max_width <= 0)
        throw runtime_error("Invalid max width");
    ++*this;
}

WrapView::Iterator& WrapView::Iterator::operator++()
{
    using Utf8It = utf8::iterator<const char*>;
    Utf8It it{m_remaining.begin(), m_remaining};
    Utf8It last_word_end = it;

    while (it != m_remaining.end())
    {
        const CharCategories cat = categorize(*it, {'_'});
        if (cat == CharCategories::EndOfLine)
        {
            m_current = StringView{m_remaining.begin(), it.base()};
            m_remaining = StringView{(it+1).base(), m_remaining.end()};
            return *this;
        }

        Utf8It word_end = it+1;
        while (word_end != m_remaining.end() and categorize(*word_end, {'_'}) == cat)
            ++word_end;

        if (word_end > m_remaining.begin() and
            utf8::column_distance(m_remaining.begin(), word_end.base()) >= m_max_width)
        {
            auto line_end = last_word_end <= m_remaining.begin() ?
                Utf8It{utf8::advance(m_remaining.begin(), m_remaining.end(), m_max_width), m_remaining}
              : last_word_end;

            m_current = StringView{m_remaining.begin(), line_end.base()};

            while (line_end != m_remaining.end() and is_horizontal_blank(*line_end))
                ++line_end;

            if (line_end != m_remaining.end() and *line_end == '\n')
                ++line_end;

            m_remaining = StringView{line_end.base(), m_remaining.end()};
            return *this;
        }
        if (cat == CharCategories::Word or cat == CharCategories::Punctuation)
            last_word_end = word_end;

        if (word_end > m_remaining.begin())
            it = word_end;
    }
    m_current = m_remaining;
    m_remaining = StringView{};
    return *this;
}

String double_up(StringView s, StringView characters)
{
    String res;
    auto pos = s.begin();
    for (auto it = s.begin(), end = s.end(); it != end; ++it)
    {
        if (contains(characters, *it))
        {
            res += StringView{pos, it+1};
            res += *it;
            pos = it+1;
        }
    }
    res += StringView{pos, s.end()};
    return res;
}

UnitTest test_string{[]()
{
    kak_assert(String("youpi ") + "matin" == "youpi matin");

    kak_assert(StringView{"youpi"}.starts_with(""));
    kak_assert(StringView{"youpi"}.starts_with("you"));
    kak_assert(StringView{"youpi"}.starts_with("youpi"));
    kak_assert(not StringView{"youpi"}.starts_with("youpi!"));

    kak_assert(StringView{"youpi"}.ends_with(""));
    kak_assert(StringView{"youpi"}.ends_with("pi"));
    kak_assert(StringView{"youpi"}.ends_with("youpi"));
    kak_assert(not StringView{"youpi"}.ends_with("oup"));

    auto wrapped = "wrap this paragraph\n respecting whitespaces and much_too_long_words" | wrap_at(16) | gather<Vector<String>>();
    kak_assert(wrapped.size() == 6);
    kak_assert(wrapped[0] == "wrap this");
    kak_assert(wrapped[1] == "paragraph");
    kak_assert(wrapped[2] == " respecting");
    kak_assert(wrapped[3] == "whitespaces and");
    kak_assert(wrapped[4] == "much_too_long_wo");
    kak_assert(wrapped[5] == "rds");

    auto wrapped2 = "error: unknown type" | wrap_at(7) | gather<Vector<String>>();
    kak_assert(wrapped2.size() == 3);
    kak_assert(wrapped2[0] == "error:");
    kak_assert(wrapped2[1] == "unknown");
    kak_assert(wrapped2[2] == "type");

    kak_assert(trim_indent(" ") == "");
    kak_assert(trim_indent("no-indent") == "no-indent");
    kak_assert(trim_indent("\nno-indent") == "no-indent");
    kak_assert(trim_indent("\n  indent\n  indent") == "indent\nindent");
    kak_assert(trim_indent("\n  indent\n    indent") == "indent\n  indent");
    kak_assert(trim_indent("\n  indent\n  indent\n   ") == "indent\nindent");

    kak_expect_throw(runtime_error, trim_indent("\n  indent\nno-indent"));

    kak_assert(escape(R"(\youpi:matin:tchou\:)", ":\\", '\\') == R"(\\youpi\:matin\:tchou\\\:)");
    kak_assert(unescape(R"(\\youpi\:matin\:tchou\\\:)", ":\\", '\\') == R"(\youpi:matin:tchou\:)");

    kak_assert(prefix_match("tchou kanaky", "tchou"));
    kak_assert(prefix_match("tchou kanaky", "tchou kanaky"));
    kak_assert(prefix_match("tchou kanaky", "t"));
    kak_assert(not prefix_match("tchou kanaky", "c"));

    kak_assert(subsequence_match("tchou kanaky", "tknky"));
    kak_assert(subsequence_match("tchou kanaky", "knk"));
    kak_assert(subsequence_match("tchou kanaky", "tchou kanaky"));
    kak_assert(not subsequence_match("tchou kanaky", "tchou  kanaky"));

    kak_assert(format("Youhou {1} {} '{0:4}' {2:04} \\{}", 10, "hehe", 5) == "Youhou hehe 5 '  10' 0005 {}");

    char buffer[20];
    kak_assert(format_to(buffer, "Hey {}", 15) == "Hey 15");

    kak_assert(str_to_int("5") == 5);
    kak_assert(str_to_int(to_string(INT_MAX)) == INT_MAX);
    kak_assert(str_to_int(to_string(INT_MIN)) == INT_MIN);
    kak_assert(str_to_int("00") == 0);
    kak_assert(str_to_int("-0") == 0);

    kak_assert(double_up(R"('foo%"bar"')", "'\"%") == R"(''foo%%""bar""'')");

    kak_assert(replace("tchou/tcha/tchi", "/", "!!") == "tchou!!tcha!!tchi");
}};

}
