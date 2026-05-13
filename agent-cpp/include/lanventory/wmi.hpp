#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace lv::wmi {

// One-shot WQL query helper. Each row is converted into a JSON object
// with property names as keys. ``namespace_path`` defaults to
// ``ROOT\\CIMV2``. Returns an empty array on any failure (callers can
// still emit the section, just empty).
nlohmann::json query(
    std::wstring_view wql,
    std::wstring_view namespace_path = L"ROOT\\CIMV2");

// Single-row variant -- returns the first row's properties as an object,
// or an empty object on miss.
nlohmann::json query_first(
    std::wstring_view wql,
    std::wstring_view namespace_path = L"ROOT\\CIMV2");

// Initialise / cleanup COM (apartment-threaded). main() owns the
// lifecycle -- scanners assume it's already up.
bool initialise_com();
void uninitialise_com();

}  // namespace lv::wmi
