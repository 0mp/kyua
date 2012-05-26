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

#include "utils/text/operations.ipp"

#include "utils/sanity.hpp"

namespace text = utils::text;


/// Fills a paragraph to the specified length.
///
/// This preserves any sequence of spaces in the input and any possible
/// newlines.  Sequences of spaces may be split in half (and thus one space is
/// lost), but the rest of the spaces will be preserved as either trailing or
/// leading spaces.
///
/// \param input The string to refill.
/// \param target_width The width to refill the paragraph to.
///
/// \return The refilled paragraph as a sequence of independent lines.
std::vector< std::string >
text::refill(const std::string& input, const std::size_t target_width)
{
    std::vector< std::string > output;

    std::string::size_type start = 0;
    while (start < input.length()) {
        std::string::size_type width;
        if (start + target_width >= input.length())
            width = input.length() - start;
        else {
            if (input[start + target_width] == ' ') {
                width = target_width;
            } else {
                const std::string::size_type pos = input.find_last_of(
                    " ", start + target_width - 1);
                if (pos == std::string::npos || pos < start + 1) {
                    width = input.find_first_of(" ", start + target_width);
                    if (width == std::string::npos)
                        width = input.length() - start;
                    else
                        width -= start;
                } else {
                    width = pos - start;
                }
            }
        }
        INV(width != std::string::npos);
        INV(start + width <= input.length());
        INV(input[start + width] == ' ' || input[start + width] == '\0');
        output.push_back(input.substr(start, width));

        start += width + 1;
    }

    if (input.empty()) {
        INV(output.empty());
        output.push_back("");
    }

    return output;
}


/// Fills a paragraph to the specified length.
///
/// See the documentation for refill() for additional details.
///
/// \param input The string to refill.
/// \param target_width The width to refill the paragraph to.
///
/// \return The refilled paragraph as a string with embedded newlines.
std::string
text::refill_as_string(const std::string& input, const std::size_t target_width)
{
    return join(refill(input, target_width), "\n");
}


/// Splits a string into different components.
///
/// \param str The string to split.
/// \param delimiter The separator to use to split the words.
///
/// \return The different words in the input string as split by the provided
/// delimiter.
std::vector< std::string >
text::split(const std::string& str, const char delimiter)
{
    std::vector< std::string > words;
    if (!str.empty()) {
        std::string::size_type pos = str.find(delimiter);
        words.push_back(str.substr(0, pos));
        while (pos != std::string::npos) {
            ++pos;
            const std::string::size_type next = str.find(delimiter, pos);
            words.push_back(str.substr(pos, next - pos));
            pos = next;
        }
    }
    return words;
}


/// Specialization of to_type() for strings.
///
/// Converting a string to a string is a no-op, so just do nothing and return
/// the input value.
///
/// \param str The input string.
///
/// \return The same as str.
template<>
std::string
text::to_type(const std::string& str)
{
    return str;
}
