/*
        ##########    Copyright (C) 2016 Vincenzo Pacella
        ##      ##    Distributed under MIT license, see file LICENSE
        ##      ##    or <http://opensource.org/licenses/MIT>
        ##      ##
##########      ############################################################# shaduzlabs.com #####*/

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// -------------------------------------------------------------------------------------------------

struct gpiod_chip;
struct gpiod_line_request;
struct gpiod_edge_event_buffer;

namespace sl
{
namespace pi
{

// -------------------------------------------------------------------------------------------------

//! The SoC's GPIO controller (pinctrl-bcm2835 on the Pi Zero), via the libgpiod v2 character
//! device API. Line offsets are BCM GPIO numbers. Without libgpiod (desktop builds) all classes
//! here are silent stand-ins.
class GpioChip
{
public:
  GpioChip();
  ~GpioChip();
  GpioChip(const GpioChip&) = delete;
  GpioChip& operator=(const GpioChip&) = delete;

  gpiod_chip* handle() const
  {
    return m_chip;
  }

private:
  gpiod_chip* m_chip{nullptr};
};

// -------------------------------------------------------------------------------------------------

//! A group of output lines, requested together and written with one call.
class GpioOutputs
{
public:
  GpioOutputs(GpioChip& chip_, std::vector<unsigned> lines_, const std::string& consumer_);
  ~GpioOutputs();
  GpioOutputs(const GpioOutputs&) = delete;
  GpioOutputs& operator=(const GpioOutputs&) = delete;

  //! Bit i of mask_ is the level of lines_[i].
  void write(std::uint32_t mask_);

  //! Set a single line (index into lines_).
  void set(std::size_t index_, bool high_);

private:
  std::vector<unsigned> m_lines;
  [[maybe_unused]] gpiod_line_request* m_request{nullptr};
};

// -------------------------------------------------------------------------------------------------

//! A group of input lines with pull-ups and edge detection on both edges.
class GpioInputs
{
public:
  struct Line
  {
    unsigned offset;
    unsigned debounceUs; //!< 0 = no debouncing (encoder), else kernel debounce period
  };

  struct Event
  {
    unsigned offset;
    bool rising;
  };

  GpioInputs(GpioChip& chip_, std::vector<Line> lines_, const std::string& consumer_);
  ~GpioInputs();
  GpioInputs(const GpioInputs&) = delete;
  GpioInputs& operator=(const GpioInputs&) = delete;

  //! Current level of a line (by offset). False on error.
  bool read(unsigned offset_);

  //! Waits up to timeout_ for edge events and returns them (empty on timeout).
  std::vector<Event> wait(std::chrono::milliseconds timeout_);

private:
  [[maybe_unused]] gpiod_line_request* m_request{nullptr};
  [[maybe_unused]] gpiod_edge_event_buffer* m_events{nullptr};
};

// -------------------------------------------------------------------------------------------------

} // namespace pi
} // namespace sl
