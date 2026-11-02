/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "SystemVolume.h"
#include <algorithm>
#include <cmath>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
// The SDK's property-key declarations require this include order.
// clang-format off
#include <propkeydef.h>
#include <functiondiscoverykeys_devpkey.h>
// clang-format on
#include <wrl/client.h>
#elif defined(__APPLE__)
#include <AudioToolbox/AudioServices.h>
#include <CoreAudio/CoreAudio.h>
#endif

namespace deskflow::volume {
namespace {
[[maybe_unused]] uint16_t percent(float scalar)
{
  return static_cast<uint16_t>(std::lround(std::clamp(scalar, 0.0f, 1.0f) * Maximum));
}

#if defined(_WIN32)
using Microsoft::WRL::ComPtr;
class Apartment
{
public:
  Apartment() : m_result(CoInitializeEx(nullptr, COINIT_MULTITHREADED))
  {
  }
  ~Apartment()
  {
    if (SUCCEEDED(m_result))
      CoUninitialize();
  }
  bool valid() const
  {
    return SUCCEEDED(m_result) || m_result == RPC_E_CHANGED_MODE;
  }

private:
  HRESULT m_result;
};

struct Endpoint
{
  ComPtr<IMMDevice> device;
  ComPtr<IAudioEndpointVolume> volume;
  QString id;
  QString name;

  bool open()
  {
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(
            CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(enumerator.GetAddressOf()))
        ) ||
        FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device)))
      return false;
    LPWSTR deviceId = nullptr;
    if (FAILED(device->GetId(&deviceId)))
      return false;
    id = QString::fromWCharArray(deviceId);
    CoTaskMemFree(deviceId);
    ComPtr<IPropertyStore> properties;
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties))) {
      PROPVARIANT value{};
      if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR)
        name = QString::fromWCharArray(value.pwszVal);
      PropVariantClear(&value);
    }
    device->Activate(
        __uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, reinterpret_cast<void **>(volume.GetAddressOf())
    );
    return true;
  }
};

// Identifies our writes to other Core Audio notification consumers. We observe
// through bounded polling/readback, keeping all COM objects on the calling thread.
const GUID volumeContext = {0x7e0a4a6c, 0x3231, 0x4d5e, {0x8b, 0x97, 0x46, 0xa9, 0x31, 0x15, 0x72, 0x20}};

class NativeControl : public Control
{
public:
  std::optional<Snapshot> read() override
  {
    Apartment apartment;
    Endpoint endpoint;
    if (!apartment.valid() || !endpoint.open())
      return std::nullopt;
    Snapshot result{endpoint.id, endpoint.name, {}, 0};
    if (!endpoint.volume)
      return result;
    float level = 0;
    BOOL muted = FALSE;
    if (SUCCEEDED(endpoint.volume->GetMasterVolumeLevelScalar(&level)) && std::isfinite(level)) {
      result.state.volume = percent(level);
      result.capabilities |= Volume;
    }
    if (SUCCEEDED(endpoint.volume->GetMute(&muted))) {
      result.state.muted = muted != FALSE;
      result.capabilities |= Mute;
    }
    return result;
  }

  bool write(const QString &expectedDevice, State state, uint8_t mask) override
  {
    if (state.volume > Maximum || mask == 0 || (mask & ~3) != 0)
      return false;
    Apartment apartment;
    Endpoint endpoint;
    if (!apartment.valid() || !endpoint.open() || endpoint.id != expectedDevice || !endpoint.volume)
      return false;
    // Mute before changing level; set the new level before unmuting.
    if ((mask & Mute) && state.muted && FAILED(endpoint.volume->SetMute(TRUE, &volumeContext)))
      return false;
    if ((mask & Volume) &&
        FAILED(endpoint.volume->SetMasterVolumeLevelScalar(float(state.volume) / Maximum, &volumeContext)))
      return false;
    if ((mask & Mute) && !state.muted && FAILED(endpoint.volume->SetMute(FALSE, &volumeContext)))
      return false;
    return true;
  }
};
#elif defined(__APPLE__)
AudioObjectPropertyAddress address(AudioObjectPropertySelector selector, AudioObjectPropertyScope scope)
{
  return {selector, scope, kAudioObjectPropertyElementMain};
}

AudioDeviceID defaultOutput()
{
  auto property = address(kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal);
  AudioDeviceID device = kAudioObjectUnknown;
  UInt32 bytes = sizeof(device);
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &property, 0, nullptr, &bytes, &device) != noErr)
    return kAudioObjectUnknown;
  return device;
}

QString stringProperty(AudioDeviceID device, AudioObjectPropertySelector selector)
{
  auto property = address(selector, kAudioObjectPropertyScopeGlobal);
  CFStringRef value = nullptr;
  UInt32 bytes = sizeof(value);
  if (AudioObjectGetPropertyData(device, &property, 0, nullptr, &bytes, &value) != noErr || !value)
    return {};
  const auto result = QString::fromCFString(value);
  CFRelease(value);
  return result;
}

struct VolumeProperty
{
  AudioObjectPropertyAddress property;
  bool virtualControl = false;
};

std::optional<VolumeProperty> volumeProperty(AudioDeviceID device)
{
  auto property = address(kAudioHardwareServiceDeviceProperty_VirtualMainVolume, kAudioDevicePropertyScopeOutput);
  Boolean writable = false;
  if (AudioHardwareServiceHasProperty(device, &property) &&
      AudioHardwareServiceIsPropertySettable(device, &property, &writable) == noErr && writable)
    return VolumeProperty{property, true};
  property.mSelector = kAudioDevicePropertyVolumeScalar;
  if (AudioObjectHasProperty(device, &property) &&
      AudioObjectIsPropertySettable(device, &property, &writable) == noErr && writable)
    return VolumeProperty{property, false};
  return std::nullopt;
}

QString deviceIdentity(AudioDeviceID device)
{
  return QString::number(device) + ":" + stringProperty(device, kAudioDevicePropertyDeviceUID);
}

class NativeControl : public Control
{
public:
  std::optional<Snapshot> read() override
  {
    const auto device = defaultOutput();
    if (device == kAudioObjectUnknown)
      return std::nullopt;
    Snapshot result{deviceIdentity(device), stringProperty(device, kAudioObjectPropertyName), {}, 0};
    if (const auto control = volumeProperty(device)) {
      Float32 value = 0;
      UInt32 bytes = sizeof(value);
      const auto error =
          control->virtualControl
              ? AudioHardwareServiceGetPropertyData(device, &control->property, 0, nullptr, &bytes, &value)
              : AudioObjectGetPropertyData(device, &control->property, 0, nullptr, &bytes, &value);
      if (error == noErr && std::isfinite(value)) {
        result.state.volume = percent(value);
        result.capabilities |= Volume;
      }
    }
    auto mute = address(kAudioDevicePropertyMute, kAudioDevicePropertyScopeOutput);
    Boolean writable = false;
    UInt32 value = 0, bytes = sizeof(value);
    if (AudioObjectHasProperty(device, &mute) && AudioObjectIsPropertySettable(device, &mute, &writable) == noErr &&
        writable && AudioObjectGetPropertyData(device, &mute, 0, nullptr, &bytes, &value) == noErr) {
      result.state.muted = value != 0;
      result.capabilities |= Mute;
    }
    return result;
  }

  bool write(const QString &expectedDevice, State state, uint8_t mask) override
  {
    if (state.volume > Maximum || mask == 0 || (mask & ~3) != 0)
      return false;
    const auto device = defaultOutput();
    if (device == kAudioObjectUnknown || deviceIdentity(device) != expectedDevice)
      return false;
    auto mute = address(kAudioDevicePropertyMute, kAudioDevicePropertyScopeOutput);
    UInt32 muted = state.muted ? 1 : 0;
    if ((mask & Mute) && state.muted &&
        AudioObjectSetPropertyData(device, &mute, 0, nullptr, sizeof(muted), &muted) != noErr)
      return false;
    if (mask & Volume) {
      const auto control = volumeProperty(device);
      if (!control)
        return false;
      const Float32 value = Float32(state.volume) / Maximum;
      const auto error =
          control->virtualControl
              ? AudioHardwareServiceSetPropertyData(device, &control->property, 0, nullptr, sizeof(value), &value)
              : AudioObjectSetPropertyData(device, &control->property, 0, nullptr, sizeof(value), &value);
      if (error != noErr)
        return false;
    }
    return !(mask & Mute) || state.muted ||
           AudioObjectSetPropertyData(device, &mute, 0, nullptr, sizeof(muted), &muted) == noErr;
  }
};
#else
class NativeControl : public Control
{
public:
  std::optional<Snapshot> read() override
  {
    return std::nullopt;
  }
  bool write(const QString &, State, uint8_t) override
  {
    return false;
  }
};
#endif
} // namespace

bool systemVolumeSupported()
{
#if defined(_WIN32) || defined(__APPLE__)
  return true;
#else
  return false;
#endif
}

std::unique_ptr<Control> makeSystemVolumeControl()
{
  return std::make_unique<NativeControl>();
}
} // namespace deskflow::volume
