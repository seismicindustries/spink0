/*
        ##########    Copyright (C) 2016 Vincenzo Pacella
        ##      ##    Distributed under MIT license, see file LICENSE
        ##      ##    or <http://opensource.org/licenses/MIT>
        ##      ##
##########      ############################################################# shaduzlabs.com #####*/

#include "ui/NetworkControl.h"

#include <cstdlib>

#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

// -------------------------------------------------------------------------------------------------

namespace
{
const char* kDefaultHelper = "/usr/local/bin/spink-net";
}

// -------------------------------------------------------------------------------------------------

namespace sl
{
namespace pi
{

// -------------------------------------------------------------------------------------------------

NetworkControl::NetworkControl(Callback onResult_)
  : m_onResult(std::move(onResult_)), m_thread(&NetworkControl::worker, this)
{
}

// -------------------------------------------------------------------------------------------------

NetworkControl::~NetworkControl()
{
  {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_running = false;
  }
  m_cv.notify_all();
  if (m_thread.joinable())
  {
    m_thread.join();
  }
}

// -------------------------------------------------------------------------------------------------

bool NetworkControl::toggle()
{
  {
    std::lock_guard<std::mutex> lock(m_mtx);
    if (m_busy || m_requested)
    {
      return false;
    }
    m_requested = true;
  }
  m_cv.notify_all();
  return true;
}

// -------------------------------------------------------------------------------------------------

void NetworkControl::worker()
{
  std::unique_lock<std::mutex> lock(m_mtx);
  while (true)
  {
    m_cv.wait(lock, [this] { return m_requested || !m_running; });
    if (!m_running)
    {
      return;
    }
    m_requested = false;
    m_busy = true;
    lock.unlock();

    const auto mode = runHelper({"toggle"});
    if (m_onResult)
    {
      m_onResult(mode.empty() ? "ERR" : mode);
    }

    lock.lock();
    m_busy = false;
  }
}

// -------------------------------------------------------------------------------------------------

std::string NetworkControl::runHelper(const std::vector<std::string>& args_)
{
  const char* helper = std::getenv("PINK_NET_HELPER");
  const std::string program = helper != nullptr ? helper : kDefaultHelper;

  std::vector<char*> argv;
  argv.push_back(const_cast<char*>(program.c_str()));
  for (const auto& arg : args_)
  {
    argv.push_back(const_cast<char*>(arg.c_str()));
  }
  argv.push_back(nullptr);

  int fds[2];
  if (pipe(fds) != 0)
  {
    return {};
  }

  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
  posix_spawn_file_actions_addclose(&actions, fds[0]);
  posix_spawn_file_actions_addclose(&actions, fds[1]);

  pid_t pid = -1;
  const int spawned = posix_spawn(&pid, program.c_str(), &actions, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  close(fds[1]);
  if (spawned != 0)
  {
    close(fds[0]);
    return {};
  }

  std::string output;
  char buffer[128];
  ssize_t n;
  while ((n = read(fds[0], buffer, sizeof(buffer))) > 0)
  {
    output.append(buffer, static_cast<std::size_t>(n));
  }
  close(fds[0]);

  int status = 0;
  waitpid(pid, &status, 0);
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
  {
    return {};
  }

  // first line, trimmed
  const auto end = output.find_first_of("\r\n");
  output = output.substr(0, end);
  while (!output.empty() && output.back() == ' ')
  {
    output.pop_back();
  }
  return output;
}

// -------------------------------------------------------------------------------------------------

} // namespace pi
} // namespace sl
