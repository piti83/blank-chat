#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include <boost/asio.hpp>
#include <gtest/gtest.h>

#include <network/tcp_server.h>
#include <protocol/frame.h>
#include <protocol/i_frame_handler.h>
#include <protocol/mailbox_id.h>

namespace bc::network::test {

namespace {

class ServerMockFrameHandler : public bc::protocol::IFrameHandler
{
public:
    ServerMockFrameHandler() = default;
    ~ServerMockFrameHandler() override = default;

    ServerMockFrameHandler(const ServerMockFrameHandler&) = delete;
    auto operator=(const ServerMockFrameHandler&) -> ServerMockFrameHandler& = delete;

    std::atomic<std::size_t> pushCallCount{0};

    auto ProcessPush(bc::protocol::Frame&& /*frame*/) -> void override
    {
        pushCallCount++;
    }

    [[nodiscard]] auto ProcessPoll(const bc::protocol::MailboxID& /*mid*/)
        -> std::optional<bc::protocol::Frame> override
    {
        return std::nullopt;
    }
};

std::atomic<std::uint16_t> testPortAllocator{40000};

} // namespace

class TcpServerTest : public ::testing::Test
{
protected:
    boost::asio::io_context ioContext;
    ServerMockFrameHandler mockHandler;
    std::uint16_t currentTestPort;

    void SetUp() override
    {
        currentTestPort = testPortAllocator.fetch_add(1, std::memory_order_relaxed);
    }

    auto PumpIoContext(std::chrono::milliseconds duration = std::chrono::milliseconds(50)) -> void
    {
        ioContext.restart();
        ioContext.run_for(duration);
    }
};

TEST_F(TcpServerTest, ConstructorThrowsWhenPortIsAlreadyBound)
{
    boost::system::error_code ec;
    boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::v4(), currentTestPort);
    boost::asio::ip::tcp::acceptor blocker(ioContext);

    blocker.open(endpoint.protocol(), ec);
    ASSERT_FALSE(ec);
    blocker.bind(endpoint, ec);
    ASSERT_FALSE(ec);
    blocker.listen(boost::asio::socket_base::max_listen_connections, ec);
    ASSERT_FALSE(ec);

    EXPECT_DEATH({ TcpServer server(ioContext, currentTestPort, mockHandler); }, ".*");
}

TEST_F(TcpServerTest, ConstructorSucceedsOnAvailablePort)
{
    TcpServer server(ioContext, currentTestPort, mockHandler);
    server.Start();
}

TEST_F(TcpServerTest, GracefullyHandlesOperationAbortedOnDestruction)
{
    std::optional<TcpServer> server;
    server.emplace(ioContext, currentTestPort, mockHandler);
    server->Start();
    server.reset();

    PumpIoContext();
    EXPECT_TRUE(ioContext.stopped());
}

TEST_F(TcpServerTest, SurvivesImmediateClientDisconnectionLikePortScanners)
{
    TcpServer server(ioContext, currentTestPort, mockHandler);
    server.Start();

    for (int i = 0; i < 50; ++i) {
        boost::asio::ip::tcp::socket maliciousClient(ioContext);
        boost::system::error_code ec;

        maliciousClient.connect(boost::asio::ip::tcp::endpoint(
                                    boost::asio::ip::address_v4::loopback(), currentTestPort),
                                ec);

        boost::asio::socket_base::linger option(true, 0);
        maliciousClient.set_option(option, ec);
        maliciousClient.close(ec);
    }

    // Removed EXPECT_NO_THROW.
    PumpIoContext(std::chrono::milliseconds(200));

    EXPECT_EQ(mockHandler.pushCallCount.load(), 0);
}

TEST_F(TcpServerTest, AcceptsConnectionsAndProperlyRoutesDataToSessions)
{
    TcpServer server(ioContext, currentTestPort, mockHandler);
    server.Start();

    constexpr int clientsCount = 10;
    std::vector<std::unique_ptr<boost::asio::ip::tcp::socket>> clients;

    bc::protocol::MailboxID testMid;
    testMid.Fill(0xAA);

    for (int i = 0; i < clientsCount; ++i) {
        auto socket = std::make_unique<boost::asio::ip::tcp::socket>(ioContext);
        boost::system::error_code ec;
        socket->connect(boost::asio::ip::tcp::endpoint(boost::asio::ip::address_v4::loopback(),
                                                       currentTestPort),
                        ec);

        ASSERT_FALSE(ec) << "Failed to connect client " << i;

        auto frame = bc::protocol::Frame::CreatePush(testMid, {static_cast<uint8_t>(i)});
        auto serialized = frame.Serialize();

        boost::asio::write(*socket, boost::asio::buffer(serialized), ec);
        ASSERT_FALSE(ec);

        clients.push_back(std::move(socket));
    }

    PumpIoContext(std::chrono::milliseconds(500));

    EXPECT_EQ(mockHandler.pushCallCount.load(), clientsCount);
}

} // namespace bc::network::test
