#include "lib.hpp"

#include <fmt/core.h>

namespace cv {

std::atomic_size_t PeakVideoCapture::_instanceCount(0);

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

PeakVideoCapture::PeakVideoCapture(uint64_t bufferTimeout)
  : VideoCapture()
{
    if (0 == _instanceCount++) {
        peak::Library::Initialize();
        fmt::println(
          stderr, "Peak Version: {}", peak::Library::Version().ToString());
    }
    _bufferTimeout = bufferTimeout;
}

PeakVideoCapture::PeakVideoCapture(int index, uint64_t bufferTimeout)
  : PeakVideoCapture(bufferTimeout)
{
    open(index);
}

PeakVideoCapture::~PeakVideoCapture()
{
    PeakVideoCapture::release();
    VideoCapture::release();
    if (1 == _instanceCount--) {
        peak::Library::Close();
    }
}

bool
PeakVideoCapture::open(int _index, int _apiPreference)
{
    if (_index < 0) {
        if (throwOnFail)
            CV_Error(Error::StsBadArg, "Negative camera index");
        else
            return false;
    }

    auto index = static_cast<size_t>(_index);

    try {
        auto& deviceManager = peak::DeviceManager::Instance();
        deviceManager.Update();

        auto devices = deviceManager.Devices();
        if (index >= devices.size()) {
            if (throwOnFail)
                CV_Error(Error::StsBadArg, "Index out of range");

            return false;
        }

        _device =
          devices.at(index)->OpenDevice(peak::core::DeviceAccessType::Control);

        auto dataStreams = _device->DataStreams();
        if (dataStreams.empty()) {
            if (throwOnFail)
                CV_Error(Error::StsInternal, "No data streams for device!");

            return false;
        }

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
            fmt::println(stderr, "Set Default UserSet failed");
        }

        return true;

    } catch (const peak::core::InternalErrorException& iee) {
        if (throwOnFail)
            CV_Error(Error::StsInternal, iee.what());
    } catch (const peak::core::BadAccessException& bae) {
        if (throwOnFail)
            CV_Error(Error::StsError, bae.what());
    } catch (const peak::core::NotFoundException& nfe) {
        if (throwOnFail)
            CV_Error(Error::StsNotImplemented, nfe.what());
    }

    return false;
}

void
PeakVideoCapture::release()
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
PeakVideoCapture::isOpened() const
{
    return _device && _nodeMap && _dataStream;
}

bool
PeakVideoCapture::grab()
{
    if (!_isAcquiring) {
        startAcquisition();
    }

    try {
        _filledBuffer = _dataStream->WaitForFinishedBuffer(_bufferTimeout);
    } catch (const peak::core::TimeoutException& te) {
        if (throwOnFail)
            CV_Error(Error::StsError, te.what());

        return false;
    }

    return true;
}

bool
PeakVideoCapture::retrieve(OutputArray image, int flag)
{
    if (nullptr == _filledBuffer) {
        cv::Mat empty;
        empty.copyTo(image);
        return false;
    }

    cv::Mat ref(_filledBuffer->Height(),
                _filledBuffer->Width(),
                CV_8UC1,
                _filledBuffer->BasePtr(),
                _filledBuffer->Width());
    ref.copyTo(image);

    _dataStream->QueueBuffer(_filledBuffer);
    _filledBuffer = nullptr;

    return true;
}

bool
PeakVideoCapture::read(OutputArray image)
{
    if (nullptr == _filledBuffer) {
        grab();
    }

    return retrieve(image);
}

double
PeakVideoCapture::get(int propId) const
{
    try {

        switch (propId) {

            case cv::CAP_PROP_AUTO_EXPOSURE: {
                auto node =
                  _nodeMap->FindNode<peak::core::nodes::EnumerationNode>(
                    "ExposureAuto");

                if (!isReadable(node))
                    return 0;

                return node->CurrentEntry()->StringValue() == "Continuous";
            }

            case cv::CAP_PROP_EXPOSURE: {
                auto node = _nodeMap->FindNode<peak::core::nodes::FloatNode>(
                  "ExposureTime");

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
                auto node =
                  _nodeMap->FindNode<peak::core::nodes::EnumerationNode>(
                    "TriggerMode");

                if (!isReadable(node))
                    return 0;

                return node->CurrentEntry()->StringValue() == "On";
            }
        }

    } catch (const peak::core::NotFoundException& nfe) {
        if (throwOnFail)
            CV_Error(Error::StsNotImplemented, nfe.what());
    }

    return 0;
}

bool
PeakVideoCapture::set(int propId, double value)
{
    if (_isAcquiring)
        stopAcquisition();

    try {

        switch (propId) {

            case cv::CAP_PROP_AUTO_EXPOSURE: {
                auto node =
                  _nodeMap->FindNode<peak::core::nodes::EnumerationNode>(
                    "ExposureAuto");

                if (!isWriteable(node)) {
                    if (throwOnFail)
                        CV_Error(Error::StsError,
                                 "AutoExposure is not writeable");

                    return false;
                }

                node->SetCurrentEntry(0.0 == value ? "Off" : "Continuous");
            } break;

            case cv::CAP_PROP_EXPOSURE: {
                auto node = _nodeMap->FindNode<peak::core::nodes::FloatNode>(
                  "ExposureTime");

                if (value < node->Minimum() || node->Maximum() < value) {
                    if (throwOnFail)
                        CV_Error(Error::StsBadArg, "Argument out of range");

                    return false;
                }

                if (!isWriteable(node)) {
                    if (throwOnFail)
                        CV_Error(Error::StsError,
                                 "ExposureTime is not writeable");

                    return false;
                }

                nodeCheckedSetValue(node, value);

            } break;

            case cv::CAP_PROP_FPS: {

                bool hasTargetEnable =
                       _nodeMap->HasNode("AcquisitionFrameRateTargetEanble"),
                     hasTarget =
                       _nodeMap->HasNode("AcquisitionFrameRateTarget"),
                     hasRate = _nodeMap->HasNode("AcquisitionFrameRate");

                if (hasTargetEnable && hasTarget) {

                    auto targetEnableNode =
                      _nodeMap->FindNode<peak::core::nodes::BooleanNode>(
                        "AcquisitionFrameRateTargetEnable");

                    if (!isReadable(targetEnableNode)) {
                        if (throwOnFail)
                            CV_Error(Error::StsError,
                                     "AcquisitionFrameRateTargetEnable is not "
                                     "readable");

                        return false;
                    }

                    if (!isWriteable(targetEnableNode)) {
                        if (throwOnFail)
                            CV_Error(Error::StsError,
                                     "AcquisitionFrameRateTargetEnable is not "
                                     "writeable");

                        return false;
                    }

                    if (targetEnableNode->Value())
                        targetEnableNode->SetValue(false);

                    auto targetNode =
                      _nodeMap->FindNode<peak::core::nodes::FloatNode>(
                        "AcquisitionFrameRateTarget");

                    if (!isWriteable(targetNode)) {
                        if (throwOnFail)
                            CV_Error(
                              Error::StsError,
                              "AcquisitionFrameRateTarget is not writeable");

                        return false;
                    }

                    nodeCheckedSetValue(targetNode, value);

                    targetEnableNode->SetValue(true);

                } else if (hasRate) {

                    auto rateNode =
                      _nodeMap->FindNode<peak::core::nodes::FloatNode>(
                        "AcquisitionFrameRate");

                    if (!isWriteable(rateNode)) {
                        if (throwOnFail)
                            CV_Error(Error::StsError,
                                     "AcquisitionFrameRate is not writeable");

                        return false;
                    }

                    nodeCheckedSetValue(rateNode, value);

                } else {
                    if (throwOnFail)
                        CV_Error(Error::StsNotImplemented,
                                 "CAP_PROP_FPS is not supported");

                    return false;
                }
            } break;

            case cv::CAP_PROP_TRIGGER: {
                auto triggerModeNode =
                  _nodeMap->FindNode<peak::core::nodes::EnumerationNode>(
                    "TriggerMode");

                if (!isWriteable(triggerModeNode)) {
                    if (throwOnFail)
                        CV_Error(Error::StsError,
                                 "TriggerMode is not writeable");

                    return false;
                }

                if (0.0 == value) {
                    triggerModeNode->SetCurrentEntry("Off");
                    return true;
                } else {
                    auto triggerSourceNode =
                      _nodeMap->FindNode<peak::core::nodes::EnumerationNode>(
                        "TriggerSource");

                    if (!isWriteable(triggerSourceNode)) {
                        if (throwOnFail)
                            CV_Error(Error::StsError,
                                     "TriggerSource is not writeable");

                        return false;
                    }

                    triggerModeNode->SetCurrentEntry("On");
                    triggerSourceNode->SetCurrentEntry("Line0");

                    auto triggerActivationNode =
                      _nodeMap->FindNode<peak::core::nodes::EnumerationNode>(
                        "TriggerActivation");

                    if (!isWriteable(triggerActivationNode)) {
                        if (throwOnFail)
                            CV_Error(Error::StsError,
                                     "TriggerActivation is not writeable");

                        triggerModeNode->SetCurrentEntry("Off");

                        return false;
                    }

                    triggerActivationNode->SetCurrentEntry("RisingEdge");
                }

            } break;

            default:
                return false;
        }

        return true;

    } catch (const peak::core::NotFoundException& nfe) {
        if (throwOnFail)
            CV_Error(Error::StsNotImplemented, nfe.what());
    }

    return false;
}

void
PeakVideoCapture::startAcquisition()
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
PeakVideoCapture::stopAcquisition()
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
