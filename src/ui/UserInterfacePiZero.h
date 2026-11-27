/*
        ##########    Copyright (C) 2016 Vincenzo Pacella
        ##      ##    Distributed under MIT license, see file LICENSE
        ##      ##    or <http://opensource.org/licenses/MIT>
        ##      ##
##########      ############################################################# shaduzlabs.com #####*/

#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "ui/Gpio.h"
#include "ui/NetworkControl.h"
#include "ui/UserInterface.h"

// -------------------------------------------------------------------------------------------------

namespace sl
{
namespace pi
{

// -------------------------------------------------------------------------------------------------

//! spink0 front panel: 4-digit 7-segment display (multiplexed in software), 4 LEDs, rotary
//! encoder with push button, run button.
//!
//! - encoder: change the value shown (tempo, loop length, clock multiplier)
//! - encoder button, short press: next display page (tempo, length, ppqn, peers)
//! - encoder button, held 3 s: switch WLAN mode (access point <-> client)
//! - run button: start/stop the clock (on press)
class UserInterfacePiZero : public UserInterface
{
public:
  enum class Led
  {
    tempo,
    length,
    ppqn,
    running,
  };

  enum class DisplayState
  {
    tempo,
    length,
    ppqn,
    peers,
  };

  //! Throws if the GPIO lines cannot be requested.
  explicit UserInterfacePiZero(std::shared_ptr<Pink> /*pink_*/);
  ~UserInterfacePiZero() override;

  void statusChanged(bool /*enabled_*/) override
  {
  }
  void tempoChanged(double /*t_*/) override;
  void loopLengthChanged(double /*t_*/) override;
  void numPeersChanged(std::size_t /*n_*/) override;
  void runStatusChanged(bool /*playing_*/) override;
  void clockMultiplierChanged(const std::string& /*cm_*/) override;

private:
  using Clock = std::chrono::steady_clock;

  void display(double /*value_*/);
  void display(const std::string& /*value_*/);

  //! Shows text instead of the current page until the given time.
  void showMessage(const std::string& text_, Clock::duration duration_);

  void registerCallbacks();

  void displayLoop();
  void inputLoop();
  void updateUI();
  void writeDigit(unsigned digit_, char c_, bool dot_);
  void encoderStep();

  GpioChip m_chip;
  GpioOutputs m_display; // 8 segments + 4 digit selects
  GpioOutputs m_leds;
  GpioInputs m_inputs;

  std::atomic_bool m_running{true};
  std::atomic_bool m_updateUI{true};

  std::mutex m_mtxDisplay; // guards the display text and the message
  std::string m_displayString{"    "};
  bool m_displayShowDot{false};
  std::string m_message;
  Clock::time_point m_messageUntil{};

  std::atomic<DisplayState> m_displayState{DisplayState::tempo};
  unsigned m_lastEncoderValue{0b11};

  // encoder button long press (input thread only)
  bool m_encoderButtonDown{false};
  bool m_encoderButtonLongFired{false};
  Clock::time_point m_encoderButtonDownSince{};

  std::atomic_bool m_engineRunning{false};

  // Written by Pink listener callbacks (Link, input threads), read by the display thread.
  std::mutex m_mtxState;
  std::size_t m_numPeers{0};
  double m_engineTempo{0.};
  double m_engineLoopLength{0.};
  std::string m_engineClockMultiplier;

  // Started last, after all GPIO requests succeeded.
  std::thread m_threadDisplay;
  std::thread m_threadInput;

  // Declared last so it is destroyed first: its worker thread calls showMessage().
  NetworkControl m_network;
};

// -------------------------------------------------------------------------------------------------

} // namespace pi
} // namespace sl
