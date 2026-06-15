#include <sstream>
#include <thread>
#include <vector>

#include <boost/asio.hpp>
#include <gtest/gtest.h>

#include <client/address_book.h>
#include <crypto/bip39.h>
#include <crypto/identity_key.h>
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

    std::optional<bc::crypto::IdentityKey> testIdentity;
    bc::domain::client::AddressBook testAddressBook;

    void SetUp() override
    {
        testIdentity.emplace(bc::crypto::IdentityKey::Generate());
        testAddressBook.Initialize("test_contacts.json", *testIdentity);

        std::cin.clear();

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
        std::cin.clear();
        std::cin.rdbuf(orig_cin);
        std::cout.rdbuf(orig_cout);

        serverIo.stop();
        if (serverThread.joinable()) {
            serverThread.join();
        }
    }

    auto MockTorHandshake(boost::asio::ip::tcp::socket& sock) -> void
    {
        boost::system::error_code ec;

        std::array<std::uint8_t, 3> greeting{};
        boost::asio::read(sock, boost::asio::buffer(greeting), ec);
        if (ec)
            return;

        std::array<std::uint8_t, 2> greetingResp = {0x05, 0x00};
        boost::asio::write(sock, boost::asio::buffer(greetingResp), ec);
        if (ec)
            return;

        std::array<std::uint8_t, 5> reqHeader{};
        boost::asio::read(sock, boost::asio::buffer(reqHeader), ec);
        if (ec)
            return;

        std::vector<std::uint8_t> addr(reqHeader[4] + 2);
        boost::asio::read(sock, boost::asio::buffer(addr), ec);
        if (ec)
            return;

        std::array<std::uint8_t, 10> success = {0x05, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0};
        boost::asio::write(sock, boost::asio::buffer(success), ec);
    }
};

TEST_F(ReplTest, HandlesEmptyPayloadSecurely)
{
    acceptor->async_accept([this](auto ec, auto sock) {
        if (!ec) {
            MockTorHandshake(sock);

            std::vector<uint8_t> buf(128);
            boost::system::error_code readEc;
            sock.read_some(boost::asio::buffer(buf), readEc);
        }
    });

    Repl repl(testAddressBook, *testIdentity, "127.0.0.1", serverPort);

    auto mockContact = bc::crypto::IdentityKey::Generate();
    auto mnemonic = bc::crypto::bip39::Encode(mockContact.GetPublicKey());
    test_in << "add alice " << mnemonic.StringView() << "\n";
    test_in << "connect test.onion 80\n";
    test_in << "send alice \n";
    test_in << "exit\n";

    repl.Run();

    EXPECT_NE(test_out.str().find("added successfully"), std::string::npos) << "Actual Output: \n"
                                                                            << test_out.str();

    EXPECT_NE(test_out.str().find("Successfully connected"), std::string::npos);
    EXPECT_NE(test_out.str().find("Frame serialized and transmitted"), std::string::npos);
}

TEST_F(ReplTest, RejectsInvalidMnemonic)
{
    Repl repl(testAddressBook, *testIdentity, "127.0.0.1", serverPort);

    test_in << "add alice short-invalid-mnemonic\nexit\n";
    repl.Run();

    EXPECT_NE(test_out.str().find("Error: Invalid BIP39 Mnemonic"), std::string::npos)
        << "Actual Output: \n"
        << test_out.str();
}

TEST_F(ReplTest, UnknownCommandDoesNotCrash)
{
    Repl repl(testAddressBook, *testIdentity, "127.0.0.1", serverPort);

    test_in << "invalid_command_name\nexit\n";
    repl.Run();

    EXPECT_NE(test_out.str().find("Unknown command"), std::string::npos);
}

TEST_F(ReplTest, SendToUnknownContactFailsSecurely)
{
    Repl repl(testAddressBook, *testIdentity, "127.0.0.1", serverPort);
    test_in << "send ghost \nexit\n";
    repl.Run();
    EXPECT_NE(test_out.str().find("not found in address book"), std::string::npos);
}

TEST_F(ReplTest, PollUnknownContactFailsSecurely)
{
    Repl repl(testAddressBook, *testIdentity, "127.0.0.1", serverPort);
    test_in << "poll ghost\nexit\n";
    repl.Run();
    EXPECT_NE(test_out.str().find("not found in address book"), std::string::npos);
}

} // namespace bc::cli::test
