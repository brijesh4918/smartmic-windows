#include "smartmic/media/udp_transport.h"

#include <cstring>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
using socklen_t = int;
#define SM_CLOSE_SOCKET closesocket
#define SM_INVALID_SOCKET INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define SM_CLOSE_SOCKET ::close
#define SM_INVALID_SOCKET (-1)
#endif

#include "smartmic/logging.h"

namespace smartmic::media {
namespace {
constexpr char kComponent[] = "UdpTransport";
// One media packet is a couple of hundred bytes; this is generous and bounded.
constexpr size_t kMaxDatagram = 2048;

#if defined(_WIN32)
struct WinsockInit {
    WinsockInit() { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); }
    ~WinsockInit() { WSACleanup(); }
};
const WinsockInit g_winsock;
#endif
}  // namespace

UdpTransport::UdpTransport(std::string name, uint16_t localPort)
    : name_(std::move(name)), requestedPort_(localPort) {}

UdpTransport::~UdpTransport() { stop(); }

bool UdpTransport::start() {
    if (running_.load()) return true;

    socket_ = static_cast<int>(::socket(AF_INET, SOCK_DGRAM, 0));
    if (socket_ == SM_INVALID_SOCKET) {
        logError(kComponent, "socket() failed");
        return false;
    }

    int reuse = 1;
    ::setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(requestedPort_);
    if (::bind(socket_, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
        logError(kComponent, "bind() failed on port " + std::to_string(requestedPort_));
        SM_CLOSE_SOCKET(socket_);
        socket_ = SM_INVALID_SOCKET;
        return false;
    }

    sockaddr_in bound{};
    socklen_t boundLen = sizeof(bound);
    if (::getsockname(socket_, reinterpret_cast<sockaddr*>(&bound), &boundLen) == 0) {
        boundPort_ = ntohs(bound.sin_port);
    }

    running_.store(true);
    thread_ = std::thread([this] { receiveLoop(); });
    return true;
}

void UdpTransport::stop() {
    if (!running_.exchange(false)) return;
    if (socket_ != SM_INVALID_SOCKET) {
        // Shutdown unblocks the receive thread without relying on a timeout.
#if defined(_WIN32)
        ::shutdown(socket_, SD_BOTH);
#else
        ::shutdown(socket_, SHUT_RDWR);
#endif
        SM_CLOSE_SOCKET(socket_);
        socket_ = SM_INVALID_SOCKET;
    }
    if (thread_.joinable()) thread_.join();
}

bool UdpTransport::setPeer(const std::string& host, uint16_t port) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    const std::string portStr = std::to_string(port);
    if (::getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0 || !res) {
        logError(kComponent, "cannot resolve peer " + host);
        return false;
    }
    std::memcpy(peerAddr_, res->ai_addr, res->ai_addrlen);
    peerAddrLen_ = res->ai_addrlen;
    ::freeaddrinfo(res);
    peerDescription_ = host + ":" + portStr;
    return true;
}

bool UdpTransport::send(const uint8_t* data, size_t len) {
    if (socket_ == SM_INVALID_SOCKET || peerAddrLen_ == 0) {
        ++stats_.sendFailures;
        return false;
    }
    const auto n = ::sendto(socket_, reinterpret_cast<const char*>(data), static_cast<int>(len), 0,
                            reinterpret_cast<const sockaddr*>(peerAddr_),
                            static_cast<socklen_t>(peerAddrLen_));
    if (n < 0) {
        ++stats_.sendFailures;
        return false;
    }
    ++stats_.packetsSent;
    stats_.bytesSent += len;
    return true;
}

void UdpTransport::receiveLoop() {
    std::vector<uint8_t> buf(kMaxDatagram);
    while (running_.load()) {
        sockaddr_in from{};
        socklen_t fromLen = sizeof(from);
        const auto n = ::recvfrom(socket_, reinterpret_cast<char*>(buf.data()),
                                  static_cast<int>(buf.size()), 0,
                                  reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n <= 0) {
            if (!running_.load()) break;
            continue;
        }
        if (peerAddrLen_ == 0) {
            // First contact on a listening socket: remember where to reply.
            std::memcpy(peerAddr_, &from, fromLen);
            peerAddrLen_ = fromLen;
            char text[INET_ADDRSTRLEN] = {0};
            ::inet_ntop(AF_INET, &from.sin_addr, text, sizeof(text));
            peerDescription_ = std::string(text) + ":" + std::to_string(ntohs(from.sin_port));
            logInfo(kComponent, "peer is " + peerDescription_);
        }

        // Anything that is not from a peer we can authenticate is rejected one
        // layer up by SecureChannel, so no source-address filtering is done
        // here: address filtering would be security theatre on a LAN.
        ++stats_.packetsReceived;
        stats_.bytesReceived += static_cast<size_t>(n);
        if (handler_) handler_(buf.data(), static_cast<size_t>(n));
    }
}

std::string UdpTransport::describe() const {
    return "udp:" + name_ + "(local " + std::to_string(boundPort_) +
           (peerDescription_.empty() ? "" : " -> " + peerDescription_) + ")";
}

}  // namespace smartmic::media
