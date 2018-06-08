#include <napi.h>

#include "windows.h"
#include "winuser.h"
#include "PhysicalMonitorEnumerationAPI.h"
#include "HighLevelMonitorConfigurationAPI.h"
#include "LowLevelMonitorConfigurationAPI.h"

#include <vector>
#include <map>
#include <iostream>
#include <sstream>


BOOL CALLBACK monitorEnumProc (
        HMONITOR hMonitor
      , HDC hdcMonitor
      , LPRECT lprcMonitor
      , LPARAM dwData)
{
    // Get vector from dwData
    std::vector<HMONITOR>* monitorList =
            reinterpret_cast<std::vector<HMONITOR>*>(dwData);

    // Insert monitor handle into vector
    monitorList->push_back(hMonitor);

    return TRUE;
}


std::map<std::string, HANDLE> monitorMap;

void populateMonitorMap() {
    if (!monitorMap.empty()) {
        for (auto const& monitor : monitorMap) {
            DestroyPhysicalMonitor(monitor.second);
        }
        monitorMap.clear();
    }

    std::vector<HMONITOR> monitorHandles;
    EnumDisplayMonitors(NULL, NULL, &monitorEnumProc
          , reinterpret_cast<LPARAM>(&monitorHandles));

    for (auto const& handle : monitorHandles) {
        MONITORINFOEX monitorInfo;
        monitorInfo.cbSize = sizeof(MONITORINFOEX);
        GetMonitorInfo(handle, &monitorInfo);

        DWORD numPhysicalMonitors;
        LPPHYSICAL_MONITOR physicalMonitors = NULL;

        if (!GetNumberOfPhysicalMonitorsFromHMONITOR(handle
              , &numPhysicalMonitors)) {
            throw std::runtime_error("Failed to get number of physical monitors");
            break;
        }

        physicalMonitors = new PHYSICAL_MONITOR[numPhysicalMonitors];
        if (physicalMonitors == NULL) {
            throw std::runtime_error("Failed to allocate monitor array");
            break;
        }

        if (!GetPhysicalMonitorsFromHMONITOR(
                handle, numPhysicalMonitors, physicalMonitors)) {
            throw std::runtime_error("Failed to get physical monitors");
            break;
        }

        std::string monitorName =
                static_cast<std::string>(monitorInfo.szDevice);

        monitorMap.insert({
            monitorName
          , physicalMonitors[0].hPhysicalMonitor
        });

        delete physicalMonitors;
    }
}

Napi::Value refresh(const Napi::CallbackInfo& info)
{
    Napi::Env env = info.Env();

    try {
        populateMonitorMap();
    } catch (std::runtime_error& e) {
        throw Napi::Error::New(env, e.what());
    }

    return env.Undefined();
}


Napi::Array getMonitorList(const Napi::CallbackInfo& info)
{
    Napi::Env env = info.Env();
    Napi::Array ret = Napi::Array::New(env, monitorMap.size());

    int i = 0;
    for (auto const& monitor : monitorMap) {
        ret.Set(i++, monitor.first);
    }

    return ret;
}


Napi::Value setVCP(const Napi::CallbackInfo& info)
{

    Napi::Env env = info.Env();

    if (info.Length() < 3) {
        throw Napi::TypeError::New(env, "Not enough arguments");
    }
    if (!info[0].IsString() || !info[1].IsNumber() || !info[2].IsNumber()) {
        throw Napi::TypeError::New(env, "Invalid arguments");
    }

    std::string monitorName = info[0].As<Napi::String>().Utf8Value();
    BYTE vcpCode = static_cast<BYTE>(info[1].As<Napi::Number>().Int32Value());
    DWORD newValue = static_cast<DWORD>(info[2].As<Napi::Number>().Int32Value());

    auto it = monitorMap.find(monitorName);
    if (it == monitorMap.end()) {
        throw Napi::Error::New(env, "Monitor not found");
    }

    if (!SetVCPFeature(it->second, vcpCode, newValue)) {
        throw Napi::Error::New(env, "Failed to set VCP code value");
    }

    return env.Undefined();
}

Napi::Value getVCP(const Napi::CallbackInfo& info)
{
    Napi::Env env = info.Env();

    if (info.Length() < 2) {
        throw Napi::TypeError::New(env, "Not enough arguments");
    }
    if (!info[0].IsString() || !info[1].IsNumber()) {
        throw Napi::TypeError::New(env, "Invalid arguments");
    }

    std::string monitorName = info[0].As<Napi::String>().Utf8Value();
    BYTE vcpCode = static_cast<BYTE>(info[1].As<Napi::Number>().Int32Value());

    auto it = monitorMap.find(monitorName);
    if (it == monitorMap.end()) {
        throw Napi::Error::New(env, "Monitor not found");
    }

    DWORD currentValue;
    if (!GetVCPFeatureAndVCPFeatureReply(
            it->second, vcpCode, NULL, &currentValue, NULL)) {
        throw Napi::Error::New(env, "Failed to get VCP code value");
    }

    return Napi::Number::New(env, static_cast<double>(currentValue));
}

Napi::Object Init(Napi::Env env, Napi::Object exports)
{
    exports.Set("getMonitorList", Napi::Function::New(env
          , getMonitorList, "getMonitorList"));
    exports.Set("refresh", Napi::Function::New(env
          , refresh, "refresh"));
    exports.Set("setVCP", Napi::Function::New(env
          , setVCP, "setVCP"));
    exports.Set("getVCP", Napi::Function::New(env
          , getVCP, "getVCP"));

    try {
        populateMonitorMap();
    } catch (std::runtime_error& e) {
        throw Napi::Error::New(env, e.what());
    }

    return exports;
}

NODE_API_MODULE(NODE_GYP_MODULE_NAME, Init)
