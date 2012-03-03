// Copyright 2012 Google Inc.
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

#if !defined(UTILS_TEXT_IPP)
#define UTILS_TEXT_IPP

#include "utils/text.hpp"

#include <sstream>
#include <stdexcept>


/// Converts a string to a native type.
///
/// \tparam Type The type to convert the string to.  An input stream operator
///     must exist to extract such a type from an std::istream.
/// \param str The string to convert.
///
/// \return The converted string, if the input string was valid.
///
/// \throw std::runtime_error If the input string does not represent a valid
///     target type.  This exception does not include any details, so the caller
///     must take care to re-raise it with appropriate details.
template< typename Type >
Type
utils::text::to_type(const std::string& str)
{
    if (str.empty())
        throw std::runtime_error("Empty string");
    if (str[0] == ' ')
        throw std::runtime_error("Invalid value");

    std::istringstream input(str);
    Type value;
    input >> value;
    if (!input.eof() || input.bad() || input.fail())
        throw std::runtime_error("Invalid value");
    return value;
}


#endif  // !defined(UTILS_TEXT_IPP)
