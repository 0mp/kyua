// Copyright 2010, Google Inc.
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
#include <fstream>
#include <utility>
#include <vector>

#include "cli/cmd_about.hpp"
#include "utils/cmdline/base_command.ipp"
#include "utils/cmdline/exceptions.hpp"
#include "utils/cmdline/options.hpp"
#include "utils/cmdline/parser.ipp"
#include "utils/cmdline/ui.hpp"
#include "utils/format/macros.hpp"
#include "utils/fs/path.hpp"
#include "utils/sanity.hpp"

#if defined(HAVE_CONFIG_H)
#   include "config.h"
#endif

namespace cmdline = utils::cmdline;
namespace fs = utils::fs;

using cli::cmd_about;


namespace {


/// Print the contents of a document.
///
/// If the file cannot be opened for whatever reason, an error message is
/// printed to the output of the program instead of the contents of the file.
///
/// \param ui Object to interact with the I/O of the program.
/// \param file The file to print.
///
/// \return True if the file was printed, false otherwise.
static bool
cat_file(cmdline::ui* ui, const fs::path& file)
{
    std::ifstream input(file.c_str());
    if (!input) {
        ui->err(F("Failed to open %s") % file);
        return false;
    }

    std::string line;
    while (std::getline(input, line).good())
        ui->out(line);
    input.close();
    return true;
}


/// Constructs the path to a distribution document.
///
/// \param docdir The directory containing the documents.  If empty, defaults to
///     the documents directory set at configuration time.
/// \param docname The base name of the document.
///
/// \return The path to the document.
static fs::path
path_to_doc(const std::string& docdir, const char* docname)
{
    if (docdir.empty())
        return fs::path(KYUA_DOCDIR) / docname;
    else
        return fs::path(docdir) / docname;
}


}  // anonymous namespace


/// Default constructor for cmd_about.
///
/// \param docdir_ Path to the directory containing the documents.  If empty
///     defaults to the value determined by configure.  Provided for testing
///     purposes only.
cmd_about::cmd_about(const char* docdir_) : cmdline::base_command(
    "about", "", 0, 0,
    "Shows general program information"),
    _docdir(docdir_)
{
    add_option(cmdline::string_option(
        "show", "What to show", "all|authors|license|version", "all"));
}


/// Entry point for the "about" subcommand.
///
/// \param ui Object to interact with the I/O of the program.
/// \param cmdline Representation of the command line to the subcommand.
///
/// \return 0 if everything is OK, 1 if any of the necessary documents cannot be
/// opened.
int
cmd_about::run(cmdline::ui* ui, const cmdline::parsed_cmdline& cmdline)
{
    const std::string show = cmdline.get_option< cmdline::string_option >(
        "show");

    bool success = true;

    if (show == "all") {
        ui->out(PACKAGE " (" PACKAGE_NAME ") " PACKAGE_VERSION);
        ui->out("");
        ui->out("License terms:");
        ui->out("");
        success &= cat_file(ui, path_to_doc(_docdir, "COPYING"));
        ui->out("");
        ui->out("Brought to you by:");
        ui->out("");
        success &= cat_file(ui, path_to_doc(_docdir, "AUTHORS"));
        ui->out("");
        ui->out(F("Homepage: %s") % PACKAGE_URL);
    } else if (show == "authors") {
        success &= cat_file(ui, path_to_doc(_docdir, "AUTHORS"));
    } else if (show == "license") {
        success &= cat_file(ui, path_to_doc(_docdir, "COPYING"));
    } else if (show == "version") {
        ui->out(PACKAGE " (" PACKAGE_NAME ") " PACKAGE_VERSION);
    } else {
        throw cmdline::usage_error(F("Invalid value passed to --show: %s") %
                                   show);
    }

    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
