#include "stream_server.hpp"
#include "lib.hpp"

#include <opencv2/imgcodecs.hpp>

using namespace XVII;
using namespace websocketpp;
using namespace frame;

static std::string
stringify(StreamingStatus status)
{
    switch (status) {
        case IDLE:
            return "idle";
        case STARTING:
            return "starting";
        case STREAMING:
            return "streaming";
        case NOT_STREAMING:
            return "not streaming";
        case ERROR_CAPTURE_IN_USE:
            return "capture in use";
        case ERROR_UNKNOWN:
            return "unknown error";
        default:
            throw "not implemented";
    }
}

inline void
StreamServer::log_activity(connection_hdl handle, const std::string& message)
{
    std::string remote_host = "unknown host";
    try {
        remote_host = _endpoint.get_con_from_hdl(handle)->get_remote_endpoint();
    } catch (...) {
    }
    std::cout << remote_host << "\t| " << message << '\n';
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
StreamServer::remove_subscriber(connection_hdl subscriber)
{
    _subscribersMutex.lock();

    _subscribers.erase(subscriber);

    _subscribersMutex.unlock();
}

void
StreamServer::on_close(connection_hdl handle)
{
    log_activity(handle, "closed");

    remove_subscriber(handle);
}

void
StreamServer::add_subscriber(connection_hdl subscriber)
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

    _threadStatus.store(STARTING);

    cv::PeakVideoCapture capture;

    while (!_shouldThreadStop.test_and_set()) {
        _shouldThreadStop.clear();

        if (!has_subscribers()) {
            _threadStatus.store(IDLE);
            capture.release();
            sleep(1000);
            continue;
        }

        if (!capture.isOpened()) {
            capture.setExceptionMode(true);
            try {
                capture.open(0);
                capture.set(cv::CAP_PROP_FPS, targetFps);
                capture.set(cv::CAP_PROP_AUTO_EXPOSURE, true);
            } catch (const cv::Exception& e) {
                if (e.code != cv::Error::StsInternal) {
                    std::cerr << "[CaptureThread] Unexpected exception when "
                                 "opening capture: "
                              << e.what() << '\n';
                    _threadStatus.store(ERROR_UNKNOWN);
                    std::exit(1);
                } else {
                    _threadStatus.store(ERROR_CAPTURE_IN_USE);
                    continue;
                }
            }
            std::cout << "[CaptureThread] opened capture at index 0\n";
            capture.setExceptionMode(false);
        }

        _threadStatus.store(STREAMING);

        cv::Mat image;
        if (!capture.read(image) || image.empty())
            continue;

        std::vector<uchar> buffer;
        cv::imencode(compression, image, buffer);

        auto currentSubscribers = _subscribers;
        for (const auto& handle : currentSubscribers) {
            try {
                _endpoint.send(
                  handle, buffer.data(), buffer.size(), opcode::binary);
            } catch (...) {
                log_activity(handle, "send failed");
                remove_subscriber(handle);
            }
        }
    }
}

void
StreamServer::on_message(connection_hdl handle,
                         server<config::asio>::message_ptr message)
{
    auto payload = message->get_payload();

    log_activity(handle, "message: " + payload);

    if ("status" == payload) {
        _endpoint.send(handle, stringify(_threadStatus.load()), opcode::text);
    } else if ("start" == payload) {
        add_subscriber(handle);
    } else if ("stop" == payload) {
        remove_subscriber(handle);
    } else if ("EXIT" == payload) {
        std::exit(0);
    }
}

StreamServer::StreamServer(std::optional<std::string> compressionExt,
                           std::optional<double> targetFps)
{
    _compressionExt = compressionExt;
    _targetFps = targetFps;

    _endpoint.set_error_channels(log::elevel::all);
    _endpoint.clear_access_channels(log::alevel::all);

    _endpoint.init_asio();

    _endpoint.set_message_handler(lib::bind(&StreamServer::on_message,
                                            this,
                                            lib::placeholders::_1,
                                            lib::placeholders::_2));
    _endpoint.set_close_handler(
      lib::bind(&StreamServer::on_close, this, lib::placeholders::_1));
}

void
StreamServer::run(uint16_t port)
{
    _captureThreadHandle = std::thread(&StreamServer::capture_thread, this);

    _endpoint.listen(port);
    _endpoint.start_accept();

    std::cout << "Server listening on port " << port << '\n';

    _endpoint.run();
}

void
StreamServer::stop()
{
    _shouldThreadStop.test_and_set();
    _endpoint.stop_listening();

    if (_captureThreadHandle.joinable())
        _captureThreadHandle.join();

    auto subscribers = _subscribers;

    for (const auto& handle : subscribers) {
        try {
            _endpoint.close(handle, close::status::normal, "shutdown");
        } catch (...) {
        }
    }

    _endpoint.stop();
}
