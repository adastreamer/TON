#pragma once

#ifdef USE_STANDALONE_ASIO
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>

namespace SimpleWeb {
  using error_code = std::error_code;
  using errc = std::errc;
  namespace make_error_code = std;
} // namespace SimpleWeb
#else
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>

namespace SimpleWeb {
    namespace asio = boost::asio;
    using error_code = boost::system::error_code;
    namespace errc = boost::system::errc;
    namespace make_error_code = boost::system::errc;
} // namespace SimpleWeb
#endif

// Late 2017 TODO: remove the following checks and always use std::regex
#ifdef USE_BOOST_REGEX
#include <boost/regex.hpp>
namespace SimpleWeb {
  namespace regex = boost;
}
#else
#include <regex>
namespace SimpleWeb {
    namespace regex = std;
}
#endif