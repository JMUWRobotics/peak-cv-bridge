#pragma once

#include "pixelformat.hpp"

#include <optional>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace XVII::detail {

class Impl
{
  protected:
    PixelFormat _pixelFormat = PixelFormat::UNKNOWN;
    bool _isAcquiring = false;

    bool _debayer;
    std::optional<uint64_t> _bufferTimeoutMs;

  public:
    inline Impl(bool debayer, std::optional<uint64_t> bufferTimeoutMs)
      : _debayer(debayer)
      , _bufferTimeoutMs(bufferTimeoutMs)
    {
    }
    virtual bool open(int index) = 0;
    virtual inline void release() noexcept
    {
        if (_isAcquiring) {
            try {
                stopAcquisition();
            } catch (...) {
                _isAcquiring = false;
            }
        }
    }
    virtual bool isOpened() const noexcept = 0;
    virtual bool grab() = 0;
    virtual bool retrieve(cv::OutputArray image) = 0;
    virtual double get(int propId) const = 0;
    virtual bool set(int propId, double value) = 0;
    virtual void startAcquisition() = 0;
    virtual void stopAcquisition() = 0;
    virtual ~Impl() = default;
    inline void debayer(size_t rows,
                        size_t cols,
                        int type,
                        const void* data,
                        size_t step,
                        cv::OutputArray output)
    {
        const cv::Mat ref(rows, cols, type, const_cast<void*>(data), step);
        if (!_debayer || _pixelFormat == PixelFormat::UNKNOWN ||
            _pixelFormat == PixelFormat::Mono8)
            ref.copyTo(output);
        else {
            int code;
            switch (_pixelFormat) {
                case PixelFormat::BayerRG8:
                    code = cv::COLOR_BayerRG2BGR;
                    break;
                case PixelFormat::BayerBG8:
                    code = cv::COLOR_BayerBG2BGR;
                    break;
                default:
                    throw std::runtime_error("unknown pixelformat");
            }
            cv::cvtColor(ref, output, code);
        }
    }
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
};

}