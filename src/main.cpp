/*
        ##########    Copyright (C) 2016 Vincenzo Pacella
        ##      ##    Distributed under MIT license, see file LICENSE
        ##      ##    or <http://opensource.org/licenses/MIT>
        ##      ##
##########      ############################################################# shaduzlabs.com #####*/

// pinkd runs in the foreground; on the Pi it is started and supervised by systemd (pinkd.service),
// which also restarts it if it ever exits. Logs go to syslog / the journal (journalctl -t pinkd).

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <memory>
#include <thread>

#ifdef __linux__
#include <syslog.h>
#endif

#include <pink-config.h>

#include "Pink.h"
#include "StatusFile.h"

#include "ui/UserInterfaceNone.h"
#ifdef UI_USE_RASPBERRY_PI_ZERO
#include "ui/UserInterfacePiZero.h"
#endif

#define DAEMON_NAME "pinkd"

using namespace sl;
using namespace sl::pi;

//--------------------------------------------------------------------------------------------------

namespace
{

std::atomic_bool g_running{true};

extern "C" void onSignal(int)
{
  g_running = false; // async-signal-safe: the main loop notices within a second
}

//--------------------------------------------------------------------------------------------------

class Log
{
public:
  Log()
  {
#ifdef __linux__
    setlogmask(LOG_UPTO(LOG_INFO));
    openlog(DAEMON_NAME, LOG_PID, LOG_USER);
#endif
  }
  ~Log()
  {
#ifdef __linux__
    closelog();
#endif
  }

  void info(const char* format_, ...) __attribute__((format(printf, 2, 3)))
  {
    va_list args;
    va_start(args, format_);
    write(6 /* LOG_INFO */, format_, args);
    va_end(args);
  }

  void warning(const char* format_, ...) __attribute__((format(printf, 2, 3)))
  {
    va_list args;
    va_start(args, format_);
    write(4 /* LOG_WARNING */, format_, args);
    va_end(args);
  }

  void error(const char* format_, ...) __attribute__((format(printf, 2, 3)))
  {
    va_list args;
    va_start(args, format_);
    write(3 /* LOG_ERR */, format_, args);
    va_end(args);
  }

private:
  void write(int priority_, const char* format_, va_list args_)
  {
    char message[512];
    vsnprintf(message, sizeof(message), format_, args_);
#ifdef __linux__
    syslog(priority_, "%s", message);
#else
    (void)priority_;
    std::cout << DAEMON_NAME << ": " << message << std::endl;
#endif
  }
};

//--------------------------------------------------------------------------------------------------

//! Logs session events, so hiccups can be correlated with peer changes.
class LogListener : public Pink::Listener
{
public:
  LogListener(std::shared_ptr<Pink> pink_, Log& log_) : m_pink(std::move(pink_)), m_log(log_)
  {
    m_pink->addListener(pinkListenerId(), this);
  }
  ~LogListener() override
  {
    m_pink->removeListener(pinkListenerId());
  }

  void statusChanged(bool enabled_) override
  {
    m_log.info("link %s", enabled_ ? "enabled" : "disabled");
  }
  void tempoChanged(double) override
  {
  }
  void loopLengthChanged(double) override
  {
  }
  void numPeersChanged(std::size_t n_) override
  {
    m_log.info("link peers: %zu", n_);
  }
  void runStatusChanged(bool playing_) override
  {
    m_log.info("clock %s", playing_ ? "started" : "stopped");
  }
  void clockMultiplierChanged(const std::string&) override
  {
  }

private:
  std::shared_ptr<Pink> m_pink;
  Log& m_log;
};

//--------------------------------------------------------------------------------------------------

//! Reports new audio underruns and timeline jumps (the audio thread only counts them).
void logDiagnostics(Log& log_, const Engine::Diagnostics& now_, Engine::Diagnostics& last_)
{
  if (now_.underruns != last_.underruns)
  {
    log_.warning("audio underrun x%u (total %u)", now_.underruns - last_.underruns, now_.underruns);
  }
  if (now_.beatJumps != last_.beatJumps)
  {
    log_.warning("timeline jump x%u (total %u), last %+.2f ms",
      now_.beatJumps - last_.beatJumps,
      now_.beatJumps,
      now_.lastBeatJumpUs / 1000.);
  }
  last_ = now_;
}

} // namespace

//--------------------------------------------------------------------------------------------------

int main(int /*argc_*/, char* /*argv_*/[])
{
  Log log;
  log.info("pinkd %s (%s) starting", PINK_VERSION_STRING, PINK_GIT_REVISION);

  struct sigaction action
  {
  };
  action.sa_handler = onSignal;
  sigemptyset(&action.sa_mask);
  sigaction(SIGTERM, &action, nullptr);
  sigaction(SIGINT, &action, nullptr);

  try
  {
    // Exactly one instance: every Pink owns its own Link peer and its own audio stream.
    auto pink = std::make_shared<Pink>(120., 4., 1.0);
    log.info("audio: %s", pink->audioDescription().c_str());

    LogListener logListener(pink, log);

#ifdef UI_USE_RASPBERRY_PI_ZERO
    // Without the front panel the module still works as a Link clock (headless).
    std::unique_ptr<UserInterfacePiZero> frontPanel;
    try
    {
      frontPanel = std::make_unique<UserInterfacePiZero>(pink);
    }
    catch (const std::exception& e)
    {
      log.error("front panel unavailable, running headless: %s", e.what());
      pink->togglePlay(); // no run button: start the clock right away
    }
#else
    UserInterfaceNone ui(pink);
#endif

    StatusFile statusFile(pink->audioDescription());
    bool statusFileWarned = false;

    log.info("running");

    auto lastDiagnostics = pink->diagnostics();
    while (g_running)
    {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      logDiagnostics(log, pink->diagnostics(), lastDiagnostics);
      if (!statusFile.write(*pink) && !statusFileWarned)
      {
        log.warning("cannot write %s (status for the setup page)", statusFile.path().c_str());
        statusFileWarned = true;
      }
    }
    log.info("stopping");
  }
  catch (const std::exception& e)
  {
    log.error("fatal: %s", e.what());
    return 1; // systemd restarts the service
  }

  return 0;
}
