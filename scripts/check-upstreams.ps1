# Copyright (C) 2026 KajPS5 contributors
# SPDX-License-Identifier: GPL-2.0-or-later

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$lockPath = Join-Path $repositoryRoot 'UPSTREAMS.lock.json'
$lock = Get-Content -Raw -LiteralPath $lockPath | ConvertFrom-Json
$updates = 0

foreach ($upstream in $lock.upstreams) {
  $remoteLine = & git ls-remote $upstream.repository HEAD
  if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($remoteLine)) {
    throw "Could not read HEAD for $($upstream.name)."
  }

  $remoteCommit = ($remoteLine -split '\s+')[0].ToLowerInvariant()
  $pinnedCommit = ([string]$upstream.commit).ToLowerInvariant()
  if ($remoteCommit -eq $pinnedCommit) {
    Write-Output "upstream.current name=$($upstream.name) commit=$pinnedCommit"
    continue
  }

  ++$updates
  Write-Output (
    "upstream.update name=$($upstream.name) pinned=$pinnedCommit head=$remoteCommit")
}

Write-Output "upstream.summary updates=$updates"
