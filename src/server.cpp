#include "stream_server.hpp"

#include <boost/program_options.hpp>

namespace po = boost::program_options;

static constexpr uint16_t DEFAULT_PORT = 8888;

static bool __server_stopped = false;
static std::function<void(void)> __exit_handler;
static void
handle_exit()
{
    __exit_handler();
}

void
validate_compression(const std::string& value)
{
    if (!value.empty() && value[0] != '.') {
        throw po::validation_error(
          po::validation_error::invalid_option_value, "compression", value);
    }
}

int
main(int argc, char** argv)
{
    std::string compression_ext;
    double target_fps;
    uint16_t port = DEFAULT_PORT;

    if (const auto env = std::getenv("STREAMSERVER_PORT"); env != nullptr) {
        port = static_cast<uint16_t>(std::stoul(env));
    }

    po::options_description desc("Program options");
    desc.add_options()("help", "produce this message")(
      "compression,c",
      po::value<std::string>(&compression_ext)
        ->default_value(".jpg")
        ->notifier(validate_compression),
      "OpenCV compression extension, has to start with '.'")(
      "framerate,f",
      po::value<double>(&target_fps)->default_value(5.0),
      "target fps")(
      "port,p",
      po::value<uint16_t>(&port),
      "port to listen on. if not set, will check the environment "
      "variable STREAMSERVER_PORT, or resort to a default value else.");

    po::variables_map vm;
    try {
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);
    } catch (const po::error& e) {
        std::cerr << "Invalid arguments: " << e.what() << "\nUsage:\n"
                  << desc << '\n';
        return EXIT_FAILURE;
    }

    if (vm.count("help")) {
        std::cout << desc << '\n';
        return EXIT_SUCCESS;
    }

    XVII::StreamServer streamServer(compression_ext, target_fps);

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