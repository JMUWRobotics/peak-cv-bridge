#include "genicvbridge.hpp"
#include "genicvstream_server.hpp"
#include <cctype>

// clang-format off

constexpr uint16_t    DEFAULT_PORT        = 8888;
constexpr uint        DEFAULT_CAMIDX      = 0;
constexpr const char* DEFAULT_COMPRESSION = ".jpg";
constexpr double      DEFAULT_FRAMERATE   = 3.0;
constexpr size_t      DEFAULT_MAXQUEUE    = 10;

// clang-format on

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
    std::string compression_ext = DEFAULT_COMPRESSION, backend_str;
    double target_fps = DEFAULT_FRAMERATE;
    uint camera_index = DEFAULT_CAMIDX;
    uint16_t port = DEFAULT_PORT;
    size_t max_queue = DEFAULT_MAXQUEUE;
    XVII::GenICamVideoCapture::Backend backend =
      XVII::GenICamVideoCapture::Backend::IDS_PEAK;

    if (const auto env = std::getenv("STREAMSERVER_COMPRESSIONEXT");
        env != nullptr)
        compression_ext = env;

    if (const auto env = std::getenv("STREAMSERVER_FPS"); env != nullptr)
        target_fps = std::stod(env);

    if (const auto env = std::getenv("STREAMSERVER_CAMIDX"); env != nullptr)
        camera_index = static_cast<uint>(std::stoul(env));

    if (const auto env = std::getenv("STREAMSERVER_PORT"); env != nullptr)
        port = static_cast<uint16_t>(std::stoul(env));

    if (const auto env = std::getenv("STREAMSERVER_MAXQUEUE"); env != nullptr)
        max_queue = std::stoull(env);

    if (const auto env = std::getenv("STREAMSERVER_BACKEND"); env != nullptr) {
        backend_str = env;
        std::transform(backend_str.begin(),
                       backend_str.end(),
                       backend_str.begin(),
                       ::tolower);
        if (backend_str == "any")
            throw std::invalid_argument(
              "Backend for streamserver can't be 'any'");
        else if (backend_str == "spinnaker")
            backend = XVII::GenICamVideoCapture::Backend::SPINNAKER;
        else if (backend_str == "ids")
            backend = XVII::GenICamVideoCapture::Backend::IDS_PEAK;
        else if (backend_str == "aravis")
            backend = XVII::GenICamVideoCapture::Backend::ARAVIS;
    }

    XVII::StreamServer streamServer(
      backend, camera_index, max_queue, compression_ext, target_fps);

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