/*
        ##########    Copyright (C) 2016 Vincenzo Pacella
        ##      ##    Distributed under MIT license, see file LICENSE
        ##      ##    or <http://opensource.org/licenses/MIT>
        ##      ##
##########      ############################################################# shaduzlabs.com #####*/

#include "Engine.h"

#include <algorithm>
#include <cmath>
#include <limits>

// -------------------------------------------------------------------------------------------------

namespace sl
{
namespace pi
{

using namespace ableton;

namespace
{
// A timeline discontinuity larger than this between two consecutive audio buffers is counted as
// a "beat jump" (it shows up as a stretched, shortened or doubled clock pulse at the outputs).
const double kBeatJumpThresholdUs{1000.};
} // namespace

// -------------------------------------------------------------------------------------------------

Engine::Engine(Link& link)
  : m_link(link)
  , m_sampleRate(44100.)
  , m_outputLatency{}
  , m_engineDataShared({0., false, false, 4., 1.})
  , m_engineDataLockFree(m_engineDataShared)
  , m_timeAtLastClick{}
  , m_timeAtLastReset{}
  , m_phase(0.)
  , m_hasExpectedBeat(false)
  , m_expectedBeat(0.)
  , m_expectedBeatQuantum(0.)
  , m_underruns(0)
  , m_beatJumps(0)
  , m_lastBeatJumpUs(0)
{
}

// -------------------------------------------------------------------------------------------------

void Engine::startPlaying()
{
  std::lock_guard<std::mutex> lock(m_engineDataGuard);
  m_engineDataShared.resetBeatTime = true;
  m_engineDataShared.isPlaying = true;
}

// -------------------------------------------------------------------------------------------------

void Engine::stopPlaying()
{
  std::lock_guard<std::mutex> lock(m_engineDataGuard);
  m_engineDataShared.isPlaying = false;
}

// -------------------------------------------------------------------------------------------------

bool Engine::isPlaying() const
{
  std::lock_guard<std::mutex> lock(m_engineDataGuard);
  return m_engineDataShared.isPlaying;
}

// -------------------------------------------------------------------------------------------------

double Engine::beatTime() const
{
  const auto timeline = m_link.captureAppSessionState();
  return timeline.beatAtTime(m_link.clock().micros(), quantum());
}

// -------------------------------------------------------------------------------------------------

std::size_t Engine::bufferSize() const
{
  return m_bufferClock.size();
}

// -------------------------------------------------------------------------------------------------

void Engine::setBufferSize(std::size_t size)
{
  m_bufferClock = std::vector<double>(size, 1.);
  m_bufferReset = std::vector<double>(size, 1.);
}

// -------------------------------------------------------------------------------------------------

double Engine::sampleRate() const
{
  return m_sampleRate;
}

// -------------------------------------------------------------------------------------------------

void Engine::setSampleRate(double sampleRate)
{
  m_sampleRate = sampleRate;
}

// -------------------------------------------------------------------------------------------------

double Engine::tempo() const
{
  return m_link.captureAppSessionState().tempo();
}

// -------------------------------------------------------------------------------------------------

void Engine::setTempo(double tempo)
{
  std::lock_guard<std::mutex> lock(m_engineDataGuard);
  m_engineDataShared.requestedTempo = tempo;
}

// -------------------------------------------------------------------------------------------------

double Engine::quantum() const
{
  std::lock_guard<std::mutex> lock(m_engineDataGuard);
  return m_engineDataShared.quantum;
}

// -------------------------------------------------------------------------------------------------

void Engine::setQuantum(double quantum)
{
  std::lock_guard<std::mutex> lock(m_engineDataGuard);
  m_engineDataShared.quantum = quantum;
}

// -------------------------------------------------------------------------------------------------

double Engine::clockMultiplier() const
{
  std::lock_guard<std::mutex> lock(m_engineDataGuard);
  return m_engineDataShared.clockMultiplier;
}

// -------------------------------------------------------------------------------------------------

void Engine::setClockMultiplier(double cr_)
{
  std::lock_guard<std::mutex> lock(m_engineDataGuard);
  m_engineDataShared.clockMultiplier = cr_;
}

// -------------------------------------------------------------------------------------------------

const Engine::Latency& Engine::outputLatency() const
{
  return m_outputLatency;
}

// -------------------------------------------------------------------------------------------------

void Engine::setOutputLatency(Latency latency_)
{
  m_outputLatency = latency_;
}

// -------------------------------------------------------------------------------------------------

const Engine::AudioBuffer& Engine::bufferClock() const
{
  return m_bufferClock;
}

// -------------------------------------------------------------------------------------------------

const Engine::AudioBuffer& Engine::bufferReset() const
{
  return m_bufferReset;
}

// -------------------------------------------------------------------------------------------------

void Engine::process(const std::chrono::microseconds hostTime, const std::size_t numSamples)
{
  const auto engineData = pullEngineData();

  // Realtime-safe capture. captureAppSessionState() locks a mutex shared with Link's network
  // thread and must never be called from the audio callback.
  auto timeline = m_link.captureAudioSessionState();

  // Clear the buffer
  std::fill(m_bufferClock.begin(), m_bufferClock.end(), 0.);
  std::fill(m_bufferReset.begin(), m_bufferReset.end(), 0.);

  if (engineData.resetBeatTime)
  {
    // Reset the timeline so that beat 0 lands at the beginning of
    // this buffer and clear the flag.
    timeline.requestBeatAtTime(0, hostTime, engineData.quantum);
  }

  if (engineData.requestedTempo > 0)
  {
    // Set the newly requested tempo from the beginning of this buffer
    timeline.setTempo(engineData.requestedTempo, hostTime);
  }

  // Timeline modifications are complete, commit the results
  m_link.commitAudioSessionState(timeline);

  // Timeline continuity check: the beat at the start of this buffer should be where the previous
  // buffer ended. Deliberate changes (our own reset/tempo request, a new quantum) are skipped.
  {
    const auto microsPerSample = 1e6 / m_sampleRate;
    const auto bufferEnd = hostTime + std::chrono::microseconds(llround(numSamples * microsPerSample));
    const bool deliberateChange = engineData.resetBeatTime || engineData.requestedTempo > 0
                                  || engineData.quantum != m_expectedBeatQuantum;
    if (m_hasExpectedBeat && !deliberateChange)
    {
      const auto beatAtBegin = timeline.beatAtTime(hostTime, engineData.quantum);
      const auto jumpUs = (beatAtBegin - m_expectedBeat) * 60e6 / timeline.tempo();
      if (std::abs(jumpUs) > kBeatJumpThresholdUs)
      {
        const double limit = std::numeric_limits<std::int32_t>::max();
        m_lastBeatJumpUs.store(static_cast<std::int32_t>(std::max(-limit, std::min(limit, jumpUs))));
        m_beatJumps.fetch_add(1);
      }
    }
    m_expectedBeat = timeline.beatAtTime(bufferEnd, engineData.quantum);
    m_expectedBeatQuantum = engineData.quantum;
    m_hasExpectedBeat = true;
  }

  if (engineData.isPlaying)
  {
    // As long as the engine is playing, generate metronome clicks in
    // the buffer at the appropriate beats.
    renderMetronomeIntoBuffer(
      timeline, engineData.quantum, engineData.clockMultiplier, hostTime, numSamples);
  }
}

// -------------------------------------------------------------------------------------------------

void Engine::noteUnderrun()
{
  m_underruns.fetch_add(1);
}

// -------------------------------------------------------------------------------------------------

Engine::Diagnostics Engine::diagnostics() const
{
  return {m_underruns.load(), m_beatJumps.load(), m_lastBeatJumpUs.load()};
}

// -------------------------------------------------------------------------------------------------

Engine::Data Engine::pullEngineData()
{
  auto engineData = Data{};
  if (m_engineDataGuard.try_lock())
  {
    engineData.requestedTempo = m_engineDataShared.requestedTempo;
    m_engineDataShared.requestedTempo = 0;
    engineData.resetBeatTime = m_engineDataShared.resetBeatTime;
    m_engineDataShared.resetBeatTime = false;

    m_engineDataLockFree.isPlaying = m_engineDataShared.isPlaying;
    m_engineDataLockFree.quantum = m_engineDataShared.quantum;
    m_engineDataLockFree.clockMultiplier = m_engineDataShared.clockMultiplier;

    m_engineDataGuard.unlock();
  }
  engineData.isPlaying = m_engineDataLockFree.isPlaying;
  engineData.quantum = m_engineDataLockFree.quantum;
  engineData.clockMultiplier = m_engineDataLockFree.clockMultiplier;

  return engineData;
}

// -------------------------------------------------------------------------------------------------

void Engine::renderMetronomeIntoBuffer(const Link::SessionState timeline,
  const double quantum,
  const double clockMultiplier,
  const std::chrono::microseconds beginHostTime,
  const std::size_t numSamples)
{
  using namespace std::chrono;

  // 1 ms click pulse duration, 20ms reset pulse duration
  static const auto resetDuration = duration<double>{0.02};

  // The number of microseconds that elapse between samples
  const auto microsPerSample = 1e6 / m_sampleRate;

  const double pulseWrapFactor = 1.0 / (4.0 * clockMultiplier);

  for (std::size_t i = 0; i < numSamples; ++i)
  {
    double clockAmplitude = 1.;
    double resetAmplitude = 1.;

    // Compute the host time for this sample and the last.
    const auto hostTime = beginHostTime + microseconds(llround(i * microsPerSample));
    const auto lastSampleHostTime = hostTime - microseconds(llround(microsPerSample));

    // Only emit clock pulses for positive beat magnitudes. Negative beat
    // magnitudes are count-in beats.
    if (timeline.beatAtTime(hostTime, quantum) >= 0.)
    {
      auto clickDuration = duration<double>{(30.0 / timeline.tempo()) * pulseWrapFactor};

      if (timeline.phaseAtTime(hostTime, pulseWrapFactor)
          < timeline.phaseAtTime(lastSampleHostTime, pulseWrapFactor))
      {
        m_timeAtLastClick = hostTime;
      }

      const auto secondsAfterClick = duration_cast<duration<double>>(hostTime - m_timeAtLastClick);
      const auto secondsAfterReset = duration_cast<duration<double>>(hostTime - m_timeAtLastReset);

      auto phase = timeline.phaseAtTime(hostTime, quantum);
      if (phase < m_phase || phase == 0)
      {
        // reset!
        m_timeAtLastReset = hostTime;
      }
      m_phase = phase;

      if (secondsAfterClick < clickDuration)
      {
        clockAmplitude = -1.;
      }
      if (secondsAfterReset < resetDuration)
      {
        resetAmplitude = -1.;
      }
    }

    m_bufferClock[i] = clockAmplitude;
    m_bufferReset[i] = resetAmplitude;
  }
}

// -------------------------------------------------------------------------------------------------

} // namespace pi
} // namespace sl
