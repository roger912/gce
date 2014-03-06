﻿///
/// Copyright (c) 2009-2014 Nous Xiong (348944179 at qq dot com)
///
/// Distributed under the Boost Software License, Version 1.0. (See accompanying
/// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
///
/// See https://github.com/nousxiong/gce for latest version.
///

#include <gce/actor/detail/cache_pool.hpp>
#include <gce/actor/context.hpp>
#include <gce/actor/actor.hpp>
#include <gce/actor/detail/socket.hpp>
#include <gce/actor/detail/acceptor.hpp>
#include <boost/foreach.hpp>

namespace gce
{
namespace detail
{
///------------------------------------------------------------------------------
cache_pool::cache_pool(
  context& ctx, std::size_t id,
  attributes const& attrs, bool mixed
  )
  : ctx_(&ctx)
  , id_(id)
  , cache_num_(attrs.thread_num_*attrs.per_thread_cache_ + attrs.mixin_num_)
  , cache_match_size_(attrs.max_cache_match_size_)
  , mixed_(mixed)
  , snd_(ctx.get_io_service())
  , gc_tmr_(ctx.get_io_service())
  , actor_pool_(
      this, &ctx,
      size_nil,
      attrs.actor_pool_reserve_size_
      )
  , socket_pool_(
      this, &ctx,
      size_nil,
      attrs.socket_pool_reserve_size_
      )
  , acceptor_pool_(
      this, &ctx,
      size_nil,
      attrs.acceptor_pool_reserve_size_
      )
  , pack_pool_(
      this,
      attrs.pack_pool_free_size_,
      attrs.pack_pool_reserve_size_
      )
  , actor_cache_list_(cache_num_)
  , socket_cache_list_(cache_num_)
  , acceptor_cache_list_(cache_num_)
  , pack_cache_list_(cache_num_)
  , stopped_(false)
  , ctxid_(attrs.id_)
{
  actor_cache_dirty_list_.reserve(cache_num_);
  socket_cache_dirty_list_.reserve(cache_num_);
  acceptor_cache_dirty_list_.reserve(cache_num_);
  pack_cache_dirty_list_.reserve(cache_num_);
}
///------------------------------------------------------------------------------
cache_pool::~cache_pool()
{
  free_object();
}
///------------------------------------------------------------------------------
actor* cache_pool::get_actor()
{
  if (!mixed_ && !snd_.running_in_this_thread())
  {
    throw std::runtime_error("get_actor strand thread error");
  }

  return actor_pool_.get();
}
///------------------------------------------------------------------------------
socket* cache_pool::get_socket()
{
  if (!mixed_ && !snd_.running_in_this_thread())
  {
    throw std::runtime_error("get_socket strand thread error");
  }

  return socket_pool_.get();
}
///------------------------------------------------------------------------------
acceptor* cache_pool::get_acceptor()
{
  if (!mixed_ && !snd_.running_in_this_thread())
  {
    throw std::runtime_error("get_acceptor strand thread error");
  }

  return acceptor_pool_.get();
}
///------------------------------------------------------------------------------
pack* cache_pool::get_pack()
{
  if (!mixed_ && !snd_.running_in_this_thread())
  {
    throw std::runtime_error("get_pack strand thread error");
  }

  return pack_pool_.get();
}
///------------------------------------------------------------------------------
void cache_pool::free_actor(cache_pool* owner, actor* a)
{
  free_object(
    owner, a, actor_cache_list_, actor_pool_,
    owner->actor_free_queue_, actor_cache_dirty_list_
    );
}
///------------------------------------------------------------------------------
void cache_pool::free_socket(cache_pool* owner, socket* skt)
{
  free_object(
    owner, skt, socket_cache_list_, socket_pool_,
    owner->socket_free_queue_, socket_cache_dirty_list_
    );
}
///------------------------------------------------------------------------------
void cache_pool::free_acceptor(cache_pool* owner, acceptor* acpr)
{
  free_object(
    owner, acpr, acceptor_cache_list_, acceptor_pool_,
    owner->acceptor_free_queue_, acceptor_cache_dirty_list_
    );
}
///------------------------------------------------------------------------------
void cache_pool::free_pack(cache_pool* owner, pack* pk)
{
  free_object(
    owner, pk, pack_cache_list_, pack_pool_,
    owner->pack_free_queue_, pack_cache_dirty_list_
    );
}
///------------------------------------------------------------------------------
template <typename T, typename Pool, typename FreeQueue, typename DirtyList>
void cache_pool::free_object(
  cache_pool* owner, T* o, std::vector<cache<T, FreeQueue> >& cache_list,
  Pool& pool, FreeQueue& free_que, DirtyList& dirty_list
  )
{
  if (owner != this)
  {
    std::size_t id = owner->get_id();
    cache<T, FreeQueue>& cac = cache_list[id];
    cac.push(o, &free_que);
    if (cac.size_ >= GCE_FREE_CACHE_SIZE)
    {
      cac.free();
    }
    else if (cac.size_ == 1)
    {
      dirty_list.push_back(&cac);
    }
  }
  else
  {
    pool.free(o);
  }
}
///------------------------------------------------------------------------------
template <typename T, typename Pool, typename FreeQueue>
void cache_pool::free_object(Pool& pool, FreeQueue& free_que)
{
  T* o = free_que.pop_all_reverse();
  while (o)
  {
    T* next = node_access::get_next(o);
    node_access::set_next(o, (T*)0);
    pool.free(o);
    o = next;
  }
}
///------------------------------------------------------------------------------
void cache_pool::free_object()
{
  free_object<actor>(actor_pool_, actor_free_queue_);
  free_object<socket>(socket_pool_, socket_free_queue_);
  free_object<acceptor>(acceptor_pool_, acceptor_free_queue_);
  free_object<pack>(pack_pool_, pack_free_queue_);
}
///------------------------------------------------------------------------------
void cache_pool::free_cache()
{
  if (!actor_cache_dirty_list_.empty())
  {
    BOOST_FOREACH(actor_cache_t* cac, actor_cache_dirty_list_)
    {
      cac->free();
    }
    actor_cache_dirty_list_.clear();
  }

  if (!socket_cache_dirty_list_.empty())
  {
    BOOST_FOREACH(socket_cache_t* cac, socket_cache_dirty_list_)
    {
      cac->free();
    }
    socket_cache_dirty_list_.clear();
  }

  if (!acceptor_cache_dirty_list_.empty())
  {
    BOOST_FOREACH(acceptor_cache_t* cac, acceptor_cache_dirty_list_)
    {
      cac->free();
    }
    acceptor_cache_dirty_list_.clear();
  }

  if (!pack_cache_dirty_list_.empty())
  {
    BOOST_FOREACH(pack_cache_t* cac, pack_cache_dirty_list_)
    {
      cac->free();
    }
    pack_cache_dirty_list_.clear();
  }
}
///------------------------------------------------------------------------------
void cache_pool::register_socket(ctxid_t ctxid, aid_t skt)
{
  std::pair<conn_list_t::iterator, bool> pr =
    conn_list_.insert(std::make_pair(ctxid, dummy_));
  skt_list_t& skt_list = pr.first->second;
  skt_list.insert(skt);
  if (skt_list.size() == 1)
  {
    curr_skt_ = skt_list.begin();
  }
}
///------------------------------------------------------------------------------
aid_t cache_pool::select_socket(ctxid_t ctxid)
{
  aid_t skt;
  conn_list_t::iterator itr(conn_list_.find(ctxid));
  if (itr != conn_list_.end())
  {
    skt_list_t& skt_list = itr->second;
    if (!skt_list.empty())
    {
      if (curr_skt_ != skt_list.end())
      {
        skt = *curr_skt_;
      }
      ++curr_skt_;
      if (curr_skt_ == skt_list.end())
      {
        curr_skt_ = skt_list.begin();
      }
    }
  }
  return skt;
}
///------------------------------------------------------------------------------
void cache_pool::deregister_socket(ctxid_t ctxid, aid_t skt)
{
  conn_list_t::iterator itr(conn_list_.find(ctxid));
  if (itr != conn_list_.end())
  {
    itr->second.erase(skt);
    curr_skt_ = itr->second.begin();
  }
}
///------------------------------------------------------------------------------
void cache_pool::cache_socket(socket* s)
{
  socket_list_.insert(s);
}
///------------------------------------------------------------------------------
void cache_pool::cache_acceptor(acceptor* a)
{
  acceptor_list_.insert(a);
}
///------------------------------------------------------------------------------
void cache_pool::remove_socket(socket* s)
{
  socket_list_.erase(s);
}
///------------------------------------------------------------------------------
void cache_pool::remove_acceptor(acceptor* a)
{
  acceptor_list_.erase(a);
}
///------------------------------------------------------------------------------
void cache_pool::stop()
{
  stopped_ = true;
  errcode_t ignored_ec;
  gc_tmr_.cancel(ignored_ec);

  BOOST_FOREACH(socket* s, socket_list_)
  {
    s->stop();
  }
  socket_list_.clear();

  BOOST_FOREACH(acceptor* a, acceptor_list_)
  {
    a->stop();
  }
  acceptor_list_.clear();
}
///------------------------------------------------------------------------------
}
}
