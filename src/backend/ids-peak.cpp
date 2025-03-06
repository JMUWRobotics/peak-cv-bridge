#include "backend/ids-peak.hpp"

#include <fmt/core.h>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

namespace XVII::detail {

std::atomic_size_t IdsPeakBackend::_instanceCount(0);

static bool
isWriteable(std::shared_ptr<peak::core::nodes::Node> node)
{
    using peak::core::nodes::NodeAccessStatus;
    switch (node->AccessStatus()) {
        case NodeAccessStatus::WriteOnly:
        case NodeAccessStatus::ReadWrite:
            return true;
        default:
            return false;
    }
}

static bool
isReadable(std::shared_ptr<peak::core::nodes::Node> node)
{
    using peak::core::nodes::NodeAccessStatus;
    switch (node->AccessStatus()) {
        case NodeAccessStatus::ReadOnly:
        case NodeAccessStatus::ReadWrite:
            return true;
        default:
            return false;
    }
}

template<typename TNode, typename TValue>
static void
nodeCheckedSetValue(TNode node, TValue value)
{
    if (node->IncrementType() !=
        peak::core::nodes::NodeIncrementType::NoIncrement) {
        if constexpr (std::is_floating_point_v<TValue>)
            value -= std::fmod(value, node->Increment());
        else
            value -= value % node->Increment();
    }

    node->SetValue(std::max(node->Minimum(), std::min(value, node->Maximum())));
}

IdsPeakBackend::IdsPeakBackend(bool debayer,
                               std::optional<uint64_t> bufferTimeoutMs)
  : Impl(debayer, bufferTimeoutMs)
{
    if (0 == _instanceCount++) {
        peak::Library::Initialize();
        fmt::println(
          stderr, "Peak Version: {}", peak::Library::Version().ToString());
    }
}

IdsPeakBackend::~IdsPeakBackend()
{
    if (1 == _instanceCount--)
        peak::Library::Close();
}

bool
IdsPeakBackend::open(int _index)
{
    auto index = static_cast<size_t>(_index);

    auto& deviceManager = peak::DeviceManager::Instance();
    deviceManager.Update();

    auto devices = deviceManager.Devices();
    if (index >= devices.size())
        throw std::invalid_argument("Index out of range");

    _device =
      devices.at(index)->OpenDevice(peak::core::DeviceAccessType::Control);

    auto dataStreams = _device->DataStreams();
    if (dataStreams.empty())
        throw std::runtime_error("No data streams for device!");

    _dataStream = dataStreams.at(0)->OpenDataStream();
    _nodeMap = _device->RemoteDevice()->NodeMaps().at(0);

    int64_t payloadSize =
      _nodeMap->FindNode<peak::core::nodes::IntegerNode>("PayloadSize")
        ->Value();

    _dataStream->Flush(peak::core::DataStreamFlushMode::DiscardAll);
    size_t numBuffersMinRequired =
      _dataStream->NumBuffersAnnouncedMinRequired();

    for (size_t i = 0; i < numBuffersMinRequired; i++) {
        auto buffer = _dataStream->AllocAndAnnounceBuffer(
          static_cast<size_t>(payloadSize), nullptr);
        _dataStream->QueueBuffer(buffer);
    }

    try {
        _nodeMap
          ->FindNode<peak::core::nodes::EnumerationNode>("UserSetSelector")
          ->SetCurrentEntry("Default");
        _nodeMap->FindNode<peak::core::nodes::CommandNode>("UserSetLoad")
          ->Execute();
    } catch (const std::exception& e) {
        fmt::println(stderr, "Set Default UserSet failed: {}", e.what());
    }

    try {
        const auto pixfmtStr =
          _nodeMap->FindNode<peak::core::nodes::EnumerationNode>("PixelFormat")
            ->CurrentEntry()
            ->StringValue();

        if (pixfmtStr == "Mono8")
            _pixelFormat = PixelFormat::Mono8;
        else if (pixfmtStr == "BayerRG8")
            _pixelFormat = PixelFormat::BayerRG8;
        else
            fmt::println(stderr, "Unknown pixel format: {}", pixfmtStr);
    } catch (const std::exception& e) {
        fmt::println(stderr, "Querying PixelFormat failed: {}", e.what());
    }

    return true;
}

void
IdsPeakBackend::release() noexcept
{
    if (_isAcquiring) {
        try {
            stopAcquisition();
        } catch (...) {
            _isAcquiring = false;
        }
    }

    if (_dataStream) {
        _dataStream->Flush(peak::core::DataStreamFlushMode::DiscardAll);

        for (const auto& buffer : _dataStream->AnnouncedBuffers()) {
            _dataStream->RevokeBuffer(buffer);
        }
    }

    // reverse order is probably important
    _dataStream = nullptr;
    _nodeMap = nullptr;
    _device = nullptr;
}

bool
IdsPeakBackend::isOpened() const noexcept
{
    return _device && _nodeMap && _dataStream;
}

bool
IdsPeakBackend::grab()
{
    if (!_isAcquiring)
        startAcquisition();

    _filledBuffer = _dataStream->WaitForFinishedBuffer(
      _bufferTimeoutMs.value_or(PEAK_INFINITE_TIMEOUT));

    return true;
}

bool
IdsPeakBackend::retrieve(cv::OutputArray image)
{
    if (nullptr == _filledBuffer)
        return false;

    debayer(_filledBuffer->Height(),
            _filledBuffer->Width(),
            CV_8UC1,
            _filledBuffer->BasePtr(),
            _filledBuffer->Width(),
            image);

    _dataStream->QueueBuffer(_filledBuffer);
    _filledBuffer = nullptr;

    return true;
}

double
IdsPeakBackend::get(int propId) const
{
    switch (propId) {

        case cv::CAP_PROP_AUTO_EXPOSURE: {
            auto node = _nodeMap->FindNode<peak::core::nodes::EnumerationNode>(
              "ExposureAuto");

            if (!isReadable(node))
                return 0;

            return node->CurrentEntry()->StringValue() == "Continuous";
        }

        case cv::CAP_PROP_EXPOSURE: {
            auto node =
              _nodeMap->FindNode<peak::core::nodes::FloatNode>("ExposureTime");

            if (!isReadable(node))
                return 0;

            return node->Value();
        }

        case cv::CAP_PROP_FPS: {
            auto node = _nodeMap->FindNode<peak::core::nodes::FloatNode>(
              "AcquisitionFrameRate");

            if (!isReadable(node))
                return 0;

            return node->Value();
        }

        case cv::CAP_PROP_TRIGGER: {
            auto node = _nodeMap->FindNode<peak::core::nodes::EnumerationNode>(
              "TriggerMode");

            if (!isReadable(node))
                return 0;

            return node->CurrentEntry()->StringValue() == "On";
        }
    }

    return 0;
}

bool
IdsPeakBackend::set(int propId, double value)
{
    if (_isAcquiring)
        stopAcquisition();

    switch (propId) {

        case cv::CAP_PROP_AUTO_EXPOSURE: {
            auto node = _nodeMap->FindNode<peak::core::nodes::EnumerationNode>(
              "ExposureAuto");

            if (!isWriteable(node))
                throw std::runtime_error("AutoExposure is not writeable");

            node->SetCurrentEntry(0.0 == value ? "Off" : "Continuous");
        } break;

        case cv::CAP_PROP_EXPOSURE: {
            auto node =
              _nodeMap->FindNode<peak::core::nodes::FloatNode>("ExposureTime");

            if (value < node->Minimum() || node->Maximum() < value)
                throw std::invalid_argument("Argument out of range");

            if (!isWriteable(node))
                throw std::runtime_error("ExposureTime is not writeable");

            nodeCheckedSetValue(node, value);

        } break;

        case cv::CAP_PROP_FPS: {

            bool hasTargetEnable =
                   _nodeMap->HasNode("AcquisitionFrameRateTargetEanble"),
                 hasTarget = _nodeMap->HasNode("AcquisitionFrameRateTarget"),
                 hasRate = _nodeMap->HasNode("AcquisitionFrameRate");

            if (hasTargetEnable && hasTarget) {

                auto targetEnableNode =
                  _nodeMap->FindNode<peak::core::nodes::BooleanNode>(
                    "AcquisitionFrameRateTargetEnable");

                if (!isReadable(targetEnableNode))
                    throw std::runtime_error(
                      "AcquisitionFrameRateTargetEnable is not readable");

                if (!isWriteable(targetEnableNode))
                    throw std::runtime_error(
                      "AcquisitionFrameRateTargetEnable is not writeable");

                if (targetEnableNode->Value())
                    targetEnableNode->SetValue(false);

                auto targetNode =
                  _nodeMap->FindNode<peak::core::nodes::FloatNode>(
                    "AcquisitionFrameRateTarget");

                if (!isWriteable(targetNode))
                    throw std::runtime_error(
                      "AcquisitionFrameRateTarget is not writeable");

                nodeCheckedSetValue(targetNode, value);

                targetEnableNode->SetValue(true);

            } else if (hasRate) {

                auto rateNode =
                  _nodeMap->FindNode<peak::core::nodes::FloatNode>(
                    "AcquisitionFrameRate");

                if (!isWriteable(rateNode))
                    throw std::runtime_error(
                      "AcquisitionFrameRate is not writeable");

                nodeCheckedSetValue(rateNode, value);

            } else
                throw std::invalid_argument("CAP_PROP_FPS is not supported");
        } break;

        case cv::CAP_PROP_TRIGGER: {
            auto triggerModeNode =
              _nodeMap->FindNode<peak::core::nodes::EnumerationNode>(
                "TriggerMode");

            if (!isWriteable(triggerModeNode))
                throw std::runtime_error("TriggerMode is not writeable");

            if (0.0 == value) {
                triggerModeNode->SetCurrentEntry("Off");
                return true;
            } else {
                auto triggerSourceNode =
                  _nodeMap->FindNode<peak::core::nodes::EnumerationNode>(
                    "TriggerSource");

                if (!isWriteable(triggerSourceNode))
                    throw std::runtime_error("TriggerSource is not writeable");

                triggerModeNode->SetCurrentEntry("On");
                triggerSourceNode->SetCurrentEntry("Line0");

                auto triggerActivationNode =
                  _nodeMap->FindNode<peak::core::nodes::EnumerationNode>(
                    "TriggerActivation");

                if (!isWriteable(triggerActivationNode)) {
                    triggerModeNode->SetCurrentEntry("Off");
                    throw std::runtime_error(
                      "TriggerActivation is not writeable");
                }

                triggerActivationNode->SetCurrentEntry("RisingEdge");
            }

        } break;

        default:
            return false;
    }

    return true;
}

void
IdsPeakBackend::startAcquisition()
{
    _dataStream->StartAcquisition(peak::core::AcquisitionStartMode::Default,
                                  PEAK_INFINITE_NUMBER);

    _nodeMap->FindNode<peak::core::nodes::IntegerNode>("TLParamsLocked")
      ->SetValue(1);
    _nodeMap->FindNode<peak::core::nodes::CommandNode>("AcquisitionStart")
      ->Execute();

    _isAcquiring = true;
}

void
IdsPeakBackend::stopAcquisition()
{
    if (_nodeMap) {
        _nodeMap->FindNode<peak::core::nodes::CommandNode>("AcquisitionStop")
          ->Execute();
        _nodeMap->FindNode<peak::core::nodes::IntegerNode>("TLParamsLocked")
          ->SetValue(0);
    }

    if (_dataStream)
        _dataStream->StopAcquisition(peak::core::AcquisitionStopMode::Default);

    _isAcquiring = false;
}
}
