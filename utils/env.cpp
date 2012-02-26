// Copyright 2010 Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// * Redistributions of source code must retain the above copyright
//   notice, this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright
//   notice, this list of conditions and the following disclaimer in the
//   documentation and/or other materials provided with the distribution.
// * Neither the name of Google Inc. nor the names of its contributors
//   may be used to endorse or promote products derived from this software
//   without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#if defined(HAVE_CONFIG_H)
#  include "config.h"
#endif

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

#include "utils/env.hpp"
#include "utils/format/macros.hpp"
#include "utils/optional.ipp"

using utils::none;
using utils::optional;


/// Gets the value of an environment variable.
///
/// \param name The name of the environment variable to query.
///
/// \return The value of the environment variable if it is defined, or none
/// otherwise.
optional< std::string >
utils::getenv(const std::string& name)
{
    const char* value = std::getenv(name.c_str());
    if (value == NULL)
        return none;
    else
        return utils::make_optional(std::string(value));
}


/// Sets the value of an environment variable.
///
/// \param name The name of the environment variable to set.
///
/// \throw std::runtime_error If there is an error setting the environment
///     variable.
void
utils::setenv(const std::string& name, const std::string& val)
{
#if defined(HAVE_SETENV)
    if (::setenv(name.c_str(), val.c_str(), 1) == -1) {
        const int original_errno = errno;
        throw std::runtime_error(F("Failed to set environment variable '%s' to "
                                   "'%s': %s") %
                                 name % val % std::strerror(original_errno));
    }
#elif defined(HAVE_PUTENV)
    if (::putenv((F("%s=%s") % name % val).c_str()) == -1) {
        const int original_errno = errno;
        throw std::runtime_error(F("Failed to set environment variable '%s' to "
                                   "'%s': %s") %
                                 name % val % std::strerror(original_errno));
    }
#else
#   error "Don't know how to set an environment variable."
#endif
}


/// Unsets an environment variable.
///
/// \param name The name of the environment variable to unset.
///
/// \throw std::runtime_error If there is an error unsetting the environment
///     variable.
void
utils::unsetenv(const std::string& name)
{
#if defined(HAVE_UNSETENV)
    if (::unsetenv(name.c_str()) == -1) {
        const int original_errno = errno;
        throw std::runtime_error(F("Failed to unset environment variable "
                                   "'%s'") %
                                 name % std::strerror(original_errno));
    }
#elif defined(HAVE_PUTENV)
    if (::putenv((F("%s=") % name).c_str()) == -1) {
        const int original_errno = errno;
        throw std::runtime_error(F("Failed to unset environment variable "
                                   "'%s'") %
                                 name % std::strerror(original_errno));
    }
#else
#   error "Don't know how to unset an environment variable."
#endif
}
