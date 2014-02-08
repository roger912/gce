﻿///
/// Copyright (c) 2009-2014 Nous Xiong (348944179 at qq dot com)
///
/// Distributed under the Boost Software License, Version 1.0. (See accompanying
/// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
///
/// See https://github.com/nousxiong/gce for latest version.
///

#include <gce/actor/detail/socket.hpp>
#include <gce/actor/detail/cache_pool.hpp>
#include <gce/actor/mixin.hpp>
#include <gce/actor/impl/tcp/socket.hpp>
#include <gce/actor/detail/mailbox.hpp>
#include <gce/actor/message.hpp>
#include <gce/detail/scope.hpp>
#include <gce/detail/cache_aligned_new.hpp>
#include <gce/actor/impl/protocol.hpp>
#include <gce/amsg/amsg.hpp>
#include <gce/amsg/zerocopy.hpp>
#include <boost/asio/placeholders.hpp>
#include <boost/bind.hpp>
#include <boost/variant/get.hpp>
#include <boost/static_assert.hpp>

namespace gce
{
namespace detail
{
///----------------------------------------------------------------------------
socket::socket(context* ctx)
  : basic_actor(ctx->get_attributes().max_cache_match_size_)
  , stat_(ready)
  , skt_(0)
  , hb_(*ctx->get_io_service())
  , sync_(*ctx->get_io_service())
  , recv_cache_(recv_buffer_, GCE_SOCKET_RECV_CACHE_SIZE)
  , conn_(false)
  , curr_reconn_(0)
{
}
///----------------------------------------------------------------------------
socket::~socket()
{
}
///----------------------------------------------------------------------------
void socket::init(cache_pool* user, cache_pool* owner, net_option opt)
{
  BOOST_ASSERT_MSG(stat_ == ready, "socket 状态不正确，必须为ready");
  user_ = user;
  owner_ = owner;
  opt_ = opt;
  curr_reconn_ = opt_.reconn_count_;

  base_type::update_aid();
}
///----------------------------------------------------------------------------
void socket::connect(std::string const& ep, aid_t master)
{
  master_ = master;
  base_type::add_link(master_);

  boost::asio::spawn(
    *user_->get_strand(),
    boost::bind(
      &socket::run_conn, this, ep, _1
      ),
    boost::coroutines::attributes(
      boost::coroutines::stack_allocator::default_stacksize()
      )
    );
}
///----------------------------------------------------------------------------
void socket::start(basic_socket* skt, aid_t acpr_aid, aid_t master)
{
  master_ = master;
  acpr_aid_ = acpr_aid;
  base_type::add_link(acpr_aid_);
  conn_ = true;

  boost::asio::spawn(
    *user_->get_strand(),
    boost::bind(
      &socket::run, this, skt, _1
      ),
    boost::coroutines::attributes(
      boost::coroutines::stack_allocator::default_stacksize()
      )
    );
}
///----------------------------------------------------------------------------
void socket::on_free()
{
  base_type::on_free();

  stat_ = ready;
  skt_ = 0;
  master_ = aid_t();
  acpr_aid_ = aid_t();
  recv_cache_.clear();
  conn_ = false;
  curr_reconn_ = 0;
}
///----------------------------------------------------------------------------
void socket::on_recv(pack* pk)
{
  strand_t* snd = user_->get_strand();
  snd->dispatch(
    boost::bind(
      &socket::handle_recv, this, pk
      )
    );
}
///----------------------------------------------------------------------------
void socket::send(aid_t recver, message const& m)
{
  pack* pk = base_type::alloc_pack(user_.get());
  pk->tag_ = get_aid();
  pk->recver_ = recver;
  pk->msg_ = m;

  basic_actor* a = recver.get_actor_ptr();
  a->on_recv(pk);
}
///----------------------------------------------------------------------------
void socket::send(message const& m)
{
  BOOST_ASSERT(skt_);
  match_t type = m.get_type();
  std::size_t size = m.size();
  if (conn_)
  {
    while (!conn_cache_.empty())
    {
      message const& m = conn_cache_.front();
      send_msg(m);
      std::cout << "socket::send, " << this <<
        ", " << m.get_type() << ", " << m.size() << std::endl;
      conn_cache_.pop();
    }
    send_msg(m);
    std::cout << "socket::send, " << this <<
      ", " << type << ", " << size << std::endl;
  }
  else
  {
    std::cout << "socket::send push, " << this << ", " << type << ", " << size << std::endl;
    conn_cache_.push(m);
  }
}
///----------------------------------------------------------------------------
void socket::send_msg(message const& m)
{
  msg::header hdr;
  hdr.size_ = m.size();
  hdr.type_ = m.get_type();

  byte_t buf[sizeof(msg::header)];
  boost::amsg::zero_copy_buffer zbuf(buf, sizeof(msg::header));
  boost::amsg::write(zbuf, hdr);
  skt_->send(
    buf, zbuf.write_length(),
    m.data(), hdr.size_
    );
}
///----------------------------------------------------------------------------
void socket::send_msg_hb()
{
  send(message(detail::msg_hb));
}
///----------------------------------------------------------------------------
void socket::run_conn(std::string const& ep, yield_t yield)
{
  exit_code_t exc = exit_normal;
  try
  {
    stat_ = on;
    skt_ = make_socket(ep);
    connect(yield);

    while (stat_ == on)
    {
      message msg;
      errcode_t ec = recv(msg, yield);
      if (ec)
      {
        std::cout << "socket::run_conn: " << ec.message() << std::endl;
        --curr_reconn_;
        if (curr_reconn_ == 0)
        {
          exc = exit_neterr;
          close();
          break;
        }
        connect(yield);
      }
      else
      {
        match_t type = msg.get_type();
        if (type == exit_remote)
        {
          close();
        }
        else
        {
          if (type != detail::msg_hb)
          {
            send(master_, msg);
          }
          hb_.beat();
        }
      }
    }
  }
  catch (std::exception& ex)
  {
    std::cerr << ex.what() << std::endl;
    exc = exit_except;
    close();
  }
  free_self(exc, yield);
}
///----------------------------------------------------------------------------
void socket::run(basic_socket* skt, yield_t yield)
{
  exit_code_t exc = exit_normal;
  try
  {
    stat_ = on;
    skt_ = skt;
    skt_->init(user_.get());
    start_heartbeat(boost::bind(&socket::close, this));

    while (stat_ == on)
    {
      message msg;
      errcode_t ec = recv(msg, yield);
      if (ec)
      {
        std::cout << "socket::run: " << ec.message() << std::endl;
        close();
        break;
      }

      if (msg.get_type() != detail::msg_hb)
      {
        send(master_, msg);
      }
      hb_.beat();
    }
  }
  catch (std::exception& ex)
  {
    std::cerr << ex.what() << std::endl;
    exc = exit_except;
    close();
  }
  free_self(exc, yield);
}
///----------------------------------------------------------------------------
basic_socket* socket::make_socket(std::string const& ep)
{
  /// 找出协议名
  std::size_t pos = ep.find("://");
  if (pos == std::string::npos)
  {
    throw std::runtime_error("protocol name parse failed");
  }

  std::string prot_name = ep.substr(0, pos);
  if (prot_name == "tcp")
  {
    /// 解析地址
    std::size_t begin = pos + 3;
    pos = ep.find(':', begin);
    if (pos == std::string::npos)
    {
      throw std::runtime_error("tcp address parse failed");
    }

    std::string address = ep.substr(begin, pos - begin);

    /// 解析端口
    begin = pos + 1;
    pos = ep.size();

    std::string port = ep.substr(begin, pos - begin);
    basic_socket* skt =
      GCE_CACHE_ALIGNED_NEW(tcp::socket)(
        user_->get_strand()->get_io_service(),
        address, port
        );
    skt->init(user_.get());
    return skt;
  }

  throw std::runtime_error("unsupported protocol");
}
///----------------------------------------------------------------------------
void socket::handle_recv(pack* pk)
{
  scope scp(boost::bind(&basic_actor::dealloc_pack, user_.get(), pk));
  if (check(pk->recver_))
  {
    if (boost::get<aid_t>(&pk->tag_))
    {
      send(pk->msg_);
    }
    else if (link_t* link = boost::get<link_t>(&pk->tag_))
    {
      add_link(link->get_aid());
    }
    else if (exit_t* ex = boost::get<exit_t>(&pk->tag_))
    {
      base_type::remove_link(ex->get_aid());
      if (
        ex->get_aid() == acpr_aid_ ||
        ex->get_aid() == master_
        )
      {
        if (acpr_aid_)
        {
          send(message(exit_remote));
        }
        close();
      }
    }
  }
  else
  {
    if (link_t* link = boost::get<link_t>(&pk->tag_))
    {
      /// send actor exit msg
      send(link->get_aid(), message(exit_already));
    }
  }
}
///----------------------------------------------------------------------------
bool socket::parse_message(message& msg)
{
  msg::header hdr;
  byte_t* data = recv_cache_.get_read_data();
  std::size_t remain_size = recv_cache_.remain_read_size();
  boost::amsg::zero_copy_buffer zbuf(data, remain_size);
  boost::amsg::read(zbuf, hdr);
  if (zbuf.bad())
  {
    return false;
  }

  if (hdr.size_ > GCE_MAX_MSG_SIZE)
  {
    throw std::runtime_error("message overlength");
  }

  std::size_t header_size = zbuf.read_length();
  if (remain_size - header_size < hdr.size_)
  {
    return false;
  }

  recv_cache_.read(header_size + hdr.size_);
  msg = message(hdr.type_, data + header_size, hdr.size_);

  /// 根据cache大小重置read_cache
  if (recv_cache_.read_size() > GCE_SOCKET_RECV_MAX_SIZE)
  {
    BOOST_ASSERT(recv_cache_.write_size() >= recv_cache_.read_size());
    std::size_t copy_size =
      recv_cache_.write_size() - recv_cache_.read_size();
    std::memmove(recv_buffer_, recv_cache_.get_read_data(), copy_size);
    recv_cache_.clear();
    recv_cache_.write(copy_size);
  }
  return true;
}
///----------------------------------------------------------------------------
void socket::connect(yield_t yield)
{
  errcode_t ec;
  conn_ = false;
  if (stat_ == on)
  {
    for (std::size_t i=0; i<opt_.reconn_count_; ++i)
    {
      if (curr_reconn_ < opt_.reconn_try_ || i > 0)
      {
        sync_.expires_from_now(opt_.reconn_period_);
        sync_.async_wait(yield[ec]);
        if (stat_ != on)
        {
          break;
        }
      }

      skt_->connect(yield[ec]);
      if (!ec)
      {
        break;
      }
    }

    if (stat_ != on)
    {
      return;
    }

    if (ec)
    {
      throw std::runtime_error(ec.message());
    }

    conn_ = true;
    std::cout << "socket::connect ok\n";
    start_heartbeat(boost::bind(&socket::reconn, this));
  }
}
///----------------------------------------------------------------------------
errcode_t socket::recv(message& msg, yield_t yield)
{
  BOOST_STATIC_ASSERT((GCE_SOCKET_RECV_CACHE_SIZE > GCE_SOCKET_RECV_MAX_SIZE));

  errcode_t ec;
  while (stat_ != off && !parse_message(msg))
  {
    std::size_t size =
      skt_->recv(
        recv_cache_.get_write_data(),
        recv_cache_.remain_write_size(),
        yield[ec]
        );
    if (ec)
    {
      break;
    }

    recv_cache_.write(size);
  }

  return ec;
}
///----------------------------------------------------------------------------
void socket::close()
{
  stat_ = off;
  hb_.stop();
  skt_->close();
  errcode_t ignore_ec;
  sync_.cancel(ignore_ec);
}
///----------------------------------------------------------------------------
void socket::reconn()
{
  skt_->reset();
}
///----------------------------------------------------------------------------
template <typename F>
void socket::start_heartbeat(F f)
{
  hb_.init(
    user_.get(),
    opt_.heartbeat_period_, opt_.heartbeat_count_,
    f, boost::bind(&socket::send_msg_hb, this)
    );
  hb_.start();
}
///----------------------------------------------------------------------------
void socket::free_self(exit_code_t exc, yield_t yield)
{
  try
  {
    hb_.wait_end(yield);
    if (skt_)
    {
      skt_->wait_end(yield);
    }
  }
  catch (...)
  {
  }
  GCE_CACHE_ALIGNED_DELETE(basic_socket, skt_);

  hb_.clear();
  base_type::send_exit(exc, user_.get());
  base_type::update_aid();
  user_->free_socket(owner_.get(), this);
  std::cout << "socket::free_self, skt: " << skt_ << std::endl;
}
///----------------------------------------------------------------------------
}
}