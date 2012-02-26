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

#include <cstdlib>
#include <utility>
#include <vector>

#include "cli/cmd_list.hpp"
#include "cli/common.hpp"
#include "engine/test_case.hpp"
#include "engine/test_program.hpp"
#include "engine/user_files/kyuafile.hpp"
#include "utils/cmdline/base_command.ipp"
#include "utils/cmdline/options.hpp"
#include "utils/cmdline/parser.ipp"
#include "utils/cmdline/ui.hpp"
#include "utils/format/macros.hpp"
#include "utils/fs/path.hpp"
#include "utils/sanity.hpp"

namespace cmdline = utils::cmdline;
namespace fs = utils::fs;
namespace user_files = engine::user_files;


/// Lists a single test case.
///
/// \param ui [out] Object to interact with the I/O of the program.
/// \param verbose Whether to be verbose or not.
/// \param test_case The test case to print.
/// \param test_suite_name The name of the test suite containing the test case.
void
cli::detail::list_test_case(cmdline::ui* ui, const bool verbose,
                            const engine::test_case& test_case,
                            const std::string& test_suite_name)
{
    if (!verbose) {
        ui->out(test_case.identifier.str());
    } else {
        ui->out(F("%s (%s)") % test_case.identifier.str() % test_suite_name);

        const engine::properties_map props = test_case.all_properties();
        for (engine::properties_map::const_iterator iter = props.begin();
             iter != props.end(); iter++)
            ui->out(F("    %s = %s") % (*iter).first % (*iter).second);
    }
}


/// Lists a single test program.
///
/// \param ui [out] Object to interact with the I/O of the program.
/// \param verbose Whether to be verbose or not.
/// \param root The top directory of the test suite.
/// \param test_program The test program to print.
/// \param filters [in,out] The filters used to select which test cases to
///     print.  These filters are updated on output to mark which of them
///     actually matched a test case.
///
/// \throw engine::error If there is any problem gathering the test case list
///     from the test program.
void
cli::detail::list_test_program(cmdline::ui* ui, const bool verbose,
                               const fs::path& root,
                               const user_files::test_program& test_program,
                               cli::filters_state& filters)
{
    const engine::test_cases_vector test_cases = engine::load_test_cases(
        root, test_program.binary_path);

    for (engine::test_cases_vector::const_iterator iter = test_cases.begin();
         iter != test_cases.end(); iter++) {
        if (filters.match_test_case((*iter).identifier))
            list_test_case(ui, verbose, *iter, test_program.test_suite_name);
    }
}


/// Default constructor for cmd_list.
cli::cmd_list::cmd_list(void) : cmdline::base_command(
    "list", "[test-program ...]", 0, -1,
    "Lists test cases and their meta-data")
{
    add_option(kyuafile_option);
    add_option(cmdline::bool_option('v', "verbose", "Show properties"));
}


/// Entry point for the "list" subcommand.
///
/// \param ui Object to interact with the I/O of the program.
/// \param cmdline Representation of the command line to the subcommand.
///
/// \return 0 to indicate success.
int
cli::cmd_list::run(cmdline::ui* ui, const cmdline::parsed_cmdline& cmdline)
{
    cli::filters_state filters(cmdline.arguments());
    const user_files::kyuafile kyuafile = load_kyuafile(cmdline);

    const user_files::test_programs_vector& test_programs =
        kyuafile.test_programs();
    for (user_files::test_programs_vector::const_iterator
         iter = test_programs.begin(); iter != test_programs.end(); iter++) {
        if (filters.match_test_program((*iter).binary_path))
            detail::list_test_program(ui, cmdline.has_option("verbose"),
                                      kyuafile.root(), *iter, filters);
    }

    return filters.report_unused_filters(ui) ? EXIT_FAILURE : EXIT_SUCCESS;
}
