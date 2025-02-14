#pragma once

#include <mutex>
#include <optional>
#include <set>
#include <string>

#include <server_ws.hpp>

namespace XVII {

using WsServer = SimpleWeb::SocketServer<SimpleWeb::WS>;
using WsConn = std::shared_ptr<WsServer::Connection>;
using WsConnHandle = std::weak_ptr<WsServer::Connection>;
using WsMsg = std::shared_ptr<WsServer::InMessage>;
using HandleSet = std::set<WsConnHandle, std::owner_less<WsConnHandle>>;

enum class StreamingStatus
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
    unsigned int _cameraIndex;
    size_t _connMaxQueue;
    std::optional<std::string> _compressionExt;
    std::optional<double> _targetFps;

    std::recursive_mutex _subscribersMutex;
    HandleSet _subscribers;

    WsServer _server;

    std::atomic_flag _shouldThreadStop = ATOMIC_FLAG_INIT;
    std::atomic<StreamingStatus> _threadStatus = StreamingStatus::NOT_STREAMING;

    std::thread _captureThreadHandle;

    size_t n_subscribers();
    void remove_subscriber(WsConnHandle subscriber);
    void add_subscriber(WsConnHandle subscriber);
    HandleSet get_subscribers();

    void capture_thread();

  public:
    StreamServer(uint cameraIndex = 0,
                 size_t connMaxQueue = 10,
                 std::optional<std::string> compressionExt = std::nullopt,
                 std::optional<double> targetFps = std::nullopt);
    void run(uint16_t port);
    void stop();
};

}
