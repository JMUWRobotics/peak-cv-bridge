#include "genicvbridge.hpp"
#include "genicvstream_server.hpp"

#include <fmt/core.h>

// clang-format off

using Backend = XVII::GenICamVideoCapture::Backend;

constexpr uint16_t            DEFAULT_PORT        = 8888;
constexpr uint                DEFAULT_CAMIDX      = 0;
constexpr const char*         DEFAULT_COMPRESSION = ".jpg";
constexpr double              DEFAULT_FRAMERATE   = 3.0;
constexpr size_t              DEFAULT_MAXQUEUE    = 10;
constexpr Backend             DEFAULT_BACKEND     = Backend::IDS_PEAK;
constexpr bool                DEFAULT_LINEENABLE  = false;
constexpr std::optional<uint> DEFAULT_TRIGGERPIN  = std::nullopt;

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
    Backend backend = DEFAULT_BACKEND;
    std::optional trigger_pin = DEFAULT_TRIGGERPIN;
    bool line_enable = DEFAULT_LINEENABLE;

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

    if (const auto env = std::getenv("STREAMSERVER_LINEENABLE"); env != nullptr)
        line_enable = std::strcmp(env, "0") != 0;

    if (const auto env = std::getenv("STREAMSERVER_TRIGGERPIN"); env != nullptr)
        trigger_pin = static_cast<uint>(std::stoul(env));

    XVII::StreamServer streamServer(backend,
                                    camera_index,
                                    max_queue,
                                    compression_ext,
                                    target_fps,
                                    trigger_pin,
                                    line_enable);

    __exit_handler = [&streamServer]() {
        if (!__server_stopped) {
            fmt::println(stderr, "Stopping server ...");
            streamServer.stop();
            __server_stopped = true;
        }
    };

    auto signal_handler = [](int sig) {
        fmt::println(stderr, "Caught signal: {} ({})", sig, strsignal(sig));
        handle_exit();
    };

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    streamServer.run(port);

    return 0;
}