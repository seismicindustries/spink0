/*
        ##########    Copyright (C) 2016 Vincenzo Pacella
        ##      ##    Distributed under MIT license, see file LICENSE
        ##      ##    or <http://opensource.org/licenses/MIT>
        ##      ##
##########      ############################################################# shaduzlabs.com #####*/

#include "ui/UserInterfacePiZero.h"

#include <cmath>
#include <sstream>

// -------------------------------------------------------------------------------------------------

namespace
{

// All pins are BCM GPIO numbers (see the main board schematic).

// LEDs (active high), in the order of the Led enum
const unsigned kPinLedTempo{20}; // was GPIO14, moved so GPIO14 (UART TX) can drive MIDI out
const unsigned kPinLedLength{3};
const unsigned kPinLedPPQN{2};
const unsigned kPinLedRunning{7};

// Inputs: pull-ups on the board / internal, all active low
const unsigned kPinEncoderA{19};
const unsigned kPinEncoderB{16};
const unsigned kPinBtnEncoder{6};
const unsigned kPinBtnRun{5};
const unsigned kButtonDebounceUs{10000};

// Display: segments a..g + dot (low = lit), digit selects (high = selected)
const unsigned kDisplayNumSegments{8};
const unsigned kDisplayNumDigits{4};
const std::vector<unsigned> kPinsDisplay{
  8, 25, 10, 17, 9, 23, 4, 27, // segments
  11, 24, 22, 15,              // digits
};

const auto kDisplayRefreshPeriod = std::chrono::microseconds(500); // per digit
const auto kInputPollTimeout = std::chrono::milliseconds(50);
const auto kLongPressDuration = std::chrono::seconds(3);
const auto kMessageDuration = std::chrono::seconds(3);
const auto kSwitchingTimeout = std::chrono::seconds(60);

const uint8_t kDisplayFontData[] = {
#include "Font7Seg.h"
};
const unsigned kFontFirstChar{45};
const unsigned kFontLastChar{90};

} // namespace

// -------------------------------------------------------------------------------------------------

namespace sl
{
namespace pi
{

// -------------------------------------------------------------------------------------------------

UserInterfacePiZero::UserInterfacePiZero(std::shared_ptr<Pink> pink_)
  : UserInterface(std::move(pink_))
  , m_display(m_chip, kPinsDisplay, "pinkd-display")
  , m_leds(m_chip, {kPinLedTempo, kPinLedLength, kPinLedPPQN, kPinLedRunning}, "pinkd-leds")
  , m_inputs(m_chip,
      {{kPinEncoderA, 0}, {kPinEncoderB, 0}, {kPinBtnEncoder, kButtonDebounceUs},
        {kPinBtnRun, kButtonDebounceUs}},
      "pinkd-inputs")
  , m_network([this](const std::string& mode_) { showMessage(mode_, kMessageDuration); })
{
  {
    std::lock_guard<std::mutex> lock(m_mtxState);
    m_engineTempo = m_pink->tempo();
    m_engineLoopLength = m_pink->loopLength();
    m_engineClockMultiplier = m_pink->clockMultiplierLabel();
    m_numPeers = m_pink->numPeers();
  }
  m_engineRunning = m_pink->isPlaying();
  m_lastEncoderValue
    = (m_inputs.read(kPinEncoderA) ? 0b10u : 0u) | (m_inputs.read(kPinEncoderB) ? 0b01u : 0u);

  registerCallbacks();

  m_threadDisplay = std::thread(&UserInterfacePiZero::displayLoop, this);
  m_threadInput = std::thread(&UserInterfacePiZero::inputLoop, this);

  startListening();
}

// -------------------------------------------------------------------------------------------------

UserInterfacePiZero::~UserInterfacePiZero()
{
  stopListening();
  m_running = false;
  if (m_threadInput.joinable())
  {
    m_threadInput.join();
  }
  if (m_threadDisplay.joinable())
  {
    m_threadDisplay.join();
  }
  m_display.write(0x00FF); // all segments off, no digit selected
  m_leds.write(0);
}

// -------------------------------------------------------------------------------------------------

void UserInterfacePiZero::tempoChanged(double t_)
{
  {
    std::lock_guard<std::mutex> lock(m_mtxState);
    m_engineTempo = t_;
  }
  m_updateUI = true;
}

void UserInterfacePiZero::loopLengthChanged(double l_)
{
  {
    std::lock_guard<std::mutex> lock(m_mtxState);
    m_engineLoopLength = l_;
  }
  m_updateUI = true;
}

void UserInterfacePiZero::numPeersChanged(std::size_t n_)
{
  {
    std::lock_guard<std::mutex> lock(m_mtxState);
    m_numPeers = n_;
  }
  m_updateUI = true;
}

void UserInterfacePiZero::runStatusChanged(bool playing_)
{
  m_engineRunning = playing_;
  m_updateUI = true;
}

void UserInterfacePiZero::clockMultiplierChanged(const std::string& cm_)
{
  {
    std::lock_guard<std::mutex> lock(m_mtxState);
    m_engineClockMultiplier = cm_;
  }
  m_updateUI = true;
}

// -------------------------------------------------------------------------------------------------

void UserInterfacePiZero::display(double value_)
{
  double integral;
  double fractional = modf(value_, &integral);
  std::string strValue = std::to_string(static_cast<int>(integral));
  std::string strFill;
  bool showDot = false;
  if (strValue.length() < 4)
  {
    auto uFractional = lround(fractional * 10);
    if (uFractional > 9)
    {
      uFractional = 0;
      strValue = std::to_string(static_cast<int>(integral + 1));
    }
    std::string strFractional = std::to_string(uFractional);
    const auto length = strValue.length() + strFractional.length();
    strFill = std::string(length > 4 ? 0 : 4 - length, ' ');
    strValue.append(strFractional);
    showDot = true;
  }
  else if (strValue.length() > 4)
  {
    strValue = "-HI-";
  }

  std::lock_guard<std::mutex> lock(m_mtxDisplay);
  m_displayString = strFill + strValue;
  m_displayShowDot = showDot;
}

// -------------------------------------------------------------------------------------------------

void UserInterfacePiZero::display(const std::string& value_)
{
  std::string text = value_.substr(0, 4);
  text = std::string(4 - text.length(), ' ') + text;

  std::lock_guard<std::mutex> lock(m_mtxDisplay);
  m_displayString = text;
  m_displayShowDot = false;
}

// -------------------------------------------------------------------------------------------------

void UserInterfacePiZero::showMessage(const std::string& text_, Clock::duration duration_)
{
  {
    std::lock_guard<std::mutex> lock(m_mtxDisplay);
    m_message = text_;
    m_messageUntil = Clock::now() + duration_;
  }
  m_updateUI = true;
}

// -------------------------------------------------------------------------------------------------

void UserInterfacePiZero::registerCallbacks()
{
  onEncoderChanged([this](bool clockwise_, bool /*buttonPressed_*/) {
    switch (m_displayState.load())
    {
      case DisplayState::tempo:
      {
        m_pink->setTempo(m_pink->tempo() + (clockwise_ ? 0.25 : -0.25));
        break;
      }
      case DisplayState::length:
      {
        const auto loopLength = m_pink->loopLength() + (clockwise_ ? 0.25 : -0.25);
        if (loopLength > 0.)
        {
          m_pink->setLoopLength(loopLength);
        }
        break;
      }
      case DisplayState::ppqn:
      {
        const auto current = m_pink->clockMultiplierIndex();
        if (clockwise_ && current + 1 < m_pink->numClockMultipliers())
        {
          m_pink->setClockMultiplierIndex(current + 1);
        }
        else if (!clockwise_ && current >= 1)
        {
          m_pink->setClockMultiplierIndex(current - 1);
        }
        break;
      }
      default:
        break;
    }
  });

  onRunButtonPressed([this]() { m_pink->togglePlay(); });

  onEncoderButtonPressed([this]() {
    switch (m_displayState.load())
    {
      case DisplayState::tempo:
        m_displayState = DisplayState::length;
        break;
      case DisplayState::length:
        m_displayState = DisplayState::ppqn;
        break;
      case DisplayState::ppqn:
        m_displayState = DisplayState::peers;
        break;
      case DisplayState::peers:
      default:
        m_displayState = DisplayState::tempo;
        break;
    }
    m_updateUI = true;
  });
}

// -------------------------------------------------------------------------------------------------

void UserInterfacePiZero::displayLoop()
{
  unsigned currentDigit = 0;

  while (m_running)
  {
    if (m_updateUI.exchange(false))
    {
      updateUI();
    }

    char digitChar = ' ';
    bool showDot = false;
    {
      std::lock_guard<std::mutex> lock(m_mtxDisplay);
      if (!m_message.empty() && Clock::now() >= m_messageUntil)
      {
        m_message.clear();
        m_updateUI = true; // back to the current page
      }
      const auto& text = m_message.empty() ? m_displayString : m_message;
      const std::string padded = std::string(4 - std::min<std::size_t>(4, text.size()), ' ') + text;
      digitChar = padded[currentDigit];
      showDot = m_message.empty() && m_displayShowDot && currentDigit == 2;
    }

    writeDigit(currentDigit, digitChar, showDot);
    currentDigit = (currentDigit + 1) % kDisplayNumDigits;

    std::this_thread::sleep_for(kDisplayRefreshPeriod);
  }
}

// -------------------------------------------------------------------------------------------------

void UserInterfacePiZero::writeDigit(unsigned digit_, char c_, bool dot_)
{
  // Segment bits: 1 = off (segments are lit low). Default: all off.
  std::uint32_t segments = 0xFF;
  const auto charNum = static_cast<unsigned char>(c_);
  if (charNum >= kFontFirstChar && charNum <= kFontLastChar)
  {
    const auto charData = kDisplayFontData[charNum - kFontFirstChar] | (dot_ ? 0b10000000 : 0);
    segments = ~static_cast<std::uint32_t>(charData) & 0xFF;
  }
  const std::uint32_t digitSelect = 1u << (kDisplayNumSegments + digit_);
  m_display.write(segments | digitSelect);
}

// -------------------------------------------------------------------------------------------------

void UserInterfacePiZero::updateUI()
{
  double engineTempo;
  double engineLoopLength;
  std::string engineClockMultiplier;
  std::size_t numPeers;
  {
    std::lock_guard<std::mutex> lock(m_mtxState);
    engineTempo = m_engineTempo;
    engineLoopLength = m_engineLoopLength;
    engineClockMultiplier = m_engineClockMultiplier;
    numPeers = m_numPeers;
  }

  const auto state = m_displayState.load();
  m_leds.set(static_cast<std::size_t>(Led::running), m_engineRunning);
  m_leds.set(static_cast<std::size_t>(Led::tempo), state == DisplayState::tempo);
  m_leds.set(static_cast<std::size_t>(Led::length), state == DisplayState::length);
  m_leds.set(static_cast<std::size_t>(Led::ppqn), state == DisplayState::ppqn);

  switch (state)
  {
    case DisplayState::tempo:
      display(engineTempo);
      break;
    case DisplayState::length:
      display(engineLoopLength);
      break;
    case DisplayState::ppqn:
      display(engineClockMultiplier);
      break;
    case DisplayState::peers:
    default:
    {
      std::ostringstream ss;
      ss << numPeers << "P";
      display(ss.str());
      break;
    }
  }
}

// -------------------------------------------------------------------------------------------------

void UserInterfacePiZero::encoderStep()
{
  const unsigned encoded
    = (m_inputs.read(kPinEncoderA) ? 0b10u : 0u) | (m_inputs.read(kPinEncoderB) ? 0b01u : 0u);
  if (encoded == 0b00)
  {
    if (m_lastEncoderValue == 0b01)
    {
      encoderChanged(true, m_encoderButtonDown);
    }
    else if (m_lastEncoderValue == 0b10)
    {
      encoderChanged(false, m_encoderButtonDown);
    }
  }
  m_lastEncoderValue = encoded;
}

// -------------------------------------------------------------------------------------------------

void UserInterfacePiZero::inputLoop()
{
  while (m_running)
  {
    for (const auto& event : m_inputs.wait(kInputPollTimeout))
    {
      const bool pressed = !event.rising; // buttons pull to ground
      if (event.offset == kPinEncoderA || event.offset == kPinEncoderB)
      {
        encoderStep();
      }
      else if (event.offset == kPinBtnRun)
      {
        if (pressed)
        {
          runButtonPressed(); // on press: starting the clock should not wait for the release
        }
      }
      else if (event.offset == kPinBtnEncoder)
      {
        if (pressed)
        {
          m_encoderButtonDown = true;
          m_encoderButtonLongFired = false;
          m_encoderButtonDownSince = Clock::now();
        }
        else if (m_encoderButtonDown)
        {
          m_encoderButtonDown = false;
          if (!m_encoderButtonLongFired)
          {
            encoderButtonPressed(); // short press: next page (on release, to tell it from a hold)
          }
        }
      }
    }

    if (m_encoderButtonDown && !m_encoderButtonLongFired
        && Clock::now() - m_encoderButtonDownSince >= kLongPressDuration)
    {
      m_encoderButtonLongFired = true;
      if (m_network.toggle())
      {
        showMessage("----", kSwitchingTimeout); // replaced by the result ("AP" / "NET")
      }
    }
  }
}

// -------------------------------------------------------------------------------------------------

} // namespace pi
} // namespace sl
