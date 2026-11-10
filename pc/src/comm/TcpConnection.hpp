#pragma once
#ifdef _WIN32
#include "Connection.hpp"
#include "LineSplitter.hpp"
#include <atomic>
#include <mutex>
#include <winsock2.h>
#include <ws2tcpip.h>
// Numeric IPv4 only: no blocking resolver and no discovery. Credentials never
// enter DeviceLink logs.
class TcpConnection final : public IConnection
{
  public:
    TcpConnection(std::string host, unsigned port, std::string token)
        : host_(std::move(host)), port_(port), token_(std::move(token)), split_([this](std::string l) {
              // An authentication rejection is terminal, including the rest of this packet.
              if (blocked_)
                  return;
              if (!ready_)
              {
                  if (l == "OK")
                  {
                      ready_ = true;
                      ++opens_;
                      problem_.clear();
                  }
                  else if (l.rfind("ERR", 0) == 0)
                  {
                      blocked_ = true;
                      problem_ = "TCP authentication failed";
                  }
              }
              else
                  EmitLine(std::move(l));
          })
    {
        WSADATA w{};
        initialized_ = WSAStartup(MAKEWORD(2, 2), &w) == 0;
    }
    ~TcpConnection() override
    {
        Close();
        if (initialized_)
            WSACleanup();
    }
    void Tick(std::int64_t now) override
    {
        std::lock_guard lock(m_);
        if (!enabled_ || blocked_)
        {
            Close();
            return;
        }
        if (sock_ == INVALID_SOCKET)
        {
            if (now < retry_)
                return;
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_port = htons(static_cast<u_short>(port_));
            if (!initialized_ || port_ == 0 || port_ > 65535 || InetPtonA(AF_INET, host_.c_str(), &a.sin_addr) != 1 ||
                token_.empty() || token_.find_first_of(" \r\n\t") != std::string::npos)
            {
                problem_ = "Set numeric IPv4, port 1..65535 and token";
                blocked_ = true;
                return;
            }
            sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (sock_ == INVALID_SOCKET)
            {
                Fail(now);
                return;
            }
            u_long nonblock = 1;
            if (ioctlsocket(sock_, FIONBIO, &nonblock) != 0)
            {
                Fail(now);
                return;
            }
            int rc = connect(sock_, reinterpret_cast<sockaddr *>(&a), sizeof(a));
            if (rc == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
            {
                Fail(now);
                return;
            }
            connecting_ = true;
            deadline_ = now + 5000;
            problem_ = "TCP connecting";
        }
        if (now > deadline_ && !ready_)
        {
            Fail(now);
            return;
        }
        if (connecting_)
        {
            fd_set wr, er;
            FD_ZERO(&wr);
            FD_ZERO(&er);
            FD_SET(sock_, &wr);
            FD_SET(sock_, &er);
            timeval tv{};
            int rc = select(0, nullptr, &wr, &er, &tv);
            if (rc == SOCKET_ERROR || FD_ISSET(sock_, &er))
            {
                Fail(now);
                return;
            }
            if (!FD_ISSET(sock_, &wr))
                return;
            int error = 0, len = sizeof(error);
            if (getsockopt(sock_, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&error), &len) != 0 || error)
            {
                Fail(now);
                return;
            }
            connecting_ = false;
            pending_ = "auth " + token_ + "\n";
        }
        if (!pending_.empty())
        {
            int n = send(sock_, pending_.data(), static_cast<int>(pending_.size()), 0);
            if (n > 0)
                pending_.erase(0, n);
            else if (n == 0 || WSAGetLastError() != WSAEWOULDBLOCK)
            {
                Fail(now);
                return;
            }
        }
        char buf[4096];
        for (int i = 0; i < 16; ++i)
        {
            int n = recv(sock_, buf, sizeof(buf), 0);
            if (n > 0)
                split_.Feed(buf, n);
            else
            {
                if (n == 0 || WSAGetLastError() != WSAEWOULDBLOCK)
                    Fail(now);
                break;
            }
            if (blocked_)
            {
                Close();
                break;
            }
        }
    }
    bool IsOpen() override
    {
        std::lock_guard l(m_);
        return ready_;
    }
    bool Write(const std::string &d) override
    {
        std::lock_guard l(m_);
        if (!ready_ || pending_.size() + d.size() > 65536)
            return false;
        pending_ += d;
        return true;
    }
    void SetEnabled(bool b) override
    {
        enabled_ = b;
        if (b)
        {
            std::lock_guard l(m_);
            blocked_ = false;
            retry_ = 0;
        }
    }
    bool Enabled() const override
    {
        return enabled_;
    }
    void Reject(std::int64_t now, bool foreign) override
    {
        std::lock_guard l(m_);
        Close();
        blocked_ = foreign;
        retry_ = now + 30000;
        problem_ = "TCP endpoint is not BWM427";
    }
    void Reopen(std::int64_t now) override
    {
        std::lock_guard l(m_);
        Fail(now);
    }
    std::string PortName() override
    {
        return "tcp://" + host_ + ":" + std::to_string(port_);
    }
    std::uint64_t OpenCount() override
    {
        std::lock_guard l(m_);
        return opens_;
    }
    std::vector<PortInfo> Ports() override
    {
        return {};
    }
    void SetPortChoice(const std::string &) override
    {
    }
    bool IsDemo() const override
    {
        return false;
    }
    std::string Problem() override
    {
        std::lock_guard l(m_);
        return problem_;
    }

  private:
    void Close()
    {
        if (sock_ != INVALID_SOCKET)
            closesocket(sock_);
        sock_ = INVALID_SOCKET;
        ready_ = false;
        connecting_ = false;
        pending_.clear();
        split_.Reset();
    }
    void Fail(std::int64_t now)
    {
        Close();
        retry_ = now + 1000;
        problem_ = "TCP disconnected; retrying";
    }
    std::mutex m_;
    std::atomic<bool> enabled_{true};
    bool initialized_ = false, ready_ = false, connecting_ = false, blocked_ = false;
    SOCKET sock_ = INVALID_SOCKET;
    std::string host_, token_, pending_, problem_;
    unsigned port_;
    LineSplitter split_;
    std::uint64_t opens_ = 0;
    std::int64_t retry_ = 0, deadline_ = 0;
};
#endif
