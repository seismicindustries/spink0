/*
        ##########    Copyright (C) 2016 Vincenzo Pacella
        ##      ##    Distributed under MIT license, see file LICENSE
        ##      ##    or <http://opensource.org/licenses/MIT>
        ##      ##
##########      #############################################################
shaduzlabs.com #####*/

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <memory>
#include <thread>
#include <tuple>

#ifdef __linux__

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

#endif // __linux__

#include "Audio.h"
#include "Pink.h"

#include "ui/UserInterfaceNone.h"
#ifdef UI_USE_WEBSOCKET
#include "ui/UserInterfaceWebSocket.h"
#endif
#ifdef UI_USE_RASPBERRY_PI_ZERO
#include "ui/UserInterfacePiZero.h"
#endif

#define DAEMON_NAME "pinkd"

//--------------------------------------------------------------------------------------------------

#ifdef __linux__

int pidFilehandle;

#endif // __linux__

//--------------------------------------------------------------------------------------------------

#ifdef __linux__

void daemonShutdown()
{
  close(pidFilehandle);
}

#endif // __linux__

//--------------------------------------------------------------------------------------------------

#ifdef __linux__

void signalHandler(int sig)
{
  switch (sig)
  {
    case SIGHUP:
      syslog(LOG_WARNING, "Received SIGHUP signal.");
      break;
    case SIGINT:
    case SIGTERM:
      syslog(LOG_INFO, "Daemon exiting");
      daemonShutdown();
      exit(EXIT_SUCCESS);
      break;
    default:
      syslog(LOG_WARNING, "Unhandled signal %s", strsignal(sig));
      break;
  }
}

#endif // __linux__

//--------------------------------------------------------------------------------------------------

static void daemonize(const char* runDir_, const char* pidFile_)
{
#ifndef __linux__
  std::ignore = runDir_;
  std::ignore = pidFile_;
#else
  /* Our process ID and Session ID */
  pid_t pid, sid;
  int i;
  char str[10];
  struct sigaction newSigAction;
  sigset_t newSigSet;

  /* Check if parent process id is set */
  if (getppid() == 1)
  {
    /* PPID exists, therefore we are already a daemon */
    return;
  }

  sigemptyset(&newSigSet);
  sigaddset(&newSigSet, SIGCHLD);           /* ignore child - i.e. we don't need to wait for it */
  sigaddset(&newSigSet, SIGTSTP);           /* ignore Tty stop signals */
  sigaddset(&newSigSet, SIGTTOU);           /* ignore Tty background writes */
  sigaddset(&newSigSet, SIGTTIN);           /* ignore Tty background reads */
  sigprocmask(SIG_BLOCK, &newSigSet, NULL); /* Block the above specified signals */

  /* Set up a signal handler */
  newSigAction.sa_handler = signalHandler;
  sigemptyset(&newSigAction.sa_mask);
  newSigAction.sa_flags = 0;

  /* Signals to handle */
  sigaction(SIGHUP, &newSigAction, NULL);  /* catch hangup signal */
  sigaction(SIGTERM, &newSigAction, NULL); /* catch term signal */
  sigaction(SIGINT, &newSigAction, NULL);  /* catch interrupt signal */

  /* Fork off the parent process */
  pid = fork();
  if (pid < 0)
  {
    exit(EXIT_FAILURE);
  }
  /* If we got a good PID, then we can exit the parent process. */
  if (pid > 0)
  {
    exit(EXIT_SUCCESS);
  }

  /* Change the file mode mask */
  umask(027);

  /* Create a new SID for the child process */
  sid = setsid();
  if (sid < 0)
  {
    /* Log the failure */
    syslog(LOG_ERR, "ERROR: Failed to create a new session");
    exit(EXIT_FAILURE);
  }

  /* Close out the open file descriptors. Close syslog's socket properly first: otherwise its
     descriptor number gets reused (e.g. by the PID lock file below) and syslog later closes it. */
  closelog();
  for (int fd = sysconf(_SC_OPEN_MAX); fd >= 0; fd--)
  {
    close(fd);
  }

  /* Route I/O connections */

  /* Open STDIN */
  i = open("/dev/null", O_RDWR);

  /* STDOUT */
  dup(i);

  /* STDERR */
  dup(i);

  openlog(DAEMON_NAME, LOG_CONS | LOG_PID, LOG_USER);

  if ((chdir(runDir_)) < 0)
  {
    syslog(LOG_ERR, "ERROR: Failed to change the working directory to %s", runDir_);
    exit(EXIT_FAILURE);
  }

  /* Ensure only one copy */
  pidFilehandle = open(pidFile_, O_RDWR | O_CREAT, 0600);

  if (pidFilehandle == -1)
  {
    /* Couldn't open lock file */
    syslog(LOG_INFO, "Could not open PID lock file %s, exiting", pidFile_);
    exit(EXIT_FAILURE);
  }

  /* Try to lock file */
  if (lockf(pidFilehandle, F_TLOCK, 0) == -1)
  {
    /* Couldn't get lock on lock file */
    syslog(LOG_INFO, "Could not lock PID lock file %s, exiting", pidFile_);
    exit(EXIT_FAILURE);
  }

  /* Get and format PID */
  sprintf(str, "%d\n", getpid());

  /* write pid to lockfile */
  write(pidFilehandle, str, strlen(str));
#endif
}

//--------------------------------------------------------------------------------------------------

class ScopedLog
{
public:
  ScopedLog()
  {
#ifdef __linux__
    setlogmask(LOG_UPTO(LOG_INFO));
    openlog(DAEMON_NAME, LOG_CONS | LOG_PERROR, LOG_USER);
#endif // __linux__
  }
  ~ScopedLog()
  {
#ifdef __linux__
    closelog();
#endif // __linux__
  }

  void info(const char* message_)
  {
#ifdef __linux__
    syslog(LOG_INFO, "%s", message_); // never pass a message as the format string
#else
    std::cout << DAEMON_NAME << ": " << message_ << std::endl;
#endif
  }

  void warning(const char* format_, ...) __attribute__((format(printf, 2, 3)))
  {
    char message[256];
    va_list args;
    va_start(args, format_);
    vsnprintf(message, sizeof(message), format_, args);
    va_end(args);
#ifdef __linux__
    syslog(LOG_WARNING, "%s", message);
#else
    std::cout << DAEMON_NAME << ": " << message << std::endl;
#endif
  }

  void exception(const std::exception& e_)
  {
#ifdef __linux__
    syslog(LOG_ERR, "ERROR: An exception occurred  (%s)", e_.what());
#else
    std::cerr << "An exception occurred: " << e_.what() << std::endl;
#endif
  }
};

//--------------------------------------------------------------------------------------------------

using namespace sl;
using namespace sl::pi;

//--------------------------------------------------------------------------------------------------

//! Logs session events, so hiccups can be correlated with peer changes (journalctl -t pinkd).
class LogListener : public Pink::Listener
{
public:
  LogListener(std::shared_ptr<Pink> pink_, ScopedLog& log_) : m_pink(std::move(pink_)), m_log(log_)
  {
    m_pink->addListener(pinkListenerId(), this);
  }
  ~LogListener() override
  {
    m_pink->removeListener(pinkListenerId());
  }

  void statusChanged(bool enabled_) override
  {
    m_log.warning("link %s", enabled_ ? "enabled" : "disabled");
  }
  void tempoChanged(double) override
  {
  }
  void loopLengthChanged(double) override
  {
  }
  void numPeersChanged(std::size_t n_) override
  {
    m_log.warning("link peers: %zu", n_);
  }
  void runStatusChanged(bool playing_) override
  {
    m_log.warning("clock %s", playing_ ? "started" : "stopped");
  }
  void clockMultiplierChanged(const std::string&) override
  {
  }

private:
  std::shared_ptr<Pink> m_pink;
  ScopedLog& m_log;
};

//--------------------------------------------------------------------------------------------------

//! Reports new audio underruns and timeline jumps once per second (the audio thread only counts).
static void logDiagnostics(ScopedLog& log_, const Engine::Diagnostics& now_, Engine::Diagnostics& last_)
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

int main(int argc_, char* argv_[])
{
  ScopedLog log;

  log.info("daemon starting");
  if (argc_ != 2 || std::string(argv_[1]) != "no-daemon")
  {
    daemonize("/tmp/", "/var/run/pinkd.pid");
  }

  try
  {
    // Exactly one instance: every Pink owns its own Link peer and its own audio stream.
    auto pink = std::make_shared<Pink>(120., 4., 1.0);

#ifdef UI_USE_WEBSOCKET
    UserInterfaceWebSocket webSocket(pink);
#endif
#ifdef UI_USE_RASPBERRY_PI_ZERO
    UserInterfacePiZero zero(pink);
#endif
#if !defined(UI_USE_RASPBERRY_PI_ZERO) && !defined(UI_USE_WEBSOCKET)
    UserInterfaceNone zero(pink);
#endif

    LogListener logListener(pink, log);

    log.info("daemon running");

    auto lastDiagnostics = pink->diagnostics();
    while (true)
    {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      logDiagnostics(log, pink->diagnostics(), lastDiagnostics);
    }
  }
  catch (const std::exception& e)
  {
    log.exception(e);
  }

  log.info("daemon terminated");

  return 0;
}

//--------------------------------------------------------------------------------------------------
