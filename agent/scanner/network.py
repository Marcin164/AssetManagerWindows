"""Network section -- matches Network.tsx."""

from __future__ import annotations

from typing import Any

import psutil

from .helpers import as_list, run_powershell, safe


_NIC_PRELUDE = r"""
function ConvertFrom-CidrToMask([int]$cidr) {
  $bin = ('1' * $cidr) + ('0' * (32 - $cidr))
  ($bin -split '(.{8})' | Where-Object { $_ } |
    ForEach-Object { [Convert]::ToInt32($_,2) }) -join '.'
}
"""

_NIC_CONFIG_PS = r"""
Get-NetIPConfiguration -All | ForEach-Object {
  $alias = $_.InterfaceAlias
  $ipv4  = $_.IPv4Address | Select-Object -First 1
  $ipv6  = $_.IPv6Address | Select-Object -First 1
  $linkLocal = $_.IPv6LinkLocalAddress | Select-Object -First 1
  $gw4 = $_.IPv4DefaultGateway | Select-Object -First 1
  $gw6 = $_.IPv6DefaultGateway | Select-Object -First 1
  $adapter = Get-NetAdapter -Name $alias -ErrorAction SilentlyContinue
  $netipv4 = Get-NetIPAddress -InterfaceAlias $alias -AddressFamily IPv4 -ErrorAction SilentlyContinue | Select-Object -First 1
  $nicCfg = Get-CimInstance Win32_NetworkAdapterConfiguration | Where-Object { $_.Description -eq $adapter.InterfaceDescription } | Select-Object -First 1

  [pscustomobject]@{
    InterfaceAlias = $alias
    InterfaceDescription = $_.InterfaceDescription
    'Speed(Mbps)' = if ($adapter) { [int64]([math]::Round($adapter.LinkSpeed/1e6)) } else { 0 }
    Connected = if ($adapter) { $adapter.Status -eq 'Up' } else { $false }
    DHCP = if ($nicCfg -and $nicCfg.DHCPEnabled) { 1 } else { 0 }
    IPv4Address = if ($ipv4) { $ipv4.IPAddress } else { $null }
    NetMask = if ($netipv4) { (ConvertFrom-CidrToMask $netipv4.PrefixLength) } else { $null }
    IPv4Gateway = if ($gw4) { $gw4.NextHop } else { $null }
    IPv6Address = if ($ipv6) { $ipv6.IPAddress } else { $null }
    IPv6Gateway = if ($gw6) { $gw6.NextHop } else { $null }
    IPv6LinkLocal = if ($linkLocal) { $linkLocal.IPAddress } else { $null }
    DNSServers = @{ value = @($_.DNSServer.ServerAddresses) }
    MAC = if ($adapter) { $adapter.MacAddress } else { $null }
  }
}
"""

_FIREWALL_RULES_PS = r"""
Get-NetFirewallRule -ErrorAction SilentlyContinue |
  Where-Object { $_.Enabled -eq 'True' } |
  ForEach-Object {
    [pscustomobject]@{
      Name = $_.Name; DisplayName = $_.DisplayName
      Enabled = if ($_.Enabled -eq 'True') { 1 } else { 0 }
      Profile = "$($_.Profile)"; Direction = "$($_.Direction)"
      Action = "$($_.Action)"
    }
  }
"""

_MAPPED_DRIVES_PS = r"""
Get-WmiObject -Class Win32_MappedLogicalDisk -ErrorAction SilentlyContinue |
  ForEach-Object {
    [pscustomobject]@{
      DriveLetter  = $_.Name
      RemotePath   = $_.ProviderName
      FileSystem   = $_.FileSystem
      SizeGB       = if ($_.Size)     { [math]::Round($_.Size     / 1GB, 2) } else { $null }
      FreeGB       = if ($_.FreeSpace){ [math]::Round($_.FreeSpace/ 1GB, 2) } else { $null }
    }
  }
"""

_SHARES_PS = r"""
Get-SmbShare -ErrorAction SilentlyContinue |
  Where-Object { $_.Special -eq $false } |
  ForEach-Object {
    [pscustomobject]@{
      Name         = $_.Name
      Path         = $_.Path
      Description  = $_.Description
      CurrentUsers = $_.CurrentUsers
    }
  }
"""


def _collect_adapters() -> dict[str, list[dict[str, Any]]]:
    out: dict[str, list[dict[str, Any]]] = {}
    for name, addrs in psutil.net_if_addrs().items():
        out[name] = [{
            "family": str(a.family), "address": a.address,
            "netmask": a.netmask, "broadcast": a.broadcast, "ptp": a.ptp,
        } for a in addrs]
    return out


def _collect_net_stats() -> dict[str, dict[str, Any]]:
    counters = psutil.net_io_counters(pernic=True)
    return {
        nic: {
            "bytes_sent":    c.bytes_sent,
            "bytes_recv":    c.bytes_recv,
            "packets_sent":  c.packets_sent,
            "packets_recv":  c.packets_recv,
            "errin":         c.errin,
            "errout":        c.errout,
            "dropin":        c.dropin,
            "dropout":       c.dropout,
        }
        for nic, c in counters.items()
    }


def _collect_connections() -> list[dict[str, Any]]:
    conns: list[dict[str, Any]] = []
    pids: dict[int, str] = {}
    for c in psutil.net_connections(kind="inet"):
        pid = c.pid or 0
        if pid and pid not in pids:
            try:
                pids[pid] = psutil.Process(pid).name()
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                pids[pid] = ""
        conns.append({
            "pid": pid, "process_name": pids.get(pid, ""),
            "family": str(c.family), "type": str(c.type), "status": c.status,
            "laddr": f"{c.laddr.ip}:{c.laddr.port}" if c.laddr else "",
            "raddr": f"{c.raddr.ip}:{c.raddr.port}" if c.raddr else "",
        })
    return conns


def collect_network() -> dict[str, Any]:
    return {
        "nic_config":     as_list(safe(run_powershell, [], _NIC_PRELUDE + _NIC_CONFIG_PS, timeout=120)),
        "adapters":       safe(_collect_adapters, {}),
        "connections":    safe(_collect_connections, []),
        "firewall_rules": as_list(safe(run_powershell, [], _FIREWALL_RULES_PS, timeout=120)),
        "mapped_drives":  as_list(safe(run_powershell, [], _MAPPED_DRIVES_PS, timeout=30)),
        "shares":         as_list(safe(run_powershell, [], _SHARES_PS, timeout=30)),
        "net_stats":      safe(_collect_net_stats, {}),
    }
