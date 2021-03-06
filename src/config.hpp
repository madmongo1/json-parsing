#pragma once

#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>

namespace program
{
    namespace asio  = boost::asio;
    namespace net   = boost::asio;
    namespace beast = boost::beast;
    namespace ssl   = boost::asio::ssl;
    namespace system = boost::system;

}   // namespace program