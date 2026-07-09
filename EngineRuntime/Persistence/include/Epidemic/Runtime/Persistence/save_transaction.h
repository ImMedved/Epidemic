#pragma once

#include "Epidemic/Foundation/result.h"

namespace epidemic::runtime
{
enum class SaveTransactionState
{
    NotStarted,
    Open,
    Committing,
    Committed,
    RolledBack,
    Failed,
};

class ISaveTransaction
{
  public:
    virtual ~ISaveTransaction() = default;

    [[nodiscard]] virtual SaveTransactionState GetState() const = 0;
    [[nodiscard]] virtual foundation::Result<void> Commit() = 0;
    virtual void Rollback() = 0;
};
} // namespace epidemic::runtime
