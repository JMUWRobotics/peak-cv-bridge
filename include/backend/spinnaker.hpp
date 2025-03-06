#pragma once

#include "impl.hpp"

#include <atomic>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverloaded-virtual"
#include <Spinnaker.h>
#pragma GCC diagnostic pop

namespace XVII::detail {

class SpinnakerBackend : public Impl
{
  private:
    static std::atomic_size_t _instanceCount;
    static Spinnaker::SystemPtr _sys;
    Spinnaker::CameraPtr _camera;
    Spinnaker::ImagePtr _filledBuffer;

  public:
    SpinnakerBackend(bool debayer, std::optional<uint64_t> bufferTimeoutMs);
    ~SpinnakerBackend() override;
    virtual bool open(int index) override;
    virtual void release() noexcept override;
    virtual bool isOpened() const noexcept override;
    virtual bool grab() override;
    virtual bool retrieve(cv::OutputArray image) override;
    virtual double get(int propId) const override;
    virtual bool set(int propId, double value) override;
    virtual void startAcquisition() override;
    virtual void stopAcquisition() override;
};

}