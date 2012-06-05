#include "ncurses.hh"

#include "window.hh"
#include "register_manager.hh"

#include <ncurses.h>
#include <map>

#define CTRL(x) x - 'a' + 1

namespace Kakoune
{

NCursesUI::NCursesUI()
{
    // setlocale(LC_ALL, "");
    initscr();
    cbreak();
    noecho();
    nonl();
    intrflush(stdscr, false);
    keypad(stdscr, true);
    curs_set(0);
    start_color();
    use_default_colors();
    ESCDELAY=25;
}

NCursesUI::~NCursesUI()
{
    endwin();
}

static void set_attribute(int attribute, bool on)
{
    if (on)
        attron(attribute);
    else
        attroff(attribute);
}

static int nc_color(Color color)
{
    switch (color)
    {
    case Color::Black:   return COLOR_BLACK;
    case Color::Red:     return COLOR_RED;
    case Color::Green:   return COLOR_GREEN;
    case Color::Yellow:  return COLOR_YELLOW;
    case Color::Blue:    return COLOR_BLUE;
    case Color::Magenta: return COLOR_MAGENTA;
    case Color::Cyan:    return COLOR_CYAN;
    case Color::White:   return COLOR_WHITE;

    case Color::Default:
    default:
        return -1;
    }
}

static void set_color(Color fg_color, Color bg_color)
{
    static std::map<std::pair<Color, Color>, int> colorpairs;
    static int current_pair = -1;
    static int next_pair = 1;

    if (current_pair != -1)
        attroff(COLOR_PAIR(current_pair));

    if (fg_color == Color::Default and bg_color == Color::Default)
        return;

    std::pair<Color, Color> colorpair(fg_color, bg_color);
    auto it = colorpairs.find(colorpair);
      if (it != colorpairs.end())
    {
        current_pair = it->second;
        attron(COLOR_PAIR(it->second));
    }
    else
    {
        init_pair(next_pair, nc_color(fg_color), nc_color(bg_color));
        colorpairs[colorpair] = next_pair;
        current_pair = next_pair;
        attron(COLOR_PAIR(next_pair));
        ++next_pair;
    }
}

void NCursesUI::draw_window(Window& window)
{
    int max_x,max_y;
    getmaxyx(stdscr, max_y, max_x);
    max_y -= 1;

    window.set_dimensions(DisplayCoord(max_y, max_x));
    window.update_display_buffer();

    DisplayCoord position;
    for (const DisplayAtom& atom : window.display_buffer())
    {
        assert(position == atom.coord());
        const String content = atom.content();

        set_attribute(A_UNDERLINE, atom.attribute() & Underline);
        set_attribute(A_REVERSE, atom.attribute() & Reverse);
        set_attribute(A_BLINK, atom.attribute() & Blink);
        set_attribute(A_BOLD, atom.attribute() & Bold);

        set_color(atom.fg_color(), atom.bg_color());

        auto pos = content.begin();
        while (true)
        {
            move(position.line, position.column);
            clrtoeol();
            auto end = std::find(pos, content.end(), '\n');
            String line(pos, end);
            addstr(line.c_str());

            if (end != content.end())
            {
                addch(' ');
                position.line = position.line + 1;
                position.column = 0;
                pos = end + 1;

                if (position.line >= max_y)
                    break;
            }
            else
            {
                position.column += line.length();
                break;
            }
        }
        if (position.line >= max_y)
            break;
    }

    set_attribute(A_UNDERLINE, 0);
    set_attribute(A_REVERSE, 0);
    set_attribute(A_BLINK, 0);
    set_attribute(A_BOLD, 0);
    set_color(Color::Blue, Color::Black);
    while (++position.line < max_y)
    {
        move(position.line, 0);
        clrtoeol();
        addch('~');
    }

    set_color(Color::Cyan, Color::Black);
    String status_line = window.status_line();
    static int last_status_length = 0;
    move(max_y, max_x - last_status_length);
    clrtoeol();
    move(max_y, max_x - status_line.length());
    addstr(status_line.c_str());
    last_status_length = status_line.length();
}

Key NCursesUI::get_key()
{
    char c = getch();

    Key::Modifiers modifiers = Key::Modifiers::None;
    if (c > 0 and c < 27)
    {
        modifiers = Key::Modifiers::Control;
        c = c - 1 + 'a';
    }
    else if (c == 27)
    {
        timeout(0);
        char new_c = getch();
        timeout(-1);
        if (new_c != ERR)
        {
            c = new_c;
            modifiers = Key::Modifiers::Alt;
        }
    }
    return Key(modifiers, c);
}

String NCursesUI::prompt(const String& text, Completer completer)
{
    curs_set(2);
    auto restore_cursor = on_scope_end([]() { curs_set(0); });

    int max_x, max_y;
    getmaxyx(stdscr, max_y, max_x);
    move(max_y-1, 0);
    addstr(text.c_str());
    clrtoeol();

    size_t cursor_pos = 0;

    Completions completions;
    int current_completion = -1;
    String text_before_completion;

    String result;
    String saved_result;

    static std::unordered_map<String, std::vector<String>> history_per_prompt;
    std::vector<String>& history = history_per_prompt[text];
    auto history_it = history.end();

    while (true)
    {
        int c = getch();
        switch (c)
        {
        case '\r':
        {
            std::vector<String>::iterator it;
            while ((it = find(history, result)) != history.end())
                history.erase(it);

            history.push_back(result);
            return result;
        }
        case KEY_UP:
            if (history_it != history.begin())
            {
                if (history_it == history.end())
                   saved_result = result;
                --history_it;
                result = *history_it;
                cursor_pos = result.length();
            }
            break;
        case KEY_DOWN:
            if (history_it != history.end())
            {
                ++history_it;
                if (history_it != history.end())
                    result = *history_it;
                else
                    result = saved_result;
                cursor_pos = result.length();
            }
            break;
        case KEY_LEFT:
            if (cursor_pos > 0)
                --cursor_pos;
            break;
        case KEY_RIGHT:
            if (cursor_pos < result.length())
                ++cursor_pos;
            break;
        case KEY_BACKSPACE:
            if (cursor_pos != 0)
            {
                result = result.substr(0, cursor_pos - 1)
                       + result.substr(cursor_pos, String::npos);

                --cursor_pos;
            }

            current_completion = -1;
            break;
        case CTRL('r'):
            {
                c = getch();
                String reg = RegisterManager::instance()[c].get();
                current_completion = -1;
                result = result.substr(0, cursor_pos) + reg + result.substr(cursor_pos, String::npos);
                cursor_pos += reg.length();
            }
            break;
        case 27:
            throw prompt_aborted();
        case '\t':
        {
            if (current_completion == -1)
            {
                completions = completer(result, cursor_pos);
                if (completions.candidates.empty())
                    break;

                text_before_completion = result.substr(completions.start,
                                                       completions.end - completions.start);
            }
            ++current_completion;

            String completion;
            if (current_completion >= completions.candidates.size())
            {
                if (current_completion == completions.candidates.size() and
                    std::find(completions.candidates.begin(), completions.candidates.end(), text_before_completion) == completions.candidates.end())
                    completion = text_before_completion;
                else
                {
                    current_completion = 0;
                    completion = completions.candidates[0];
                }
            }
            else
                completion = completions.candidates[current_completion];

            move(max_y-1, text.length());
            result = result.substr(0, completions.start) + completion;
            cursor_pos = completions.start + completion.length();
            break;
        }
        default:
            current_completion = -1;
            result = result.substr(0, cursor_pos) + (char)c + result.substr(cursor_pos, String::npos);
            ++cursor_pos;
        }

        move(max_y - 1, text.length());
        clrtoeol();
        addstr(result.c_str());
        move(max_y - 1, text.length() + cursor_pos);
        refresh();
    }
    return result;
}

void NCursesUI::print_status(const String& status)
{
    int x,y;
    getmaxyx(stdscr, y, x);
    move(y-1, 0);
    clrtoeol();
    addstr(status.c_str());
}

}
