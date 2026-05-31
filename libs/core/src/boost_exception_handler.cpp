#include <cstdlib>
#include <exception>

#include <boost/assert/source_location.hpp>

#include "core/logger.h"

namespace boost {

// NOLINTBEGIN(readability-identifier-naming)
void throw_exception([[maybe_unused]] const std::exception& err)
{
    BC_CRITICAL("FATAL BOOST ERROR: System halted due to unrecoverable exception: {}", err.what());
    std::abort();
}

void throw_exception([[maybe_unused]] const std::exception& err,
                     [[maybe_unused]] const boost::source_location& loc)
{
    BC_CRITICAL("FATAL BOOST ERROR at {}:{}: {}", loc.file_name(), loc.line(), err.what());
    std::abort();
}
// NOLINTEND(readability-identifier-naming)

} // namespace boost
