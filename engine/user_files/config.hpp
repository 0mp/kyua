// Copyright 2010, 2011 Google Inc.
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

/// \file engine/user_files/config.hpp
/// Test suite configuration parsing and representation.

#if !defined(ENGINE_USER_FILES_CONFIG_HPP)
#define ENGINE_USER_FILES_CONFIG_HPP

#include "utils/fs/path.hpp"
#include "utils/optional.ipp"
#include "utils/passwd.hpp"

namespace utils {
namespace lua {
class state;
}  // namespace lua
}  // namespace utils

namespace engine {
namespace user_files {


namespace detail {


std::string get_string_var(utils::lua::state&, const std::string&,
                           const std::string&);
utils::optional< utils::passwd::user > get_user_var(utils::lua::state&,
                                                    const std::string&);


}  // namespace detail


/// Representation of Kyua configuration files.
///
/// This class provides the parser for configuration files and methods to
/// access the parsed data.
struct config {
    /// Name of the system architecture (aka processor type).
    std::string architecture;

    /// Name of the system platform (aka machine name).
    std::string platform;

    /// The unprivileged user to run test cases as, if any.
    utils::optional< utils::passwd::user > unprivileged_user;

    explicit config(const std::string&, const std::string&,
                    const utils::optional< utils::passwd::user >&);
    static config defaults(void);
    static config load(const utils::fs::path&);
};


}  // namespace user_files
}  // namespace engine

#endif  // !defined(ENGINE_USER_FILES_CONFIG_HPP)