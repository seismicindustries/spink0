/*
        ##########    Copyright (C) 2016 Vincenzo Pacella
        ##      ##    Distributed under MIT license, see file LICENSE
        ##      ##    or <http://opensource.org/licenses/MIT>
        ##      ##
##########      ############################################################# shaduzlabs.com #####*/

#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <ableton/Link.hpp>

#include "Audio.h"
#include "MidiOut.h"

// -------------------------------------------------------------------------------------------------

namespace sl
{
namespace pi
{

// -------------------------------------------------------------------------------------------------

class Pink
{
public:
  struct ClockMultiplier
  {
    ClockMultiplier(double v, std::string l) : value(v), label(std::move(l))
    {
    }

    double value;
    std::string label;
  };

  using ClockMultipliers = std::vector<ClockMultiplier>;

  Pink(double /*tempo_*/, double /*length_*/, double /*clockMultiplier_*/);

  bool isEnabled() const;
  void setEnabled(bool /*enabled_*/);

  void togglePlay();
  bool isPlaying();

  double tempo();
  //! Request a new session tempo (user action). Do not call this from Link's tempo callback.
  void setTempo(double /*tempo_*/);

  double loopLength();
  void setLoopLength(double /*loopLength_*/);

  std::size_t numClockMultipliers() const;
  std::size_t clockMultiplierIndex() const;
  const std::string& clockMultiplierLabel() const;
  const std::string& clockMultiplierLabel(std::size_t /*index_*/) const;
  void setClockMultiplierIndex(std::size_t /*index_*/);

  std::size_t numPeers() const;

  //! Monitoring only (see Engine::Diagnostics).
  Engine::Diagnostics diagnostics() const;

  class Listener
  {
  public:
    using Id = std::string;

    Listener();
    virtual ~Listener() = default;
    const Id& pinkListenerId() const
    {
      return m_id;
    }
    virtual void statusChanged(bool /*enabled_*/) = 0;
    virtual void tempoChanged(double /*t_*/) = 0;
    virtual void loopLengthChanged(double /*l_*/) = 0;
    virtual void numPeersChanged(std::size_t /*n_*/) = 0;
    virtual void runStatusChanged(bool /*playing_*/) = 0;
    virtual void clockMultiplierChanged(const std::string& /*cm_*/) = 0;

  private:
    Id m_id;
  };

  using Listeners = std::map<Listener::Id, Listener*>;

  void addListener(const Listener::Id /*id_*/, Listener* /*l_*/);
  void removeListener(const Listener::Id /*id_*/);

private:
  // Link callbacks, invoked on a Link-managed thread.
  void onLinkTempoChanged(double tempo_);
  void onLinkNumPeersChanged(std::size_t numPeers_);

  void notifyListeners(const std::function<void(Listener&)>& fn_);

  unsigned m_defaultClockMultiplier;
  std::atomic<unsigned> m_currentClockMultiplier;

  std::mutex m_mtxRunning;
  std::mutex m_mtxListeners;

  ableton::Link m_link;
  Audio m_audio;
  MidiOut m_midiOut; // after m_audio: needs its engine, and is destroyed before it

  Listeners m_listeners;
};

// -------------------------------------------------------------------------------------------------

} // namespace pi
} // namespace sl
