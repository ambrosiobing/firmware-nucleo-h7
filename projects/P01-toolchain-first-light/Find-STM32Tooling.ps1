<#
.SYNOPSIS
    Inventory every STM32, ST-LINK, Cube and SEGGER tool on a Windows machine.

.DESCRIPTION
    P01 owns the toolchain question for this volume, so this lives here.

    It answers one thing: can this machine build and flash the NUCLEO-H7A3ZI-Q.
    Three answers matter, and they are independent:

      arm-none-eabi-gcc   can it COMPILE. STM32CubeIDE and STM32CubeCLT each
                          bundle their own copy, usually not on PATH, so the
                          search looks inside the install roots rather than
                          trusting PATH.
      a flashing tool     can it FLASH by command. STM32_Programmer_CLI, openocd
                          or a SEGGER server. NONE OF THESE IS REQUIRED: the
                          ST-LINK presents a mass storage disk and a .bin dragged
                          onto it is flashed, which is why the removable-disk
                          section at the end is part of the answer.
      ninja or make       can CMake drive a build at all.

    Reports absence as plainly as presence, because an absent tool is a fact and
    a blank line is not.

    One deliberate exclusion. Java's JDK ships jlink.exe, the module linker,
    which has nothing to do with SEGGER's J-Link. It is reported as ignored and
    named, rather than counted as a debug probe or silently dropped.

.NOTES
    Written Friday 2 October 2026. Run on win11 skyhorizon, the demo laptop,
    which has CubeMX and CubeIDE; win11 aquamarine, the authoring laptop, has
    neither and this script reports that correctly, which is how it was tested.
    Read-only: it starts no service, installs nothing and changes no setting.
#>

$ErrorActionPreference='SilentlyContinue'
Write-Host "=== STM32 tooling inventory ===" -ForegroundColor Cyan
Write-Host ("host {0}   {1}" -f $env:COMPUTERNAME,(Get-Date -Format 'dddd dd MMMM yyyy HH:mm'))
Write-Host ''

Write-Host '--- on PATH ---' -ForegroundColor Yellow
'arm-none-eabi-gcc','arm-none-eabi-gdb','arm-none-eabi-size','arm-none-eabi-objcopy','STM32_Programmer_CLI','ST-LINK_CLI','openocd','JLink.exe','JLinkGDBServer','probe-rs','cmake','ninja','make','gcc','python' | ForEach-Object {
  $c = Get-Command $_ -ErrorAction SilentlyContinue
  # Java's jlink.exe is the JDK module linker and has nothing to do with SEGGER.
  if ($c -and $c.Source -match 'jdk|Adoptium|Java') { Write-Host ("  ignored {0,-21} {1}  (Java tool, not SEGGER)" -f $_,$c.Source) }
  elseif ($c) { Write-Host ("  FOUND   {0,-21} {1}" -f $_,$c.Source) }
  else { Write-Host ("  absent  {0}" -f $_) }
}
Write-Host ''

Write-Host '--- install roots ---' -ForegroundColor Yellow
$roots = @()
'C:\ST','C:\Program Files\STMicroelectronics','C:\Program Files (x86)\STMicroelectronics',
'C:\Program Files\SEGGER','C:\Program Files (x86)\SEGGER','C:\Program Files\stlink',
'C:\ProgramData\STMicroelectronics','C:\Program Files\STM32CubeCLT','C:\Program Files\STM32CubeIDE' | ForEach-Object {
  if (Test-Path $_) {
    $roots += $_
    Write-Host ("  FOUND  {0}" -f $_)
    Get-ChildItem $_ -Directory | Select-Object -First 12 | ForEach-Object { Write-Host ("           {0}" -f $_.Name) }
  } else { Write-Host ("  absent {0}" -f $_) }
}
Write-Host ''

Write-Host '--- the executables that matter, one pass over the roots that exist ---' -ForegroundColor Yellow
if ($roots.Count -eq 0) {
  Write-Host '  no ST or SEGGER root exists, so nothing to search'
} else {
  $want = 'arm-none-eabi-gcc.exe','arm-none-eabi-gdb.exe','arm-none-eabi-size.exe','arm-none-eabi-objcopy.exe',
          'STM32_Programmer_CLI.exe','STM32CubeProgrammer.exe','STM32CubeMX.exe','stm32cubeide.exe',
          'STM32CubeMonitor.exe','openocd.exe','JLink.exe','JLinkGDBServerCL.exe','ST-LINK_gdbserver.exe',
          'ninja.exe','cmake.exe','make.exe'
  $hits = Get-ChildItem -Path $roots -Recurse -Include $want -File -ErrorAction SilentlyContinue
  if ($hits) {
    $hits | Group-Object Name | Sort-Object Name | ForEach-Object {
      Write-Host ("  {0}" -f $_.Name) -ForegroundColor Green
      $_.Group | Select-Object -First 3 | ForEach-Object {
        if ($_.FullName -match '\jre\bin\jlink\.exe$') {
          Write-Host ("      {0}   (Java module linker, NOT SEGGER)" -f $_.FullName) -ForegroundColor DarkGray
        } else { Write-Host ("      {0}" -f $_.FullName) }
      }
    }
    $gcc = $hits | Where-Object { $_.Name -eq 'arm-none-eabi-gcc.exe' } | Select-Object -First 1
    if ($gcc) {
      Write-Host ''
      Write-Host '  the cross compiler reports:' -ForegroundColor Green
      & $gcc.FullName --version 2>&1 | Select-Object -First 2 | ForEach-Object { Write-Host ("      {0}" -f $_) }
      Write-Host ("  add to PATH for a session with:")
      Write-Host ("      `$env:PATH = '{0}' + ';' + `$env:PATH" -f $gcc.DirectoryName)
    }
  } else { Write-Host '  none of the wanted executables found under those roots' }
}
Write-Host ''

Write-Host '--- STM32Cube firmware packages, which supply stm32h7xx.h ---' -ForegroundColor Yellow
# P06 and c/instr include stm32h7xx.h, the vendor device header this repository
# deliberately does not vendor. It comes from the STM32CubeH7 MCU firmware
# package. CubeMX unpacks downloaded packs into the repository below; a zip
# unpacked by hand can be anywhere, so both are searched.
$packRoots = @()
"$env:USERPROFILE\STM32Cube\Repository", "$env:USERPROFILE\Downloads", "$env:USERPROFILE\Desktop", 'C:\ST' | ForEach-Object {
  if (Test-Path $_) { $packRoots += $_ }
}
$packs = @()
foreach ($r in $packRoots) {
  $packs += Get-ChildItem $r -Directory -Filter 'STM32Cube_FW_H7*' -ErrorAction SilentlyContinue
  $packs += Get-ChildItem $r -Directory -Filter 'stm32cubeh7*' -ErrorAction SilentlyContinue
}
$zips = @()
foreach ($r in $packRoots) { $zips += Get-ChildItem $r -File -Filter '*stm32cubeh7*.zip' -ErrorAction SilentlyContinue }
if ($zips) {
  Write-Host '  zips found, NOT yet unpacked unless a folder appears below:'
  $zips | ForEach-Object { Write-Host ("      {0}   {1} MB" -f $_.FullName,[int]($_.Length/1MB)) }
}
if ($packs) {
  foreach ($pk in $packs) {
    Write-Host ("  FOUND  {0}" -f $pk.FullName) -ForegroundColor Green
    # The three files that decide whether this pack covers THIS part.
    $h   = Get-ChildItem $pk.FullName -Recurse -Filter 'stm32h7xx.h'        -ErrorAction SilentlyContinue | Select-Object -First 1
    $hq  = Get-ChildItem $pk.FullName -Recurse -Filter 'stm32h7a3xxq.h'     -ErrorAction SilentlyContinue | Select-Object -First 1
    $st  = Get-ChildItem $pk.FullName -Recurse -Filter 'startup_stm32h7a3*' -ErrorAction SilentlyContinue | Select-Object -First 1
    $brd = Get-ChildItem $pk.FullName -Recurse -Directory -Filter 'NUCLEO-H7A3ZI-Q' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($h)  { Write-Host ("      stm32h7xx.h        {0}" -f $h.FullName) }  else { Write-Host '      stm32h7xx.h        NOT in this pack' }
    if ($hq) { Write-Host ("      stm32h7a3xxq.h     {0}" -f $hq.FullName) } else { Write-Host '      stm32h7a3xxq.h     NOT in this pack: it may not cover the H7A3' }
    if ($st) { Write-Host ("      startup for H7A3   {0}" -f $st.FullName) } else { Write-Host '      startup for H7A3   not found' }
    if ($brd){ Write-Host ("      board examples     {0}" -f $brd.FullName) } else { Write-Host '      board examples     no NUCLEO-H7A3ZI-Q directory' }
    if ($h) {
      Write-Host ''
      Write-Host '      configure the firmware build with:' -ForegroundColor Green
      Write-Host ("        -DCMSIS_DEVICE_DIR='{0}'" -f $h.DirectoryName)
    }
  }
} else { Write-Host '  no STM32Cube_FW_H7 pack directory found. A zip alone is not enough: unpack it.' }
Write-Host ''

Write-Host '--- installed programs naming ST, Cube, SEGGER or J-Link ---' -ForegroundColor Yellow
$apps = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*','HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*','HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*' -ErrorAction SilentlyContinue |
  Where-Object { $_.DisplayName -match 'STM32|STMicro|STCube|ST-LINK|STLink|SEGGER|J-Link|JLink' }
if ($apps) { $apps | Sort-Object DisplayName | Select-Object DisplayName,DisplayVersion,InstallLocation | Format-Table -AutoSize }
else { Write-Host '  none registered' }

Write-Host '--- debug probes attached right now ---' -ForegroundColor Yellow
$p = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue | Where-Object { $_.InstanceId -match 'VID_0483|VID_1366' }
if ($p) { $p | Select-Object Status,Class,FriendlyName,InstanceId | Format-Table -AutoSize }
else { Write-Host '  no ST-LINK (VID_0483) and no SEGGER (VID_1366) device present' }

Write-Host '--- serial ports ---' -ForegroundColor Yellow
$s = Get-CimInstance Win32_SerialPort -ErrorAction SilentlyContinue
if ($s) { $s | Select-Object DeviceID,Description | Format-Table -AutoSize } else { Write-Host '  none' }

Write-Host '--- removable disks: the probe presents one, and dragging a .bin onto it flashes ---' -ForegroundColor Yellow
$d = Get-CimInstance Win32_LogicalDisk -Filter 'DriveType=2' -ErrorAction SilentlyContinue
if ($d) { $d | Select-Object DeviceID,VolumeName,@{n='FreeMB';e={[int]($_.FreeSpace/1MB)}} | Format-Table -AutoSize }
else { Write-Host '  none mounted' }

Write-Host ''
Write-Host '=== end ===' -ForegroundColor Cyan
