//-------------------------------------------------------------------------------------------------------
// corsl - Coroutine Support Library
// Copyright (C) 2017 HHD Software Ltd.
// Written by Alexander Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#include <chrono>
#include <iostream>

#include <corsl/all.h>

#include <future>
#include <sstream>

#include <numeric>

using namespace std::chrono_literals;
using namespace corsl::timer;

const auto first_timer_duration = 3s;
const auto second_timer_duration = 5s;
const auto third_timer_duration = 7s;

corsl::future<void> void_timer(winrt::Windows::Foundation::TimeSpan duration)
{
	co_await duration;
}

corsl::future<bool> bool_timer(winrt::Windows::Foundation::TimeSpan duration)
{
	co_await duration;
	co_return true;
}

corsl::future<int> int_timer(winrt::Windows::Foundation::TimeSpan duration)
{
	co_await duration;
	co_return 42;
}

corsl::future<void> test_when_all_void()
{
	// Test when_all with awaitables
	co_await corsl::when_all(std::experimental::suspend_never{}, std::experimental::suspend_never{});

	// Test when_all with future
	co_await corsl::when_all(void_timer(first_timer_duration), void_timer(second_timer_duration));
}

corsl::future<void> test_when_all_void_range()
{
	std::vector<std::experimental::suspend_never> tasks1(2);

	co_await corsl::when_all_range(tasks1.begin(), tasks1.end());

	std::vector<corsl::future<void>> tasks2
	{
		void_timer(first_timer_duration),
		void_timer(second_timer_duration)
	};

	co_await corsl::when_all_range(tasks2.begin(), tasks2.end());
}

corsl::future<void> test_when_all_bool_range()
{
	std::vector<corsl::future<bool>> tasks2
	{
		bool_timer(first_timer_duration),
		bool_timer(second_timer_duration)
	};

	auto result = co_await corsl::when_all_range(tasks2.begin(), tasks2.end());
}

corsl::future<void> test_when_any_void_range()
{
	std::vector<std::experimental::suspend_never> tasks1(2);

	co_await corsl::when_any_range(tasks1.begin(), tasks1.end());

	std::vector<corsl::future<void>> tasks2
	{
		void_timer(first_timer_duration),
		void_timer(second_timer_duration)
	};

	co_await corsl::when_any_range(tasks2.begin(), tasks2.end());
}

corsl::future<void> test_when_any_bool_range()
{
	std::vector<corsl::future<bool>> tasks2
	{
		bool_timer(first_timer_duration),
		bool_timer(second_timer_duration)
	};

	auto result = co_await corsl::when_any_range(tasks2.begin(), tasks2.end());
}


corsl::future<void> test_when_all_mixed()
{
	std::tuple<corsl::no_result, bool> result = co_await corsl::when_all(void_timer(first_timer_duration), bool_timer(second_timer_duration));
	std::tuple<bool, int, corsl::no_result> result2 = co_await corsl::when_all(bool_timer(first_timer_duration), int_timer(second_timer_duration), corsl::resume_after{ third_timer_duration });
}

corsl::future<void> test_when_all_bool()
{
	// Test when_all with IAsyncOperation<T>
	std::promise<bool> promise;
	promise.set_value(true);
	co_await corsl::when_all(bool_timer(first_timer_duration), bool_timer(second_timer_duration), promise.get_future());
}

corsl::future<void> test_when_any_void()
{
	// Test when_any with awaitables
	co_await corsl::when_any(std::experimental::suspend_never{}, std::experimental::suspend_never{});

	auto timer1 = void_timer(first_timer_duration);
	// Test when_any with IAsyncAction
	co_await corsl::when_any(timer1, void_timer(second_timer_duration));
}

corsl::future<void> test_when_any_bool()
{
	// Test when_any with IAsyncOperation<T>
	co_await corsl::when_any(bool_timer(first_timer_duration), bool_timer(second_timer_duration));
}

corsl::future<void> test_async_timer()
{
	// Test cancellable async_timer. Start a timer for 20 minutes and cancel it after 2 seconds
	corsl::async_timer atimer;

	auto timer_task = corsl::start(atimer.wait(20min));
	co_await 2s;
	atimer.cancel();
	try
	{
		co_await timer_task;
	}
	catch (corsl::hresult_error)
	{
		std::wcout << L"Timer cancelled. ";
	}
}

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

template<class F>
void measure(const wchar_t *name, const F &f)
{
	std::wcout << L"Starting operation " << name << L" ... ";
	auto start = std::chrono::high_resolution_clock::now();
	f();
	auto stop = std::chrono::high_resolution_clock::now();

	auto seconds = std::chrono::duration_cast<std::chrono::duration<double>>(stop - start);
	std::wcout << seconds.count() << L" seconds\r\n";
}

template<class F>
corsl::future<void> measure_async(const wchar_t *name, const F &f)
{
	std::wstringstream ws;

	ws << L"Operation " << name << L" ... ";
	auto start = std::chrono::high_resolution_clock::now();
	co_await f();
	auto stop = std::chrono::high_resolution_clock::now();

	auto seconds = std::chrono::duration_cast<std::chrono::duration<double>>(stop - start);
	ws << seconds.count() << L" seconds\r\n";
	std::wcout << ws.str();
}

void sequential_test()
{
	std::wcout << L"Running all tests sequentially...\n";
	measure(L"test async_timer", [] { test_async_timer().get(); });
	measure(L"test when_all_void", [] { test_when_all_void().get(); });
	measure(L"test when_all_bool", [] { test_when_all_bool().get(); });
	measure(L"test when_all_mixed", [] { test_when_all_mixed().get(); });
	measure(L"test when_any_void", [] { test_when_any_void().get(); });
	measure(L"test when_any_bool", [] {test_when_any_bool().get(); });

	measure(L"test when_all_void_range", [] { test_when_all_void_range().get(); });
	measure(L"test when_all_bool_range", [] { test_when_all_bool_range().get(); });

	measure(L"test when_any_void_range", [] { test_when_any_void_range().get(); });
	measure(L"test when_any_bool_range", [] { test_when_any_bool_range().get(); });
}

void concurrent_test()
{
	std::wcout << L"Running all tests in parallel...\n";

	corsl::start(
		corsl::when_all(
			measure_async(L"test async_timer", [] { return test_async_timer(); }),
			measure_async(L"test when_all_void", [] { return test_when_all_void(); }),
			measure_async(L"test when_all_bool", [] { return test_when_all_bool(); }),
			measure_async(L"test when_all_mixed", [] { return test_when_all_mixed(); }),
			measure_async(L"test when_any_void", [] { return test_when_any_void(); }),
			measure_async(L"test when_any_bool", [] {return test_when_any_bool(); }),
			measure_async(L"test when_all_void_range", [] { return test_when_all_void_range(); }),
			measure_async(L"test when_all_bool_range", [] { return test_when_all_bool_range(); }),
			measure_async(L"test when_any_void_range", [] { return test_when_any_void_range(); }),
			measure_async(L"test when_any_bool_range", [] { return test_when_any_bool_range(); })
			)
	).get();
}

corsl::future<void> promise_test_start(corsl::promise<void> &promise)
{
	return promise.get_future();
}

void promise_test_complete(corsl::promise<void> &promise)
{
	promise.set();
}

void test_shared_future()
{
	corsl::promise<int> promise;
	corsl::shared_future<int> shared_future{ promise.get_future() };

	auto event = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	std::atomic<int> counter{ 0 };

	auto lambda = [&](int i) -> corsl::fire_and_forget
	{
		using namespace std::string_literals;
		co_await corsl::resume_background{};
		std::wcout << (std::to_wstring(i + 1) + L". shared_future await completed with result "s + std::to_wstring(co_await shared_future) + L"\n"s);
		if (counter.fetch_add(1, std::memory_order_relaxed) == 9)
			SetEvent(event);
	};

	for (int i = 0; i < 10; ++i)
	{
		lambda(i);
	}

	promise.set(42);
	corsl::block_wait(corsl::resume_on_signal{ event });
	CloseHandle(event);
}

//corsl::async_generator<int> test_generator()
//{
//	using namespace corsl::timer;
//	for (int i = 0; i < 10; ++i)
//	{
//		co_await 2s;
//		co_yield i;
//	}
//}

//corsl::future<> test_generator_base()
//{
//	int sum = 0;
//	for (auto x : test_generator())
//	{
//		sum += co_await x;
//	}
//}
//
int main()
{
	//test_generator_base().get();

	{
		corsl::promise<void> promise;

		auto ptest = promise_test_start(promise);
		promise_test_complete(promise);
		ptest.wait();
	}

	test_shared_future();

	sequential_test();
	concurrent_test();

	corsl::block_wait(
		corsl::when_all(
			test_async_queue_producer(),
			test_async_queue_consumer()
		)
	);
}
