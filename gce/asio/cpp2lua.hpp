///
/// Copyright (c) 2009-2015 Nous Xiong (348944179 at qq dot com)
///
/// Distributed under the Boost Software License, Version 1.0. (See accompanying
/// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
///
/// See https://github.com/nousxiong/gce for latest version.
///

#ifndef GCE_ASIO_CPP2LUA_HPP
#define GCE_ASIO_CPP2LUA_HPP

#include <gce/asio/config.hpp>
#include <gce/asio/detail/lua_wrap.hpp>

namespace gce
{
namespace asio
{
namespace lua
{
///------------------------------------------------------------------------------
/// tcp_option
///------------------------------------------------------------------------------
inline void load(lua_State* L, int arg, tcpopt_t& opt)
{
  detail::lua::load(L, arg, opt);
}

inline void push(lua_State* L, tcpopt_t const& opt)
{
  detail::lua::push(L, opt);
}
///------------------------------------------------------------------------------
/// ssl_option
///------------------------------------------------------------------------------
inline void load(lua_State* L, int arg, sslopt_t& opt)
{
  detail::lua::load(L, arg, opt);
}

inline void push(lua_State* L, sslopt_t const& opt)
{
  detail::lua::push(L, opt);
}
} /// namespace lua
} /// namespace asio
} /// namespace gce

#endif /// GCE_ASIO_CPP2LUA_HPP