/*
        ##########    Copyright (C) 2016 Vincenzo Pacella
        ##      ##    Distributed under MIT license, see file LICENSE
        ##      ##    or <http://opensource.org/licenses/MIT>
        ##      ##
##########      ############################################################# shaduzlabs.com #####*/

#include "StatusFile.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include <pink-config.h>

// -------------------------------------------------------------------------------------------------

namespace
{

const char* kDefaultPath = "/run/spink/status.json";

std::string jsonString(const std::string& value_)
{
  std::string out = "\"";
  for (const char c : value_)
  {
    switch (c)
    {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      default:
        if (static_cast<unsigned char>(c) >= 0x20)
        {
          out += c;
        }
    }
  }
  return out + "\"";
}

} // namespace

// -------------------------------------------------------------------------------------------------

namespace sl
{
namespace pi
{

// -------------------------------------------------------------------------------------------------

StatusFile::StatusFile(std::string audioDescription_) : m_audioDescription(std::move(audioDescription_))
{
  const char* path = std::getenv("PINK_STATUS_FILE");
  m_path = path != nullptr ? path : kDefaultPath;
}

// -------------------------------------------------------------------------------------------------

bool StatusFile::write(const Pink& pink_)
{
  const auto diagnostics = pink_.diagnostics();

  std::ostringstream json;
  json.precision(6);
  json << "{"
       << "\"version\":" << jsonString(PINK_VERSION_STRING) << ","
       << "\"revision\":" << jsonString(PINK_GIT_REVISION) << ","
       << "\"linkEnabled\":" << (pink_.isEnabled() ? "true" : "false") << ","
       << "\"playing\":" << (pink_.isPlaying() ? "true" : "false") << ","
       << "\"tempo\":" << pink_.tempo() << ","
       << "\"loopLength\":" << pink_.loopLength() << ","
       << "\"clockMultiplier\":" << jsonString(pink_.clockMultiplierLabel()) << ","
       << "\"peers\":" << pink_.numPeers() << ","
       << "\"audio\":" << jsonString(m_audioDescription) << ","
       << "\"underruns\":" << diagnostics.underruns << ","
       << "\"timelineJumps\":" << diagnostics.beatJumps << "}\n";

  const std::string tmpPath = m_path + ".tmp";
  {
    std::ofstream file(tmpPath, std::ios::trunc);
    if (!file)
    {
      return false;
    }
    file << json.str();
    if (!file)
    {
      return false;
    }
  }
  return std::rename(tmpPath.c_str(), m_path.c_str()) == 0;
}

// -------------------------------------------------------------------------------------------------

} // namespace pi
} // namespace sl
