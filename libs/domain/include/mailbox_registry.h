#ifndef BC_LIBS_DOMAIN_INCLUDE_MAILBOXREGISTRY_H_
#define BC_LIBS_DOMAIN_INCLUDE_MAILBOXREGISTRY_H_

#include <frame.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace bc::domain {

class MailboxRegistry
{
public:
    void Push(const std::string& mailbox_id, const std::string& message);
    std::vector<std::string> Poll(const std::string& mailbox_id);

private:
    std::unordered_map<std::string, std::vector<std::string>> queues_;
};

} // namespace bc::domain

#endif // BC_LIBS_DOMAIN_INCLUDE_MAILBOXREGISTRY_H_
