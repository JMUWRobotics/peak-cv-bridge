#pragma once

#include <opencv2/videoio.hpp>
#include <peak/peak.hpp>

namespace cv {

class PeakVideoCapture : public VideoCapture
{
  private:
    static std::atomic_size_t _instanceCount;

    bool _debayer, _isAcquiring = false;
    uint64_t _bufferTimeout;
    enum PixelFormat
    {
        UNKNOWN,
        Mono8,
        BayerRG8,
    } _pixelFormat = UNKNOWN;

    std::shared_ptr<peak::core::Device> _device;
    std::shared_ptr<peak::core::DataStream> _dataStream;
    std::shared_ptr<peak::core::NodeMap> _nodeMap;
    std::shared_ptr<peak::core::Buffer> _filledBuffer;

  public:
    PeakVideoCapture(
      bool debayer = false,
      uint64_t bufferTimeoutMs = peak::core::Timeout::INFINITE_TIMEOUT);
    PeakVideoCapture(
      int index,
      bool debayer = false,
      uint64_t bufferTimeoutMs = peak::core::Timeout::INFINITE_TIMEOUT);

    virtual ~PeakVideoCapture() override;

    // Second parameter unused
    virtual bool open(int index, int = 0) override;

    virtual void release() override;

    virtual bool isOpened() const override;

    virtual bool grab() override;

    void startAcquisition();
    void stopAcquisition();

    // Second parameter unused
    virtual bool retrieve(OutputArray image, int = 0) override;

    virtual bool read(OutputArray image) override;

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
    virtual double get(int propId) const override;

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
    virtual bool set(int propId, double value) override;
};

}