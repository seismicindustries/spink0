/*
        ##########    Copyright (C) 2016 Vincenzo Pacella
        ##      ##    Distributed under MIT license, see file LICENSE
        ##      ##    or <http://opensource.org/licenses/MIT>
        ##      ##
##########      ############################################################# shaduzlabs.com #####*/

#pragma once

#include <atomic>
#include <future>
#include <mutex>
#include <string>
#include <thread>

#include "UserInterface.h"

// -------------------------------------------------------------------------------------------------

namespace sl
{
namespace pi
{

// -------------------------------------------------------------------------------------------------

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

  UserInterfacePiZero(std::shared_ptr<Pink> /*pink_*/);
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
  void display(int /*value_*/);
  void display(double /*value_*/);
  void display(const std::string& /*value_*/);

  void registerCallbacks();

  void hardwareIO();
  void updateUI();
  void setLed(Led /*led_*/, bool /*state_*/);

  static void encoderHandler();
  static void runButtonHandler();
  static void encoderButtonHandler();

  std::atomic_bool m_updateUI{true};
  std::atomic_bool m_running{true};

  std::future<void> m_runButtonDebouncer;
  std::future<void> m_encoderButtonDebouncer;
  std::atomic_bool m_runButtonPressed{false};
  std::atomic_bool m_encoderButtonPressed{false};

  std::thread m_threadDisplay; // started at the end of the constructor, after GPIO setup

  std::mutex m_mtxDisplay; // guards m_displayString, m_displayShowDot
  std::string m_displayString{"    "};
  bool m_displayShowDot{false};

  std::atomic<DisplayState> m_displayState;

  std::mutex m_mtxEncoder; // encoder A and B interrupts arrive on two different threads
  unsigned m_lastEncoderValue;

  std::atomic_bool m_engineRunning{false};

  // Written by Pink listener callbacks (Link, GPIO interrupt and web socket threads), read by the
  // display thread.
  std::mutex m_mtxState;
  std::size_t m_numPeers{0};
  double m_engineTempo{0.};
  double m_engineLoopLength{0.};
  std::string m_engineClockMultiplier;
};

// -------------------------------------------------------------------------------------------------

} // namespace pi
} // namespace sl
