/*
 * Copyright (C) 2023 Emeric Poupon
 *
 * This file is part of LMS.
 *
 * LMS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LMS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LMS.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "database/TransactionChecker.hpp"

#include <cassert>

#include "database/Session.hpp"

#if !defined(NDEBUG)
#define LMS_CHECK_TRANSACTION_ACCESSES 1
#else
#define LMS_CHECK_TRANSACTION_ACCESSES 0
#endif

namespace Database
{
#if LMS_CHECK_TRANSACTION_ACCESSES
    namespace
    {
        struct StackEntry
        {
            TransactionChecker::TransactionType type;
            Wt::Dbo::Session* session{};
        };

        static thread_local std::vector<StackEntry> transactionStack;
    }
#endif

    void TransactionChecker::pushWriteTransaction(Wt::Dbo::Session& session)
    {
        pushTransaction(TransactionType::Write, session);
    }

    void TransactionChecker::pushReadTransaction(Wt::Dbo::Session& session)
    {
        pushTransaction(TransactionType::Read, session);
    }

    void TransactionChecker::popWriteTransaction(Wt::Dbo::Session& session)
    {
        popTransaction(TransactionType::Write, session);
    }

    void TransactionChecker::popReadTransaction(Wt::Dbo::Session& session)
    {
        popTransaction(TransactionType::Read, session);
    }

    void TransactionChecker::pushTransaction([[maybe_unused]] TransactionType type, [[maybe_unused]] Wt::Dbo::Session& session)
    {
#if LMS_CHECK_TRANSACTION_ACCESSES
        assert(transactionStack.empty() || transactionStack.back().session == &session);
        transactionStack.push_back(StackEntry{ type, &session });
#endif // LMS_CHECK_TRANSACTION_ACCESSES
    }

    void TransactionChecker::popTransaction([[maybe_unused]] TransactionType type, [[maybe_unused]] Wt::Dbo::Session& session)
    {
#if LMS_CHECK_TRANSACTION_ACCESSES

        assert(!transactionStack.empty());
        assert(transactionStack.back().type == type);
        assert(transactionStack.back().session == &session);
        transactionStack.pop_back();
#endif // LMS_CHECK_TRANSACTION_ACCESSES
    }

    void TransactionChecker::checkWriteTransaction([[maybe_unused]] Wt::Dbo::Session& session)
    {
        assert(!transactionStack.empty());
        assert(transactionStack.back().type == TransactionType::Write);
        assert(transactionStack.back().session == &session);
    }

    void TransactionChecker::checkWriteTransaction(Session& session)
    {
        checkWriteTransaction(session.getDboSession());
    }

    void TransactionChecker::checkReadTransaction([[maybe_unused]] Wt::Dbo::Session& session)
    {
        assert(!transactionStack.empty());
        assert(transactionStack.back().session == &session);
    }

    void TransactionChecker::checkReadTransaction(Session& session)
    {
        checkReadTransaction(session.getDboSession());
    }
}