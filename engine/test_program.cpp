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

#include "engine/test_program.hpp"

#include <stdexcept>

#include "engine/atf_iface/test_program.hpp"
#include "engine/exceptions.hpp"
#include "engine/plain_iface/test_program.hpp"
#include "utils/format/macros.hpp"
#include "utils/optional.ipp"
#include "utils/sanity.hpp"

namespace fs = utils::fs;

using utils::optional;


/// Internal implementation of a base_test_program.
struct engine::base_test_program::base_impl {
    /// Name of the test program interface.
    std::string interface_name;

    /// Name of the test program binary relative to root.
    fs::path binary;

    /// Root of the test suite containing the test program.
    fs::path root;

    /// Name of the test suite this program belongs to.
    std::string test_suite_name;

    /// Metadata of the test program.
    metadata md;

    /// List of test casees in the test program; lazily initialized.
    optional< test_cases_vector > test_cases;

    /// Constructor.
    ///
    /// \param interface_name_ Name of the test program interface.
    /// \param binary_ The name of the test program binary relative to root_.
    /// \param root_ The root of the test suite containing the test program.
    /// \param test_suite_name_ The name of the test suite this program
    ///     belongs to.
    /// \param md_ Metadata of the test program.
    base_impl(const std::string& interface_name_, const fs::path& binary_,
              const fs::path& root_, const std::string& test_suite_name_,
              const metadata& md_) :
        interface_name(interface_name_),
        binary(binary_),
        root(root_),
        test_suite_name(test_suite_name_),
        md(md_)
    {
        PRE_MSG(!binary.is_absolute(),
                F("The program '%s' must be relative to the root of the test "
                  "suite '%s'") % binary % root);
    }
};


/// Constructs a new test program.
///
/// \param interface_name_ Name of the test program interface.
/// \param binary_ The name of the test program binary relative to root_.
/// \param root_ The root of the test suite containing the test program.
/// \param test_suite_name_ The name of the test suite this program belongs to.
/// \param md_ Metadata of the test program.
engine::base_test_program::base_test_program(
    const std::string& interface_name_,
    const fs::path& binary_,
    const fs::path& root_,
    const std::string& test_suite_name_,
    const metadata& md_) :
    _pbimpl(new base_impl(interface_name_, binary_, root_,
                          test_suite_name_, md_))
{
}


/// Destroys a test program.
engine::base_test_program::~base_test_program(void)
{
}


/// Gets the name of the test program interface.
///
/// \return An interface name.
const std::string&
engine::base_test_program::interface_name(void) const
{
    return _pbimpl->interface_name;
}


/// Gets the path to the test program relative to the root of the test suite.
///
/// \return The relative path to the test program binary.
const fs::path&
engine::base_test_program::relative_path(void) const
{
    return _pbimpl->binary;
}


/// Gets the absolute path to the test program.
///
/// \return The absolute path to the test program binary.
const fs::path
engine::base_test_program::absolute_path(void) const
{
    const fs::path full_path = _pbimpl->root / _pbimpl->binary;
    return full_path.is_absolute() ? full_path : full_path.to_absolute();
}


/// Gets the root of the test suite containing this test program.
///
/// \return The path to the root of the test suite.
const fs::path&
engine::base_test_program::root(void) const
{
    return _pbimpl->root;
}


/// Gets the name of the test suite containing this test program.
///
/// \return The name of the test suite.
const std::string&
engine::base_test_program::test_suite_name(void) const
{
    return _pbimpl->test_suite_name;
}


/// Gets the metadata of the test program.
///
/// \return The metadata.
const engine::metadata&
engine::base_test_program::get_metadata(void) const
{
    return _pbimpl->md;
}


/// Gets a test case by its name.
///
/// \param name The name of the test case to locate.
///
/// \return The requested test case.
///
/// \throw not_found_error If the specified test case is not in the test
///     program.
const engine::test_case_ptr&
engine::base_test_program::find(const std::string& name) const
{
    // TODO(jmmv): Should use a test_cases_map instead of a vector to optimize
    // lookups.
    const test_cases_vector& tcs = test_cases();
    for (test_cases_vector::const_iterator iter = tcs.begin();
         iter != tcs.end(); iter++) {
        if ((*iter)->name() == name)
            return *iter;
    }
    throw not_found_error(F("Unknown test case %s in test program %s") % name %
                          relative_path());
}


/// Gets the list of test cases from the test program.
///
/// Note that this operation may be expensive because it may lazily load the
/// test cases list from the test program.  Errors during the processing of the
/// test case list are represented as a single test case describing the failure.
///
/// \return The list of test cases provided by the test program.
const engine::test_cases_vector&
engine::base_test_program::test_cases(void) const
{
    if (!_pbimpl->test_cases) {
        try {
            // TODO(jmmv): Yes, hardcoding the interface names here is nasty.
            // But this will go away once we implement the testers as individual
            // binaries, as we just auto-discover the ones that exist and use
            // their generic interface.
            if (_pbimpl->interface_name == "atf") {
                _pbimpl->test_cases = atf_iface::load_atf_test_cases(this);
            } else if (_pbimpl->interface_name == "plain") {
                _pbimpl->test_cases = plain_iface::load_plain_test_cases(this);
            } else
                UNREACHABLE_MSG("Unknown interface " + _pbimpl->interface_name);
        } catch (const std::runtime_error& e) {
            UNREACHABLE_MSG(F("Should not have thrown, but got: %s") %
                            e.what());
        }
    }
    return _pbimpl->test_cases.get();
}


/// Sets the collection of test cases included in this test program.
///
/// This function is provided so that when we load test programs from the
/// database we can populate them with the test cases they include.  We don't
/// want such test programs to be executed to gather this information.
///
/// We cannot provide this collection of tests in the constructor of the test
/// program because the test cases have to point to their test programs.
///
/// \pre The test program must not have attempted to load its test cases yet.
///     I.e. test_cases() has not been called.
///
/// \param test_cases_ The test cases to add to this test program.
void
engine::base_test_program::set_test_cases(const test_cases_vector& test_cases_)
{
    PRE(!_pbimpl->test_cases);
    _pbimpl->test_cases = test_cases_;
}
