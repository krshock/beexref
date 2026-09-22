#pragma once

#include <QString>

#include <utility>
#include <variant>

namespace board {

// Failure of a board operation: SQLite's extended result code and
// message, plus the file it happened on. Code 0 means the failure was
// raised by the board layer itself rather than by SQLite.
struct Error
{
    int code = 0;
    QString message;
    QString path;

    QString toString() const;
};

// Result of an operation that has no value.
class Status
{
public:
    static Status ok() { return Status(true, {}); }
    static Status fail(Error error) { return Status(false, std::move(error)); }

    Status(Error error)
        : ok_(false)
        , error_(std::move(error))
    {
    }

    bool isOk() const { return ok_; }
    explicit operator bool() const { return ok_; }
    const Error &error() const { return error_; }

private:
    Status(bool ok, Error error)
        : ok_(ok)
        , error_(std::move(error))
    {
    }

    bool ok_;
    Error error_;
};

// Result of an operation that yields a value.
template <typename T>
class Result
{
public:
    Result(T value)
        : data_(std::move(value))
    {
    }
    Result(Error error)
        : data_(std::move(error))
    {
    }

    bool isOk() const { return std::holds_alternative<T>(data_); }
    explicit operator bool() const { return isOk(); }

    const T &value() const & { return std::get<T>(data_); }
    T &value() & { return std::get<T>(data_); }
    T take() { return std::get<T>(std::move(data_)); }

    const Error &error() const { return std::get<Error>(data_); }

private:
    std::variant<T, Error> data_;
};

} // namespace board
