#include "stream_server.hpp"
#include "lib.hpp"

#include <fmt/core.h>
#include <functional>
#include <iterator>
#include <opencv2/imgcodecs.hpp>

using namespace XVII;

#define LOG(format, ...)                                                       \
    fmt::println(stderr,                                                       \
                 "{}:{}\t| " format,                                           \
                 handle->remote_endpoint().address().to_string(),              \
                 handle->remote_endpoint().port(),                             \
                 ##__VA_ARGS__)

static std::string
stringify(StreamingStatus status)
{
    switch (status) {
        case StreamingStatus::IDLE:
            return "idle";
        case StreamingStatus::STARTING:
            return "starting";
        case StreamingStatus::STREAMING:
            return "streaming";
        case StreamingStatus::NOT_STREAMING:
            return "not streaming";
        case StreamingStatus::ERROR_CAPTURE_IN_USE:
            return "capture in use";
        case StreamingStatus::ERROR_UNKNOWN:
            return "unknown error";
        default:
            throw "not implemented";
    }
}

bool
StreamServer::has_subscribers()
{
    _subscribersMutex.lock();

    auto has_subscribers = !_subscribers.empty();

    _subscribersMutex.unlock();
    return has_subscribers;
}

void
StreamServer::remove_subscriber(WsConnHandle subscriber)
{
    _subscribersMutex.lock();

    _subscribers.erase(subscriber);

    _subscribersMutex.unlock();
}

void
StreamServer::on_close(WsConn handle, int status, const std::string& reason)
{
    LOG("closed, status: {}, reason: {}", status, reason);

    remove_subscriber(handle);
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

        if (!has_subscribers()) {
            if (_threadStatus.load() != StreamingStatus::IDLE)
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
            if (auto conn = handle.lock()) {
                conn->send(
                  payload,
                  [&](const auto& error) {
                      if (error) {
                          fmt::println("[capture_thread] send error: {}",
                                       error.message());
                          remove_subscriber(conn);
                      }
                  },
                  130);
            } else
                remove_subscriber(handle);
        }
    }
}

void
StreamServer::on_message(WsConn handle, WsMsg message)
{
    auto payload = message->string();

    LOG("message: {}", payload);

    if ("status" == payload) {
        handle->send(stringify(_threadStatus.load()));
    } else if ("start" == payload) {
        add_subscriber(handle);
    } else if ("stop" == payload) {
        remove_subscriber(handle);
    } else if ("EXIT" == payload) {
        std::exit(0);
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
