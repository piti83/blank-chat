#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <boost/asio.hpp>
#include <gtest/gtest.h>

#include <core/string_utils.h>
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

    auto AuthenticateClient(bool validPoW = true) -> void
    {
        PumpIoContext();

        std::vector<std::uint8_t> rxBuffer(1024);
        boost::system::error_code ec;
        std::size_t bytes = clientSocket->read_some(boost::asio::buffer(rxBuffer), ec);
        ASSERT_FALSE(ec);

        bc::protocol::FrameParser parser;
        parser.FeedBytes(std::span<const std::uint8_t>(rxBuffer.data(), bytes));
        auto frameOpt = parser.TryExtractFrame();

        ASSERT_TRUE(frameOpt.has_value());
        ASSERT_EQ(frameOpt->GetActionType(), bc::protocol::ActionType::AUTH_CHALLENGE);

        auto challenge = frameOpt->GetPayload();
        std::uint64_t nonce = 0;

        if (validPoW) {
            std::string hashHex;
            std::vector<std::uint8_t> combined = challenge;
            combined.resize(challenge.size() + sizeof(nonce));
            do {
                nonce++;
                std::memcpy(combined.data() + challenge.size(), &nonce, sizeof(nonce));
                hashHex = bc::core::HashPayload(combined);
            } while (!hashHex.starts_with("000"));
        } else {
            nonce = 42;
        }

        std::vector<std::uint8_t> responsePayload(sizeof(nonce));
        std::memcpy(responsePayload.data(), &nonce, sizeof(nonce));

        bc::protocol::MailboxID dummy;
        dummy.Fill(0);
        auto respFrame = bc::protocol::Frame::CreateAuthResponse(dummy, responsePayload);

        boost::asio::write(*clientSocket, boost::asio::buffer(respFrame.Serialize()));
        PumpIoContext();
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

TEST_F(TcpSessionTest, DropsConnectionOnPrematurePush)
{
    bc::protocol::Payload payload = {0x01, 0x02};
    auto frame = bc::protocol::Frame::CreatePush(testMailboxId, std::move(payload));

    boost::asio::write(*clientSocket, boost::asio::buffer(frame.Serialize()));
    PumpIoContext();

    EXPECT_EQ(mockHandler.pushCallCount, 0);
}

TEST_F(TcpSessionTest, DropsConnectionOnInvalidPoW)
{
    AuthenticateClient(false);

    std::vector<std::uint8_t> rxBuffer(10);
    boost::system::error_code ec;
    clientSocket->read_some(boost::asio::buffer(rxBuffer), ec);

    EXPECT_EQ(ec, boost::asio::error::eof);
}

TEST_F(TcpSessionTest, SuccessfullyParsesAndInjectsPushFrame)
{
    AuthenticateClient(true);

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
    AuthenticateClient(true);

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
    PumpIoContext();
    std::vector<std::uint8_t> authBuffer(1024);
    boost::system::error_code authEc;
    clientSocket->read_some(boost::asio::buffer(authBuffer), authEc);
    ASSERT_FALSE(authEc);

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
    AuthenticateClient(true);

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
    AuthenticateClient(true);

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
