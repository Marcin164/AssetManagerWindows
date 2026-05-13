"""Security section -- matches Antivirus/Bitlocker/Firewall/RDP/TPM.tsx."""

from __future__ import annotations

from typing import Any

from .helpers import as_list, run_powershell, safe


_AV_PS = r"""
Get-CimInstance -Namespace 'root\SecurityCenter2' -ClassName AntiVirusProduct -ErrorAction SilentlyContinue |
  ForEach-Object {
    [pscustomobject]@{
      displayName = $_.displayName; productState = $_.productState
      pathToSignedProductExe = $_.pathToSignedProductExe
    }
  }
"""

_BITLOCKER_PS = r"""
try {
  Get-BitLockerVolume -ErrorAction Stop | ForEach-Object {
    [pscustomobject]@{
      MountPoint = $_.MountPoint; CapacityGB = [double]$_.CapacityGB
      VolumeType = [int]$_.VolumeType
      LockStatus = if ($_.LockStatus -eq 'Locked') { 1 } else { 0 }
      ProtectionStatus = [int]$_.ProtectionStatus
      EncryptionMethod = [int]$_.EncryptionMethod
      VolumeStatus = "$($_.VolumeStatus)"
    }
  }
} catch { @() }
"""

_FIREWALL_PROFILE_PS = r"""
Get-NetFirewallProfile -ErrorAction SilentlyContinue | ForEach-Object {
  [pscustomobject]@{
    Name = "$($_.Name)"
    Enabled = if ($_.Enabled -eq 'True') { 1 } else { 0 }
    AllowUserApps = if ($_.AllowUserApps -eq 'True') { 1 } else { 0 }
    AllowUserPorts = if ($_.AllowUserPorts -eq 'True') { 1 } else { 0 }
    AllowInboundRules = if ($_.AllowInboundRules -eq 'True') { 1 } else { 0 }
  }
}
"""

_RDP_PS = r"""
$reg = Get-ItemProperty 'HKLM:\System\CurrentControlSet\Control\Terminal Server' -ErrorAction SilentlyContinue
$enabled = if ($reg -and $reg.fDenyTSConnections -eq 0) { $true } else { $false }
[pscustomobject]@{ RDP_Enabled = $enabled }
"""

_TPM_PS = r"""
try {
  $tpm = Get-CimInstance -Namespace 'root\cimv2\Security\MicrosoftTpm' -ClassName Win32_Tpm -ErrorAction Stop | Select-Object -First 1
  if ($tpm) {
    [pscustomobject]@{
      SpecVersion = $tpm.SpecVersion
      ManufacturerIDTxt = $tpm.ManufacturerIdTxt
      ManufacturerVersion = $tpm.ManufacturerVersion
      IsOwned_InitialValue = $tpm.IsOwned_InitialValue
      IsEnabled_InitialValue = $tpm.IsEnabled_InitialValue
      IsActivated_InitialValue = $tpm.IsActivated_InitialValue
    }
  }
} catch {}
"""

_UPDATES_PS = r"""
Get-HotFix -ErrorAction SilentlyContinue | ForEach-Object {
  [pscustomobject]@{
    hotfix_id = $_.HotFixID; description = $_.Description
    installedOn = if ($_.InstalledOn) { $_.InstalledOn.ToString('yyyy-MM-dd') } else { $null }
  }
}
"""


def collect_security() -> dict[str, Any]:
    return {
        "antivirus":        as_list(safe(run_powershell, [], _AV_PS)),
        "bitlocker":        as_list(safe(run_powershell, [], _BITLOCKER_PS)),
        "firewall_profile": as_list(safe(run_powershell, [], _FIREWALL_PROFILE_PS)),
        "rdp_status":       safe(run_powershell, None, _RDP_PS) or {"RDP_Enabled": False},
        "tpm":              safe(run_powershell, None, _TPM_PS) or {},
        "uac_status":       {},
        "updates":          as_list(safe(run_powershell, [], _UPDATES_PS, timeout=120)),
        "startup_apps":     [],
    }
