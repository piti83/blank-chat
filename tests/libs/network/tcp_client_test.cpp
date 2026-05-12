#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include <boost/asio.hpp>
#include <gtest/gtest.h>

#include <network/tcp_client.h>
#include <protocol/frame.h>
#include <protocol/mailbox_id.h>

namespace bc::network::test {

class TcpClientTest : public ::testing::Test
{
protected:
    boost::asio::io_context serverIo;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> acceptor;
    std::uint16_t serverPort{0};
    std::thread serverThread;

    boost::asio::io_context clientIo;
    protocol::MailboxID defaultMailbox;

    void SetUp() override
    {
        defaultMailbox.Fill(0xAA);

        acceptor = std::make_unique<boost::asio::ip::tcp::acceptor>(
            serverIo, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 0));
        serverPort = acceptor->local_endpoint().port();

        serverThread = std::thread([this]() {
            auto workGuard = boost::asio::make_work_guard(serverIo);
            serverIo.run();
        });
    }

    void TearDown() override
    {
        serverIo.stop();

        if (serverThread.joinable()) {
            serverThread.join();
        }
    }
};

TEST_F(TcpClientTest, ConnectsSuccessfullyToValidEndpoint)
{
    TcpClient client(clientIo);
    bool result = client.Connect("127.0.0.1", serverPort);

    EXPECT_TRUE(result);
}

TEST_F(TcpClientTest, ConnectFailsGracefullyOnInvalidEndpoint)
{
    TcpClient client(clientIo);

    acceptor->close();

    bool result = client.Connect("127.0.0.1", serverPort);

    EXPECT_FALSE(result);
}

TEST_F(TcpClientTest, DisconnectClosesSocketSafelyMultipleTimes)
{
    TcpClient client(clientIo);
    ASSERT_TRUE(client.Connect("127.0.0.1", serverPort));

    EXPECT_NO_THROW({
        client.Disconnect();
        client.Disconnect();
    });
}

TEST_F(TcpClientTest, SendFrameSuccessfullyTransmitsDataZeroCopy)
{
    auto frame = protocol::Frame::CreatePush(defaultMailbox, {0x01, 0x02, 0x03});
    auto expectedSerialized = frame.Serialize();

    struct SharedState
    {
        std::mutex mutex;
        std::condition_variable cv;
        std::vector<uint8_t> data;
        bool ready = false;
    };
    auto state = std::make_shared<SharedState>();

    acceptor->async_accept([expectedSize = expectedSerialized.size(), state](
                               boost::system::error_code ec, boost::asio::ip::tcp::socket sock) {
        if (ec)
            return;

        auto sockPtr = std::make_shared<boost::asio::ip::tcp::socket>(std::move(sock));
        auto buffer = std::make_shared<std::vector<uint8_t>>(expectedSize);

        boost::asio::async_read(
            *sockPtr, boost::asio::buffer(*buffer),
            [sockPtr, buffer, state](boost::system::error_code ec, std::size_t) {
                if (!ec) {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    state->data = *buffer;
                    state->ready = true;
                    state->cv.notify_one();
                }
                boost::system::error_code ignored;
                sockPtr->close(ignored);
            });
    });

    TcpClient client(clientIo);
    ASSERT_TRUE(client.Connect("127.0.0.1", serverPort));

    bool sendResult = client.SendFrame(std::move(frame));
    EXPECT_TRUE(sendResult);

    {
        std::unique_lock<std::mutex> lock(state->mutex);
        bool waitResult =
            state->cv.wait_for(lock, std::chrono::seconds(2), [&] { return state->ready; });

        ASSERT_TRUE(waitResult) << "Timeout waiting for server to receive data";
        EXPECT_EQ(state->data, expectedSerialized);
    }

    client.Disconnect();

    serverIo.stop();
    if (serverThread.joinable()) {
        serverThread.join();
    }
}

TEST_F(TcpClientTest, SendFrameReturnsFalseWhenSocketIsClosed)
{
    TcpClient client(clientIo);

    auto frame = protocol::Frame::CreatePoll(defaultMailbox);
    bool result = client.SendFrame(std::move(frame));

    EXPECT_FALSE(result);
}

TEST_F(TcpClientTest, ReceiveFrameSuccessfullyParsesValidData)
{
    auto serverFrame = protocol::Frame::CreatePush(defaultMailbox, {0xFF, 0xEE});
    auto serialized = serverFrame.Serialize();

    acceptor->async_accept(
        [serialized](boost::system::error_code ec, boost::asio::ip::tcp::socket sock) {
            if (ec)
                return;
            boost::asio::write(sock, boost::asio::buffer(serialized));
        });

    TcpClient client(clientIo);
    ASSERT_TRUE(client.Connect("127.0.0.1", serverPort));

    auto receivedOpt = client.ReceiveFrame();

    ASSERT_TRUE(receivedOpt.has_value());
    EXPECT_EQ(receivedOpt->GetActionType(), protocol::ActionType::PUSH);
    EXPECT_EQ(receivedOpt->GetPayload(), (protocol::Payload{0xFF, 0xEE}));
}

TEST_F(TcpClientTest, ReceiveFrameHandlesTcpFragmentationSafely)
{
    auto serverFrame = protocol::Frame::CreatePoll(defaultMailbox);
    auto serialized = serverFrame.Serialize();

    acceptor->async_accept(
        [serialized](boost::system::error_code ec, boost::asio::ip::tcp::socket sock) {
            if (ec)
                return;
            for (const auto& byte : serialized) {
                boost::asio::write(sock, boost::asio::buffer(&byte, 1));
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        });

    TcpClient client(clientIo);
    ASSERT_TRUE(client.Connect("127.0.0.1", serverPort));

    auto receivedOpt = client.ReceiveFrame();

    ASSERT_TRUE(receivedOpt.has_value());
    EXPECT_EQ(receivedOpt->GetActionType(), protocol::ActionType::POLL);
}

TEST_F(TcpClientTest, ReceiveFrameReturnsNulloptOnServerEofDrop)
{
    acceptor->async_accept([](boost::system::error_code ec, boost::asio::ip::tcp::socket sock) {
        if (ec)
            return;
        sock.close();
    });

    TcpClient client(clientIo);
    ASSERT_TRUE(client.Connect("127.0.0.1", serverPort));

    auto receivedOpt = client.ReceiveFrame();

    EXPECT_FALSE(receivedOpt.has_value());
}

TEST_F(TcpClientTest, ReceiveFrameReturnsNulloptOnMalformedVolumetricData)
{
    acceptor->async_accept([](boost::system::error_code ec, boost::asio::ip::tcp::socket sock) {
        if (ec)
            return;
        std::vector<std::uint8_t> garbage(50, 0x99);
        boost::asio::write(sock, boost::asio::buffer(garbage));
    });

    TcpClient client(clientIo);
    ASSERT_TRUE(client.Connect("127.0.0.1", serverPort));

    auto receivedOpt = client.ReceiveFrame();

    EXPECT_FALSE(receivedOpt.has_value());
}

} // namespace bc::network::test
