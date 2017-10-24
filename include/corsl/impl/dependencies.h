//-------------------------------------------------------------------------------------------------------
// corsl - Coroutine Support Library
// Copyright (C) 2017 HHD Software Ltd.
// Written by Alexander Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

#include <atomic>
#include <type_traits>
#include <tuple>
#include <utility>
#include <exception>
#include <memory>
#include <array>
#include <mutex>
#include <string>
#include <functional>
#include <cassert>
#include <experimental/resumable>
#include <variant>

#include <winrt/base.h>
