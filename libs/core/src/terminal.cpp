#include "core/terminal.h"

#include <iostream>

#include <termios.h>
#include <unistd.h>

namespace bc::core {

constexpr std::size_t bufferSize = 128;

auto Terminal::ReadPasswordSecurely(std::string_view prompt) -> SecureString
{
    termios oldt{};
    tcgetattr(STDIN_FILENO, &oldt);
    termios newt = oldt;
    newt.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    std::cout << prompt << std::flush;

    SecureString secureStr(bufferSize);
    char ch = 0;
    while (std::cin.get(ch) && ch != '\n') {
        secureStr.PushBack(ch);
    }

    std::cout << "\n";
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return secureStr;
}
} // namespace bc::core
