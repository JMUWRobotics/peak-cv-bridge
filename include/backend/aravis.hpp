#pragma once

#include "impl.hpp"

#include <arv.h>

namespace XVII::detail {

class AravisBackend : public Impl
{
  private:
    ArvCamera* _camera = nullptr;
    ArvStream* _stream = nullptr;
    ArvBuffer* _buffer = nullptr;

  public:
    AravisBackend(bool debayer, std::optional<uint64_t> bufferTimeoutMs);
    bool open(int index) override;
    void release() noexcept override;
    bool isOpened() const noexcept override;
    bool grab() override;
    bool retrieve(cv::OutputArray image) override;
    double get(int propId) const override;
    bool set(int propId, double value) override;
    void startAcquisition() override;
    void stopAcquisition() override;
};

}
