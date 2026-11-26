/*
        ##########    Copyright (C) 2016 Vincenzo Pacella
        ##      ##    Distributed under MIT license, see file LICENSE
        ##      ##    or <http://opensource.org/licenses/MIT>
        ##      ##
##########      ############################################################# shaduzlabs.com #####*/

#include "MidiOut.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <iostream>
#include <vector>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <termios.h>
#include <unistd.h>

#ifdef __linux__
#include <syslog.h>
#endif

// -------------------------------------------------------------------------------------------------

namespace
{

using std::chrono::microseconds;

const std::uint8_t kMidiClock{0xF8};
const std::uint8_t kMidiStart{0xFA};
const std::uint8_t kMidiContinue{0xFB};
const std::uint8_t kMidiStop{0xFC};
const std::uint8_t kMidiSongPosition{0xF2};

const int kTicksPerBeat{24};  // MIDI clock resolution, independent of the CV clock multiplier
const int kTicksPerSixteenth{kTicksPerBeat / 4};

//! One MIDI byte on the wire: 10 bits at 31250 baud. Bytes are sent this much earlier, so that
//! the last byte of a message is complete at the tick time.
const microseconds kByteTime{320};

//! Longest single sleep: the next tick is recomputed at least this often, so tempo changes made
//! while waiting are picked up.
const microseconds kMaxSleep{5000};

//! Sleep until this close to the target, then busy-wait. Keeps wake-up jitter out of the output.
const microseconds kSpinTime{200};

//! After the audio thread has applied a start, give Link a moment to publish the new timeline to
//! the app-side session state before acting on it (beat 0 lies at least one output latency ahead).
const microseconds kStartSettleTime{10000};

//! SCHED_FIFO priority: just above PortAudio's audio thread (which uses 1), so a running audio
//! callback cannot delay a clock tick. The busy-wait is limited to kSpinTime per tick.
const int kRealtimePriority{2};

void logMessage(int priority_, const std::string& message_)
{
#ifdef __linux__
  syslog(priority_, "midi: %s", message_.c_str());
#else
  (void)priority_;
  std::cerr << "pinkd: midi: " << message_ << std::endl;
#endif
}

#ifndef __linux__
const int LOG_INFO{6};
const int LOG_WARNING{4};
#endif

} // namespace

// -------------------------------------------------------------------------------------------------

namespace sl
{
namespace pi
{

// -------------------------------------------------------------------------------------------------

MidiOut::MidiOut(ableton::Link& link_, Engine& engine_, std::string device_)
  : m_link(link_), m_engine(engine_), m_device(std::move(device_))
{
  if (openDevice())
  {
    m_thread = std::thread(&MidiOut::run, this);
  }
}

// -------------------------------------------------------------------------------------------------

MidiOut::~MidiOut()
{
  m_running = false;
  if (m_thread.joinable())
  {
    m_thread.join();
  }
  if (m_fd >= 0)
  {
    close(m_fd);
  }
}

// -------------------------------------------------------------------------------------------------

bool MidiOut::openDevice()
{
  m_fd = open(m_device.c_str(), O_WRONLY | O_NOCTTY);
  if (m_fd < 0)
  {
    logMessage(LOG_WARNING,
      "cannot open " + m_device + " (" + std::strerror(errno) + "), MIDI out disabled");
    return false;
  }

  termios tty{};
  if (tcgetattr(m_fd, &tty) != 0)
  {
    logMessage(LOG_WARNING, "tcgetattr failed on " + m_device + ", MIDI out disabled");
    close(m_fd);
    m_fd = -1;
    return false;
  }
  cfmakeraw(&tty);
  tty.c_cflag |= CLOCAL;
  tty.c_cflag &= ~CRTSCTS;
  cfsetospeed(&tty, B38400); // 31250 baud with dtoverlay=midi-uart0
  cfsetispeed(&tty, B38400);
  if (tcsetattr(m_fd, TCSANOW, &tty) != 0)
  {
    logMessage(LOG_WARNING, "tcsetattr failed on " + m_device + ", MIDI out disabled");
    close(m_fd);
    m_fd = -1;
    return false;
  }

  logMessage(LOG_INFO, "MIDI clock out on " + m_device);
  return true;
}

// -------------------------------------------------------------------------------------------------

void MidiOut::send(std::initializer_list<std::uint8_t> bytes_)
{
  const std::vector<std::uint8_t> data(bytes_);
  std::size_t written = 0;
  while (written < data.size())
  {
    const auto n = write(m_fd, data.data() + written, data.size() - written);
    if (n < 0)
    {
      if (errno == EINTR)
      {
        continue;
      }
      return; // nothing sensible to do in the timing thread; the next tick tries again
    }
    written += static_cast<std::size_t>(n);
  }
}

// -------------------------------------------------------------------------------------------------

void MidiOut::sleepUntil(const microseconds linkTime_) const
{
  while (m_running)
  {
    const auto remaining = linkTime_ - m_link.clock().micros();
    if (remaining <= microseconds(0))
    {
      return;
    }
    if (remaining > kSpinTime)
    {
      std::this_thread::sleep_for(remaining - kSpinTime);
    }
    // else: busy-wait the last few hundred microseconds
  }
}

// -------------------------------------------------------------------------------------------------

void MidiOut::run()
{
  sched_param param{};
  param.sched_priority = kRealtimePriority;
  if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) != 0)
  {
    logMessage(LOG_WARNING, "no realtime priority for the MIDI thread, clock may jitter");
  }

  bool wasPlaying = m_engine.isPlaying();
  auto lastStartGeneration = m_engine.startGeneration();
  bool startPending = false;
  microseconds startSeenAt{0};
  std::int64_t lastSentTick = std::numeric_limits<std::int64_t>::min();

  while (m_running)
  {
    const auto now = m_link.clock().micros();

    // Transport ------------------------------------------------------------------------------
    const bool playing = m_engine.isPlaying();
    const auto startGeneration = m_engine.startGeneration();
    if (startGeneration != lastStartGeneration)
    {
      // The audio thread has re-aligned the timeline (beat 0 = start of the CV clock).
      lastStartGeneration = startGeneration;
      startPending = true;
      startSeenAt = now;
      lastSentTick = std::numeric_limits<std::int64_t>::min();
    }
    if (wasPlaying && !playing)
    {
      send({kMidiStop});
      startPending = false;
    }
    wasPlaying = playing;
    if (!playing)
    {
      startPending = false;
    }

    // Next clock tick ------------------------------------------------------------------------
    const auto quantum = m_engine.quantum();
    const auto state = m_link.captureAppSessionState();

    // Earliest tick we can still deliver in time: account for the longest message (5 bytes).
    const auto beatNow = state.beatAtTime(now + 5 * kByteTime, quantum);
    auto tick = static_cast<std::int64_t>(std::floor(beatNow * kTicksPerBeat)) + 1;
    if (lastSentTick != std::numeric_limits<std::int64_t>::min()
        && std::llabs(tick - (lastSentTick + 1)) <= kTicksPerBeat)
    {
      // Receivers count ticks, so never skip or repeat one: a tick we are late for is sent late
      // (immediately) rather than dropped. Only a real timeline jump (> 1 beat) resynchronizes.
      tick = lastSentTick + 1;
    }
    const auto tickTime = state.timeAtBeat(static_cast<double>(tick) / kTicksPerBeat, quantum);

    if (tickTime - now > kMaxSleep)
    {
      // Re-read tempo and transport regularly, but never sleep past the tick itself.
      std::this_thread::sleep_for(std::min(kMaxSleep, tickTime - now - kMaxSleep / 2));
      continue;
    }

    const bool startSettled = startPending && (now - startSeenAt) >= kStartSettleTime;

    if (startSettled && tick == 0)
    {
      // Normal case: Start, then the first clock lands exactly on beat 0.
      sleepUntil(tickTime - 2 * kByteTime);
      send({kMidiStart, kMidiClock});
      startPending = false;
    }
    else if (startSettled && tick > 0 && tick % kTicksPerSixteenth == 0)
    {
      // Beat 0 was missed: tell devices where we are (in 16ths) and continue from there.
      const auto position = std::min<std::int64_t>(tick / kTicksPerSixteenth, 0x3FFF);
      sleepUntil(tickTime - 5 * kByteTime);
      send({kMidiSongPosition,
        static_cast<std::uint8_t>(position & 0x7F),
        static_cast<std::uint8_t>((position >> 7) & 0x7F),
        kMidiContinue,
        kMidiClock});
      startPending = false;
      logMessage(LOG_WARNING, "start missed beat 0, sent song position + continue");
    }
    else
    {
      // Negative ticks are the count-in before beat 0: keep the clock running, no start yet.
      sleepUntil(tickTime - kByteTime);
      send({kMidiClock});
    }
    lastSentTick = tick;
  }
}

// -------------------------------------------------------------------------------------------------

} // namespace pi
} // namespace sl
