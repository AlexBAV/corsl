//-------------------------------------------------------------------------------------------------------
// corsl - Coroutine Support Library
// Copyright (C) 2017 HHD Software Ltd.
// Written by Alexander Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

#include "dependencies.h"
#include <winrt/Windows.Foundation.h>

namespace corsl
{
	namespace details
	{
		// forward declarations
		template<class T>
		class future;

		template<class T>
		class shared_future;

		// no_result will substitute 'void' in tuple
		struct no_result {};

		// get_result_type
		// get the coroutine result type
		// supports IAsyncAction, IAsyncOperation<T> and awaitable object that implements await_resume
		// yields result_type<T> where T is a coroutine result type
		template<class T>
		struct result_type
		{
			using type = T;
		};

		// Get real type back from result_type<T>
		template<class T>
		using invoke_result = typename T::type;

		// Overloads for future and shared_future
		constexpr result_type<void> get_result_type(const future<void> &)
		{
			return {};
		}

		constexpr result_type<void> get_result_type(const shared_future<void> &)
		{
			return {};
		}

		template<class T>
		constexpr result_type<T> get_result_type(const future<T> &)
		{
			return {};
		}

		template<class T>
		constexpr result_type<T> get_result_type(const shared_future<T> &)
		{
			return {};
		}

		// overload for IAsyncAction
		constexpr result_type<void> get_result_type(const ::winrt::Windows::Foundation::IAsyncAction &)
		{
			return {};
		}

		// overload for IAsyncOperation<T>
		template<class T>
		constexpr result_type<T> get_result_type(const ::winrt::Windows::Foundation::IAsyncOperation<T> &)
		{
			return {};
		}

		// overload for IAsyncOperationWithProgress<T, V>
		template<class T, class V>
		constexpr result_type<T> get_result_type(const ::winrt::Windows::Foundation::IAsyncOperationWithProgress<T, V> &)
		{
			return {};
		}

		template<class T>
		concept has_await_resume = requires(T & v)
		{
			v.await_resume();
		};

		template<class T>
		concept has_external_await_resume = requires(T & v)
		{
			await_resume(v);
		};

		//
		template<has_await_resume T>
		constexpr result_type<decltype(std::declval<T &>().await_resume())> get_result_type(const T &)
		{
			return {};
		}

		template<has_external_await_resume T>
		constexpr result_type<decltype(await_resume(std::declval<T &>()))> get_result_type(const T &)
		{
			return {};
		}

		// Helper to get a coroutine result type for a first item in variadic sequence
		template<class First, class...Rest>
		constexpr auto get_first_result_type(const First &first, const Rest &...)
		{
			return get_result_type(first);
		}

		// Get a first type in a variadic type list
		template<class...T>
		struct get_first;

		template<class F, class...R>
		struct get_first<F, R...>
		{
			using type = F;
		};

		template<class...T>
		using get_first_t = typename get_first<T...>::type;

		template<class RT>
		struct pure_result_type
		{
			using type = result_type<std::decay_t<typename RT::type>>;
		};

		template<class RT>
		using pure_result_type_t = typename pure_result_type<RT>::type;

		template<class...Ts>
		struct are_all_same
		{
			using first_type = pure_result_type_t<get_first_t<Ts...>>;

			using type = std::conjunction<std::is_same<first_type, pure_result_type_t<Ts>>...>;
		};

		template<class...Ts>
		using are_all_same_t = typename are_all_same<Ts...>::type;

		template<class...Ts>
		constexpr bool are_all_same_v = are_all_same<Ts...>::type::value;
	}

	using details::no_result;
}
