// Copyright 2011 Google Inc.
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

/// \file cli/common.hpp
/// Utility functions to implement CLI subcommands.

#if !defined(CLI_COMMON_HPP)
#define CLI_COMMON_HPP

#include <utility>

#include "utils/cmdline/options.hpp"
#include "utils/cmdline/parser.hpp"
#include "utils/cmdline/ui.hpp"
#include "utils/noncopyable.hpp"

namespace utils {
namespace fs {
class path;
}  // namespace fs
}  // namespace utils

namespace engine {
struct test_case_id;
namespace user_files {
struct config;
class kyuafile;
}  // namespace user_files
}  // namespace engine

namespace cli {


extern const utils::cmdline::path_option config_option;
extern const utils::cmdline::path_option kyuafile_option;


engine::user_files::config load_config(
    const utils::cmdline::parsed_cmdline&);
engine::user_files::kyuafile load_kyuafile(
    const utils::cmdline::parsed_cmdline&);
void set_confdir_for_testing(const utils::fs::path&);


/// Represents user-specified test filters and their current match state.
class filters_state : utils::noncopyable {
    struct impl;
    std::auto_ptr< impl > _pimpl;

public:
    filters_state(const utils::cmdline::args_vector&);

    bool match_test_program(const utils::fs::path&) const;
    bool match_test_case(const engine::test_case_id&) const;

    bool report_unused_filters(utils::cmdline::ui*) const;
};


}  // namespace cli

#endif  // !defined(CLI_COMMON_HPP)
