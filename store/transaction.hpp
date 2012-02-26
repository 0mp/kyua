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

/// \file store/transaction.hpp
/// Implementation of transactions on the backend.

#if !defined(STORE_TRANSACTION_HPP)
#define STORE_TRANSACTION_HPP

extern "C" {
#include <stdint.h>
}

#include <tr1/memory>
#include <utility>

namespace engine {
class action;
class base_test_case;
class base_test_program;
class context;
class test_result;
};

namespace store {


class transaction;


/// Iterator for the set of test case results that are part of an action.
///
/// \todo Note that this is not a "standard" C++ iterator.  I have chosen to
/// implement a different interface because it makes things easier to represent
/// an SQL statement state.  Rewrite as a proper C++ iterator, inheriting from
/// std::iterator.
class results_iterator {
    struct impl;
    std::tr1::shared_ptr< impl > _pimpl;

    friend class transaction;
    results_iterator(std::tr1::shared_ptr< impl >);

public:
    ~results_iterator(void);

    results_iterator& operator++(void);
    operator bool(void) const;

    utils::fs::path binary_path(void) const;
    std::string test_case_name(void) const;
    engine::test_result result(void) const;
};


/// Representation of a transaction.
///
/// Transactions are the entry place for high-level calls that access the
/// database.
class transaction {
    struct impl;
    std::tr1::shared_ptr< impl > _pimpl;

    friend class backend;
    transaction(backend&);

public:
    ~transaction(void);

    void commit(void);
    void rollback(void);

    engine::action get_action(const int64_t);
    results_iterator get_action_results(const int64_t);
    std::pair< int64_t, engine::action > get_latest_action(void);
    engine::context get_context(const int64_t);

    int64_t put_action(const engine::action&, const int64_t);
    int64_t put_context(const engine::context&);
    int64_t put_test_program(const engine::base_test_program&, const int64_t);
    int64_t put_test_case(const engine::base_test_case&, const int64_t);
    int64_t put_result(const engine::test_result&, const int64_t);
};


}  // namespace store

#endif  // !defined(STORE_TRANSACTION_HPP)
