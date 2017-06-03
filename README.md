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
* [`shared_future<T>` Class](#shared_futuret-class)
* [`promise<T>`: Asynchronous Promise Type](#promiset-asynchronous-promise-type)
* [`start` Function](#start-function)
* [`async_timer` Class](#async_timer-class)
* [`resumable_io_timeout` Class](#resumable_io_timeout-class)
* [`when_all` Function](#when_all-function)
* [`when_any` Function](#when_any-function)
* [`async_queue` Class](#async_queue-class)
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

In addition to `resume_background` class, library has `resume_background_long` class. An instance of this class should be awaited instead of `resume_background` if the coroutine is going to spend **long** time before the next suspension point. This is only a hint to the thread pool implementation, though.

### `future<T>`: Light-Weight Awaitable Class

```C++
#include <corsl/future.h>
```

`corsl` introduces a light-weight awaitable class `future<T>`. It may be used as a return type for any coroutine. `T` should be `void` for coroutines returning `void`. `T` cannot be a reference type.

`future<T>` may be copied and moved. Using `co_await` on a future suspends the current context until the coroutine produces a result. `co_await` expression then produces the result or re-throws an exception.

`future<T>` provides a blocking `get` method. If coroutine throws an exception, it is re-thrown in `get` method. It also provides a blocking `wait` method. It returns only when coroutine is finished and does not throw any exceptions.

Using `co_await` or calling `wait` or `get` with a default-initialized `future` triggers an assertion.

#### Notes

1. In the current version, when `await_resume` is called as part of execution of `co_await` expression, future's value is _moved_ to the caller in case co_await is applied to a rvalue reference (or temporary).
2. `future<T>` must only be awaited once. Calling `get` or `wait` counts as well. Library will fire an assertion in debug mode if `future` is awaited more than once. However, it is allowed to call `get` and `wait` on already completed future.
3. `future<T>` integrates with library's [cancellation support](#cancellation-support). If a cancellation source is associated with a future and it is cancelled, any `co_await` expression automatically throws an exception.

### `shared_future<T>` Class

```C++
#include <corsl/shared_future.h>
```

`shared_future<T>` class may be used to bypass a `future`'s limitation of only allowing a single continuation. If multiple continuations are required, an instance of `shared_future<T>` must be constructed from a `future<T>`. It allows any number of callers to `co_await` shared future. `shared_future<T>` is default constructible, copyable and moveable. 

Awaiting or blocking on default-constructed (or moved-from) `shared_future<T>` is an undefined behavior.

```C++
#include <corsl/shared_future.h>
#include <corsl/future.h>
#include <corsl/compatible_base.h>

corsl::promise<int> promise;
corsl::shared_future<int> shared_future{ promise.get_future() };

auto event = CreateEvent(nullptr, TRUE, FALSE, nullptr);
std::atomic<int> counter{ 0 };

for (int i = 0; i < 10; ++i)
{
    [&](int i) -> winrt::fire_and_forget
    {
        using namespace std::string_literals;
        co_await corsl::resume_background{};
        std::wcout << (std::to_wstring(i + 1) + L". shared_future await completed with result "s + std::to_wstring(co_await shared_future) + L"\n"s);
        if (counter.fetch_add(1, std::memory_order_relaxed) == 9)
            SetEvent(event);
    }(i);
}

promise.set(42);
corsl::block_wait(corsl::resume_on_signal{ event });
CloseHandle(event);
```

### `promise<T>`: Asynchronous Promise Type

```C++
#include <corsl/promise.h>
```

PPL provides `task_completion_event` class that may be used to perform late completion of tasks. `corsl` provides similar functionality with a help of its `promise<T>` class. `T` is a promise result type or `void`. Reference types are not allowed.

After construction, a related future object may be created by calling `get_future` method. When the operation result is received, `set` method is called to set the promise value or `set_exception` if exception has occurred. Corresponding future object is then completed and the result (or exception) is propagated to the continuation.

```C++
#include <corsl/promise.h>

corsl::promise<void> promise;

corsl::future<void> start_async_operation()
{
    return promise.get_future();
}

void complete_async_operation()
{
    promise.set();
}
```

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

In addition to `start`, `block_get` and `block_wait` functions are provided:

```C++
template<class Awaitable>
inline auto block_get(Awaitable &&awaitable)
{
    return start(std::forward<Awaitable>(awaitable)).get();
}

template<class Awaitable>
inline void block_wait(Awaitable &&awaitable) noexcept
{
    start(std::forward<Awaitable>(awaitable)).wait();
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

When the list of tasks to await is not known at compile time, `when_all_range` function must be used instead. It takes a pair of iterators to a range of tasks. In this case, all tasks must be of the same type. `when_all_range` guarantees to traverse the range only once, thus supporting input iterators (or better). Note, that it copies task objects during its execution.

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

If all input tasks produce no result, `when_any` produces the index to the first completed task. Otherwise, it produces `std::pair<T, size_t>`, where the first result is the result of completed task and second is an index of completed task.

When the list of tasks to await is not known at compile time, `when_any_range` function must be used instead. It takes a pair of iterators to a range of tasks. `when_any_range` guarantees to traverse the range only once, thus supporting input iterators (or better). Note, that it copies task objects during its execution.

```C++
corsl::future<void> coroutine7()
{
    // The following operation will complete in 10 seconds and produce 0
    size_t index = co_await corsl::when_any(void_timer(10s), void_timer(20s), void_timer(30s));

    // The following operation will complete in 10 seconds and produce std::pair<bool, size_t> { true, 0 }
    std::pair<bool, size_t> result = co_await corsl::when_any(bool_timer(10s), bool_timer(20s), bool_timer(30s));
}
```

### `async_queue` Class

```C++
#include <corsl/async_queue.h>
```

`async_queue<T>` is an awaitable producer-consumer queue. Producer adds values to the queue by calling `push` or `emplace` methods while consumer calls `next` method to get an awaitable that produces the result from the head of a queue.

**Note**: While multiple producers are allowed to add values to a queue concurrently (actual access is synchronized with a lock), only single consumer is supported. Calling `next` from multiple coroutines or threads will lead to undefined behavior.

```C++
#include <corsl/async_queue.h>
#include <corsl/future.h>
#include <corsl/compatible_base.h>

using namespace std::chrono_literals;
using namespace corsl::timer;

corsl::async_queue<int> queue;

corsl::future<void> test_async_queue_producer()
{
	co_await 2s;
	queue.push(17);
	co_await 1s;
	queue.push(23);
	co_await 2s;
	queue.push(42);
}

corsl::future<void> test_async_queue_consumer()
{
	int value;
	do
	{
		value = co_await queue.next();
		std::wcout << value << L" received from async queue\n";
	} while (value != 42);
}
```

### Cancellation Support

```C++
#include <corsl/cancel.h>
```

**Breaking Change**: Cancellation support has been reworked and new API is not compatible with previous version.

**Note**: Including `cancel.h` will add dependency on `Boost.Intrusive` header-only library.

Cancellation in `corsl` is provided by means of three classes: `cancellation_source`, `cancellation_token` and `cancellation_subscription<>`.

#### `cancellation_source`

An object of `cancellation_source` type must be created outside of a coroutine. It is always external to a coroutine. A reference to `cancellation_source` is usually passed to coroutine (or made available to it by any other means, for example, as class member variable). 

A connected cancellation source may be created by calling `create_connected_source`. When a source is cancelled, all connected sources are cancelled as well.

To cancel a source, call its `cancel` method. This method returns immediately. If the caller needs to block until the actual cancellation occurs, it should use `future<T>::get` or `future<T>::wait` methods after cancelling a token source object.

#### `cancellation_token`

A coroutine that supports cancellation needs to associate itself with a cancellation source by creating an instance of `cancellation_token` class on its stack, passing it the reference to the source object. The source object must be *awaited* before passing it to a token constructor:

```C++
corsl::cancellation_token_source source;

// ...

corsl::future<void> coroutine()
{
    corsl::cancellation_token token { co_await source };

    // ...
}
```

Once association is done, any `co_await` the coroutine executes will throw `operation_cancelled` exception if the source has been cancelled. The `cancellation_token` constructor will also throw if the source is cancelled at the object construction time.

The coroutine may then check cancellation state of a token by either calling token's `is_cancelled` method or casting a token to `bool`. Calling `check_cancelled` method throws `operation_cancelled` exception if the token has been cancelled.

#### `cancellation_subscription<>`

Coroutine may also subscribe to the cancellation event with a callback by creating an instance of `cancellation_subscription<>` class:

```C++
#include <corsl/future.h>
#include <corsl/async_timer.h>
#include <corsl/cancel.h>

class server
{
    corsl::cancellation_source cancel;
    corsl::future<void> refresh_task;

    void refresh() { ... }
public:
    void start()
    {
        refresh_task = [this]() -> corsl::future<void>
        {
            // Associate this coroutine with a cancellation source
            corsl::cancellation_token token { co_await cancel };
            corsl::async_timer timer;

            // When cancelled, cancel timer first
            // Create a callback subscription
            corsl::cancellation_subscription subscription { token, [&]
            {
                timer.cancel();
            } };

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
        refresh_task.wait();
    }
};
```

If cancellation has been requested and `cancellation_subscription` destructor is invoked, it will block until the callback exits.
