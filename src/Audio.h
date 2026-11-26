/*
        ##########    Copyright (C) 2016 Vincenzo Pacella
        ##      ##    Distributed under MIT license, see file LICENSE
        ##      ##    or <http://opensource.org/licenses/MIT>
        ##      ##
##########      ############################################################# shaduzlabs.com #####*/

#pragma once

#include <string>

#include <ableton/link/HostTimeFilter.hpp>
#include <ableton/platforms/Config.hpp>
#include <portaudio.h>

#include "Engine.h"

// -------------------------------------------------------------------------------------------------

namespace sl
{
namespace pi
{

// -------------------------------------------------------------------------------------------------

//! Renders the clock (right channel) and reset (left channel) signals through PortAudio. On the
//! spink0 these are the Pi's PWM audio pins (GPIO18 / GPIO13), buffered by the op-amp stage.
class Audio
{
public:
  //! Output device: the first one whose name contains $PINK_AUDIO_DEVICE if set, otherwise the
  //! Pi's PWM output ("Headphones"), otherwise the default output device.
  explicit Audio(ableton::Link& link);
  ~Audio();

  Audio(const Audio&) = delete;
  Audio& operator=(const Audio&) = delete;

  Engine& engine()
  {
    return m_engine;
  }
  const Engine& engine() const
  {
    return m_engine;
  }

  //! "<device name> @ <rate> Hz, latency <n> ms", for the log.
  const std::string& description() const
  {
    return m_description;
  }

private:
  static int audioCallback(const void* inputBuffer,
    void* outputBuffer,
    unsigned long inNumFrames,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void* userData);

  void initialize();
  void uninitialize();
  void start();
  void stop();

  ableton::link::HostTimeFilter<ableton::link::platform::Clock> m_hostTimeFilter;

  Engine m_engine;
  double m_sampleTime;
  PaStream* m_stream;
  std::string m_description;
};

// -------------------------------------------------------------------------------------------------

} // namespace pi
} // namespace sl
