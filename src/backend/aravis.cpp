#include "backend/aravis.hpp"
#include "backend/impl.hpp"

#include <arv.h>

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
    GError* error = nullptr;

    arv_update_device_list();
    n_devices = arv_get_n_devices();

    if (index >= n_devices)
        throw std::invalid_argument("Index out of range");

    _device = arv_open_device(arv_get_device_id(index), &error);

	if (!ARV_IS_DEVICE(_device))
		goto abort;

	_camera = arv_camera_new_with_device(_device, &error);

	if (!ARV_IS_CAMERA(_camera))
		goto abort;

	arv_camera_set_acquisition_mode(_camera, ARV_ACQUISITION_MODE_CONTINUOUS, &error);
	if (error)
		goto abort;

	_stream = arv_camera_create_stream(_camera, NULL, NULL, &error);
	if (!ARV_IS_STREAM(&_stream))
		goto abort;

	payload = arv_camera_get_payload(_camera, &error);
	if (error)
		goto abort;

	for (size_t i = 0; i < 3; ++i)
		arv_stream_push_buffer(_stream, arv_buffer_new(payload, NULL));

	return true;
abort:
	g_clear_object(&_stream);
	g_clear_object(&_camera);
	g_clear_object(&_device);
	if (error)
		throw std::runtime_error(error->message);
	return false;
}

#define WRAP_ERROR(expr) do {\
	GError *error = nullptr;\
	expr;\
	if (error)\
		throw std::runtime_error(error->message);\
} while (0)

void AravisBackend::startAcquisition() {
	WRAP_ERROR(arv_camera_start_acquisition(_camera, &error));
}

void AravisBackend::stopAcquisition() {
	WRAP_ERROR(arv_camera_stop_acquisition(_camera, &error));
}

void AravisBackend::release() noexcept {
	g_clear_object(&_stream);
	g_clear_object(&_camera);
	g_clear_object(&_device);
}

}