#include "remote.hh"

#include "buffer_manager.hh"
#include "buffer_utils.hh"
#include "client_manager.hh"
#include "command_manager.hh"
#include "display_buffer.hh"
#include "event_manager.hh"
#include "file.hh"
#include "id_map.hh"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pwd.h>
#include <fcntl.h>


namespace Kakoune
{

enum class MessageType
{
    Connect,
    Command,
    MenuShow,
    MenuSelect,
    MenuHide,
    InfoShow,
    InfoHide,
    Draw,
    DrawStatus,
    Refresh,
    SetOptions,
    Key
};

struct socket_error{};

class Message
{
public:
    Message(int sock, MessageType type) : m_socket(sock)
    {
        write(type);
    }

    ~Message() noexcept(false)
    {
        if (m_stream.size() == 0)
            return;
        int res = ::write(m_socket, m_stream.data(), m_stream.size());
        if (res == 0)
            throw peer_disconnected{};
    }

    void write(const char* val, size_t size)
    {
        m_stream.insert(m_stream.end(), val, val + size);
    }

    template<typename T>
    void write(const T& val)
    {
        write((const char*)&val, sizeof(val));
    }

    void write(StringView str)
    {
        write(str.length());
        write(str.data(), (int)str.length());
    };

    void write(const String& str)
    {
        write(StringView{str});
    }

    template<typename T>
    void write(ConstArrayView<T> view)
    {
        write<uint32_t>(view.size());
        for (auto& val : view)
            write(val);
    }

    template<typename T, MemoryDomain domain>
    void write(const Vector<T, domain>& vec)
    {
        write(ConstArrayView<T>(vec));
    }

    template<typename Val, MemoryDomain domain>
    void write(const IdMap<Val, domain>& map)
    {
        write<uint32_t>(map.size());
        for (auto& val : map)
        {
            write(val.key);
            write(val.value);
        }
    }

    void write(Color color)
    {
        write(color.color);
        if (color.color == Color::RGB)
        {
            write(color.r);
            write(color.g);
            write(color.b);
        }
    }

    void write(const DisplayAtom& atom)
    {
        write(atom.content());
        write(atom.face);
    }

    void write(const DisplayLine& line)
    {
        write(line.atoms());
    }

    void write(const DisplayBuffer& display_buffer)
    {
        write(display_buffer.lines());
    }

private:
    Vector<char> m_stream;
    int m_socket;
};

void read(int socket, char* buffer, size_t size)
{
    while (size)
    {
        int res = ::read(socket, buffer, size);
        if (res == 0)
            throw peer_disconnected{};
        if (res < 0)
            throw socket_error{};

        buffer += res;
        size   -= res;
    }
}

template<typename T>
T read(int socket)
{
    union U
    {
        T object;
        alignas(T) char data[sizeof(T)];
        U() {}
        ~U() { object.~T(); }
    } u;
    read(socket, u.data, sizeof(T));
    return u.object;
}

template<>
String read<String>(int socket)
{
    ByteCount length = read<ByteCount>(socket);
    String res;
    if (length > 0)
    {
        res.force_size((int)length);
        read(socket, &res[0_byte], (int)length);
    }
    return res;
}

template<typename T>
Vector<T> read_vector(int socket)
{
    uint32_t size = read<uint32_t>(socket);
    Vector<T> res;
    res.reserve(size);
    while (size--)
        res.push_back(read<T>(socket));
    return res;
}

template<>
Color read<Color>(int socket)
{
    Color res;
    res.color = read<Color::NamedColor>(socket);
    if (res.color == Color::RGB)
    {
        res.r = read<unsigned char>(socket);
        res.g = read<unsigned char>(socket);
        res.b = read<unsigned char>(socket);
    }
    return res;
}

template<>
DisplayAtom read<DisplayAtom>(int socket)
{
    DisplayAtom atom(read<String>(socket));
    atom.face = read<Face>(socket);
    return atom;
}
template<>
DisplayLine read<DisplayLine>(int socket)
{
    return DisplayLine(read_vector<DisplayAtom>(socket));
}

template<>
DisplayBuffer read<DisplayBuffer>(int socket)
{
    DisplayBuffer db;
    db.lines() = read_vector<DisplayLine>(socket);
    return db;
}

template<typename Val, MemoryDomain domain>
IdMap<Val, domain> read_idmap(int socket)
{
    uint32_t size = read<uint32_t>(socket);
    IdMap<Val, domain> res;
    res.reserve(size);
    while (size--)
    {
        auto key = read<String>(socket);
        auto val = read<Val>(socket);
        res.append({std::move(key), std::move(val)});
    }
    return res;
}

class RemoteUI : public UserInterface
{
public:
    RemoteUI(int socket, CharCoord dimensions);
    ~RemoteUI();

    void menu_show(ConstArrayView<DisplayLine> choices,
                   CharCoord anchor, Face fg, Face bg,
                   MenuStyle style) override;
    void menu_select(int selected) override;
    void menu_hide() override;

    void info_show(StringView title, StringView content,
                   CharCoord anchor, Face face,
                   InfoStyle style) override;
    void info_hide() override;

    void draw(const DisplayBuffer& display_buffer,
              const Face& default_face,
              const Face& padding_face) override;

    void draw_status(const DisplayLine& status_line,
                     const DisplayLine& mode_line,
                     const Face& default_face) override;

    void refresh(bool force) override;

    bool is_key_available() override;
    Key  get_key() override;
    CharCoord dimensions() override;

    void set_input_callback(InputCallback callback) override;

    void set_ui_options(const Options& options) override;

private:
    FDWatcher    m_socket_watcher;
    CharCoord m_dimensions;
    InputCallback m_input_callback;
};


RemoteUI::RemoteUI(int socket, CharCoord dimensions)
    : m_socket_watcher(socket, [this](FDWatcher&, EventMode mode) {
                           if (m_input_callback)
                               m_input_callback(mode);
                       }),
      m_dimensions(dimensions)
{
    write_to_debug_buffer(format("remote client connected: {}", m_socket_watcher.fd()));
}

RemoteUI::~RemoteUI()
{
    write_to_debug_buffer(format("remote client disconnected: {}", m_socket_watcher.fd()));
    m_socket_watcher.close_fd();
}

void RemoteUI::menu_show(ConstArrayView<DisplayLine> choices,
                         CharCoord anchor, Face fg, Face bg,
                         MenuStyle style)
{
    Message msg{m_socket_watcher.fd(), MessageType::MenuShow};
    msg.write(choices);
    msg.write(anchor);
    msg.write(fg);
    msg.write(bg);
    msg.write(style);
}

void RemoteUI::menu_select(int selected)
{
    Message msg{m_socket_watcher.fd(), MessageType::MenuSelect};
    msg.write(selected);
}

void RemoteUI::menu_hide()
{
    Message msg{m_socket_watcher.fd(), MessageType::MenuHide};
}

void RemoteUI::info_show(StringView title, StringView content,
                         CharCoord anchor, Face face,
                         InfoStyle style)
{
    Message msg{m_socket_watcher.fd(), MessageType::InfoShow};
    msg.write(title);
    msg.write(content);
    msg.write(anchor);
    msg.write(face);
    msg.write(style);
}

void RemoteUI::info_hide()
{
    Message msg{m_socket_watcher.fd(), MessageType::InfoHide};
}

void RemoteUI::draw(const DisplayBuffer& display_buffer,
                    const Face& default_face,
                    const Face& padding_face)
{
    Message msg{m_socket_watcher.fd(), MessageType::Draw};
    msg.write(display_buffer);
    msg.write(default_face);
    msg.write(padding_face);
}

void RemoteUI::draw_status(const DisplayLine& status_line,
                           const DisplayLine& mode_line,
                           const Face& default_face)
{
    Message msg{m_socket_watcher.fd(), MessageType::DrawStatus};
    msg.write(status_line);
    msg.write(mode_line);
    msg.write(default_face);
}

void RemoteUI::refresh(bool force)
{
    Message msg{m_socket_watcher.fd(), MessageType::Refresh};
    msg.write(force);
}

void RemoteUI::set_ui_options(const Options& options)
{
    Message msg{m_socket_watcher.fd(), MessageType::SetOptions};
    msg.write(options);
}

bool RemoteUI::is_key_available()
{
    return fd_readable(m_socket_watcher.fd());
}

Key RemoteUI::get_key()
{
    try
    {
        const int sock = m_socket_watcher.fd();
        const auto msg = read<MessageType>(sock);
        if (msg != MessageType::Key)
            throw client_removed{ false };

        Key key = read<Key>(sock);
        if (key.modifiers == Key::Modifiers::Resize)
            m_dimensions = key.coord();
        return key;
    }
    catch (peer_disconnected&)
    {
        throw client_removed{ false };
    }
    catch (socket_error&)
    {
        write_to_debug_buffer("ungraceful deconnection detected");
        throw client_removed{ false };
    }
}

CharCoord RemoteUI::dimensions()
{
    return m_dimensions;
}

void RemoteUI::set_input_callback(InputCallback callback)
{
    m_input_callback = std::move(callback);
}

static sockaddr_un session_addr(StringView session)
{
    sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    if (find(session, '/')!= session.end())
        format_to(addr.sun_path, "/tmp/kakoune/{}", session);
    else
        format_to(addr.sun_path, "/tmp/kakoune/{}/{}", getpwuid(geteuid())->pw_name, session);
    return addr;
}

static int connect_to(StringView session)
{
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    fcntl(sock, F_SETFD, FD_CLOEXEC);
    sockaddr_un addr = session_addr(session);
    if (connect(sock, (sockaddr*)&addr, sizeof(addr.sun_path)) == -1)
        throw connection_failed(addr.sun_path);
    return sock;
}

bool check_session(StringView session)
{
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    auto close_sock = on_scope_end([sock]{ close(sock); });
    sockaddr_un addr = session_addr(session);
    return connect(sock, (sockaddr*)&addr, sizeof(addr.sun_path)) != -1;
}

RemoteClient::RemoteClient(StringView session, std::unique_ptr<UserInterface>&& ui,
                           const EnvVarMap& env_vars, StringView init_command)
    : m_ui(std::move(ui))
{
    int sock = connect_to(session);

    {
        Message msg{sock, MessageType::Connect};
        msg.write(init_command);
        msg.write(m_ui->dimensions());
        msg.write(env_vars);
    }

    m_ui->set_input_callback([this](EventMode){ write_next_key(); });

    m_socket_watcher.reset(new FDWatcher{sock, [this](FDWatcher&, EventMode){ process_available_messages(); }});
}

void RemoteClient::process_available_messages()
{
    int socket = m_socket_watcher->fd();
    do {
        process_next_message();
    } while (fd_readable(socket));
}

void RemoteClient::process_next_message()
{
    int socket = m_socket_watcher->fd();
    const auto msg = read<MessageType>(socket);
    switch (msg)
    {
    case MessageType::MenuShow:
    {
        auto choices = read_vector<DisplayLine>(socket);
        auto anchor = read<CharCoord>(socket);
        auto fg = read<Face>(socket);
        auto bg = read<Face>(socket);
        auto style = read<MenuStyle>(socket);
        m_ui->menu_show(choices, anchor, fg, bg, style);
        break;
    }
    case MessageType::MenuSelect:
        m_ui->menu_select(read<int>(socket));
        break;
    case MessageType::MenuHide:
        m_ui->menu_hide();
        break;
    case MessageType::InfoShow:
    {
        auto title = read<String>(socket);
        auto content = read<String>(socket);
        auto anchor = read<CharCoord>(socket);
        auto face = read<Face>(socket);
        auto style = read<InfoStyle>(socket);
        m_ui->info_show(title, content, anchor, face, style);
        break;
    }
    case MessageType::InfoHide:
        m_ui->info_hide();
        break;
    case MessageType::Draw:
    {
        auto display_buffer = read<DisplayBuffer>(socket);
        auto default_face = read<Face>(socket);
        auto padding_face = read<Face>(socket);
        m_ui->draw(display_buffer, default_face, padding_face);
        break;
    }
    case MessageType::DrawStatus:
    {
        auto status_line = read<DisplayLine>(socket);
        auto mode_line = read<DisplayLine>(socket);
        auto default_face = read<Face>(socket);
        m_ui->draw_status(status_line, mode_line, default_face);
        break;
    }
    case MessageType::Refresh:
        m_ui->refresh(read<bool>(socket));
        break;
    case MessageType::SetOptions:
        m_ui->set_ui_options(read_idmap<String, MemoryDomain::Options>(socket));
        break;
    default:
        kak_assert(false);
    }
}

void RemoteClient::write_next_key()
{
    Message msg(m_socket_watcher->fd(), MessageType::Key);
    // do that before checking dimensions as get_key may
    // handle a resize event.
    msg.write(m_ui->get_key());
}

void send_command(StringView session, StringView command)
{
    int sock = connect_to(session);
    auto close_sock = on_scope_end([sock]{ close(sock); });
    Message msg{sock, MessageType::Command};
    msg.write(command);
}


// A client accepter handle a connection until it closes or a nul byte is
// recieved. Everything recieved before is considered to be a command.
//
// * When a nul byte is recieved, the socket is handed to a new Client along
//   with the command.
// * When the connection is closed, the command is run in an empty context.
class Server::Accepter
{
public:
    Accepter(int socket)
        : m_socket_watcher(socket,
                           [this](FDWatcher&, EventMode mode) {
                               if (mode == EventMode::Normal)
                                   handle_available_input();
                           })
    {}

private:
    void handle_available_input()
    {
        const int sock = m_socket_watcher.fd();
        const auto msg = read<MessageType>(sock);
        switch (msg)
        {
            case MessageType::Connect:
            {
                auto init_command = read<String>(sock);
                auto dimensions = read<CharCoord>(sock);
                auto env_vars = read_idmap<String, MemoryDomain::EnvVars>(sock);
                std::unique_ptr<UserInterface> ui{new RemoteUI{sock, dimensions}};
                ClientManager::instance().create_client(std::move(ui),
                                                        std::move(env_vars),
                                                        init_command);
                Server::instance().remove_accepter(this);
                return;
            }
            case MessageType::Command:
            {
                auto command = read<String>(sock);
                if (not command.empty()) try
                {
                    Context context{Context::EmptyContextFlag{}};
                    CommandManager::instance().execute(command, context);
                }
                catch (runtime_error& e)
                {
                    write_to_debug_buffer(format("error running command '{}': {}",
                                                 command, e.what()));
                }
                catch (client_removed&) {}
                close(sock);
                Server::instance().remove_accepter(this);
                return;
            }
            default:
                write_to_debug_buffer("Invalid introduction message received");
                close(sock);
                Server::instance().remove_accepter(this);
                break;
        }
    }

    FDWatcher m_socket_watcher;
};

Server::Server(String session_name)
    : m_session{std::move(session_name)}
{
    int listen_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    fcntl(listen_sock, F_SETFD, FD_CLOEXEC);
    sockaddr_un addr = session_addr(m_session);

    make_directory(split_path(addr.sun_path).first);

    if (bind(listen_sock, (sockaddr*) &addr, sizeof(sockaddr_un)) == -1)
       throw runtime_error(format("unable to bind listen socket '{}'", addr.sun_path));

    if (listen(listen_sock, 4) == -1)
       throw runtime_error(format("unable to listen on socket '{}'", addr.sun_path));

    auto accepter = [this](FDWatcher& watcher, EventMode mode) {
        sockaddr_un client_addr;
        socklen_t   client_addr_len = sizeof(sockaddr_un);
        int sock = accept(watcher.fd(), (sockaddr*) &client_addr,
                          &client_addr_len);
        if (sock == -1)
            throw runtime_error("accept failed");
        fcntl(sock, F_SETFD, FD_CLOEXEC);

        m_accepters.emplace_back(new Accepter{sock});
    };
    m_listener.reset(new FDWatcher{listen_sock, accepter});
}

bool Server::rename_session(StringView name)
{
    String old_socket_file = format("/tmp/kakoune/{}/{}", getpwuid(geteuid())->pw_name, m_session);
    String new_socket_file = format("/tmp/kakoune/{}/{}", getpwuid(geteuid())->pw_name, name);

    if (rename(old_socket_file.c_str(), new_socket_file.c_str()) != 0)
        return false;

    m_session = name.str();
    return true;
}

void Server::close_session(bool do_unlink)
{
    if (do_unlink)
    {
        String socket_file = format("/tmp/kakoune/{}/{}", getpwuid(geteuid())->pw_name, m_session);
        unlink(socket_file.c_str());
    }
    m_listener->close_fd();
    m_listener.reset();
}

Server::~Server()
{
    if (m_listener)
        close_session();
}

void Server::remove_accepter(Accepter* accepter)
{
    auto it = find(m_accepters, accepter);
    kak_assert(it != m_accepters.end());
    m_accepters.erase(it);
}

}
