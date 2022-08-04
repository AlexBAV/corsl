//-------------------------------------------------------------------------------------------------------
// corsl - Coroutine Support Library
// Copyright (C) 2017 - 2022 HHD Software Ltd.
// Written by Alexander Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

#include "dependencies.h"

#include <winrt/Windows.Foundation.h>
#include <boost/mp11/list.hpp>
#include <boost/mp11/algorithm.hpp>

namespace corsl
{
	namespace details
	{
		namespace sr = std::ranges;
		namespace rv = std::views;
		namespace mp11 = boost::mp11;

		// forward declarations
		template<class T>
		class future;

		template<class T>
		class shared_future;

		// no_result will substitute 'void' in tuple
		struct no_result {};

		// get_result_type

		template<class T>
		struct result_type
		{
			using type = T;
		};

// get the coroutine result type
// supports awaitable types that implement await_resume or have external await_resume defined
// yields result_type<T> where T is a coroutine result type
// Get real type back from result_type<T>
		template<class T>
		concept has_await_resume = requires(T &v)
		{
			v.await_resume();
		} || requires(const T & cv)
		{
			cv.await_resume();
		};

		template<class T>
		concept has_external_await_resume = requires(T & v)
		{
			await_resume(v);
		} || requires(const T & cv)
		{
			await_resume(cv);
		};

		template<class Awaitable>
		struct get_result_type;

		template<class T>
		using get_result_type_t = typename get_result_type<T>::type;

		template<class T>
		using invoke_result = typename T::type;

		//
		template<has_await_resume T>
		struct get_result_type<T>
		{
			using type = result_type<std::decay_t<decltype(std::declval<T &>().await_resume())>>;
		};

		template<has_external_await_resume T>
		struct get_result_type<T>
		{
			using type = result_type<std::decay_t<decltype(await_resume(std::declval<T &>()))>>;
		};
	}

	using details::no_result;
}
