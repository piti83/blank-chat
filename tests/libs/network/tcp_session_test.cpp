#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <boost/asio.hpp>
#include <gtest/gtest.h>

#include <network/tcp_session.h>
#include <protocol/frame.h>
#include <protocol/i_frame_handler.h>
#include <protocol/mailbox_id.h>

namespace bc::network::test {

class MockFrameHandler : public bc::protocol::IFrameHandler
{
public:
    MockFrameHandler() = default;
    ~MockFrameHandler() override = default;

    MockFrameHandler(const MockFrameHandler&) = delete;
    auto operator=(const MockFrameHandler&) -> MockFrameHandler& = delete;
    MockFrameHandler(MockFrameHandler&&) = delete;
    auto operator=(MockFrameHandler&&) -> MockFrameHandler& = delete;

    std::size_t pushCallCount{0};
    std::size_t pollCallCount{0};
    bc::protocol::Payload lastPushPayload{};

    std::optional<bc::protocol::Frame> mockPollResponse{std::nullopt};

    auto ProcessPush(bc::protocol::Frame&& frame) -> void override
    {
        pushCallCount++;
        lastPushPayload = std::move(frame).ExtractPayload();
    }

    [[nodiscard]] auto ProcessPoll(const bc::protocol::MailboxID& /*mid*/)
        -> std::optional<bc::protocol::Frame> override
    {
        pollCallCount++;
        return std::move(mockPollResponse);
    }
};

class TcpSessionTest : public ::testing::Test
{
protected:
    boost::asio::io_context ioContext;
    std::unique_ptr<boost::asio::ip::tcp::socket> clientSocket;
    MockFrameHandler mockHandler;
    bc::protocol::MailboxID testMailboxId;

    void SetUp() override
    {
        testMailboxId.Fill(0xAA);

        boost::asio::ip::tcp::acceptor acceptor(
            ioContext, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 0));

        clientSocket = std::make_unique<boost::asio::ip::tcp::socket>(ioContext);
        clientSocket->connect(acceptor.local_endpoint());

        boost::asio::ip::tcp::socket serverSocket(ioContext);
        acceptor.accept(serverSocket);

        session = std::make_shared<TcpSession>(std::move(serverSocket), mockHandler);
        session->Start();
    }

    void TearDown() override
    {
        if (clientSocket && clientSocket->is_open()) {
            boost::system::error_code ec;
            clientSocket->close(ec);
        }
        ioContext.stop();
    }

    auto PumpIoContext() -> void
    {
        ioContext.restart();
        ioContext.run_for(std::chrono::milliseconds(100));
    }

    std::shared_ptr<TcpSession> session;
};

TEST_F(TcpSessionTest, GracefullyHandlesClientDisconnectWithoutCrashing)
{
    clientSocket->close();
    PumpIoContext();

    EXPECT_EQ(mockHandler.pushCallCount, 0);
    EXPECT_EQ(mockHandler.pollCallCount, 0);
}

TEST_F(TcpSessionTest, SuccessfullyParsesAndInjectsPushFrame)
{
    bc::protocol::Payload payload = {0x01, 0x02, 0x03};
    auto frame = bc::protocol::Frame::CreatePush(testMailboxId, std::move(payload));
    auto serialized = frame.Serialize();

    boost::asio::write(*clientSocket, boost::asio::buffer(serialized));
    PumpIoContext();

    EXPECT_EQ(mockHandler.pushCallCount, 1);
    EXPECT_EQ(mockHandler.lastPushPayload, (bc::protocol::Payload{0x01, 0x02, 0x03}));
}

TEST_F(TcpSessionTest, HandlesPollRequestAndSendsResponseBack)
{
    auto pollFrame = bc::protocol::Frame::CreatePoll(testMailboxId);
    auto serializedRequest = pollFrame.Serialize();

    bc::protocol::Payload responseData = {0xFF, 0xEE};
    mockHandler.mockPollResponse =
        bc::protocol::Frame::CreatePush(testMailboxId, std::move(responseData));

    boost::asio::write(*clientSocket, boost::asio::buffer(serializedRequest));
    PumpIoContext();

    EXPECT_EQ(mockHandler.pollCallCount, 1);

    std::vector<std::uint8_t> rxBuffer(1024);
    boost::system::error_code ec;
    std::size_t bytesRead = clientSocket->read_some(boost::asio::buffer(rxBuffer), ec);

    ASSERT_FALSE(ec);
    ASSERT_GT(bytesRead, 0);

    bc::protocol::FrameParser clientParser;
    clientParser.FeedBytes(std::span<const std::uint8_t>(rxBuffer.data(), bytesRead));

    auto extracted = clientParser.TryExtractFrame();
    ASSERT_TRUE(extracted.has_value());
    EXPECT_EQ(extracted->GetPayload(), (bc::protocol::Payload{0xFF, 0xEE}));
}

TEST_F(TcpSessionTest, DisconnectsMaliciousClientSendingGarbageData)
{
    std::vector<std::uint8_t> garbageData(200, 0x99);

    boost::asio::write(*clientSocket, boost::asio::buffer(garbageData));
    PumpIoContext();

    std::vector<std::uint8_t> rxBuffer(10);
    boost::system::error_code ec;
    clientSocket->read_some(boost::asio::buffer(rxBuffer), ec);

    EXPECT_EQ(ec, boost::asio::error::eof);
    EXPECT_EQ(mockHandler.pushCallCount, 0);
}

TEST_F(TcpSessionTest, SuccessfullyParsesTcpFragmentedFrames)
{
    bc::protocol::Payload payload = {0x42};
    auto frame = bc::protocol::Frame::CreatePush(testMailboxId, std::move(payload));
    auto serialized = frame.Serialize();

    for (const auto& byte : serialized) {
        boost::asio::write(*clientSocket, boost::asio::buffer(&byte, 1));
        ioContext.restart();
        ioContext.run_for(std::chrono::milliseconds(5));
    }

    EXPECT_EQ(mockHandler.pushCallCount, 1);
    EXPECT_EQ(mockHandler.lastPushPayload, (bc::protocol::Payload{0x42}));
}

TEST_F(TcpSessionTest, GracefullyHandlesWriteErrorsDuringResponse)
{
    auto pollFrame = bc::protocol::Frame::CreatePoll(testMailboxId);

    bc::protocol::Payload bigData(50000, 0xAA);
    mockHandler.mockPollResponse =
        bc::protocol::Frame::CreatePush(testMailboxId, std::move(bigData));

    boost::asio::write(*clientSocket, boost::asio::buffer(pollFrame.Serialize()));

    clientSocket->close();

    PumpIoContext();

    EXPECT_EQ(mockHandler.pollCallCount, 1);

    SUCCEED();
}

} // namespace bc::network::test
