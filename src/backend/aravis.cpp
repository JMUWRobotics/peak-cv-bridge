#include "backend/aravis.hpp"
#include "backend/impl.hpp"

#include <arv.h>

namespace XVII::detail {

AravisBackend::AravisBackend(bool debayer,
                             std::optional<uint64_t> bufferTimeoutMs)
  : Impl(debayer, bufferTimeoutMs)
{
}

}