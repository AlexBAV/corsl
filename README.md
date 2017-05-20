# Overview of `corsl` Library

`corsl` stands for "Coroutine Support Library" and consists of a number of utility classes and functions that simplify asynchronous programming in Windows. It is inspired by an amazing [`cppwinrt`](https://github.com/Microsoft/cppwinrt) library, developed by Microsoft.

`cppwinrt` was created as a language projection for Windows Runtime, which is supported by Windows 8 or later operating systems. It is impossible to use in prior Windows versions.

One of the goals of `corsl` library was being able to use it under Windows Vista or later operating system.

Library is header-only and consists of several headers. The only external dependency is `cppwinrt` library. For simplicity, there is header `all.h`, which includes all other headers, except `cancel.h` header.

`cancel.h` header additionally depends on [`Boost.Intrusive`](http://www.boost.org/doc/libs/1_64_0/doc/html/intrusive.html) library and therefore is not included by default. `Boost.Intrusive` is a header-only library distributed with `boost`.

## Compiler Support

The library has been tested on Microsoft Visual Studio 2017 Version 15.2 (26430.4).

## Documentation

`corsl` library has all its classes and functions defined in `corsl` namespace.

## TOC

* [`srwlock` Class](#srwlock-class)
* ["Compatible" Versions of Awaitables from `cppwinrt`](#compatible-versions-of-awaitables-from-cppwinrt)
* [`future<T>`: Light-Weight Awaitable Class](#futuret-light-weight-awaitable-class)
* [`start` Function](#start-function)
* [`async_timer` Class](#asynctimer-class)
* [`resumable_io_timeout` Class](#resumableiotimeout-class)
* [`when_all` Function](#whenall-function)
* [`when_any` Function](#whenany-function)
* [Cancellation Support](#cancellation-support)

### `srwlock` Class

```C++
#include <corsl/srwlock.h>
```

`std::mutex` and other mutual exclusion classes from Standard Library often cannot be used in coroutines. This is because according to Standard mutex must be released by the same thread that acquired it. This is not a case for a coroutine as parts of a coroutine are often executed by different threads.

`cppwinrt` has a `srwlock` class, but, unfortunately, it does not follow the standard "interface" of STL `mutex`.

`corsl::srwlock` is compatible with `std::shared_mutex` class and, therefore, may be used with lock adapters like `std::lock_guard<T>`, `std::unique_lock<T>` and `std::shared_lock<T>`.

### "Compatible" Versions of Awaitables from `cppwinrt`

`compatible_base.h` includes copies of the following `cppwinrt` classes:  `resume_background`, `resume_after`, `resume_on_signal` and `resumable_io`.

Motivation for having those classes copied into `corsl` namespace is because they use `hresult_error` class that requires Windows Runtime.

`corsl` has simplified version of `hresult_error` that does not require Windows Runtime. Therefore, if the user is going to target older OS versions, these versions must be used instead of original versions from `winrt` namespace.

**Note**: `winrt::hresult_error` and `corsl::hresult_error` classes are unrelated!

```C++
#include <corsl/compatible_base.h>
#include <corsl/future.h>

using namespace std::chrono_literals;
using namespace corsl::timer;   // this line is required for "co_await duration"

corsl::future<void> coroutine1()
{
  // illustrate corsl::resume_background
  co_await corsl::resume_background{};

  // illustrate corsl::resume_after
  co_await 2s;

  auto event_handle = CreateEvent(...);

  // illustrate corsl::resume_on_signal
  co_await corsl::resume_on_signal { event_handle }; 

  // illustrate corsl::resumable_io
  auto file_handle = CreateFile(...);

  corsl::resumable_io io { file_handle };

  co_await io.start(...);
}
```

### `future<T>`: Light-Weight Awaitable Class

```C++
#include <corsl/future.h>
```

`corsl` introduces a light-weight awaitable class `future<T>`. It may be used as a return type for any coroutine. `T` should be `void` for coroutines returning `void`. `T` cannot be a reference type.

`future<T>` may be copied and moved. Using `co_await` on a future suspends the current context until the coroutine produces a result. `co_await` expression then produces the result or re-throws an exception.

`future<T>` provides a blocking `get()` method. If coroutine throws an exception, it is re-thrown in `get()` method. It also provides a blocking `wait()` method. It returns only when coroutine is finished and does not throw any exceptions.

Using `co_await` or calling `wait()` or `get()` with a default-initialized `future` triggers an assertion.

#### Notes

1. In the current version, when `await_resume` is called as part of execution of `co_await` expression, future's value is _moved_ to the caller in case co_await is applied to a rvalue reference (or temporary).
2. Cancellation is not built-in into the `future` class. Use the "external" cancellation as described [later](#cancellation-support).

### `start` Function

```C++
#include <corsl/start.h>
```

`cppwinrt` provides a number of convenient utility classes to initiate asynchronous waits and I/O, among other things. The only problem with those classes is that the operation does not start until the caller begins _awaiting_ its result. Consider the following:

```C++
corsl::future<void> coroutine1()
{
    // ...
    co_await 3s;
    // ...
}
```

In this code snippet, `co_await 3s;` is translated to `co_await resume_after{3s};`. `resume_after` is an _awaitable_ that starts a timer on thread pool and schedules a continuation. The problem is that you cannot *start* a timer and continue your work.

The same problem exists with `resumeable_io` class:

```C++
corsl::resumable_io io{handle};
/// ...
corsl::future<void> coroutine2()
{
    co_await io.start([](OVERLAPPED &o)
    {
       check(::ReadFile(handle,...));
    });
    // ...
}
```

And again, you cannot start an I/O and do other stuff before you _await_ for operation result.

`corsl` provides simple wrapper function that starts an asynchronous operation for you:

```C++
#include <corsl/future.h>
#include <corsl/start.h>

corsl::resumable_io io{handle};
/// ...
corsl::future<void> coroutine3()
{
    auto running_io_operation = corsl::start(io.start([](OVERLAPPED &o)
    {
       check(::ReadFile(handle,...));
    });
    // do other stuff
    // here we finally need to wait for operation to complete:
    auto result = co_await running_io_operation;
    // ...
}
```

### `async_timer` Class

```C++
#include <corsl/async_timer.h>
```

This is an awaitable cancellable timer. Its usage is very simple:

```C++
#include <corsl/future.h>
#include <corsl/async_timer.h>

corsl::async_timer timer;
// ...
corsl::future<void> coroutine4()
{
    try
    {
        co_await timer.wait(20min);
    } catch(const corsl::timer_cancelled &)
    {
        // the wait has been cancelled
    }
}
// ...
void cancel_wait()
{
    timer.cancel();
}
```

Another example of `async_timer` usage is provided below in [Cancellation Support](#cancellation-support) section.

### `resumable_io_timeout` Class

```C++
#include <advanced_io.h>
```

This is a version of `cppwinrt`'s `resumable_io` class that supports timeout for I/O operations. Its `start` method requires an additional parameter that specifies the I/O operation's timeout. If operation does not finish within a given time, it is cancelled and `operation_cancelled` exception is propagated to the continuation:

```C++
#include <future.h>
#include <advanced_io.h>

corsl::resumable_io_timeout io{ handle_to_serial_port };
// ...
corsl::future<void> coroutine5()
{
    try
    {
        auto bytes_received = co_await io.start([](OVERLAPPED &o)
        {
            check(::ReadFile(handle_to_serial_port, ... ));
        }, 10s);
        // Operation succeeded, continue processing
    } catch(const corsl::operation_cancelled &)
    {
        // operation timeout, data not ready
    } catch(const corsl::hresult_error &)
    {
        // other I/O errors
    }
}
```

### `when_all` Function

`when_all` function accepts any number of awaitables and produces an awaitable that is completed only when all input tasks are completed. If at least one of the tasks throws, the first thrown exception is rethrown by `when_all`.

Every input parameter must either be `IAsyncAction`, `IAsyncOperation<T>`, `future<T>` or an awaitable that implements `await_resume` member function (or has a free function `await_resume`).

If all input tasks produce `void`, `when_all` also produces `void`, otherwise, it produces an `std::tuple<>` of all input parameter types. For `void` tasks, an empty type `corsl::no_result` is used in the tuple.

```C++
corsl::future<void> void_timer(TimeSpan duration)
{
    co_await duration;
}

corsl::future<bool> bool_timer(TimeSpan duration)
{
    co_await duration;
    co_return true;
}

corsl::future<int> int_timer(TimeSpan duration)
{
    co_await duration;
    co_return 10;
}

corsl::future<void> coroutine6()
{
    // The following operation will complete in 30 seconds and produce void
    co_await corsl::when_all(void_timer(10s), void_timer(20s), void_timer(30s));

    // The following operation will complete in 30 seconds and produce std::tuple<bool, bool, int>
    std::tuple<bool, bool, int> result = co_await corsl::when_all(bool_timer(10s), bool_timer(20s), int_timer(30s));

    // The following operation will complete in 30 seconds and produce std::tuple<bool, no_result, int>
    std::tuple<bool, corsl::no_result, int> result = co_await corsl::when_all(bool_timer(10s), corsl::resume_after{20s}, int_timer(30s));
}
```

### `when_any` Function

`when_any` function accepts any number of awaitables and produces an awaitable that is completed when at least one of the input tasks is completed. If the first completed task throws, the thrown exception is rethrown by `when_any`.

All input parameters must be `IAsyncAction`, `IAsyncOperation<T>`, `future<T>` or an awaitable type that implements `await_resume` member function (or has a free function `await_resume`) and **must all be of the same type**.

`when_any` **does not cancel any non-completed tasks.** When other tasks complete, their results are silently discarded. `when_any` makes sure the control block does not get destroyed until all tasks complete.

If all input tasks produce no result, `when_any` produces the index to the first completed task. Otherwise, it produces `std::pair<T, size_t>`, where the first result is the result of completed task and second is an index of completed task:

```C++
corsl::future<void> coroutine7()
{
    // The following operation will complete in 10 seconds and produce 0
    size_t index = co_await corsl::when_any(void_timer(10s), void_timer(20s), void_timer(30s));

    // The following operation will complete in 10 seconds and produce std::pair<bool, size_t> { true, 0 }
    std::pair<bool, size_t> result = co_await corsl::when_any(bool_timer(10s), bool_timer(20s), bool_timer(30s));
}
```

### Cancellation Support

```C++
#include <corsl/cancel.h>
```

**Note**: Including `cancel.h` will add dependency on `Boost.Intrusive` header-only library.

Cancellation in `corsl` is provided by means of two classes: `cancellation_token_source` and `cancellation_token`.

An object of `cancellation_token_source` class should be created outside of a coroutine the user is going to cancel and a reference to it should be passed to the coroutine by any possible means.

A coroutine then creates an instance of `cancellation_token` class on its stack, passing it the reference to the source object. **Note**: `cancellation_token` must only be constructed on coroutine stack, all other uses lead to undefined behavior.

A coroutine may check cancellation state of a token by either calling token's `is_cancelled()` method or casting a token to `bool`. Calling `check_cancelled()` method throws `operation_cancelled` exception if the token has been cancelled.

Coroutine may also subscribe to the cancellation event with a callback by calling a `subscribe` method. Callback is automatically unsubscribed when token is destroyed or may be manually unsubscribed by calling `unsubscribe` method.

```C++
#include <corsl/future.h>
#include <corsl/async_timer.h>
#include <corsl/cancel.h>

class server
{
    corsl::cancellation_token_source cancel;
    corsl::future<void> refresh_task;

    void refresh() { ... }
public:
    void start()
    {
        refresh_task = [this]() -> corsl::future<void>
        {
            corsl::cancellation_token token { cancel };
            corsl::async_timer timer;

            // When cancelled, cancel timer first
            token.subscribe([&]
            {
                timer.cancel();
            });

            while (!token)
            {
                co_await timer.wait(2min);
                refresh();
            }
        }();
    }

    void stop()
    {
        cancel.cancel();
        try
        {
            refresh_task.get();
        } catch(...)
        {
        }
    }
};
```
