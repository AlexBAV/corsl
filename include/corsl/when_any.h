//-------------------------------------------------------------------------------------------------------
// corsl - Coroutine Support Library
// Copyright (C) 2017 - 2022 HHD Software Ltd.
// Written by Alexander Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

#include "impl/when_all_when_any_base.h"

namespace corsl
{
	namespace details
	{
		///////////////////////////////////
		// when_any
		template<class Result>
		struct when_any_block
		{
			std::atomic<std::coroutine_handle<>> resume{};

			Result result;
			std::exception_ptr exception;
			size_t index;

			void finished_exception() noexcept
			{
				if (auto continuation = resume.exchange(nullptr, std::memory_order_relaxed))
				{
					exception = std::current_exception();
					continuation();
				}
			}

			void finished(size_t index_) noexcept
			{
				if (auto continuation = resume.exchange(nullptr, std::memory_order_relaxed))
				{
					index = index_;
					continuation();
				}
			}

			template<class V>
			void finished(V &&result_, size_t index_) noexcept
			{
				if (auto continuation = resume.exchange(nullptr, std::memory_order_relaxed))
				{
					result = std::forward<V>(result_);
					index = index_;
					continuation();
				}
			}
		};

		template<class T, class Awaitable>
		inline fire_and_forget<> when_any_helper_single(std::shared_ptr<when_any_block<T>> master, Awaitable task, size_t index) noexcept
		{
			try
			{
				if constexpr (std::same_as<result_type<void>, get_result_type_t<Awaitable>>)
				{
					co_await task;
					master->finished(index);
				}
				else
				{
					master->finished(co_await task, index);
				}
			}
			catch (...)
			{
				master->finished_exception();
			}
		}

		template<class T, size_t N, class Tuple, size_t...I>
		inline void when_any_helper(std::array<std::shared_ptr<when_any_block<T>>, N> &&master, Tuple &&tuple, std::index_sequence<I...>) noexcept
		{
			(..., when_any_helper_single<T>(std::get<I>(std::move(master)), std::get<I>(std::move(tuple)), I));
		}

		template<class Result, class... Awaitables>
		struct when_any_awaitable
		{
			using any_block_t = when_any_block<Result>;
			static constexpr size_t N = sizeof...(Awaitables);
			static constexpr bool is_void = std::same_as<Result, result_type<void>>;

			std::shared_ptr<any_block_t> ptr{ std::make_shared<any_block_t>() };
			std::tuple<std::decay_t<Awaitables>...> awaitables;

			// If you see the compilation error on the constructor, check if you are trying to pass corsl::future as NOT an rvalue
			template<class...Args>
			when_any_awaitable(Args &&...args) noexcept :
				awaitables{ std::forward<Args>(args)... }
			{}

			bool await_ready() const noexcept
			{
				return false;
			}

			void await_suspend(std::coroutine_handle<> handle)
			{
				ptr->resume.store(handle, std::memory_order_relaxed);
				std::array<std::shared_ptr<any_block_t>, N> references;
				std::fill(references.begin(), references.end(), ptr);

				auto awaitables_copy = std::move(awaitables);
				using index_t = std::make_index_sequence<N>;
				when_any_helper(std::move(references), std::move(awaitables_copy), index_t{});
			}

			size_t iresume() const requires is_void
			{
				return ptr->index;
			}

			std::pair<size_t, Result> iresume() const requires !is_void
			{
				return { ptr->index, std::move(ptr->result) };
			}

			auto await_resume() const
			{
				if (ptr->exception)
					std::rethrow_exception(ptr->exception);
				else
					return iresume();
			}
		};

		template<class Result, class...Awaitables>
		inline auto when_any_impl(Awaitables &&...awaitables)
		{
			return when_any_awaitable<Result, Awaitables...>{ std::forward<Awaitables>(awaitables)... };
		}

		template<class T>
		using safe_invoke_result = std::conditional_t<std::same_as<T, result_type<void>>, no_result, invoke_result<T>>;

		template<class...Awaitables>
		inline auto when_any(Awaitables &&...awaitables)
		{
			static_assert(sizeof...(Awaitables) >= 2, "when_any must be passed at least two arguments");

			using namespace boost::mp11;
			using result_types = mp_list<get_result_type_t<Awaitables>...>;

			if constexpr (mp_apply<mp_same, result_types>{})
			{
				if constexpr(std::same_as<void, invoke_result<mp_first<result_types>>>)
					return when_any_impl<no_result>(std::forward<Awaitables>(awaitables)...);
				else
					return when_any_impl<safe_invoke_result<mp_first<result_types>>>(std::forward<Awaitables>(awaitables)...);
			}
			else
			{
				using filtered_types = mp_transform<invoke_result, mp_unique<mp_remove<result_types, result_type<void>>>>;
				using value_type = std::conditional_t<mp_size<filtered_types>::value == 1, mp_first<filtered_types>, mp_apply<std::variant, filtered_types>>;
				return when_any_impl<value_type>(std::forward<Awaitables>(awaitables)...);
			}
		}

		// range
		template<class Result, class Awaitable>
		struct when_any_awaitable_range
		{
			static constexpr const bool is_void = std::same_as<Result, result_type<void>>;
			static constexpr const bool is_copyable = std::copyable<Awaitable>;
			using when_any_block = when_any_block<safe_invoke_result<Result>>;
			using T = invoke_result<Result>;

			std::shared_ptr<when_any_block> ptr{ std::make_shared<when_any_block>() };
			std::vector<Awaitable> awaitables;

			template<class Range>
			when_any_awaitable_range(Range &&range) requires !is_copyable :
				awaitables{ std::make_move_iterator(sr::begin(range)), std::make_move_iterator(sr::end(range)) }
			{
			}

			template<class Range>
			when_any_awaitable_range(Range &&range) requires is_copyable :
				awaitables{ sr::begin(range), sr::end(range) }
			{
			}

			bool await_ready() const noexcept
			{
				return awaitables.empty();
			}

			void await_suspend(std::coroutine_handle<> handle)
			{
				ptr->resume.store(handle, std::memory_order_relaxed);
				std::vector<std::shared_ptr<when_any_block>> references(awaitables.size(), ptr);

				auto awaitables_local_copy = std::move(awaitables);
				for (size_t i = 0; i < references.size(); ++i)
					when_any_helper_single(std::move(references[i]), std::move(awaitables_local_copy[i]), i);
			}

			size_t await_resume() const requires is_void
			{
				if (ptr->exception)
					std::rethrow_exception(ptr->exception);
				else
					return ptr->index;
			}

			std::pair<T, size_t> await_resume() const requires !is_void
			{
				if (ptr->exception)
					std::rethrow_exception(ptr->exception);
				else
					return std::pair { std::move(ptr->result), ptr->index };
			}
		};

		template<typename Result, sr::range Range>
		inline auto range_when_any_impl(Result, Range &&range)
		{
			return when_any_awaitable_range<Result, sr::range_value_t<Range>>{ std::forward<Range>(range) };
		}


		//
		template<sr::range Range>
		inline auto when_any_range(Range &&range)
		{
			using future_type = sr::range_value_t<Range>;
			return range_when_any_impl(get_result_type_t<future_type>{}, std::move(range));
		}
	}

	using details::when_any;
	using details::when_any_range;
}
