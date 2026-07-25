#include <array>
#include <memory>
#include <string>
#include <thread>

#include <boost/asio.hpp>
#include <gtest/gtest.h>

#include <network/tor_control.h>

namespace bc::network::test {

class TorControlTest : public ::testing::Test
{
protected:
    boost::asio::io_context serverIo;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> acceptor;
    std::uint16_t controlPort{0};
    std::thread serverThread;
    boost::asio::io_context clientIo;

    void SetUp() override
    {
        acceptor = std::make_unique<boost::asio::ip::tcp::acceptor>(
            serverIo, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 0));
        controlPort = acceptor->local_endpoint().port();

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

TEST_F(TorControlTest, CreateEphemeralHiddenServiceSucceeds)
{
    acceptor->async_accept([](boost::system::error_code ec, boost::asio::ip::tcp::socket sock) {
        if (ec)
            return;

        std::array<char, 256> buffer{};

        sock.read_some(boost::asio::buffer(buffer));
        boost::asio::write(sock, boost::asio::buffer(std::string("250 OK\r\n")));

        sock.read_some(boost::asio::buffer(buffer));
        boost::asio::write(
            sock, boost::asio::buffer(std::string("250-ServiceID=myephemeral123\r\n250 OK\r\n")));
    });

    HiddenServiceConfig config{"127.0.0.1", controlPort, 8080};
    auto result = TorControl::CreateEphemeralHiddenService(clientIo, config);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "myephemeral123");
}

TEST_F(TorControlTest, FailsSecurelyWhenTorDaemonIsDown)
{
    acceptor->close();

    HiddenServiceConfig config{"127.0.0.1", controlPort, 8080};
    auto result = TorControl::CreateEphemeralHiddenService(clientIo, config);

    EXPECT_FALSE(result.has_value());
}

TEST_F(TorControlTest, FailsSecurelyOnAuthRejection)
{
    acceptor->async_accept([](boost::system::error_code ec, boost::asio::ip::tcp::socket sock) {
        if (ec)
            return;

        std::array<char, 256> buffer{};
        sock.read_some(boost::asio::buffer(buffer));
        boost::asio::write(sock, boost::asio::buffer(std::string("515 Authentication failed\r\n")));
    });

    HiddenServiceConfig config{"127.0.0.1", controlPort, 8080};
    auto result = TorControl::CreateEphemeralHiddenService(clientIo, config);

    EXPECT_FALSE(result.has_value());
}

TEST_F(TorControlTest, FailsSecurelyOnAddOnionRejection)
{
    acceptor->async_accept([](boost::system::error_code ec, boost::asio::ip::tcp::socket sock) {
        if (ec)
            return;

        std::array<char, 256> buffer{};

        sock.read_some(boost::asio::buffer(buffer));
        boost::asio::write(sock, boost::asio::buffer(std::string("250 OK\r\n")));

        sock.read_some(boost::asio::buffer(buffer));
        boost::asio::write(sock, boost::asio::buffer(std::string("510 Unrecognized command\r\n")));
    });

    HiddenServiceConfig config{"127.0.0.1", controlPort, 8080};
    auto result = TorControl::CreateEphemeralHiddenService(clientIo, config);

    EXPECT_FALSE(result.has_value());
}

} // namespace bc::network::test
