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

/// \file engine/atf_iface/test_program.hpp
/// Implementation of test programs that follow the ATF interface.

#if !defined(ENGINE_ATF_IFACE_TEST_PROGRAM_HPP)
#define ENGINE_ATF_IFACE_TEST_PROGRAM_HPP

#include "engine/test_program.hpp"

namespace engine {
namespace atf_iface {


namespace detail {


test_cases_vector parse_test_cases(const base_test_program&, std::istream&);


}  // namespace detail


/// Representation of an ATF test program.
class test_program : public base_test_program {
    test_cases_vector load_test_cases(void) const;

public:
    test_program(const utils::fs::path&, const utils::fs::path&,
                 const std::string&);
};


}  // namespace atf_iface
}  // namespace engine

#endif  // !defined(ENGINE_ATF_IFACE_TEST_PROGRAM_HPP)
