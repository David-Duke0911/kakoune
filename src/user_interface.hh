#ifndef user_interface_hh_INCLUDED
#define user_interface_hh_INCLUDED

#include "safe_ptr.hh"

#include <functional>
#include <unordered_map>

namespace Kakoune
{

class String;
class DisplayBuffer;
class DisplayLine;
struct CharCoord;
struct Face;
struct Key;
template<typename T> class memoryview;

enum class MenuStyle
{
    Prompt,
    Inline
};

enum class InfoStyle
{
    Prompt,
    Inline,
    InlineAbove,
    InlineBelow,
    MenuDoc
};

using InputCallback = std::function<void()>;

class UserInterface : public SafeCountable
{
public:
    virtual ~UserInterface() {}

    virtual void menu_show(memoryview<String> choices,
                           CharCoord anchor, Face fg, Face bg,
                           MenuStyle style) = 0;
    virtual void menu_select(int selected) = 0;
    virtual void menu_hide() = 0;

    virtual void info_show(StringView title, StringView content,
                           CharCoord anchor, Face face,
                           InfoStyle style) = 0;
    virtual void info_hide() = 0;

    virtual void draw(const DisplayBuffer& display_buffer,
                      const DisplayLine& status_line,
                      const DisplayLine& mode_line) = 0;
    virtual CharCoord dimensions() = 0;
    virtual bool is_key_available() = 0;
    virtual Key  get_key() = 0;

    virtual void refresh() = 0;

    virtual void set_input_callback(InputCallback callback) = 0;

    using Options = std::unordered_map<String, String>;
    virtual void set_ui_options(const Options& options) = 0;
};

}

#endif // user_interface_hh_INCLUDED
