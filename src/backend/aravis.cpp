#include "backend/aravis.hpp"
#include "backend/impl.hpp"

#include <arv.h>
#include <fmt/core.h>
#include <opencv2/videoio.hpp>

namespace XVII::detail {

AravisBackend::AravisBackend(bool debayer,
                             std::optional<uint64_t> bufferTimeoutMs)
  : Impl(debayer, bufferTimeoutMs)
{
}

bool
AravisBackend::open(int index)
{
    unsigned int n_devices;
    size_t payload;
    ArvPixelFormat pixfmt;
    GError* error = nullptr;

    arv_update_device_list();
    n_devices = arv_get_n_devices();

    if (index < 0 || (unsigned int)index >= n_devices)
        throw std::invalid_argument("Index out of range");

    _camera = arv_camera_new(arv_get_device_id(index), &error);

    if (!ARV_IS_CAMERA(_camera))
        goto abort;

    arv_camera_set_acquisition_mode(
      _camera, ARV_ACQUISITION_MODE_CONTINUOUS, &error);
    if (error)
        goto abort;

    _stream = arv_camera_create_stream(_camera, NULL, NULL, &error);
    if (!ARV_IS_STREAM(_stream))
        goto abort;

    payload = arv_camera_get_payload(_camera, &error);
    if (error)
        goto abort;

    for (size_t i = 0; i < 3; ++i)
        arv_stream_push_buffer(_stream, arv_buffer_new(payload, NULL));

    pixfmt = arv_camera_get_pixel_format(_camera, &error);
    if (error) {
        fmt::println(stderr, "Querying PixelFormat failed {}", error->message);
    } else {
        switch (pixfmt) {
            case ARV_PIXEL_FORMAT_MONO_8:
                _pixelFormat = PixelFormat::Mono8;
                break;
            case ARV_PIXEL_FORMAT_BAYER_RG_8:
                _pixelFormat = PixelFormat::BayerRG8;
                break;
            case ARV_PIXEL_FORMAT_BAYER_BG_8:
                _pixelFormat = PixelFormat::BayerBG8;
                break;
            default:
                fmt::println("Unknown pixel format {}",
                             arv_pixel_format_to_gst_caps_string(pixfmt));
        }
    }

    return true;
abort:
    g_clear_object(&_stream);
    g_clear_object(&_camera);
    if (error)
        throw std::runtime_error(error->message);
    return false;
}

#define WRAP_ERROR(expr)                                                       \
    do {                                                                       \
        GError* error = nullptr;                                               \
        expr;                                                                  \
        if (error)                                                             \
            throw std::runtime_error(error->message);                          \
    } while (0)

void
AravisBackend::startAcquisition()
{
    WRAP_ERROR(arv_camera_start_acquisition(_camera, &error));
}

void
AravisBackend::stopAcquisition()
{
    WRAP_ERROR(arv_camera_stop_acquisition(_camera, &error));
}

void
AravisBackend::release() noexcept
{
    Impl::release();

    if (ARV_IS_STREAM(_stream))
        arv_stream_delete_buffers(_stream);

    _buffer = nullptr;
    g_clear_object(&_stream);
    g_clear_object(&_camera);
}

bool
AravisBackend::grab()
{
    if (!_isAcquiring)
        startAcquisition();

    _buffer = arv_stream_pop_buffer(_stream);

    return ARV_IS_BUFFER(_buffer);
}

bool
AravisBackend::retrieve(cv::OutputArray image)
{
    if (!ARV_IS_BUFFER(_buffer))
        return false;

    debayer(arv_buffer_get_image_height(_buffer),
            arv_buffer_get_image_width(_buffer),
            CV_8UC1,
            arv_buffer_get_data(_buffer, NULL),
            arv_buffer_get_image_width(_buffer),
            image);

    arv_stream_push_buffer(_stream, _buffer);
    _buffer = nullptr;

    return true;
}

double
AravisBackend::get(int propId) const
{
    GError* error = nullptr;
    switch (propId) {
        case cv::CAP_PROP_AUTO_EXPOSURE: {
            ArvAuto exposure_auto =
              arv_camera_get_exposure_time_auto(_camera, &error);
            if (!error)
                return exposure_auto == ARV_AUTO_CONTINUOUS;
        } break;
        case cv::CAP_PROP_EXPOSURE: {
            double exposure = arv_camera_get_exposure_time(_camera, &error);
            if (!error)
                return exposure;
        } break;
        case cv::CAP_PROP_FPS: {
            double fps = arv_camera_get_frame_rate(_camera, &error);
            if (!error)
                return fps;
        } break;
    }

    if (error)
        throw std::runtime_error(error->message);

    throw std::runtime_error("Unsupported property");
}

#define SET_VALUE_WRAP_ERROR(node, value)                                      \
    do {                                                                       \
        double __min, __max;                                                   \
        GError* __error = nullptr;                                             \
        arv_camera_get_##node##_bounds(_camera, &__min, &__max, &__error);     \
        if (__error)                                                           \
            throw std::runtime_error(__error->message);                        \
        WRAP_ERROR(arv_camera_set_##node(                                      \
          _camera, std::max(__min, std::min(value, __max)), &error));          \
    } while (0)

bool
AravisBackend::set(int propId, double value)
{
    if (_isAcquiring)
        stopAcquisition();

    switch (propId) {
        case cv::CAP_PROP_AUTO_EXPOSURE:
            WRAP_ERROR(arv_camera_set_exposure_time_auto(
              _camera,
              value == 0.0 ? ARV_AUTO_OFF : ARV_AUTO_CONTINUOUS,
              &error));
            break;
        case cv::CAP_PROP_EXPOSURE:
            SET_VALUE_WRAP_ERROR(exposure_time, value);
            break;
        case cv::CAP_PROP_FPS:
            WRAP_ERROR(arv_camera_set_frame_rate_enable(_camera, true, &error));
            SET_VALUE_WRAP_ERROR(frame_rate, value);
            break;
        case cv::CAP_PROP_TRIGGER:
            WRAP_ERROR(arv_camera_set_trigger(
              _camera, value == 0.0 ? nullptr : "Line0", &error));
            break;
        default:
            throw std::runtime_error("Unsupporeted property");
    }

    return true;
}

bool
AravisBackend::isOpened() const noexcept
{
    return ARV_IS_CAMERA(_camera) && ARV_IS_STREAM(_stream);
}

}