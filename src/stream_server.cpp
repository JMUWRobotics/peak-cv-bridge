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

#define LOG(format, ...)                                                       \
    fmt::println(stderr, "{} -> " format, endpoint, ##__VA_ARGS__)

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
StreamServer::add_subscriber(WsConnHandle subscriber)
{
    _subscribersMutex.lock();

    _subscribers.insert(subscriber);

    _subscribersMutex.unlock();

    _captureThreadCondition.notify_one();
}

HandleSet
StreamServer::get_subscribers()
{
    _subscribersMutex.lock();

    auto ret = _subscribers;

    _subscribersMutex.unlock();

    return ret;
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
            if (StreamingStatus::IDLE != _threadStatus.load())
                fmt::println(stderr, "[capture_thread] idle");

            _threadStatus.store(StreamingStatus::IDLE);
            capture.release();

            std::unique_lock lock(_captureThreadConditionMutex);
            _captureThreadCondition.wait(lock);

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
                } else
                    _threadStatus.store(StreamingStatus::ERROR_CAPTURE_IN_USE);

                continue;
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

        auto currentSubscribers = get_subscribers();
        for (const auto& handle : currentSubscribers) {
            auto conn = handle.lock();
            if (!conn) {
                remove_subscriber(handle);
                continue;
            }

            auto endpoint = conn->remote_endpoint();

            if (conn->queue_size() > _connMaxQueue) {
                fmt::println(
                  stderr,
                  "[capture_thread] {} -> closing connection after {} "
                  "unsent messages",
                  endpoint,
                  _connMaxQueue);
                conn->send_close(1011, "queue full");
                remove_subscriber(handle);
                continue;
            }

            conn->send(
              payload,
              [this, handle, endpoint](const auto& error) {
                  if (error) {
                      fmt::println(stderr,
                                   "[capture_thread] {} -> send error: {}",
                                   endpoint,
                                   error.message());
                      remove_subscriber(handle);
                  }
              },
              130);
        }
    }
}

StreamServer::StreamServer(uint cameraIndex,
                           size_t connMaxQueue,
                           std::optional<std::string> compressionExt,
                           std::optional<double> targetFps)
{
    _cameraIndex = cameraIndex;
    _connMaxQueue = connMaxQueue;
    _compressionExt = compressionExt;
    _targetFps = targetFps;

    auto& endpoint = _server.endpoint["^/"];

    endpoint.on_message = [this](WsConn conn, WsMsg message) {
        auto payload = message->string();
        auto endpoint = conn->remote_endpoint();

        LOG("message: {}", payload);

        if ("status" == payload) {
            auto status = _threadStatus.load();
            if (status == StreamingStatus::STREAMING)
                conn->send(
                  fmt::format("streaming to {} subscribers", n_subscribers()));
            else
                conn->send(fmt::format("{}", status));
        } else if ("start" == payload)
            add_subscriber(conn);
        else if ("stop" == payload)
            remove_subscriber(conn);
    };
    endpoint.on_close =
      [this](WsConn conn, int status, const std::string& reason) {
          auto endpoint = conn->remote_endpoint();
          LOG("closed: '{}' ({})", reason, status);
          remove_subscriber(conn);
      };
    endpoint.on_error = [this](WsConn conn, const auto& error_code) {
        auto endpoint = conn->remote_endpoint();
        LOG("error: {}", error_code.message());
        remove_subscriber(conn);
    };
}

void
StreamServer::run(uint16_t port)
{
    _captureThreadHandle = std::thread(&StreamServer::capture_thread, this);

    _server.config.port = port;
    _server.config.thread_pool_size = sysconf(_SC_NPROCESSORS_ONLN);
    _server.config.max_message_size = UINT8_MAX;
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
