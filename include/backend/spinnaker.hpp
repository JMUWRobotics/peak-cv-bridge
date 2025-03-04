#pragma once

#include "impl.hpp"

#include <atomic>

#include <Spinnaker.h>

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
    virtual void release() override;
    virtual bool isOpened() const override;
    virtual bool grab() override;
    virtual bool retrieve(cv::OutputArray image) override;
    virtual double get(int propId) const override;
    virtual bool set(int propId, double value) override;
    virtual void startAcquisition() override;
    virtual void stopAcquisition() override;
};

}