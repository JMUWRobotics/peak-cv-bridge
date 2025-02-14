#include "stream_server.hpp"
#include "lib.hpp"

#include <fmt/core.h>
#include <functional>
#include <iterator>
#include <opencv2/imgcodecs.hpp>

using namespace XVII;

template<>
struct fmt::formatter<asio::ip::tcp::endpoint> : formatter<string_view>
{
    auto format(const asio::ip::tcp::endpoint& ep, format_context& ctx) const
      -> format_context::iterator
    {
        if (ep.address().is_v6())
            return format_to(
              ctx.out(), "[{}]:{}", ep.address().to_string(), ep.port());
        else
            return format_to(
              ctx.out(), "{}:{}", ep.address().to_string(), ep.port());
    }
};

template<>
struct fmt::formatter<StreamingStatus> : formatter<string_view>
{
    auto format(const StreamingStatus& status, format_context& ctx) const
      -> format_context::iterator
    {
        const char* str;
        switch (status) {
            case StreamingStatus::IDLE:
                str = "idle";
                break;
            case StreamingStatus::STARTING:
                str = "starting";
                break;
            case StreamingStatus::STREAMING:
                str = "streaming";
                break;
            case StreamingStatus::NOT_STREAMING:
                str = "not streaming";
                break;
            case StreamingStatus::ERROR_CAPTURE_IN_USE:
                str = "capture in use";
                break;
            case StreamingStatus::ERROR_UNKNOWN:
                str = "unknown error";
                break;
            default:
                throw "not implemented";
        }
        return format_to(ctx.out(), "{}", str);
    }
};

#define LOG(format, ...) fmt::println("{} -> " format, conn, ##__VA_ARGS__)

size_t
StreamServer::n_subscribers()
{
    _subscribersMutex.lock();

    size_t n = _subscribers.size();

    _subscribersMutex.unlock();
    return n;
}

void
StreamServer::remove_subscriber(WsConnHandle subscriber)
{
    _subscribersMutex.lock();

    _subscribers.erase(subscriber);

    _subscribersMutex.unlock();
}

void
StreamServer::on_close(WsConn conn, int status, const std::string& reason)
{
    LOG("closed. status: '{}'; reason: '{}'", status, reason);

    remove_subscriber(conn);
}

void
StreamServer::add_subscriber(WsConnHandle subscriber)
{
    _subscribersMutex.lock();

    _subscribers.insert(subscriber);

    _subscribersMutex.unlock();
}

void
StreamServer::capture_thread()
{
#define sleep(ms) std::this_thread::sleep_for(std::chrono::milliseconds((ms)))

    auto compression = _compressionExt.value_or(".jpg");
    auto targetFps = _targetFps.value_or(10.0);

    _threadStatus.store(StreamingStatus::STARTING);

    cv::PeakVideoCapture capture;

    while (!_shouldThreadStop.test_and_set()) {
        _shouldThreadStop.clear();

        if (n_subscribers() == 0) {
            fmt::println(stderr, "[capture_thread] idle");
            _threadStatus.store(StreamingStatus::IDLE);
            capture.release();
            sleep(1000);
            continue;
        }

        if (!capture.isOpened()) {
            capture.setExceptionMode(true);
            try {
                capture.open((int)_cameraIndex);
            } catch (const cv::Exception& e) {
                if (e.code != cv::Error::StsInternal) {
                    fmt::println(stderr,
                                 "[capture_thread] unexpected exception when "
                                 "opening capture: {}",
                                 e.what());
                    _threadStatus.store(StreamingStatus::ERROR_UNKNOWN);
                    std::exit(1);
                } else {
                    _threadStatus.store(StreamingStatus::ERROR_CAPTURE_IN_USE);
                    continue;
                }
            }
            fmt::println(stderr,
                         "[capture_thread] opened capture at index {}",
                         _cameraIndex);
            capture.setExceptionMode(false);

            if (!capture.set(cv::CAP_PROP_FPS, targetFps))
                fmt::println(stderr,
                             "[capture_thread] setting CAP_PROP_FPS failed");

            if (!capture.set(cv::CAP_PROP_AUTO_EXPOSURE, true))
                fmt::println(
                  stderr,
                  "[capture_thread] setting CAP_PROP_AUTO_EXPOSURE failed");
        }

        _threadStatus.store(StreamingStatus::STREAMING);

        cv::Mat image;
        if (!capture.read(image) || image.empty())
            continue;

        std::shared_ptr<WsServer::OutMessage> payload;
        {
            std::vector<uchar> buffer;
            cv::imencode(compression, image, buffer);
            payload = std::make_shared<WsServer::OutMessage>(buffer.size());
            std::move(buffer.begin(),
                      buffer.end(),
                      std::ostream_iterator<uchar>(*payload));
        }

        auto currentSubscribers = _subscribers;
        for (const auto& handle : currentSubscribers) {
            auto conn = handle.lock();
            if (!conn) {
                remove_subscriber(handle);
                continue;
            }

            auto endpoint = conn->remote_endpoint();
            conn->send(
              payload,
              [this, handle, endpoint](const auto& error) {
                  if (error) {
                      fmt::println("[capture_thread] {} -> send error: {}",
                                   endpoint,
                                   error.message());
                      remove_subscriber(handle);
                  }
              },
              130);
        }
    }
}

void
StreamServer::on_message(WsConn conn, WsMsg message)
{
    auto payload = message->string();

    LOG("message: {}", payload);

    if ("status" == payload) {
        auto status = _threadStatus.load();
        if (status == StreamingStatus::STREAMING)
            conn->send(
              fmt::format("Streaming to {} subscribers", n_subscribers()));
        else
            conn->send(fmt::format("{}", status));
    } else if ("start" == payload) {
        add_subscriber(conn);
    } else if ("stop" == payload) {
        remove_subscriber(conn);
    }
}

StreamServer::StreamServer(uint cameraIndex,
                           std::optional<std::string> compressionExt,
                           std::optional<double> targetFps)
{
    _cameraIndex = cameraIndex;
    _compressionExt = compressionExt;
    _targetFps = targetFps;

    auto& endpoint = _server.endpoint["^/"];

    endpoint.on_message = std::bind(&StreamServer::on_message,
                                    this,
                                    std::placeholders::_1,
                                    std::placeholders::_2);
    endpoint.on_close = std::bind(&StreamServer::on_close,
                                  this,
                                  std::placeholders::_1,
                                  std::placeholders::_2,
                                  std::placeholders::_3);
}

void
StreamServer::run(uint16_t port)
{
    _captureThreadHandle = std::thread(&StreamServer::capture_thread, this);

    _server.config.port = port;
    _server.config.thread_pool_size = 8;
    _server.start([](unsigned short port) {
        fmt::println(stderr, "Server listening on port {}", port);
    });
}

void
StreamServer::stop()
{
    _shouldThreadStop.test_and_set();
    _server.stop_accept();

    if (_captureThreadHandle.joinable())
        _captureThreadHandle.join();

    for (const auto& conn : _server.get_connections())
        conn->send_close(1001, "shutdown");

    _server.stop();
}
