/*
        ##########    Copyright (C) 2016 Vincenzo Pacella
        ##      ##    Distributed under MIT license, see file LICENSE
        ##      ##    or <http://opensource.org/licenses/MIT>
        ##      ##
##########      ############################################################# shaduzlabs.com #####*/

#include "ui/Gpio.h"

#include <stdexcept>
#include <thread>

#ifdef PINK_USE_GPIOD

#include <cerrno>
#include <cstring>
#include <gpiod.h>

// -------------------------------------------------------------------------------------------------

namespace
{

const std::size_t kMaxEvents{16};

std::runtime_error gpioError(const std::string& what_)
{
  return std::runtime_error("GPIO: " + what_ + " (" + std::strerror(errno) + ")");
}

//! RAII helpers for the short-lived libgpiod config objects.
struct LineSettings
{
  LineSettings() : p(gpiod_line_settings_new())
  {
    if (p == nullptr)
    {
      throw gpioError("gpiod_line_settings_new");
    }
  }
  ~LineSettings()
  {
    gpiod_line_settings_free(p);
  }
  gpiod_line_settings* p;
};

struct LineConfig
{
  LineConfig() : p(gpiod_line_config_new())
  {
    if (p == nullptr)
    {
      throw gpioError("gpiod_line_config_new");
    }
  }
  ~LineConfig()
  {
    gpiod_line_config_free(p);
  }
  gpiod_line_config* p;
};

gpiod_line_request* requestLines(gpiod_chip* chip_, LineConfig& config_, const std::string& consumer_)
{
  gpiod_request_config* requestConfig = gpiod_request_config_new();
  if (requestConfig == nullptr)
  {
    throw gpioError("gpiod_request_config_new");
  }
  gpiod_request_config_set_consumer(requestConfig, consumer_.c_str());
  gpiod_line_request* request = gpiod_chip_request_lines(chip_, requestConfig, config_.p);
  gpiod_request_config_free(requestConfig);
  if (request == nullptr)
  {
    throw gpioError("cannot request lines for " + consumer_);
  }
  return request;
}

//! The Pi's own GPIO controller: pinctrl-bcm2835 (Zero/1/2/3), pinctrl-bcm2711 (4).
bool isSocGpio(gpiod_chip* chip_)
{
  gpiod_chip_info* info = gpiod_chip_get_info(chip_);
  if (info == nullptr)
  {
    return false;
  }
  const std::string label = gpiod_chip_info_get_label(info);
  gpiod_chip_info_free(info);
  return label.rfind("pinctrl-bcm", 0) == 0;
}

} // namespace

// -------------------------------------------------------------------------------------------------

namespace sl
{
namespace pi
{

// -------------------------------------------------------------------------------------------------

GpioChip::GpioChip()
{
  for (int i = 0; i < 8 && m_chip == nullptr; ++i)
  {
    const std::string path = "/dev/gpiochip" + std::to_string(i);
    gpiod_chip* chip = gpiod_chip_open(path.c_str());
    if (chip == nullptr)
    {
      continue;
    }
    if (isSocGpio(chip))
    {
      m_chip = chip;
    }
    else
    {
      gpiod_chip_close(chip);
    }
  }
  if (m_chip == nullptr)
  {
    throw gpioError("no pinctrl-bcm* GPIO chip found");
  }
}

GpioChip::~GpioChip()
{
  gpiod_chip_close(m_chip);
}

// -------------------------------------------------------------------------------------------------

GpioOutputs::GpioOutputs(GpioChip& chip_, std::vector<unsigned> lines_, const std::string& consumer_)
  : m_lines(std::move(lines_))
{
  LineSettings settings;
  gpiod_line_settings_set_direction(settings.p, GPIOD_LINE_DIRECTION_OUTPUT);
  gpiod_line_settings_set_output_value(settings.p, GPIOD_LINE_VALUE_INACTIVE);
  LineConfig config;
  if (gpiod_line_config_add_line_settings(config.p, m_lines.data(), m_lines.size(), settings.p) != 0)
  {
    throw gpioError("gpiod_line_config_add_line_settings");
  }
  m_request = requestLines(chip_.handle(), config, consumer_);
}

GpioOutputs::~GpioOutputs()
{
  gpiod_line_request_release(m_request);
}

void GpioOutputs::write(std::uint32_t mask_)
{
  gpiod_line_value values[32];
  for (std::size_t i = 0; i < m_lines.size() && i < 32; ++i)
  {
    values[i] = (mask_ & (1u << i)) ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE;
  }
  gpiod_line_request_set_values(m_request, values);
}

void GpioOutputs::set(std::size_t index_, bool high_)
{
  if (index_ < m_lines.size())
  {
    gpiod_line_request_set_value(
      m_request, m_lines[index_], high_ ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE);
  }
}

// -------------------------------------------------------------------------------------------------

GpioInputs::GpioInputs(GpioChip& chip_, std::vector<Line> lines_, const std::string& consumer_)
{
  LineConfig config;
  for (const auto& line : lines_)
  {
    LineSettings settings;
    gpiod_line_settings_set_direction(settings.p, GPIOD_LINE_DIRECTION_INPUT);
    gpiod_line_settings_set_edge_detection(settings.p, GPIOD_LINE_EDGE_BOTH);
    gpiod_line_settings_set_bias(settings.p, GPIOD_LINE_BIAS_PULL_UP);
    gpiod_line_settings_set_debounce_period_us(settings.p, line.debounceUs);
    if (gpiod_line_config_add_line_settings(config.p, &line.offset, 1, settings.p) != 0)
    {
      throw gpioError("gpiod_line_config_add_line_settings");
    }
  }
  m_request = requestLines(chip_.handle(), config, consumer_);
  m_events = gpiod_edge_event_buffer_new(kMaxEvents);
  if (m_events == nullptr)
  {
    gpiod_line_request_release(m_request);
    throw gpioError("gpiod_edge_event_buffer_new");
  }
}

GpioInputs::~GpioInputs()
{
  gpiod_edge_event_buffer_free(m_events);
  gpiod_line_request_release(m_request);
}

bool GpioInputs::read(unsigned offset_)
{
  return gpiod_line_request_get_value(m_request, offset_) == GPIOD_LINE_VALUE_ACTIVE;
}

std::vector<GpioInputs::Event> GpioInputs::wait(std::chrono::milliseconds timeout_)
{
  std::vector<Event> result;
  const auto timeoutNs = std::chrono::duration_cast<std::chrono::nanoseconds>(timeout_).count();
  if (gpiod_line_request_wait_edge_events(m_request, timeoutNs) <= 0)
  {
    return result;
  }
  const int n = gpiod_line_request_read_edge_events(m_request, m_events, kMaxEvents);
  for (int i = 0; i < n; ++i)
  {
    gpiod_edge_event* event = gpiod_edge_event_buffer_get_event(m_events, static_cast<unsigned long>(i));
    result.push_back({gpiod_edge_event_get_line_offset(event),
      gpiod_edge_event_get_event_type(event) == GPIOD_EDGE_EVENT_RISING_EDGE});
  }
  return result;
}

// -------------------------------------------------------------------------------------------------

} // namespace pi
} // namespace sl

#else // !PINK_USE_GPIOD: desktop stand-ins ------------------------------------------------------

namespace sl
{
namespace pi
{

GpioChip::GpioChip() = default;
GpioChip::~GpioChip() = default;

GpioOutputs::GpioOutputs(GpioChip&, std::vector<unsigned> lines_, const std::string&)
  : m_lines(std::move(lines_))
{
}
GpioOutputs::~GpioOutputs() = default;
void GpioOutputs::write(std::uint32_t)
{
}
void GpioOutputs::set(std::size_t, bool)
{
}

GpioInputs::GpioInputs(GpioChip&, std::vector<Line>, const std::string&)
{
}
GpioInputs::~GpioInputs() = default;
bool GpioInputs::read(unsigned)
{
  return true; // pull-up: idle level
}
std::vector<GpioInputs::Event> GpioInputs::wait(std::chrono::milliseconds timeout_)
{
  std::this_thread::sleep_for(timeout_);
  return {};
}

} // namespace pi
} // namespace sl

#endif
