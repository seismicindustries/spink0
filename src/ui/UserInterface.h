/*
        ##########    Copyright (C) 2016 Vincenzo Pacella
        ##      ##    Distributed under MIT license, see file LICENSE
        ##      ##    or <http://opensource.org/licenses/MIT>
        ##      ##
##########      ############################################################# shaduzlabs.com #####*/

#pragma once

#include <atomic>
#include <functional>

#include "../Pink.h"

// -------------------------------------------------------------------------------------------------

namespace sl
{
namespace pi
{

// -------------------------------------------------------------------------------------------------

class UserInterface : public Pink::Listener
{
public:
  using tCallbackEncoderChanged = std::function<void(bool, bool)>;
  using tCallbackButtonPressed = std::function<void()>;

  explicit UserInterface(std::shared_ptr<Pink> pink_) : m_pink(std::move(pink_))
  {
  }

  ~UserInterface() override
  {
    stopListening();
  }

protected:
  //! Derived classes call this at the end of their constructor: Pink callbacks arrive on other
  //! threads and must not reach a half-constructed object.
  void startListening()
  {
    m_pink->addListener(pinkListenerId(), this);
    m_listening = true;
  }

  //! Derived classes call this first in their destructor, before their members go away.
  void stopListening()
  {
    if (m_listening)
    {
      m_pink->removeListener(pinkListenerId());
      m_listening = false;
    }
  }

  void onEncoderChanged(tCallbackEncoderChanged fnEncoderChanged_)
  {
    m_cbEncoderChanged = std::move(fnEncoderChanged_);
  }

  void onRunButtonPressed(tCallbackButtonPressed fnRunButtonPressed_)
  {
    m_cbRunButtonPressed = std::move(fnRunButtonPressed_);
  }

  void onEncoderButtonPressed(tCallbackButtonPressed fnEncoderButtonPressed_)
  {
    m_cbEncoderButtonPressed = std::move(fnEncoderButtonPressed_);
  }

  void encoderChanged(bool direction_, bool buttonPressed_)
  {
    if (m_cbEncoderChanged)
    {
      m_cbEncoderChanged(direction_, buttonPressed_);
    }
  }

  void runButtonPressed()
  {
    if (m_cbRunButtonPressed)
    {
      m_cbRunButtonPressed();
    }
  }

  void encoderButtonPressed()
  {
    if (m_cbEncoderButtonPressed)
    {
      m_cbEncoderButtonPressed();
    }
  }

  std::shared_ptr<Pink> m_pink;


private:
  bool m_listening{false};
  tCallbackEncoderChanged m_cbEncoderChanged;
  tCallbackButtonPressed m_cbRunButtonPressed;
  tCallbackButtonPressed m_cbEncoderButtonPressed;
};

// -------------------------------------------------------------------------------------------------

} // namespace pi
} // namespace sl
