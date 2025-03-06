#pragma once

#include <optional>

#include <opencv2/videoio.hpp>

namespace XVII {

namespace detail {
class Impl;
}

class GenICamVideoCapture : public cv::VideoCapture
{
  private:
    bool _debayer = false, _isAcquiring = false;
    std::optional<uint64_t> _bufferTimeoutMs = std::nullopt;

    std::unique_ptr<detail::Impl> _impl;

  public:
    enum class Backend : int
    {
        ANY = 0,
        ARAVIS = 1,
        IDS_PEAK = 2,
        SPINNAKER = 3
    };
    static std::unique_ptr<GenICamVideoCapture> OpenAnyCamera(
      bool debayer = false,
      std::optional<uint64_t> bufferTimeoutMs = std::nullopt);
    GenICamVideoCapture(bool debayer = false,
                        std::optional<uint64_t> bufferTimeoutMs = std::nullopt);
    GenICamVideoCapture(int index,
                        Backend backend = Backend::ARAVIS,
                        bool debayer = false,
                        std::optional<uint64_t> bufferTimeoutMs = std::nullopt);
    ~GenICamVideoCapture() override;

    bool open(int index, int backend) noexcept(false) override;
    void release() noexcept override;
    bool isOpened() const noexcept override;
    bool grab() noexcept(false) override;
    bool retrieve(cv::OutputArray image,
                  [[maybe_unused]] int = 0) noexcept(false) override;
    bool read(cv::OutputArray image) noexcept(false) override;

    /**
     *  Implemented properties:
     *
     *  - cv::CAP_PROP_AUTO_EXPOSURE:
     *      Zero if autoexposure in camera driver is disabled, else non-zero.
     *  - cv::CAP_PROP_EXPOSURE:
     *      Gets current exposuretime in µs.
     *  - cv::CAP_PROP_FPS:
     *      Gets current framerate.
     *  - cv::CAP_PROP_TRIGGER:
     *      Zero if trigger-mode is disabled, else non-zero.
     */
    virtual double get(int propId) const noexcept(false) override;

    /**
     *  Implemented properties:
     *
     *  - cv::CAP_PROP_AUTO_EXPOSURE:
     *      Enables or disables autoexposure in camera driver.
     *  - cv::CAP_PROP_EXPOSURE:
     *      Sets exposuretime to a value in µs.
     *      Autoexposure needs to be disabled for this to work correctly.
     *  - cv::CAP_PROP_FPS:
     *      Sets FPS target.
     *      This may not necessarily be reached, based on camera capabilities
     *      and exposure time.
     *  - cv::CAP_PROP_TRIGGER:
     *      Enables or disables trigger on Line0.
     */
    virtual bool set(int propId, double value) noexcept(false) override;

    void startAcquisition() noexcept(false);
    void stopAcquisition() noexcept(false);

    class Exception : public std::exception
    {
      private:
        std::string _message;

      public:
        template<typename TException>
        Exception(const TException& other)
          : _message(other.what())
        {
        }
        const char* what() const noexcept override;
    };
};

auto inline format_as(const GenICamVideoCapture::Backend& b)
{
    switch (b) {
        case GenICamVideoCapture::Backend::ARAVIS:
            return "Aravis";
        case GenICamVideoCapture::Backend::IDS_PEAK:
            return "IDS-Peak";
        case GenICamVideoCapture::Backend::SPINNAKER:
            return "Spinnaker";
        case GenICamVideoCapture::Backend::ANY:
            return "Any";
        default:
            __builtin_unreachable();
    }
}

}