﻿///
/// Copyright (c) 2009-2014 Nous Xiong (348944179 at qq dot com)
///
/// Distributed under the Boost Software License, Version 1.0. (See accompanying
/// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
///
/// See https://github.com/nousxiong/gce for latest version.
///

#ifndef GCE_ACTOR_SPAWN_HPP
#define GCE_ACTOR_SPAWN_HPP

#include <gce/actor/config.hpp>
#include <gce/actor/actor.hpp>
#include <gce/actor/mixin.hpp>
#include <gce/actor/detail/cache_pool.hpp>
#include <gce/actor/slice.hpp>

namespace gce
{
namespace detail
{
template <typename Sire, typename F>
inline aid_t make_actor(
  Sire& sire, cache_pool* user, cache_pool* owner,
  F f, link_type type, std::size_t stack_size
  )
{
  aid_t link_tgt;
  if (type != no_link)
  {
    link_tgt = sire.get_aid();
  }

  context& ctx = owner->get_context();
  if (!user)
  {
    user = ctx.select_cache_pool();
  }

  actor* a = owner->get_actor();
  a->init(user, owner, f, link_tgt);
  aid_t aid = a->get_aid();
  sire.add_link(detail::link_t(type, aid));
  a->start(stack_size);
  return aid;
}
}

/// Spawn a actor using given mixin
template <typename F>
inline aid_t spawn(
  mixin_t sire, F f,
  link_type type = no_link,
  std::size_t stack_size = default_stacksize()
  )
{
  detail::cache_pool* owner = sire.get_cache_pool();
  context& ctx = owner->get_context();

  ///   In boost 1.54 & 1.55, boost::asio::spawn will crash
  /// When spwan multi-coros at main thread
  /// With multi-threads run io_service::run (vc11)
  ///   I'don't know this wheather or not a bug.
  ///   So, if using mixin(means in main or other user thread(s)),
  /// We spawn actor(s) with only one cache_pool(means only one asio::strand).
  detail::cache_pool* user = ctx.select_cache_pool(0);
  return make_actor(sire, user, owner, f, type, stack_size);
}

/// Spawn a actor using given actor
template <typename F>
inline aid_t spawn(
  self_t sire, F f,
  link_type type = no_link,
  bool sync_sire = false,
  std::size_t stack_size = default_stacksize()
  )
{
  detail::cache_pool* user = 0;
  detail::cache_pool* owner = sire.get_cache_pool();
  if (sync_sire)
  {
    user = owner;
  }
  return make_actor(sire, user, owner, f, type, stack_size);
}

/// Spawn a mixin
inline mixin_t spawn(context& ctx)
{
  return ctx.make_mixin();
}

/// Spawn a slice using given mixin
inline slice_t spawn(mixin_t sire, link_type type = no_link)
{
  aid_t link_tgt;
  if (type != no_link)
  {
    link_tgt = sire.get_aid();
  }
  slice_t s(sire.get_slice());
  s->init(link_tgt);
  sire.add_link(detail::link_t(type, s->get_aid()));
  return s;
}

/// Make actor function
template <typename F, typename A>
inline actor_func_t make_func(F f, A a)
{
  return actor_func_t(f, a);
}
}

#endif /// GCE_ACTOR_SPAWN_HPP
