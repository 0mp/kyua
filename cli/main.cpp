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

#if defined(HAVE_CONFIG_H)
#   include "config.h"
#endif

#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

#include "cli/cmd_about.hpp"
#include "cli/cmd_help.hpp"
#include "cli/cmd_list.hpp"
#include "cli/cmd_test.hpp"
#include "cli/main.hpp"
#include "utils/cmdline/exceptions.hpp"
#include "utils/cmdline/globals.hpp"
#include "utils/cmdline/options.hpp"
#include "utils/cmdline/parser.hpp"
#include "utils/cmdline/ui.hpp"
#include "utils/env.hpp"
#include "utils/format/macros.hpp"
#include "utils/fs/operations.hpp"
#include "utils/fs/path.hpp"
#include "utils/logging/macros.hpp"
#include "utils/logging/operations.hpp"
#include "utils/optional.ipp"
#include "utils/sanity.hpp"

namespace cmdline = utils::cmdline;
namespace fs = utils::fs;
namespace logging = utils::logging;

using utils::none;
using utils::optional;


namespace {


/// Executes the given subcommand with proper usage_error reporting.
///
/// \param ui Object to interact with the I/O of the program.
/// \param command The subcommand to execute.
/// \param args The part of the command line passed to the subcommand.  The
///     first item of this collection must match the command name.
///
/// \return The exit code of the command.  Typically 0 on success, some other
/// integer otherwise.
///
/// \throw cmdline::usage_error If the user input to the subcommand is invalid.
///     This error does not encode the command name within it, so this function
///     extends the message in the error to specify which subcommand was
///     affected.
/// \throw std::exception This propagates any uncaught exception.  Such
///     exceptions are bugs, but we let them propagate so that the runtime will
///     abort and dump core.
static int
run_subcommand(cmdline::ui* ui, cmdline::base_command* command,
               const cmdline::args_vector& args)
{
    try {
        PRE(command->name() == args[0]);
        return command->main(ui, args);
    } catch (const cmdline::usage_error& e) {
        throw std::pair< std::string, cmdline::usage_error >(
            command->name(), e);
    }
}


/// Exception-safe version of main.
///
/// This function provides the real meat of the entry point of the program.  It
/// is allowed to throw some known exceptions which are parsed by the caller.
/// Doing so keeps this function simpler and allow tests to actually validate
/// that the errors reported are accurate.
///
/// \return The exit code of the program.  Typically 0 on success, some other
/// integer otherwise, but this depends on the subcommand executed (if any).
///
/// \param ui Object to interact with the I/O of the program.
/// \param argc The number of arguments passed on the command line.
/// \param argv NULL-terminated array containing the command line arguments.
/// \param mock_command An extra command provided for testing purposes; should
///     just be NULL other than for tests.
///
/// \throw cmdline::usage_error If the user ran the program with invalid
///     arguments.
/// \throw std::exception This propagates any uncaught exception.  Such
///     exceptions are bugs, but we let them propagate so that the runtime will
///     abort and dump core.
static int
safe_main(cmdline::ui* ui, int argc, const char* const argv[],
          cmdline::command_ptr mock_command)
{
    cmdline::options_vector options;
    const cmdline::path_option logfile_option(
        "logfile", "Path to the log file", "file",
        cli::detail::default_log_name().c_str());
    options.push_back(&logfile_option);

    cmdline::commands_map commands;
    commands.insert(cmdline::command_ptr(new cli::cmd_about()));
    commands.insert(cmdline::command_ptr(new cli::cmd_help(&options,
                                                           &commands)));
    commands.insert(cmdline::command_ptr(new cli::cmd_list()));
    commands.insert(cmdline::command_ptr(new cli::cmd_test()));
    if (mock_command.get() != NULL)
        commands.insert(mock_command);

    cmdline::parsed_cmdline cmdline = cmdline::parse(argc, argv, options);

    const fs::path logfile(cmdline.get_option< cmdline::path_option >(
        "logfile"));
    fs::mkdir_p(logfile.branch_path(), 0755);
    LD(F("Log file is %s") % logfile);
    logging::set_persistency(logfile);

    if (cmdline.arguments().empty())
        throw cmdline::usage_error("No command provided");
    const std::string cmdname = cmdline.arguments()[0];

    cmdline::base_command* command = commands.find(cmdname);
    if (command == NULL)
        throw cmdline::usage_error(F("Unknown command '%s'") % cmdname);
    return run_subcommand(ui, command, cmdline.arguments());
}


}  // anonymous namespace


/// Gets the name of the default log file.
///
/// \return The path to the log file.
fs::path
cli::detail::default_log_name(void)
{
    // Update doc/troubleshooting.texi if you change this algorithm.
    const optional< std::string > home(utils::getenv("HOME"));
    if (home) {
        return logging::generate_log_name(fs::path(home.get()) / ".kyua" /
                                          "logs", cmdline::progname());
    } else {
        const optional< std::string > tmpdir(utils::getenv("TMPDIR"));
        if (tmpdir) {
            return logging::generate_log_name(fs::path(tmpdir.get()),
                                              cmdline::progname());
        } else {
            return logging::generate_log_name(fs::path("/tmp"),
                                              cmdline::progname());
        }
    }
}


/// Testable entry point, with catch-all exception handlers.
///
/// This entry point does not perform any initialization of global state; it is
/// provided to allow unit-testing of the utility's entry point.
///
/// \param ui Object to interact with the I/O of the program.
/// \param argc The number of arguments passed on the command line.
/// \param argv NULL-terminated array containing the command line arguments.
/// \param mock_command An extra command provided for testing purposes; should
///     just be NULL other than for tests.
///
/// \return 0 on success, some other integer on error.
///
/// \throw std::exception This propagates any uncaught exception.  Such
///     exceptions are bugs, but we let them propagate so that the runtime will
///     abort and dump core.
int
cli::main(cmdline::ui* ui, const int argc, const char* const* const argv,
          cmdline::command_ptr mock_command)
{
    try {
        return safe_main(ui, argc, argv, mock_command);
    } catch (const std::pair< std::string, cmdline::usage_error >& e) {
        const std::string message = F("Usage error for command %s: %s.") %
            e.first % e.second.what();
        LE(message);
        ui->err(message);
        ui->err(F("Type '%s help %s' for usage information.") %
                cmdline::progname() % e.first);
        return EXIT_FAILURE;
    } catch (const cmdline::usage_error& e) {
        const std::string message = F("Usage error: %s.") % e.what();
        LE(message);
        ui->err(message);
        ui->err(F("Type '%s help' for usage information.") %
                cmdline::progname());
        return EXIT_FAILURE;
    }
}


/// Delegate for ::main().
///
/// This function is supposed to be called directly from the top-level ::main()
/// function.  It takes care of initializing internal libraries and then calls
/// main(ui, argc, argv).
///
/// \pre This function can only be called once.
///
/// \throw std::exception This propagates any uncaught exception.  Such
///     exceptions are bugs, but we let them propagate so that the runtime will
///     abort and dump core.
int
cli::main(const int argc, const char* const* const argv)
{
    LI(F("%s %s") % PACKAGE % VERSION);

    std::string plain_args;
    for (const char* const* arg = argv; *arg != NULL; arg++)
        plain_args += F(" %s") % *arg;
    LI(F("Command line:%s") % plain_args);

    cmdline::init(argv[0]);
    cmdline::ui ui;

    const int exit_code = main(&ui, argc, argv);
    LI(F("Clean exit with code %d") % exit_code);
    return exit_code;
}
