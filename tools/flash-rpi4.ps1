$ErrorActionPreference = 'Stop'
$log = Join-Path $PSScriptRoot 'rpi4-flash.log'
function Log([string]$m) {
    $line = '{0:o} {1}' -f (Get-Date), $m
    Add-Content -LiteralPath $log -Value $line
    Write-Host $line
}

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class NativeDisk {
  [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
  public static extern IntPtr CreateFile(string lpFileName, uint dwDesiredAccess, uint dwShareMode, IntPtr lpSecurityAttributes, uint dwCreationDisposition, uint dwFlagsAndAttributes, IntPtr hTemplateFile);
  [DllImport("kernel32.dll", SetLastError=true)]
  public static extern bool DeviceIoControl(IntPtr hDevice, uint dwIoControlCode, IntPtr lpInBuffer, uint nInBufferSize, IntPtr lpOutBuffer, uint nOutBufferSize, out uint lpBytesReturned, IntPtr lpOverlapped);
  [DllImport("kernel32.dll", SetLastError=true)]
  public static extern bool WriteFile(IntPtr hFile, byte[] lpBuffer, uint nNumberOfBytesToWrite, out uint lpNumberOfBytesWritten, IntPtr lpOverlapped);
  [DllImport("kernel32.dll", SetLastError=true)]
  public static extern bool ReadFile(IntPtr hFile, byte[] lpBuffer, uint nNumberOfBytesToRead, out uint lpNumberOfBytesRead, IntPtr lpOverlapped);
  [DllImport("kernel32.dll", SetLastError=true)]
  public static extern bool SetFilePointerEx(IntPtr hFile, long liDistanceToMove, out long lpNewFilePointer, uint dwMoveMethod);
  [DllImport("kernel32.dll", SetLastError=true)]
  public static extern bool CloseHandle(IntPtr hObject);
  [DllImport("kernel32.dll", SetLastError=true)]
  public static extern bool FlushFileBuffers(IntPtr hFile);
  public static readonly IntPtr Invalid = new IntPtr(-1);
}
"@

function Open-Win32([string]$path, [uint32]$access, [uint32]$share, [uint32]$flags) {
    $h = [NativeDisk]::CreateFile($path, $access, $share, [IntPtr]::Zero, 3, $flags, [IntPtr]::Zero)
    if ($h -eq [NativeDisk]::Invalid) {
        throw "CreateFile $path failed Win32=$([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
    }
    return $h
}

try {
    Set-Content -LiteralPath $log -Value '' -Encoding ASCII
    $isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
    Log "admin=$isAdmin user=$env:USERNAME"
    if (-not $isAdmin) { throw 'This script must run as Administrator (raw disk write).' }

    $imgPath = 'D:\rpi4.img'
    $expected = [int64]12556199936
    $diskNum = 1
    $imgLen = [int64](Get-Item -LiteralPath $imgPath).Length
    if ($imgLen -ne $expected) { throw "Wrong image size: $imgLen (need $expected)" }

    $disk = Get-Disk -Number $diskNum
    Log ("disk{0} name='{1}' bus={2} size={3} readonly={4}" -f $disk.Number, $disk.FriendlyName, $disk.BusType, $disk.Size, $disk.IsReadOnly)
    if ($disk.BusType -ne 'USB') { throw "Disk $diskNum is $($disk.BusType), not USB. Refusing to write." }
    if ($disk.Number -eq 0) { throw 'Refusing to write to disk 0.' }
    if ($disk.Size -lt $expected) { throw "Card too small: $($disk.Size)" }

    $GENERIC_RW = [Convert]::ToUInt32('C0000000', 16)
    $SHARE_RW = [uint32]3
    $FSCTL_LOCK_VOLUME = [uint32]0x00090018
    $FSCTL_DISMOUNT_VOLUME = [uint32]0x00090020
    $NO_BUFFERING_WRITETHROUGH = [Convert]::ToUInt32('A0000000', 16)
    $bytes = [uint32]0
    $volHandles = @()

    Log 'stopping Windows Search so it releases the card'
    try {
        Stop-Service -Name WSearch -Force -ErrorAction Stop
        Log 'WSearch stopped'
    } catch {
        Log "WSearch warning: $($_.Exception.Message)"
    }

    Log 'removing drive letters so Windows releases the volume lock'
    try {
        Get-Partition -DiskNumber $diskNum -ErrorAction Stop | ForEach-Object {
            $letter = $_.DriveLetter
            if ($letter) {
                $access = ($letter.ToString() + ':\')
                Log "remove access path $access"
                try {
                    Remove-PartitionAccessPath -DiskNumber $diskNum -PartitionNumber $_.PartitionNumber -AccessPath $access -ErrorAction Stop
                    Log "removed $access"
                } catch {
                    Log "remove warning $access : $($_.Exception.Message)"
                    try {
                        cmd /c "mountvol ${letter}: /d"
                        Log "mountvol ${letter}: /d done"
                    } catch {
                        Log "mountvol warning: $($_.Exception.Message)"
                    }
                }
            }
        }
    } catch {
        Log "no drive letters to remove (ok): $($_.Exception.Message)"
    }
    Start-Sleep -Seconds 3

    $disk = Get-Disk -Number $diskNum
    if ($disk.BusType -ne 'USB' -or $disk.Number -eq 0) { throw 'Safety check failed before clean.' }
    Log 'disable automount and diskpart clean so Windows drops the old volumes'
    $dpFile = Join-Path $env:TEMP 'diskpart-clean-sd.txt'
    Set-Content -LiteralPath $dpFile -Value "automount disable`r`nselect disk $diskNum`r`nclean`r`n" -Encoding ASCII
    cmd /c "mountvol /N" | Out-Null
    $dpOut = cmd /c "diskpart /s `"$dpFile`"" 2>&1 | Out-String
    Log ($dpOut.Trim())
    Start-Sleep -Seconds 3

    $SHARE_RWD = [uint32]7
    Log 'opening physical disk (share read/write/delete)'
    $dst = Open-Win32 "\\.\PhysicalDrive$diskNum" $GENERIC_RW $SHARE_RWD $NO_BUFFERING_WRITETHROUGH
    Log 'opened physical disk'
    $lockOk = [NativeDisk]::DeviceIoControl($dst, $FSCTL_LOCK_VOLUME, [IntPtr]::Zero, 0, [IntPtr]::Zero, 0, [ref]$bytes, [IntPtr]::Zero)
    Log "FSCTL_LOCK_VOLUME ok=$lockOk err=$([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
    $FSCTL_ALLOW_EXTENDED_DASD_IO = [uint32]0x00090083
    $extOk = [NativeDisk]::DeviceIoControl($dst, $FSCTL_ALLOW_EXTENDED_DASD_IO, [IntPtr]::Zero, 0, [IntPtr]::Zero, 0, [ref]$bytes, [IntPtr]::Zero)
    Log "FSCTL_ALLOW_EXTENDED_DASD_IO ok=$extOk err=$([Runtime.InteropServices.Marshal]::GetLastWin32Error())"

    try {
        Get-Partition -DiskNumber $diskNum -ErrorAction Stop | ForEach-Object {
            foreach ($p in @($_.AccessPaths)) {
                if ($p -like '\\?\Volume{*}') {
                    $vp = $p.Replace('\\?\', '\\.\').TrimEnd('\')
                    Log "lock volume $vp"
                    try {
                        $vh = Open-Win32 $vp $GENERIC_RW $SHARE_RWD 0
                        $volHandles += $vh
                        [void][NativeDisk]::DeviceIoControl($vh, $FSCTL_LOCK_VOLUME, [IntPtr]::Zero, 0, [IntPtr]::Zero, 0, [ref]$bytes, [IntPtr]::Zero)
                        $dOk = [NativeDisk]::DeviceIoControl($vh, $FSCTL_DISMOUNT_VOLUME, [IntPtr]::Zero, 0, [IntPtr]::Zero, 0, [ref]$bytes, [IntPtr]::Zero)
                        Log "volume dismount ok=$dOk err=$([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
                    } catch {
                        Log "volume lock warning: $($_.Exception.Message)"
                    }
                }
            }
        }
    } catch {
        Log "no partitions to lock (ok): $($_.Exception.Message)"
    }

    $src = [System.IO.File]::Open($imgPath, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::Read)
    try {
        $bufSize = 4 * 1024 * 1024
        $buf = New-Object byte[] $bufSize
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        $header = [int64](1 * 1024 * 1024)
        function Write-Chunk([IntPtr]$h, [byte[]]$data, [int]$n, [int64]$at) {
            $out = [uint32]0
            $ok = [NativeDisk]::WriteFile($h, $data, [uint32]$n, [ref]$out, [IntPtr]::Zero)
            if (-not $ok) {
                $err = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
                throw "WriteFile failed Win32=$err at offset $at"
            }
            return [int64]$out
        }
        function Seek-Disk([IntPtr]$h, [int64]$off) {
            $np = [int64]0
            if (-not [NativeDisk]::SetFilePointerEx($h, $off, [ref]$np, 0)) {
                throw "Seek $off failed Win32=$([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
            }
        }

        Log "WRITE body first (skip first 1 MB so Windows does not remount)"
        Seek-Disk $dst $header
        [void]$src.Seek($header, [System.IO.SeekOrigin]::Begin)
        $written = $header
        while (($n = $src.Read($buf, 0, $buf.Length)) -gt 0) {
            if (($n % 512) -ne 0) { throw "Read size $n is not a multiple of 512" }
            $written += (Write-Chunk $dst $buf $n $written)
            if (($written % [int64](256 * 1024 * 1024)) -lt $bufSize) {
                $mbps = 0
                if ($sw.Elapsed.TotalSeconds -gt 0) { $mbps = ($written / 1MB) / $sw.Elapsed.TotalSeconds }
                Log ('wrote {0:N2} / {1:N2} GB  {2:N1} MB/s  elapsed {3}' -f ($written / 1GB), ($expected / 1GB), $mbps, $sw.Elapsed.ToString('hh\:mm\:ss'))
            }
        }

        Log 'WRITE header last (first 1 MB / partition table)'
        Seek-Disk $dst 0
        [void]$src.Seek(0, [System.IO.SeekOrigin]::Begin)
        $left = $header
        $at = [int64]0
        while ($left -gt 0) {
            $want = [Math]::Min($bufSize, [int]$left)
            $n = $src.Read($buf, 0, $want)
            if ($n -le 0) { throw 'short header read' }
            $at += (Write-Chunk $dst $buf $n $at)
            $left -= $n
        }
        $written = $expected
        [void][NativeDisk]::FlushFileBuffers($dst)
        Log "WRITE done bytes=$written elapsed=$($sw.Elapsed)"

        Log 'VERIFY 4 KB at offset 1 GB against image'
        $off = [int64]1073741824
        $chunk = 4096
        $a = New-Object byte[] $chunk
        $b = New-Object byte[] $chunk
        [void]$src.Seek($off, [System.IO.SeekOrigin]::Begin)
        [void]$src.Read($a, 0, $chunk)
        $newPtr = [int64]0
        if (-not [NativeDisk]::SetFilePointerEx($dst, $off, [ref]$newPtr, 0)) {
            throw "Seek card 1GB failed Win32=$([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
        }
        $got = [uint32]0
        if (-not [NativeDisk]::ReadFile($dst, $b, [uint32]$chunk, [ref]$got, [IntPtr]::Zero)) {
            throw "Read card 1GB failed Win32=$([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
        }
        $nzImg = 0; $nzCard = 0; $mismatch = 0
        for ($i = 0; $i -lt $chunk; $i++) {
            if ($a[$i] -ne 0) { $nzImg++ }
            if ($b[$i] -ne 0) { $nzCard++ }
            if ($a[$i] -ne $b[$i]) { $mismatch++ }
        }
        Log "image@1GB nonzero=$nzImg  card@1GB nonzero=$nzCard  mismatches=$mismatch"
        if ($nzCard -eq 0) { throw 'VERIFY FAIL: 1 GB offset on card is all zeros.' }
        if ($mismatch -ne 0) { throw "VERIFY FAIL: $mismatch bytes differ at 1 GB offset." }
        Log 'VERIFY OK'
    }
    finally {
        if ($src) { $src.Dispose() }
        if ($dst -and $dst -ne [NativeDisk]::Invalid) { [void][NativeDisk]::CloseHandle($dst) }
        foreach ($vh in $volHandles) { [void][NativeDisk]::CloseHandle($vh) }
        Log 'handles closed'
    }
    Log 'SUCCESS'
}
catch {
    Log "ERROR: $($_.Exception.Message)"
    exit 1
}
exit 0
