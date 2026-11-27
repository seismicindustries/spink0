/*
        ##########    Copyright (C) 2016 Vincenzo Pacella
        ##      ##    Distributed under MIT license, see file LICENSE
        ##      ##    or <http://opensource.org/licenses/MIT>
        ##      ##
##########      ############################################################# shaduzlabs.com #####*/

#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// -------------------------------------------------------------------------------------------------

namespace sl
{
namespace pi
{

// -------------------------------------------------------------------------------------------------

//! Asks the network daemon to switch WLAN mode, without blocking the caller.
//!
//! Runs `spink-net toggle` ($PINK_NET_HELPER overrides the path) on a worker thread. The helper
//! prints the new mode, e.g. "AP" or "NET", which is passed to the callback (on the worker
//! thread). Requests made while a switch is running are ignored.
class NetworkControl
{
public:
  using Callback = std::function<void(const std::string& /*mode*/)>;

  explicit NetworkControl(Callback onResult_);
  ~NetworkControl();
  NetworkControl(const NetworkControl&) = delete;
  NetworkControl& operator=(const NetworkControl&) = delete;

  //! Returns false if a switch is already in progress.
  bool toggle();

private:
  void worker();
  static std::string runHelper(const std::vector<std::string>& args_);

  Callback m_onResult;
  std::mutex m_mtx;
  std::condition_variable m_cv;
  bool m_requested{false};
  bool m_busy{false};
  bool m_running{true};
  std::thread m_thread;
};

// -------------------------------------------------------------------------------------------------

} // namespace pi
} // namespace sl
