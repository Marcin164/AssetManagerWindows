#include "lanventory/scanner.hpp"
#include "lanventory/wmi.hpp"

namespace lv::scanner {

nlohmann::json collect_users() {
    nlohmann::json out;

    auto rename = [](const nlohmann::json& rows,
                     std::initializer_list<std::pair<const char*, const char*>> pairs) {
        nlohmann::json arr = nlohmann::json::array();
        for (auto& r : rows) {
            nlohmann::json m = nlohmann::json::object();
            for (auto& [from, to] : pairs) {
                if (r.contains(from)) m[to] = r[from];
            }
            arr.push_back(m);
        }
        return arr;
    };

    out["local_users"] = rename(wmi::query(
        L"SELECT Name, FullName, AccountType, Disabled, Lockout, "
        L"PasswordChangeable, PasswordExpires, PasswordRequired, SID, Status "
        L"FROM Win32_UserAccount WHERE LocalAccount=True"),
        {{"Name",               "Name"},
         {"FullName",           "FullName"},
         {"AccountType",        "AccountType"},
         {"Disabled",           "Disabled"},
         {"Lockout",            "Lockout"},
         {"PasswordChangeable", "PasswordChangeable"},
         {"PasswordExpires",    "PasswordExpires"},
         {"PasswordRequired",   "PasswordRequired"},
         {"SID",                "SID"},
         {"Status",             "Status"}});

    out["local_groups"] = rename(wmi::query(
        L"SELECT Name, Description, Status, SID "
        L"FROM Win32_Group WHERE LocalAccount=True"),
        {{"Name",        "Name"},
         {"Description", "Description"},
         {"Status",      "Status"},
         {"SID",         "SID"}});

    out["users_profiles"] = rename(wmi::query(
        L"SELECT LocalPath, LastUseTime, Loaded, Special, SID, Status "
        L"FROM Win32_UserProfile"),
        {{"LocalPath",   "LocalPath"},
         {"LastUseTime", "LastUseTime"},
         {"Loaded",      "Loaded"},
         {"Special",     "Special"},
         {"SID",         "SID"},
         {"Status",      "Status"}});

    // HealthStatus is on the entity in newer Windows; we default to 1
    // (Healthy) since WMI may not expose it on all SKUs.
    for (auto& p : out["users_profiles"]) {
        if (!p.contains("HealthStatus")) p["HealthStatus"] = 1;
    }
    return out;
}

}  // namespace lv::scanner
