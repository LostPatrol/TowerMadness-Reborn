# Derive verified native x86 GS adapters using Ghidra's bundled Capstone engine.
$ErrorActionPreference = 'Stop'
# Only generated experiment files receive the extracted analysis DLL and metadata.
$projectDirectory = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$experimentDirectory = Join-Path $projectDirectory 'agent/codex/x86-reuse'
$libraryDirectory = Join-Path $experimentDirectory 'package/lib/x86'
$decoderPath = Join-Path $experimentDirectory 'capstone.dll'
$wheelPath = Join-Path $projectDirectory 'agent/codex/tools/ghidra/ghidra_12.1.4_PUBLIC/Ghidra/Debug/Debugger-agent-dbgeng/pypkg/dist/capstone-5.0.6-py3-none-win_amd64.whl'
if (!(Test-Path -LiteralPath $decoderPath)) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($wheelPath)
    try { [IO.Compression.ZipFileExtensions]::ExtractToFile($archive.GetEntry('capstone/lib/capstone.dll'), $decoderPath, $true) }
    finally { $archive.Dispose() }
}
$decoderSource = @'
// Decode original instructions through a native analysis DLL, never execute them.
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public class NativeGsDecoder {
    [DllImport(@"__DLL__")] static extern int cs_open(int architecture, int mode, out UIntPtr handle);
    [DllImport(@"__DLL__")] static extern int cs_option(UIntPtr handle, int type, UIntPtr value);
    [DllImport(@"__DLL__")] static extern UIntPtr cs_disasm(UIntPtr handle, byte[] bytes, UIntPtr size, ulong address, UIntPtr count, out IntPtr instructions);
    [DllImport(@"__DLL__")] static extern void cs_free(IntPtr instructions, UIntPtr count);
    [DllImport(@"__DLL__")] static extern int cs_close(ref UIntPtr handle);

    /// <summary>Return encoded GS instructions from one original executable section.</summary>
    public static string[] Decode(byte[] bytes, int offset, int size, uint address) {
        UIntPtr handle;
        if (cs_open(3, 4, out handle) != 0) throw new Exception("Capstone x86 initialization failed");
        cs_option(handle, 5, (UIntPtr)3);
        var result = new List<string>();
        var section = new byte[size]; Array.Copy(bytes, offset, section, 0, size);
        IntPtr records; var count = cs_disasm(handle, section, (UIntPtr)size, address, UIntPtr.Zero, out records);
        for (ulong index = 0; index < count.ToUInt64(); ++index) {
            // Capstone 5's cs_insn layout is 248 bytes in the bundled Windows x64 DLL.
            IntPtr item = IntPtr.Add(records, checked((int)index * 248));
            string operands = Marshal.PtrToStringAnsi(IntPtr.Add(item, 74));
            if (!operands.Contains("gs:")) continue;
            int length = Marshal.ReadInt16(item, 16); var encoded = new byte[length];
            Marshal.Copy(IntPtr.Add(item, 18), encoded, 0, length);
            string mnemonic = Marshal.PtrToStringAnsi(IntPtr.Add(item, 42));
            result.Add(((ulong)Marshal.ReadInt64(item, 8)).ToString("x8") + "\t" +
                BitConverter.ToString(encoded).Replace("-", "").ToLowerInvariant() + "\t" + mnemonic + "\t" + operands);
        }
        cs_free(records, count); cs_close(ref handle); return result.ToArray();
    }
}
'@
if (!('NativeGsDecoder' -as [type])) { Add-Type -TypeDefinition $decoderSource.Replace('__DLL__', $decoderPath) }
$adapters = [Collections.Generic.List[object]]::new()
foreach ($file in Get-ChildItem -LiteralPath $libraryDirectory -Filter '*.so') {
    $bytes = [IO.File]::ReadAllBytes($file.FullName)
    $sectionOffset = [BitConverter]::ToUInt32($bytes, 32)
    $sectionStride = [BitConverter]::ToUInt16($bytes, 46)
    $sectionCount = [BitConverter]::ToUInt16($bytes, 48)
    for ($index = 0; $index -lt $sectionCount; ++$index) {
        $at = $sectionOffset + $index * $sectionStride
        if (([BitConverter]::ToUInt32($bytes, $at + 8) -band 4) -eq 0) { continue }
        foreach ($line in [NativeGsDecoder]::Decode($bytes, [BitConverter]::ToUInt32($bytes, $at + 16), [BitConverter]::ToUInt32($bytes, $at + 20), [BitConverter]::ToUInt32($bytes, $at + 12))) {
            $fields = $line.Split("`t")
            $absolute = [regex]::Match($fields[3], 'gs:\[0x([0-9a-f]+)\]')
            if (!$absolute.Success -or $fields[2] -notin @('mov', 'cmp')) { continue }
            $slot = [Convert]::ToUInt32($absolute.Groups[1].Value, 16)
            $encoded = [byte[]]::new($fields[1].Length / 2)
            for ($byteIndex = 0; $byteIndex -lt $encoded.Length; ++$byteIndex) { $encoded[$byteIndex] = [Convert]::ToByte($fields[1].Substring($byteIndex * 2, 2), 16) }
            $displacement = $encoded.Length - 4
            if ($encoded[0] -ne 0x65 -or $displacement -lt 2 -or [BitConverter]::ToUInt32($encoded, $displacement) -ne $slot) { continue }
            $adapters.Add([pscustomobject]@{ library=$file.Name; va=[Convert]::ToUInt32($fields[0],16); originalBytes=$fields[1]; segmentPrefixOffset=0; displacementOffset=$displacement; slot=$slot; canRedirect=$true })
        }
    }
}
[IO.File]::WriteAllText((Join-Path $experimentDirectory 'gs-instructions.json'), ($adapters | ConvertTo-Json -Depth 4), [Text.UTF8Encoding]::new($false))
Write-Output "Prepared $($adapters.Count) absolute GS adapters for the single-thread experiment."
