/*
        ##########    Copyright (C) 2016 Vincenzo Pacella
        ##      ##    Distributed under MIT license, see file LICENSE
        ##      ##    or <http://opensource.org/licenses/MIT>
        ##      ##
##########      ############################################################# shaduzlabs.com #####*/

#include "Pink.h"

// -------------------------------------------------------------------------------------------------

namespace
{

// -------------------------------------------------------------------------------------------------

static sl::pi::Pink::ClockMultipliers k_clockMultipliers{{1 / 64., "1/64"},
  {1 / 32., "1/32"},
  {1 / 16., "1/16"},
  {1 / 8., "1/8"},
  {1 / 4., "1/4"},
  {1 / 3., "1/3"},
  {1 / 2., "1/2"},
  {1., "1"},
  {2., "2"},
  {3., "3"},
  {4., "4"},
  {5., "5"},
  {6., "6"},
  {7., "7"},
  {8., "8"},
  {9., "9"},
  {10., "10"},
  {11., "11"},
  {12., "12"},
  {13., "13"},
  {14., "14"},
  {15., "15"},
  {16., "16"},
  {24., "24"},
  {32., "32"},
  {48., "48"},
  {64., "64"},
  {128., "128"}};

// -------------------------------------------------------------------------------------------------

const std::size_t kListenerIdLength{16};
const std::string kEmptyString = "";

// -------------------------------------------------------------------------------------------------

std::string randomId(std::size_t length_)
{
  static const char characters[] = "0123456789"
                                   "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                   "abcdefghijklmnopqrstuvwxyz";

  std::string id;

  for (std::size_t i = 0; i < length_; ++i)
  {
    id += characters[rand() % (sizeof(characters) - 1)];
  }

  return id;
}

// -------------------------------------------------------------------------------------------------

} // namespace

// -------------------------------------------------------------------------------------------------

namespace sl
{
namespace pi
{

// -------------------------------------------------------------------------------------------------

Pink::Pink(double tempo_, double length_, double clockMultiplier_) : m_link(tempo_), m_audio(m_link)
{
  for (unsigned i = 0; i < k_clockMultipliers.size(); ++i)
  {
    if (k_clockMultipliers[i].value == 1.)
    {
      m_defaultClockMultiplier = i;
    }
  }
  m_currentClockMultiplier = m_defaultClockMultiplier;

  if (clockMultiplier_ > 0.)
  {
    for (unsigned i = 0; i < k_clockMultipliers.size(); ++i)
    {
      if (k_clockMultipliers[i].value == clockMultiplier_)
      {
        m_currentClockMultiplier = i;
      }
    }
  }
  using namespace std::placeholders;

  // The tempo callback only reports remote changes to the UIs. It used to call setTempo(), which
  // re-committed every remote tempo change as our own and echoed it back into the session.
  m_link.setTempoCallback(std::bind(&Pink::onLinkTempoChanged, this, _1));

  m_link.setNumPeersCallback(std::bind(&Pink::onLinkNumPeersChanged, this, _1));

  m_link.enable(true);

  // No setTempo(tempo_) here: m_link was constructed with it as the initial tempo already, and
  // re-requesting it would push 120 BPM onto a session we are just joining.
  setLoopLength(length_);

  m_audio.engine().setClockMultiplier(k_clockMultipliers[m_currentClockMultiplier].value);
}

// -------------------------------------------------------------------------------------------------

bool Pink::isEnabled() const
{
  return m_link.isEnabled();
}

// -------------------------------------------------------------------------------------------------

void Pink::setEnabled(bool enabled_)
{
  std::lock_guard<std::mutex> lock(m_mtxRunning);
  m_link.enable(enabled_);

  const bool enabled = m_link.isEnabled();
  bool stopped = false;
  if (!enabled && m_audio.engine().isPlaying())
  {
    m_audio.engine().stopPlaying();
    stopped = true;
  }

  notifyListeners([&](Listener& l) {
    l.statusChanged(enabled);
    if (stopped)
    {
      l.runStatusChanged(false);
    }
  });
}

// -------------------------------------------------------------------------------------------------

void Pink::togglePlay()
{
  std::lock_guard<std::mutex> lock(m_mtxRunning);
  if (!m_link.isEnabled())
  {
    return;
  }
  
  bool playing = m_audio.engine().isPlaying();
  if (playing)
  {
    m_audio.engine().stopPlaying();
  }
  else
  {
    m_audio.engine().startPlaying();
  }

  notifyListeners([&](Listener& l) { l.runStatusChanged(!playing); });
}

// -------------------------------------------------------------------------------------------------

bool Pink::isPlaying()
{
  return m_audio.engine().isPlaying();
}

// -------------------------------------------------------------------------------------------------

double Pink::tempo()
{
  return m_audio.engine().tempo();
}

// -------------------------------------------------------------------------------------------------

void Pink::setTempo(double tempo_)
{
  m_audio.engine().setTempo(tempo_);
  notifyListeners([&](Listener& l) { l.tempoChanged(tempo_); });
}

// -------------------------------------------------------------------------------------------------

void Pink::onLinkTempoChanged(double tempo_)
{
  notifyListeners([&](Listener& l) { l.tempoChanged(tempo_); });
}

// -------------------------------------------------------------------------------------------------

void Pink::onLinkNumPeersChanged(std::size_t numPeers_)
{
  notifyListeners([&](Listener& l) { l.numPeersChanged(numPeers_); });
}

// -------------------------------------------------------------------------------------------------

void Pink::notifyListeners(const std::function<void(Listener&)>& fn_)
{
  // Listeners are called from several threads (Link, GPIO interrupts, web socket). They must not
  // call addListener()/removeListener() from inside a callback.
  std::lock_guard<std::mutex> lock(m_mtxListeners);
  for (const auto& l : m_listeners)
  {
    if (l.second != nullptr)
    {
      fn_(*l.second);
    }
  }
}

// -------------------------------------------------------------------------------------------------

double Pink::loopLength()
{
  return m_audio.engine().quantum();
}

// -------------------------------------------------------------------------------------------------

void Pink::setLoopLength(double length_)
{
  m_audio.engine().setQuantum(length_);
  notifyListeners([&](Listener& l) { l.loopLengthChanged(length_); });
}

// -------------------------------------------------------------------------------------------------

std::size_t Pink::numClockMultipliers() const
{
  return k_clockMultipliers.size();
}

// -------------------------------------------------------------------------------------------------

std::size_t Pink::clockMultiplierIndex() const
{
  return m_currentClockMultiplier;
}

// -------------------------------------------------------------------------------------------------

const std::string& Pink::clockMultiplierLabel() const
{
  return k_clockMultipliers[m_currentClockMultiplier].label;
}

// -------------------------------------------------------------------------------------------------

const std::string& Pink::clockMultiplierLabel(std::size_t index_) const
{
  if (index_ < k_clockMultipliers.size())
  {
    return k_clockMultipliers[index_].label;
  }
  return kEmptyString;
}

// -------------------------------------------------------------------------------------------------

void Pink::setClockMultiplierIndex(std::size_t index_)
{
  if (index_ < k_clockMultipliers.size())
  {
    m_currentClockMultiplier = static_cast<unsigned>(index_);
    m_audio.engine().setClockMultiplier(k_clockMultipliers[index_].value);
    const auto& label = k_clockMultipliers[index_].label;
    notifyListeners([&](Listener& l) { l.clockMultiplierChanged(label); });
  }
}

// -------------------------------------------------------------------------------------------------

std::size_t Pink::numPeers() const
{
  return m_link.numPeers();
}

// -------------------------------------------------------------------------------------------------

Engine::Diagnostics Pink::diagnostics() const
{
  return m_audio.engine().diagnostics();
}

// -------------------------------------------------------------------------------------------------

Pink::Listener::Listener() : m_id(randomId(kListenerIdLength))
{
}

// -------------------------------------------------------------------------------------------------

void Pink::addListener(const Listener::Id id_, Listener* l_)
{
  std::lock_guard<std::mutex> lock(m_mtxListeners);
  m_listeners[id_] = l_;
}

// -------------------------------------------------------------------------------------------------

void Pink::removeListener(const Listener::Id id_)
{
  std::lock_guard<std::mutex> lock(m_mtxListeners);
  m_listeners.erase(id_);
}

// -------------------------------------------------------------------------------------------------

} // namespace pi
} // namespace sl
