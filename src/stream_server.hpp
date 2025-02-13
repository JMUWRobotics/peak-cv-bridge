#pragma once

#include <optional>
#include <set>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

namespace XVII {

enum StreamingStatus
{
    IDLE,
    STARTING,
    STREAMING,
    NOT_STREAMING,
    ERROR_UNKNOWN,
    ERROR_CAPTURE_IN_USE,
};

class StreamServer
{
  private:
    uint _cameraIndex;
    std::optional<std::string> _compressionExt;
    std::optional<double> _targetFps;

    std::recursive_mutex _subscribersMutex;
    std::set<websocketpp::connection_hdl,
             std::owner_less<websocketpp::connection_hdl>>
      _subscribers;

    websocketpp::server<websocketpp::config::asio> _endpoint;

    std::atomic_flag _shouldThreadStop = ATOMIC_FLAG_INIT;
    std::atomic<StreamingStatus> _threadStatus = NOT_STREAMING;

    std::thread _captureThreadHandle;

    void log_activity(websocketpp::connection_hdl handle,
                      const std::string& message);

    bool has_subscribers();
    void remove_subscriber(websocketpp::connection_hdl subscriber);
    void add_subscriber(websocketpp::connection_hdl subscriber);

    void capture_thread();

    void on_message(
      websocketpp::connection_hdl handle,
      websocketpp::server<websocketpp::config::asio>::message_ptr message);
    void on_close(websocketpp::connection_hdl handle);

  public:
    StreamServer(uint cameraIndex = 0,
                 std::optional<std::string> compressionExt = std::nullopt,
                 std::optional<double> targetFps = std::nullopt);
    void run(uint16_t port);
    void stop();
};

}
