/*
        ##########    Copyright (C) 2016 Vincenzo Pacella
        ##      ##    Distributed under MIT license, see file LICENSE
        ##      ##    or <http://opensource.org/licenses/MIT>
        ##      ##
##########      ############################################################# shaduzlabs.com #####*/

#include "Audio.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <vector>

#ifdef __linux__
#include <pa_linux_alsa.h>
#endif

// -------------------------------------------------------------------------------------------------

namespace
{

//! Preferred sample rates, lowest first: the clock/reset pulses need little resolution
//! (11025 Hz = 91 us per sample) and the Pi Zero has a single core.
const std::vector<double> k_sampleRates{11025., 22050., 44100., 48000.};

//! Buffer duration, ~23 ms (256 frames at 11025 Hz).
const double k_bufferSeconds{256. / 11025.};

//! Device name fragments tried in order when $PINK_AUDIO_DEVICE is not set.
const std::vector<std::string> k_preferredDevices{"Headphones", "bcm2835"};

// -------------------------------------------------------------------------------------------------

bool isStereoOutput(PaDeviceIndex device_)
{
  const PaDeviceInfo* info = Pa_GetDeviceInfo(device_);
  return info != nullptr && info->maxOutputChannels >= 2;
}

// -------------------------------------------------------------------------------------------------

PaDeviceIndex findDevice(const std::string& nameFragment_)
{
  for (PaDeviceIndex i = 0; i < Pa_GetDeviceCount(); ++i)
  {
    const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
    if (info != nullptr && isStereoOutput(i)
        && std::string(info->name).find(nameFragment_) != std::string::npos)
    {
      return i;
    }
  }
  return paNoDevice;
}

// -------------------------------------------------------------------------------------------------

PaDeviceIndex selectDevice()
{
  if (const char* requested = std::getenv("PINK_AUDIO_DEVICE"))
  {
    const auto device = findDevice(requested);
    if (device == paNoDevice)
    {
      throw std::runtime_error(std::string("No audio output device matching PINK_AUDIO_DEVICE=")
                               + requested);
    }
    return device;
  }
  for (const auto& name : k_preferredDevices)
  {
    const auto device = findDevice(name);
    if (device != paNoDevice)
    {
      return device;
    }
  }
  return Pa_GetDefaultOutputDevice();
}

} // namespace

// -------------------------------------------------------------------------------------------------

namespace sl
{
namespace pi
{

// -------------------------------------------------------------------------------------------------

Audio::Audio(ableton::Link& link) : m_engine(link), m_sampleTime(0.), m_stream(nullptr)
{
  initialize();
  start();
}

// -------------------------------------------------------------------------------------------------

Audio::~Audio()
{
  try
  {
    stop();
    uninitialize();
  }
  catch (const std::exception&)
  {
    // nothing sensible to do while shutting down
  }
}

// -------------------------------------------------------------------------------------------------

int Audio::audioCallback(const void* /*inputBuffer*/,
  void* outputBuffer,
  unsigned long inNumFrames,
  const PaStreamCallbackTimeInfo* /*timeInfo*/,
  PaStreamCallbackFlags statusFlags,
  void* userData)
{
  float* buffer = static_cast<float*>(outputBuffer);
  Audio& platform = *static_cast<Audio*>(userData);
  Engine& engine = platform.m_engine;

  if (statusFlags & paOutputUnderflow)
  {
    engine.noteUnderrun();
  }

  const auto hostTime = platform.m_hostTimeFilter.sampleTimeToHostTime(platform.m_sampleTime);
  platform.m_sampleTime += static_cast<double>(inNumFrames);

  const auto bufferBeginAtOutput = hostTime + engine.outputLatency();

  // The stream is opened with a fixed buffer size equal to the engine's, this is a safety net.
  const std::size_t numFrames = std::min<std::size_t>(inNumFrames, engine.bufferSize());
  engine.process(bufferBeginAtOutput, numFrames);

  const auto& bufferReset = engine.bufferReset();
  const auto& bufferClock = engine.bufferClock();
  for (std::size_t i = 0; i < inNumFrames; ++i)
  {
    buffer[i * 2] = i < numFrames ? static_cast<float>(bufferReset[i]) : 0.f;
    buffer[i * 2 + 1] = i < numFrames ? static_cast<float>(bufferClock[i]) : 0.f;
  }

  return paContinue;
}

// -------------------------------------------------------------------------------------------------

void Audio::initialize()
{
  PaError result = Pa_Initialize();
  if (result != paNoError)
  {
    throw std::runtime_error(std::string("Could not initialize PortAudio: ") + Pa_GetErrorText(result));
  }

  PaStreamParameters outputParameters{};
  outputParameters.device = selectDevice();
  if (outputParameters.device == paNoDevice)
  {
    throw std::runtime_error("No audio output device");
  }
  const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(outputParameters.device);
  outputParameters.channelCount = 2;
  outputParameters.sampleFormat = paFloat32;
  outputParameters.suggestedLatency = deviceInfo->defaultHighOutputLatency; // robust on a Pi Zero
  outputParameters.hostApiSpecificStreamInfo = nullptr;

  double sampleRate = 0.;
  for (const auto rate : k_sampleRates)
  {
    if (Pa_IsFormatSupported(nullptr, &outputParameters, rate) == paFormatIsSupported)
    {
      sampleRate = rate;
      break;
    }
  }
  if (sampleRate == 0.)
  {
    throw std::runtime_error(
      std::string("Audio device supports none of the sample rates: ") + deviceInfo->name);
  }

  const auto bufferSize = static_cast<std::size_t>(std::lround(sampleRate * k_bufferSeconds));
  m_engine.setSampleRate(sampleRate);
  m_engine.setBufferSize(bufferSize);

  result = Pa_OpenStream(&m_stream,
    nullptr,
    &outputParameters,
    sampleRate,
    static_cast<unsigned long>(bufferSize),
    paClipOff,
    &audioCallback,
    this);
  if (result != paNoError || m_stream == nullptr)
  {
    throw std::runtime_error(std::string("Could not open audio stream: ") + Pa_GetErrorText(result));
  }

  // Compensate the latency the stream actually got.
  double latency = outputParameters.suggestedLatency;
  if (const PaStreamInfo* info = Pa_GetStreamInfo(m_stream))
  {
    latency = info->outputLatency;
  }
  m_engine.setOutputLatency(std::chrono::microseconds(std::llround(latency * 1.0e6)));

#ifdef __linux__
  // SCHED_FIFO for the ALSA callback thread (needs root or an rtprio limit, see pinkd.service).
  PaAlsa_EnableRealtimeScheduling(m_stream, 1);
#endif

  std::ostringstream description;
  description << deviceInfo->name << " @ " << sampleRate << " Hz, " << bufferSize
              << " frames, latency " << std::lround(latency * 1000.) << " ms";
  m_description = description.str();
}

// -------------------------------------------------------------------------------------------------

void Audio::uninitialize()
{
  if (m_stream != nullptr)
  {
    Pa_CloseStream(m_stream);
    m_stream = nullptr;
  }
  Pa_Terminate();
}

// -------------------------------------------------------------------------------------------------

void Audio::start()
{
  const PaError result = Pa_StartStream(m_stream);
  if (result != paNoError)
  {
    throw std::runtime_error(std::string("Could not start audio stream: ") + Pa_GetErrorText(result));
  }
}

// -------------------------------------------------------------------------------------------------

void Audio::stop()
{
  if (m_stream != nullptr)
  {
    Pa_StopStream(m_stream);
  }
}

// -------------------------------------------------------------------------------------------------

} // namespace pi
} // namespace sl
