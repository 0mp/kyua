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

/// \file engine/test_case.hpp
/// Interface to interact with test cases.

#if !defined(ENGINE_TEST_CASE_HPP)
#define ENGINE_TEST_CASE_HPP

#include <map>
#include <string>
#include <tr1/memory>

#include "engine/results.hpp"
#include "utils/fs/path.hpp"

namespace engine {


/// Collection of test case properties.
///
/// A property is just a (name, value) pair, and we represent them as a map
/// because callers always want to locate properties by name.
typedef std::map< std::string, std::string > properties_map;


/// Representation of a test case identifier.
///
/// A test case identifier is a unique value that identifies the test case
/// inside a particular test suite.  Given that the program is only supposed to
/// deal with one test suite at a time, we can assume that the test case
/// identifier is unique within the program.
struct test_case_id {
    /// Name of the test program containing the test case.
    utils::fs::path program;

    /// Name of the test case within the test program.
    std::string name;

    test_case_id(const utils::fs::path&, const std::string&);

    std::string str(void) const;

    bool operator<(const test_case_id&) const;
    bool operator==(const test_case_id&) const;
};


class test_program;
namespace user_files {
struct config;
}  // namespace user_files


/// Representation of a test case.
class test_case {
    const engine::test_program& _test_program;
    std::string _name;

    virtual properties_map get_all_properties(void) const = 0;
    virtual results::result_ptr do_run(const user_files::config&) const = 0;

public:
    test_case(const engine::test_program&, const std::string&);
    virtual ~test_case(void);

    const engine::test_program& test_program(void) const;
    const std::string& name(void) const;
    test_case_id identifier(void) const;

    properties_map all_properties(void) const;
    results::result_ptr run(const user_files::config&) const;
};


/// Pointer to a test case.
typedef std::tr1::shared_ptr< test_case > test_case_ptr;


}  // namespace engine


#endif  // !defined(ENGINE_TEST_CASE_HPP)
