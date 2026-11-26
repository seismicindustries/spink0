/*
        ##########    Copyright (C) 2016 Vincenzo Pacella
        ##      ##    Distributed under MIT license, see file LICENSE
        ##      ##    or <http://opensource.org/licenses/MIT>
        ##      ##
##########      ############################################################# shaduzlabs.com #####*/

#pragma once

#include <string>

#include "Pink.h"

// -------------------------------------------------------------------------------------------------

namespace sl
{
namespace pi
{

// -------------------------------------------------------------------------------------------------

//! Publishes the daemon state as JSON for the setup page (spink-netd), written atomically
//! (temp file + rename). Default path /run/spink/status.json, $PINK_STATUS_FILE overrides it.
class StatusFile
{
public:
  StatusFile(std::string audioDescription_);

  //! Returns false if the file could not be written (e.g. the directory does not exist).
  bool write(const Pink& pink_);

  const std::string& path() const
  {
    return m_path;
  }

private:
  std::string m_path;
  std::string m_audioDescription;
};

// -------------------------------------------------------------------------------------------------

} // namespace pi
} // namespace sl
