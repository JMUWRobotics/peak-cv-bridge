#include "backend/spinnaker.hpp"
#include "genicvbridge.hpp"
#include "pixelformat.hpp"

#include <fmt/core.h>

#include <opencv2/videoio.hpp>

namespace XVII::detail {

template<typename TNode, typename TValue>
static void
nodeCheckedSetValue(TNode& node, TValue value)
{
    if (node.HasInc()) {
        if constexpr (std::is_floating_point_v<TValue>)
            value -= std::fmod(value, node.GetInc());
        else
            value -= value % node.GetInc();
    }

    node.SetValue(std::max(node.GetMin(), std::min(value, node.GetMax())));
}

std::atomic_size_t SpinnakerBackend::_instanceCount(0);

Spinnaker::SystemPtr SpinnakerBackend::_sys(nullptr);

SpinnakerBackend::SpinnakerBackend(bool debayer,
                                   std::optional<uint64_t> bufferTimeoutMs)
  : Impl(debayer, bufferTimeoutMs)
{
    if (0 == _instanceCount++) {
        _sys = Spinnaker::System::GetInstance();
        const auto [major, minor, type, build] = _sys->GetLibraryVersion();
        fmt::println(
          stderr, "Spinnaker Version: {}.{}.{}.{}", major, minor, type, build);
    }
}

SpinnakerBackend::~SpinnakerBackend()
{
    _filledBuffer = nullptr;
    _camera = nullptr;
    if (1 == _instanceCount--)
        _sys->ReleaseInstance();
}

bool
SpinnakerBackend::open(int index)
{
    {
        auto _devices = _sys->GetCameras();
        if ((size_t)index >= _devices.GetSize())
            throw std::invalid_argument("Index out of range");

        std::vector<Spinnaker::CameraPtr> devices(_devices.GetSize(), nullptr);

        for (size_t i = 0; i < devices.size(); ++i)
            devices[i] = _devices[i];

        std::sort(
          devices.begin(), devices.end(), [](const auto& l, const auto& r) {
              const auto lid = l->GetDeviceID().c_str(),
                         rid = r->GetDeviceID().c_str();
              const int cmp = std::strcmp(lid, rid);
              return cmp;
          });

        _camera = devices[index];
    }

    _camera->Init();

    try {
        _camera->UserSetSelector.SetValue(Spinnaker::UserSetSelector_Default);
        _camera->UserSetLoad.Execute();
    } catch (const std::exception& e) {
        fmt::println(stderr, "Set Default UserSet failed: {}", e.what());
    }

    const auto pixfmtStr = _camera->PixelFormat.ToString();

    if (pixfmtStr == "Mono8")
        _pixelFormat = PixelFormat::Mono8;
    else if (pixfmtStr == "BayerRG8")
        _pixelFormat = PixelFormat::BayerBG8; // intentional
    else
        fmt::println(stderr, "Unknown pixel format: {}", pixfmtStr.c_str());

    return true;
}

void
SpinnakerBackend::release() noexcept
{
    Impl::release();
    _camera->DeInit();
}

bool
SpinnakerBackend::isOpened() const noexcept
{
    return _sys.IsValid() && _sys->IsInUse() && _camera.IsValid();
}

bool
SpinnakerBackend::grab()
{
    if (!_isAcquiring)
        startAcquisition();

    _filledBuffer = _camera->GetNextImage(
      _bufferTimeoutMs.value_or(Spinnaker::EVENT_TIMEOUT_INFINITE));

    return true;
}

bool
SpinnakerBackend::retrieve(cv::OutputArray image)
{
    if (_filledBuffer == nullptr || !_filledBuffer.IsValid())
        return false;

    debayer(_filledBuffer->GetHeight(),
            _filledBuffer->GetWidth(),
            CV_8UC1,
            _filledBuffer->GetData(),
            _filledBuffer->GetWidth(),
            image);

    _filledBuffer->Release();
    _filledBuffer = nullptr;

    return true;
}

double
SpinnakerBackend::get(int propId) const
{
    switch (propId) {
        case cv::CAP_PROP_AUTO_EXPOSURE:
            return _camera->ExposureAuto.GetCurrentEntry()->ToString() ==
                   "Continuous";
        case cv::CAP_PROP_EXPOSURE:
            return _camera->ExposureTime.GetValue();
        case cv::CAP_PROP_FPS:
            return _camera->AcquisitionFrameRate.GetValue();
        case cv::CAP_PROP_TRIGGER:
            return _camera->TriggerMode.GetCurrentEntry()->ToString() == "On";
    }

    return 0;
}

bool
SpinnakerBackend::set(int propId, double value)
{
    if (_isAcquiring)
        stopAcquisition();

#define RANGECHECK(node)                                                       \
    do {                                                                       \
        if (value < _camera->node.GetMin() || _camera->node.GetMax() < value)  \
            throw std::runtime_error("Argument out of range");                 \
    } while (0)

    switch (propId) {

        case cv::CAP_PROP_AUTO_EXPOSURE:
            _camera->ExposureAuto.SetValue(
              0.0 == value ? Spinnaker::ExposureAuto_Off
                           : Spinnaker::ExposureAuto_Continuous);
            break;
        case cv::CAP_PROP_EXPOSURE: {
            RANGECHECK(ExposureTime);
            nodeCheckedSetValue(_camera->ExposureTime, value);
        } break;
        case cv::CAP_PROP_FPS: {
            RANGECHECK(AcquisitionFrameRate);
            _camera->AcquisitionFrameRateEnable.SetValue(true);
            nodeCheckedSetValue(_camera->AcquisitionFrameRate, value);
        } break;
        case cv::CAP_PROP_TRIGGER: {
            Spinnaker::TriggerSourceEnums source;
            switch ((int)value) {
                case 0:
                    source = Spinnaker::TriggerSource_Line0;
                    break;
                case 1:
                    source = Spinnaker::TriggerSource_Line1;
                    break;
                case 2:
                    source = Spinnaker::TriggerSource_Line2;
                    break;
                case 3:
                    source = Spinnaker::TriggerSource_Line3;
                    break;
                default:
                    _camera->TriggerMode.SetValue(Spinnaker::TriggerMode_Off);
                    return true;
            }

            _camera->TriggerMode.SetValue(Spinnaker::TriggerMode_On);
            _camera->TriggerSource.SetValue(source);
            _camera->TriggerActivation.SetValue(
              Spinnaker::TriggerActivation_RisingEdge);
        } break;
        case XVII::CAP_PROP_LINE: {
            _camera->LineSelector.SetValue(Spinnaker::LineSelector_Line2);
            _camera->V3_3Enable.SetValue((int)value != 0);
        } break;
        default:
            return false;
    }

#undef RANGECHECK

    return true;
}

void
SpinnakerBackend::stopAcquisition()
{
    _camera->EndAcquisition();
    _isAcquiring = false;
}

void
SpinnakerBackend::startAcquisition()
{
    _camera->BeginAcquisition();
    _isAcquiring = true;
}

}