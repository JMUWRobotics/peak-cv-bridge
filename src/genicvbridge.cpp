#include "genicvbridge.hpp"

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
    _impl->Impl::release();
}

bool
GenICamVideoCapture::isOpened() const
{
    return _impl->isOpened();
}

bool
GenICamVideoCapture::grab()
{
    try {
        return _impl->grab();
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
        return _impl->retrieve(image);
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
        return _impl->retrieve(image) ||
               (_impl->grab() && _impl->retrieve(image));
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
        return _impl->get(propId);
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
        return _impl->set(propId, value);
    } catch (const std::exception& e) {
        if (throwOnFail)
            throw e;
        return false;
    }
}

void
GenICamVideoCapture::startAcquisition()
{
    _impl->startAcquisition();
}

void
GenICamVideoCapture::stopAcquisition()
{
    _impl->stopAcquisition();
}

}