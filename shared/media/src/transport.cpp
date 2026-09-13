#include "smartmic/media/transport.h"

namespace smartmic::media {

bool LoopbackTransport::send(const uint8_t* data, size_t len) {
    if (!running_ || !peer_) {
        ++stats_.sendFailures;
        return false;
    }
    ++stats_.packetsSent;
    stats_.bytesSent += len;
    peer_->deliver(data, len);
    return true;
}

void LoopbackTransport::deliver(const uint8_t* data, size_t len) {
    ++stats_.packetsReceived;
    stats_.bytesReceived += len;
    if (handler_) handler_(data, len);
}

}  // namespace smartmic::media
