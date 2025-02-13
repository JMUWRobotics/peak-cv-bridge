#include "stream_server.hpp"

#define DEFAULT_PORT 8888
#define DEFAULT_CAMIDX 0
#define DEFAULT_COMPRESSION ".jpg"
#define DEFAULT_FRAMERATE 3

#define XSTR(s) #s
#define STR(s) XSTR(s)

static bool __server_stopped = false;
static std::function<void(void)> __exit_handler;
static void
handle_exit()
{
    __exit_handler();
}

int
main(int argc, char** argv)
{
    std::string compression_ext = DEFAULT_COMPRESSION;
    double target_fps = DEFAULT_FRAMERATE;
    uint camera_index = DEFAULT_CAMIDX;
    uint16_t port = DEFAULT_PORT;

    if (const auto env = std::getenv("STREAMSERVER_COMPRESSIONEXT"); env != nullptr)
        compression_ext = env;

    if (const auto env = std::getenv("STREAMSERVER_FPS"); env != nullptr)
        target_fps = std::stod(env);

    if (const auto env = std::getenv("STREAMSERVER_CAMIDX"); env != nullptr)
        camera_index = static_cast<uint>(std::stoul(env));

    if (const auto env = std::getenv("STREAMSERVER_PORT"); env != nullptr)
        port = static_cast<uint16_t>(std::stoul(env));

    XVII::StreamServer streamServer(camera_index, compression_ext, target_fps);

    __exit_handler = [&streamServer]() {
        if (!__server_stopped) {
            std::cout << "\n\nStopping server\n";
            streamServer.stop();
            __server_stopped = true;
        }
    };

    auto signal_handler = [](int sig) {
        handle_exit();
        std::exit(sig);
    };

    std::atexit(handle_exit);
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    streamServer.run(port);

    return 0;
}