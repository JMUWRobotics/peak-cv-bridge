#include "genicvbridge.hpp"
#include <stdexcept>

#ifdef BRIDGE_ARAVIS
#include "backend/aravis.hpp"
#endif
#ifdef BRIDGE_IDSPEAK
#include "backend/ids-peak.hpp"
#endif
#ifdef BRIDGE_SPINNAKER
#include "backend/spinnaker.hpp"
#endif

namespace XVII {

using namespace detail;

std::unique_ptr<GenICamVideoCapture>
GenICamVideoCapture::OpenAnyCamera(bool debayer,
                                   std::optional<uint64_t> bufferTimeoutMs)
{
    auto capture =
      std::make_unique<GenICamVideoCapture>(debayer, bufferTimeoutMs);

    bool exceptionMode = capture->getExceptionMode();
    capture->setExceptionMode(false);

    for (auto backend :
         { Backend::IDS_PEAK, Backend::SPINNAKER, Backend::ARAVIS })
        if (capture->open(0, static_cast<int>(backend)))
            break;

    capture->setExceptionMode(exceptionMode);
    if (!capture->isOpened())
        throw std::runtime_error("No camera available");

    return capture;
}

GenICamVideoCapture::GenICamVideoCapture(
  bool debayer,
  std::optional<uint64_t> bufferTimeoutMs)
  : cv::VideoCapture()
  , _debayer(debayer)
  , _bufferTimeoutMs(bufferTimeoutMs)
{
}

GenICamVideoCapture::GenICamVideoCapture(
  int index,
  Backend backend,
  bool debayer,
  std::optional<uint64_t> bufferTimeoutMs)
  : GenICamVideoCapture(debayer, bufferTimeoutMs)
{
    open(index, static_cast<int>(backend));
}

GenICamVideoCapture::~GenICamVideoCapture()
{
    GenICamVideoCapture::release();
    cv::VideoCapture::release();
}

bool
GenICamVideoCapture::open(int index, int backend)
{
    if (index < 0) {
        if (throwOnFail)
            throw std::invalid_argument("Negative camera index");

        return false;
    }
    switch (static_cast<Backend>(backend)) {
#ifdef BRIDGE_ARAVIS
        case Backend::ARAVIS:
            _impl = std::make_unique<AravisBackend>(_debayer, _bufferTimeoutMs);
            break;
#endif
#ifdef BRIDGE_IDSPEAK
        case Backend::IDS_PEAK:
            _impl =
              std::make_unique<IdsPeakBackend>(_debayer, _bufferTimeoutMs);
            break;
#endif
#ifdef BRIDGE_SPINNAKER
        case Backend::SPINNAKER:
            _impl =
              std::make_unique<SpinnakerBackend>(_debayer, _bufferTimeoutMs);
            break;
#endif
        default:
            throw std::runtime_error("unsupported backend");
    }
    try {
        return _impl->open(index);
    } catch (const std::exception& e) {
        if (throwOnFail)
            throw e;
        return false;
    }
}

void
GenICamVideoCapture::release()
{
    if (_impl)
        _impl->release();
}

bool
GenICamVideoCapture::isOpened() const
{
    return _impl ? _impl->isOpened() : false;
}

bool
GenICamVideoCapture::grab()
{
    try {
        return _impl ? _impl->grab() : false;
    } catch (const std::exception& e) {
        if (throwOnFail)
            throw e;
        return false;
    }
}

bool
GenICamVideoCapture::retrieve(cv::OutputArray image, [[maybe_unused]] int flag)
{
    try {
        return _impl ? _impl->retrieve(image) : false;
    } catch (const std::exception& e) {
        if (throwOnFail)
            throw e;
        return false;
    }
}

bool
GenICamVideoCapture::read(cv::OutputArray image)
{

    try {
        return _impl ? _impl->retrieve(image) ||
                         (_impl->grab() && _impl->retrieve(image))
                     : false;
    } catch (const std::exception& e) {
        if (throwOnFail)
            throw e;
        return false;
    }
}

double
GenICamVideoCapture::get(int propId) const
{
    try {
        return _impl ? _impl->get(propId) : false;
    } catch (const std::exception& e) {
        if (throwOnFail)
            throw e;
        return false;
    }
}

bool
GenICamVideoCapture::set(int propId, double value)
{
    try {
        return _impl ? _impl->set(propId, value) : false;
    } catch (const std::exception& e) {
        if (throwOnFail)
            throw e;
        return false;
    }
}

void
GenICamVideoCapture::startAcquisition()
{
    if (_impl)
        _impl->startAcquisition();
}

void
GenICamVideoCapture::stopAcquisition()
{
    if (_impl)
        _impl->stopAcquisition();
}

}