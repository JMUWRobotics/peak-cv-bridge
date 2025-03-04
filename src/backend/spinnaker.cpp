#include "backend/spinnaker.hpp"
#include "pixelformat.hpp"

#include <fmt/core.h>

#include <opencv2/videoio.hpp>

namespace XVII::detail {

std::atomic_size_t SpinnakerBackend::_instanceCount(0);

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
    if (1 == _instanceCount--)
        _sys->ReleaseInstance();
}

bool
SpinnakerBackend::open(int _index)
{
    auto index = static_cast<unsigned int>(_index);

    auto devices = _sys->GetCameras();

    if (index >= devices.GetSize())
        throw std::invalid_argument("Index out of range");

    _camera = devices.GetByIndex(index);
    _camera->Init();

    Spinnaker::GenApi::CIntegerPtr n =
      _camera->GetTLStreamNodeMap().GetNode("StreamBufferCountManual");
    n->SetValue(3);

    const auto pixfmtStr = _camera->PixelFormat.ToString();

    if (pixfmtStr == "Mono8")
        _pixelFormat = PixelFormat::Mono8;
    else if (pixfmtStr == "BayerRG8")
        _pixelFormat = PixelFormat::BayerRG8;
    else
        fmt::println(stderr, "Unknown pixel format: {}", pixfmtStr.c_str());

    return true;
}

void
SpinnakerBackend::release()
{
    if (_isAcquiring) {
        try {
            stopAcquisition();
        } catch (...) {
            _isAcquiring = false;
        }
    }

    _camera->DeInit();
}

bool
SpinnakerBackend::isOpened() const
{
    return _camera.IsValid();
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

    switch (propId) {

        

    }
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