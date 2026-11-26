/*
        ##########    Copyright (C) 2016 Vincenzo Pacella
        ##      ##    Distributed under MIT license, see file LICENSE
        ##      ##    or <http://opensource.org/licenses/MIT>
        ##      ##
##########      ############################################################# shaduzlabs.com #####*/

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

#include <ableton/Link.hpp>

#include "Engine.h"

// -------------------------------------------------------------------------------------------------

namespace sl
{
namespace pi
{

// -------------------------------------------------------------------------------------------------

//! MIDI clock out on a serial port (spink0: UART TX on GPIO14 -> MIDI OUT jack).
//!
//! Sends 24 clock ticks per beat, locked to the Link timeline, continuously (also while stopped,
//! so connected devices can follow the tempo). Start (0xFA) is sent right before the clock tick at
//! beat 0, where the CV clock and reset outputs start as well. Stop (0xFC) is sent when the engine
//! stops. If beat 0 has already passed when a start becomes visible, Song Position Pointer +
//! Continue are sent on the next 16th note instead, so devices still lock in phase.
//!
//! The UART must run at 31250 baud. On the Pi this is done with the midi-uart0 overlay, which maps
//! a requested 38400 baud to 31250 (see README).
class MidiOut
{
public:
  MidiOut(ableton::Link& link_, Engine& engine_, std::string device_ = "/dev/serial0");
  ~MidiOut();

  MidiOut(const MidiOut&) = delete;
  MidiOut& operator=(const MidiOut&) = delete;

private:
  void run();
  bool openDevice();
  void send(std::initializer_list<std::uint8_t> bytes_);
  void sleepUntil(std::chrono::microseconds linkTime_) const;

  ableton::Link& m_link;
  Engine& m_engine;
  std::string m_device;
  int m_fd{-1};

  std::atomic_bool m_running{true};
  std::thread m_thread;
};

// -------------------------------------------------------------------------------------------------

} // namespace pi
} // namespace sl
