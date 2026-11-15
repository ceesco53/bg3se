// Console access for a game that has no terminal.
//
// The Windows build opens a console window next to the game. On macOS a game started from Steam
// or Finder has no terminal at all, so the extender console is also served on a Unix domain socket:
//
//     ~/Library/Application Support/BG3ScriptExtender/console.sock
//
// Each line received is run as a console command (same syntax as the Windows console: Lua code,
// `server`, `client`, `reset`, `!command`, ...) and everything the console prints is sent back.
// Connect with macOS/scripts/bg3se-console.sh. The socket is only created when the console is
// enabled, and is only accessible to the current user.
#include "stdafx.h"
#include <Extender/ScriptExtender.h>
#include <Extender/Shared/Console.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

BEGIN_NS(darwin)

namespace {

std::mutex gClientsMutex;
std::vector<int> gClients;

void BroadcastLine(DebugMessageType type, char const* message)
{
    std::lock_guard lock(gClientsMutex);
    for (auto it = gClients.begin(); it != gClients.end(); ) {
        std::string line(message);
        line += '\n';
        if (send(*it, line.data(), line.size(), 0) < 0) {
            close(*it);
            it = gClients.erase(it);
        } else {
            ++it;
        }
    }
}

void ServeClient(int client)
{
    {
        std::lock_guard lock(gClientsMutex);
        gClients.push_back(client);
    }

    auto console = static_cast<DebugConsole*>(gCoreLibPlatformInterface.GlobalConsole);
    std::string pending;
    char buf[4096];
    for (;;) {
        auto received = recv(client, buf, sizeof(buf), 0);
        if (received <= 0) break;

        pending.append(buf, static_cast<size_t>(received));
        size_t newline;
        while ((newline = pending.find('\n')) != std::string::npos) {
            auto line = pending.substr(0, newline);
            pending.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            // Blocks until the command has run on the game thread it targets
            console->HandleCommand(line);
        }
    }

    std::lock_guard lock(gClientsMutex);
    auto it = std::find(gClients.begin(), gClients.end(), client);
    if (it != gClients.end()) {
        close(client);
        gClients.erase(it);
    }
}

void AcceptLoop(int listener)
{
    for (;;) {
        auto client = accept(listener, nullptr, nullptr);
        if (client < 0) {
            if (errno == EINTR) continue;
            return;
        }

        // A client that disconnects mid-write must not take the game down with SIGPIPE
        int on = 1;
        setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
        std::thread(ServeClient, client).detach();
    }
}

}

void StartConsoleSocket()
{
    auto path = GetExtenderDataDir() + "/console.sock";

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) {
        ERR("Console socket path is too long: %s", path.c_str());
        return;
    }
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    auto listener = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listener < 0) return;

    unlink(path.c_str());
    auto oldMask = umask(0077);
    bool bound = bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
    umask(oldMask);
    if (!bound || listen(listener, 4) != 0) {
        ERR("Could not open the console socket at %s", path.c_str());
        close(listener);
        return;
    }

    gCoreLibPlatformInterface.GlobalConsole->SetLogCallback(&BroadcastLine);
    std::thread(AcceptLoop, listener).detach();
    INFO("Console socket: %s", path.c_str());
}

END_NS()
