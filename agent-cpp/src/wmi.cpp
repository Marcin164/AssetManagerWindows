#include "lanventory/wmi.hpp"
#include "lanventory/log.hpp"

#include <windows.h>
#include <wbemidl.h>
#include <comdef.h>
#include <oleauto.h>

#include <atomic>
#include <string>

#pragma comment(lib, "wbemuuid.lib")

namespace lv::wmi {
namespace {

std::atomic<bool> g_com_inited = false;
std::atomic<bool> g_security_inited = false;

std::string wide_to_utf8(const wchar_t* w, int len = -1) {
    if (!w) return {};
    int n = ::WideCharToMultiByte(CP_UTF8, 0, w, len, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<size_t>(len < 0 ? n - 1 : n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w, len, out.data(),
                          static_cast<int>(out.size()), nullptr, nullptr);
    return out;
}

nlohmann::json variant_to_json(const VARIANT& v) {
    using nlohmann::json;
    switch (v.vt) {
        case VT_NULL:
        case VT_EMPTY:
            return nullptr;
        case VT_BOOL:
            return v.boolVal != VARIANT_FALSE;
        case VT_I1: return static_cast<int>(v.cVal);
        case VT_UI1: return static_cast<int>(v.bVal);
        case VT_I2: return static_cast<int>(v.iVal);
        case VT_UI2: return static_cast<unsigned>(v.uiVal);
        case VT_I4: return static_cast<long>(v.lVal);
        case VT_UI4: return static_cast<unsigned long>(v.ulVal);
        case VT_I8: return static_cast<long long>(v.llVal);
        case VT_UI8: return static_cast<unsigned long long>(v.ullVal);
        case VT_INT: return v.intVal;
        case VT_UINT: return v.uintVal;
        case VT_R4: return v.fltVal;
        case VT_R8: return v.dblVal;
        case VT_BSTR:
            return wide_to_utf8(v.bstrVal);
        default:
            break;
    }
    // Arrays (e.g. SAFEARRAY of BSTR/INT) appear with VT_ARRAY bit set.
    if (v.vt & VT_ARRAY) {
        SAFEARRAY* sa = v.parray;
        if (!sa) return json::array();
        LONG lb = 0, ub = -1;
        ::SafeArrayGetLBound(sa, 1, &lb);
        ::SafeArrayGetUBound(sa, 1, &ub);
        const VARTYPE elem_type = v.vt & VT_TYPEMASK;
        json arr = json::array();
        for (LONG i = lb; i <= ub; ++i) {
            if (elem_type == VT_BSTR) {
                BSTR s = nullptr;
                if (SUCCEEDED(::SafeArrayGetElement(sa, &i, &s)) && s) {
                    arr.push_back(wide_to_utf8(s));
                    ::SysFreeString(s);
                } else {
                    arr.push_back(nullptr);
                }
            } else {
                // Generic: fetch as VARIANT and recurse.
                VARIANT elem;
                ::VariantInit(&elem);
                if (SUCCEEDED(::SafeArrayGetElement(sa, &i, &elem))) {
                    arr.push_back(variant_to_json(elem));
                    ::VariantClear(&elem);
                } else {
                    arr.push_back(nullptr);
                }
            }
        }
        return arr;
    }
    return nullptr;
}

class ScopedBstr {
public:
    explicit ScopedBstr(const wchar_t* s) : b_(::SysAllocString(s)) {}
    ~ScopedBstr() { if (b_) ::SysFreeString(b_); }
    operator BSTR() const { return b_; }  // NOLINT
private:
    BSTR b_;
};

}  // namespace

bool initialise_com() {
    if (g_com_inited.load()) return true;
    HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        log::error("wmi", "CoInitializeEx failed");
        return false;
    }
    if (!g_security_inited.exchange(true)) {
        hr = ::CoInitializeSecurity(
            nullptr, -1, nullptr, nullptr,
            RPC_C_AUTHN_LEVEL_DEFAULT,
            RPC_C_IMP_LEVEL_IMPERSONATE,
            nullptr, EOAC_NONE, nullptr);
        if (FAILED(hr) && hr != RPC_E_TOO_LATE) {
            log::warn("wmi", "CoInitializeSecurity failed (continuing)");
        }
    }
    g_com_inited.store(true);
    return true;
}

void uninitialise_com() {
    if (g_com_inited.exchange(false)) {
        ::CoUninitialize();
    }
}

nlohmann::json query(std::wstring_view wql, std::wstring_view namespace_path) {
    using nlohmann::json;
    if (!initialise_com()) return json::array();

    IWbemLocator* locator = nullptr;
    HRESULT hr = ::CoCreateInstance(
        CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
        IID_IWbemLocator, reinterpret_cast<void**>(&locator));
    if (FAILED(hr) || !locator) {
        log::warn("wmi", "CoCreateInstance(WbemLocator) failed");
        return json::array();
    }

    IWbemServices* services = nullptr;
    {
        ScopedBstr ns(std::wstring(namespace_path).c_str());
        hr = locator->ConnectServer(
            ns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &services);
    }
    locator->Release();
    if (FAILED(hr) || !services) {
        log::warn("wmi", "ConnectServer failed");
        return json::array();
    }

    hr = ::CoSetProxyBlanket(
        services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
        RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr, EOAC_NONE);
    if (FAILED(hr)) {
        log::warn("wmi", "CoSetProxyBlanket failed");
    }

    IEnumWbemClassObject* enumerator = nullptr;
    {
        ScopedBstr lang(L"WQL");
        ScopedBstr query_str(std::wstring(wql).c_str());
        hr = services->ExecQuery(
            lang, query_str,
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
            nullptr, &enumerator);
    }
    services->Release();
    if (FAILED(hr) || !enumerator) {
        log::warn("wmi", "ExecQuery failed");
        return json::array();
    }

    json out = json::array();
    while (enumerator) {
        IWbemClassObject* obj = nullptr;
        ULONG returned = 0;
        hr = enumerator->Next(WBEM_INFINITE, 1, &obj, &returned);
        if (FAILED(hr) || returned == 0 || !obj) break;

        obj->BeginEnumeration(WBEM_FLAG_NONSYSTEM_ONLY);
        json row = json::object();
        BSTR  name = nullptr;
        VARIANT value;
        ::VariantInit(&value);
        CIMTYPE type = 0;
        while (obj->Next(0, &name, &value, &type, nullptr) == WBEM_S_NO_ERROR) {
            row[wide_to_utf8(name)] = variant_to_json(value);
            ::VariantClear(&value);
            ::SysFreeString(name);
            name = nullptr;
        }
        obj->EndEnumeration();
        obj->Release();
        out.push_back(std::move(row));
    }
    enumerator->Release();
    return out;
}

nlohmann::json query_first(std::wstring_view wql, std::wstring_view namespace_path) {
    auto rows = query(wql, namespace_path);
    if (rows.is_array() && !rows.empty()) return rows.front();
    return nlohmann::json::object();
}

}  // namespace lv::wmi
