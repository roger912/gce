﻿///
/// Copyright (c) 2009-2014 Nous Xiong (348944179 at qq dot com)
///
/// Distributed under the Boost Software License, Version 1.0. (See accompanying
/// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
///
/// See https://github.com/nousxiong/gce for latest version.
///

namespace gce
{
class stackless_ut
{
public:
  static void run()
  {
    std::cout << "stackless_ut begin." << std::endl;
    test_common();
    std::cout << "stackless_ut end." << std::endl;
  }

public:
  class my_child
    : public boost::enable_shared_from_this<my_child>
  {
  public:
    void run(actor<stackless>& self)
    {
      GCE_REENTER (self)
      {
        GCE_YIELD recv(self, aid_);
        reply(self, aid_);
      }
    }

  private:
    aid_t aid_;
    message msg_;
  };

  class my_actor
    : public boost::enable_shared_from_this<my_actor>
  {
  public:
    my_actor(io_service_t& ios, aid_t base_id)
      : size_(50)
      , res_list_(size_)
      , base_id_(base_id)
      , tmr_(ios)
    {
    }

    ~my_actor()
    {
    }

  public:
    void run(actor<stackless>& self)
    {
      GCE_REENTER (self)
      {
        for (i_=0; i_<size_; ++i_)
        {
          GCE_YIELD spawn(
            self,
            boost::bind(
              &my_child::run, 
              boost::make_shared<my_child>(), _1
              ),
            aid_
            );
          res_list_[i_] = request(self, aid_);
        }

        GCE_YIELD wait(self, boost::chrono::milliseconds(1));

        for (i_=0; i_<size_; ++i_)
        {
          do
          {
            GCE_YIELD self.recv(res_list_[i_], aid_, msg_, seconds_t(1));
          }
          while (!aid_);
        }

        tmr_.expires_from_now(boost::chrono::milliseconds(1));
        GCE_YIELD tmr_.async_wait(adaptor(self, ec_));

        send(self, base_id_);
      }
    }

  private:
    std::size_t const size_;
    std::vector<response_t> res_list_;

    std::size_t i_;
    aid_t base_id_;
    aid_t aid_;
    message msg_;

    timer_t tmr_;
    errcode_t ec_;
  };

  static void my_thr(context& ctx, aid_t base_id)
  {
    actor<threaded> a = spawn(ctx);
    io_service_t& ios = ctx.get_io_service();
    for (std::size_t i=0; i<2; ++i)
    {
      spawn<stackless>(
        a, 
        boost::bind(
          &my_actor::run, 
          boost::make_shared<my_actor>(
            boost::ref(ios), base_id
            ), 
          _1
          )
        );
    }
  }

  static void test_common()
  {
    try
    {
      std::size_t free_actor_num = 20;
      std::size_t user_thr_num = 0;
      std::size_t my_actor_size = free_actor_num + user_thr_num * 2;
      attributes attrs;
      context ctx(attrs);
      actor<threaded> base = spawn(ctx);

      io_service_t& ios = ctx.get_io_service();
      aid_t base_id = base.get_aid();
      for (std::size_t i=0; i<free_actor_num; ++i)
      {
        spawn<stackless>(
          base,
          boost::bind(
            &my_actor::run, 
            boost::make_shared<my_actor>(
              boost::ref(ios), base_id
              ), 
            _1
            )
          );
      }

      boost::thread_group thrs;
      for (std::size_t i=0; i<user_thr_num; ++i)
      {
        thrs.create_thread(
          boost::bind(
            &stackless_ut::my_thr,
            boost::ref(ctx), base_id
            )
          );
      }

      thrs.join_all();

      for (std::size_t i=0; i<my_actor_size; ++i)
      {
        recv(base);
      }
    }
    catch (std::exception& ex)
    {
      std::cerr << ex.what() << std::endl;
    }
  }
};
}
