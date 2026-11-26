/*
        ##########    Copyright (C) 2016 Vincenzo Pacella
        ##      ##    Distributed under MIT license, see file LICENSE
        ##      ##    or <http://opensource.org/licenses/MIT>
        ##      ##
##########      ############################################################# shaduzlabs.com #####*/

#pragma once

#include <ableton/Link.hpp>
#include <atomic>
#include <cstdint>
#include <mutex>

// -------------------------------------------------------------------------------------------------

namespace sl
{
namespace pi
{

// -------------------------------------------------------------------------------------------------

class Engine
{
public:
  using Latency = std::chrono::microseconds;
  using AudioBuffer = std::vector<double>;

  //! Counters filled by the audio thread, read (lock-free) by the monitoring loop in main().
  struct Diagnostics
  {
    std::uint32_t underruns;
    std::uint32_t beatJumps;
    std::int32_t lastBeatJumpUs;
  };

  Engine(ableton::Link& link);
  void startPlaying();
  void stopPlaying();
  bool isPlaying() const;

  std::size_t bufferSize() const;
  void setBufferSize(std::size_t size);

  double sampleRate() const;
  void setSampleRate(double sampleRate);

  double beatTime() const;
  double tempo() const;
  void setTempo(double tempo);
  double quantum() const;
  void setQuantum(double quantum);
  double clockMultiplier() const;
  void setClockMultiplier(double);

  const Latency& outputLatency() const;
  void setOutputLatency(Latency /*latency_*/);

  const AudioBuffer& bufferClock() const;
  const AudioBuffer& bufferReset() const;

  //! Audio thread only. numSamples must not exceed bufferSize().
  void process(const std::chrono::microseconds hostTime, std::size_t numSamples);

  //! Audio thread only: the audio backend reported an output underrun.
  void noteUnderrun();

  //! Any thread.
  Diagnostics diagnostics() const;

  //! Any thread. Incremented by the audio thread each time it applies a start, i.e. when beat 0
  //! (the start of the clock outputs) has been placed on the timeline.
  std::uint32_t startGeneration() const;

private:
  struct Data
  {
    double requestedTempo;
    bool resetBeatTime;
    bool isPlaying;
    double quantum;
    double clockMultiplier;
  };

  Data pullEngineData();
  void renderMetronomeIntoBuffer(ableton::Link::SessionState sessionState,
    double quantum,
    double clockMultiplier,
    std::chrono::microseconds beginHostTime,
    std::size_t numSamples);

  ableton::Link& m_link;
  double m_sampleRate;
  Latency m_outputLatency;
  std::vector<double> m_bufferClock;
  std::vector<double> m_bufferReset;
  Data m_engineDataShared;
  Data m_engineDataLockFree;
  std::chrono::microseconds m_timeAtLastClick;
  std::chrono::microseconds m_timeAtLastReset;
  double m_phase;
  mutable std::mutex m_engineDataGuard;

  // timeline continuity check (audio thread only)
  bool m_hasExpectedBeat;
  double m_expectedBeat;
  double m_expectedBeatQuantum;

  std::atomic<std::uint32_t> m_underruns;
  std::atomic<std::uint32_t> m_beatJumps;
  std::atomic<std::int32_t> m_lastBeatJumpUs;
  std::atomic<std::uint32_t> m_startGeneration;
};

// -------------------------------------------------------------------------------------------------

} // namespace pi
} // namespace sl
