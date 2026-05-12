#include <sstream>
#include <thread>
#include <vector>

#include <boost/asio.hpp>
#include <gtest/gtest.h>

#include <protocol/frame.h>

#include <cli/repl.h>

namespace bc::cli::test {

class ReplTest : public ::testing::Test
{
protected:
    boost::asio::io_context serverIo;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> acceptor;
    std::uint16_t serverPort{0};
    std::thread serverThread;

    std::streambuf *orig_cin, *orig_cout;
    std::stringstream test_in, test_out;

    void SetUp() override
    {
        orig_cin = std::cin.rdbuf(test_in.rdbuf());
        orig_cout = std::cout.rdbuf(test_out.rdbuf());

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
        std::cin.rdbuf(orig_cin);
        std::cout.rdbuf(orig_cout);
        serverIo.stop();
        if (serverThread.joinable()) {
            serverThread.join();
        }
    }

    auto MockTorHandshake(boost::asio::ip::tcp::socket& sock) -> void
    {
        std::array<std::uint8_t, 3> greeting{};
        boost::asio::read(sock, boost::asio::buffer(greeting));

        std::array<std::uint8_t, 2> greetingResp = {0x05, 0x00};
        boost::asio::write(sock, boost::asio::buffer(greetingResp));

        std::array<std::uint8_t, 5> reqHeader{};
        boost::asio::read(sock, boost::asio::buffer(reqHeader));

        std::vector<std::uint8_t> addr(reqHeader[4] + 2);
        boost::asio::read(sock, boost::asio::buffer(addr));

        std::array<std::uint8_t, 10> success = {0x05, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0};
        boost::asio::write(sock, boost::asio::buffer(success));
    }
};

TEST_F(ReplTest, HandlesEmptyPayloadSecurely)
{
    acceptor->async_accept([this](auto ec, auto sock) {
        if (!ec) {
            MockTorHandshake(sock);
            std::vector<uint8_t> buf(128);
            sock.read_some(boost::asio::buffer(buf));
        }
    });

    Repl repl("127.0.0.1", serverPort);

    test_in << "connect test.onion 80\n";
    test_in << "send 00112233445566778899aabbccddeeff \n";
    test_in << "exit\n";

    repl.Run();

    EXPECT_NE(test_out.str().find("Successfully connected"), std::string::npos);
    EXPECT_NE(test_out.str().find("Frame serialized and transmitted"), std::string::npos);
}

TEST_F(ReplTest, RejectsInvalidHexIdLength)
{
    test_in << "send 123_invalid_id_short message\nexit\n";

    Repl repl;
    repl.Run();

    EXPECT_NE(test_out.str().find("Error: Invalid Mailbox ID"), std::string::npos);
}

TEST_F(ReplTest, UnknownCommandDoesNotCrash)
{
    test_in << "invalid_command_name\nexit\n";

    Repl repl;
    repl.Run();

    EXPECT_NE(test_out.str().find("Unknown command"), std::string::npos);
}

} // namespace bc::cli::test
