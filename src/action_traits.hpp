#pragma once

#include "action.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

/// @brief Detect whether T is a user-bindable action (has both Name and Description).
template <typename T, typename = void>
struct IsBindable : std::false_type {};

template <typename T>
struct IsBindable<T, std::void_t<decltype(T::Name), decltype(T::Description)>> : std::true_type {};

/// @brief Visitor callback type for for_each_named_action().
using ActionVisitor = void (*)(const char* name, const char* description);

namespace detail {

/// @brief Helper to iterate variant alternatives and find by name.
template <typename Variant, std::size_t I = 0>
std::optional<Action> action_from_name_impl(std::string_view name) {
  if constexpr (I >= std::variant_size_v<Variant>) {
    return std::nullopt;
  }
  else {
    using T = std::variant_alternative_t<I, Variant>;
    if constexpr (IsBindable<T>::value) {
      if (name == T::Name) {
        return T{};
      }
    }
    return action_from_name_impl<Variant, I + 1>(name);
  }
}

/// @brief Helper to iterate variant alternatives and visit named ones.
template <typename Variant, std::size_t I = 0>
void for_each_named_action_impl(ActionVisitor fn) {
  if constexpr (I < std::variant_size_v<Variant>) {
    using T = std::variant_alternative_t<I, Variant>;
    if constexpr (IsBindable<T>::value) {
      fn(T::Name, T::Description);
    }
    for_each_named_action_impl<Variant, I + 1>(fn);
  }
}

} // namespace detail

/// @brief Look up a named action by its Name string.
/// @return A default-constructed Action variant, or nullopt if not found.
inline std::optional<Action> action_from_name(std::string_view name) {
  return detail::action_from_name_impl<Action>(name);
}

/// @brief Visit all named action types in variant order.
inline void for_each_named_action(ActionVisitor fn) {
  detail::for_each_named_action_impl<Action>(fn);
}

/// @brief Normalize an action name: replace hyphens with underscores.
inline std::string normalize_action_name(std::string_view name) {
  std::string result(name);
  std::replace(result.begin(), result.end(), '-', '_');
  return result;
}
