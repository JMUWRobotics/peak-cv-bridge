#pragma once

#include "impl.hpp"

#include <atomic>
#include <optional>

#include <peak/peak.hpp>

namespace XVII::detail {

class IdsPeakBackend : public Impl
{
  private:
    static std::atomic_size_t _instanceCount;
    std::shared_ptr<peak::core::Device> _device;
    std::shared_ptr<peak::core::DataStream> _dataStream;
    std::shared_ptr<peak::core::NodeMap> _nodeMap;
    std::shared_ptr<peak::core::Buffer> _filledBuffer;

  public:
    IdsPeakBackend(bool debayer, std::optional<uint64_t> bufferTimeoutMs);
    ~IdsPeakBackend() override;
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
