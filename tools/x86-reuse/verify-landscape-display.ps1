# Verify responsive native camera/input and hotkey restore using an isolated original save.
param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][string]$CaseDirectory,
    [Parameter(Mandatory)][string]$SourceState,
    [string]$WindowSize = '1280x720',
    [switch]$EdgeOnly,
    [ValidateSet('left','right','top','bottom')][string]$EdgeSide = 'left'
)
$ErrorActionPreference = 'Stop'
# All native reads are restricted to the process created by this driver.
Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class LandscapeAcceptance {
 public delegate bool EnumProc(IntPtr h,IntPtr l);
 [StructLayout(LayoutKind.Sequential)] public struct Rect {public int l,t,r,b;}
 [StructLayout(LayoutKind.Sequential)] public struct Point {public int x,y;}
 [DllImport("user32.dll")] static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
 [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
 [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc p,IntPtr l);
 [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")] static extern bool ClientToScreen(IntPtr h,ref Point p);
 [DllImport("user32.dll")] static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int w,int z,uint f);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int n);
 [DllImport("user32.dll",EntryPoint="SendMessageW")] static extern IntPtr NativeSend(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint a,bool i,int p);
 [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr p,IntPtr a,byte[] b,int n,out IntPtr r);
 [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr p);
 // Scope physical coordinates to each call because PowerShell may switch invocation threads.
 public static Rect Client(IntPtr h) {var old=SetThreadDpiAwarenessContext(new IntPtr(-4));try{Rect r;GetClientRect(h,out r);return r;}finally{SetThreadDpiAwarenessContext(old);}}
 // Cross-process mouse messages must use the same physical DPI context as the target window.
 public static IntPtr Send(IntPtr h,uint m,IntPtr w,IntPtr l){var old=SetThreadDpiAwarenessContext(new IntPtr(-4));try{return NativeSend(h,m,w,l);}finally{SetThreadDpiAwarenessContext(old);}}
 // Select only this process's GL window, excluding audio helper windows.
 public static IntPtr Find(int pid){IntPtr found=IntPtr.Zero;EnumWindows((h,l)=>{uint p;GetWindowThreadProcessId(h,out p);var s=new StringBuilder(128);GetClassName(h,s,128);if(p==pid&&s.ToString()=="TowerMadnessOriginalX86Probe")found=h;return true;},IntPtr.Zero);return found;}
 // A read-only process handle cannot mutate money, lives, speed, or camera state.
 public static byte[] Read(int pid,long address,int length){var p=OpenProcess(0x410,false,pid);if(p==IntPtr.Zero)throw new Exception("Cannot read own native process");try{var b=new byte[length];IntPtr n;if(!ReadProcessMemory(p,new IntPtr(address),b,length,out n)||n.ToInt64()!=length)throw new Exception("Incomplete native snapshot");return b;}finally{CloseHandle(p);}}
 // Account for real non-client borders under the current monitor DPI.
 public static void Resize(IntPtr h,int w,int z){var old=SetThreadDpiAwarenessContext(new IntPtr(-4));try{ShowWindow(h,9);Rect c,r;GetClientRect(h,out c);GetWindowRect(h,out r);SetWindowPos(h,IntPtr.Zero,0,0,w+r.r-r.l-c.r,z+r.b-r.t-c.b,6);}finally{SetThreadDpiAwarenessContext(old);}}
 // WM_MOUSEWHEEL receives physical screen pixels, unlike client pointer messages.
 public static void Wheel(IntPtr h,int delta){var old=SetThreadDpiAwarenessContext(new IntPtr(-4));try{Rect r;GetClientRect(h,out r);Point p;p.x=r.r/2;p.y=r.b/2;ClientToScreen(h,ref p);Send(h,0x20a,new IntPtr(delta<<16),new IntPtr((p.x&65535)|((p.y&65535)<<16)));}finally{SetThreadDpiAwarenessContext(old);}}
}
'@
$case = [IO.Path]::GetFullPath($CaseDirectory)
if (Test-Path -LiteralPath $case) { throw 'Use a new display evidence directory.' }
$out = Join-Path $case 'evidence/run'
$state = Join-Path $case 'state'
New-Item -ItemType Directory -Force $out,$state | Out-Null
foreach ($name in @('menu-save-hotkey.bin','platform-store.txt')) { Copy-Item -LiteralPath (Join-Path $SourceState $name) -Destination $state }
$exe = Join-Path $BuildDirectory 'Release/TowerMadnessX86Probe.exe'
$hash = (Get-FileHash -LiteralPath $exe).Hash
$manifest = Get-Content -LiteralPath (Join-Path $BuildDirectory 'build-manifest.json') -Raw | ConvertFrom-Json
if ($manifest.executableSha256 -ne $hash) { throw 'Executable differs from build manifest.' }
Copy-Item -LiteralPath (Join-Path $BuildDirectory 'build-manifest.json') -Destination $out
Copy-Item -LiteralPath $PSCommandPath -Destination $out
# Save provenance binds the input checkpoint without touching its original directory.
$provenance = @(Get-ChildItem -LiteralPath $state -File | ForEach-Object { [pscustomobject]@{name=$_.Name;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash;source=$SourceState} })
$provenance | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $out 'checkpoint-source.json')
$log = Join-Path $out 'native.log'
$project = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$oldAudio=$env:TOWERMADNESS_X86_AUDIO; $oldWindow=$env:TOWERMADNESS_WINDOW
$env:TOWERMADNESS_X86_AUDIO='0'; $env:TOWERMADNESS_WINDOW=$WindowSize
try {
    $process = Start-Process -FilePath $exe -ArgumentList @(('"'+(Join-Path $project 'agent/codex/x86-reuse/package')+'"'),'menu',('"'+$log+'"')) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $out 'stdout.log') -RedirectStandardError (Join-Path $out 'stderr.log')
} finally { $env:TOWERMADNESS_X86_AUDIO=$oldAudio; $env:TOWERMADNESS_WINDOW=$oldWindow }
$null=$process.Handle
$samples=[Collections.Generic.List[object]]::new()
# Read the signed original object field at its verified byte offset.
function Read-Int([long]$Address) { return [BitConverter]::ToInt32([LandscapeAcceptance]::Read($process.Id,$Address,4),0) }
# Dispatch physical Windows pointer messages without a portrait letterbox transform.
function Send-Point([uint32]$Message,[int]$X,[int]$Y,[int]$Buttons=0) { [LandscapeAcceptance]::Send($window,$Message,[IntPtr]$Buttons,[IntPtr](($X-band 65535)-bor (($Y-band 65535)-shl 16))) | Out-Null }
# Derive the same aspect canvas independently from physical client dimensions.
function Get-Canvas { $r=[LandscapeAcceptance]::Client($window);$s=[Math]::Min($r.r/480.0,$r.b/800.0);return @([int][Math]::Round($r.r/$s),[int][Math]::Round($r.b/$s),$r.r,$r.b) }
# Map a logical point once, keeping the raw message contract visible in the evidence.
function Click-Logical([double]$X,[double]$Y) { $c=Get-Canvas;$px=[int][Math]::Round($X*$c[2]/$c[0]);$py=[int][Math]::Round($Y*$c[3]/$c[1]);Send-Point 0x201 $px $py 1;Send-Point 0x202 $px $py;Start-Sleep -Milliseconds 250 }
# Snapshot the original menu, camera matrix, resolution, and active radial options.
function Read-Snapshot([string]$Name) {
    $game=Read-Int ($menu+0x15c);$camera=Read-Int ($game+0xa00)
    $bytes=[LandscapeAcceptance]::Read($process.Id,$camera,96)
    $matrix=@(for($i=0;$i-lt 24;$i++){[BitConverter]::ToSingle($bytes,$i*4)})
    foreach($v in $matrix){if([float]::IsNaN($v)-or[float]::IsInfinity($v)){throw 'Nonfinite native camera.'}}
    $pie=Read-Int ($game+0xa48);$pb=[LandscapeAcceptance]::Read($process.Id,$pie,64)
    $options=@();$list=Read-Int ($pie+4);$node=Read-Int $list
    # The original option list uses item/next nodes; retain a small corruption guard for reads.
    for($n=0;$node-ne 0-and$n-lt 32;$n++){$item=Read-Int $node;$ob=[LandscapeAcceptance]::Read($process.Id,$item,44);$options += [pscustomobject]@{x=[BitConverter]::ToSingle($ob,0);y=[BitConverter]::ToSingle($ob,4);type=[BitConverter]::ToInt32($ob,36);action=[BitConverter]::ToInt32($ob,40)};$node=Read-Int ($node+4)}
    $record=[pscustomobject]@{name=$Name;game=$game;clock=Read-Int ($game+0x28);showing=[LandscapeAcceptance]::Read($process.Id,$menu+4,1)[0];canvas=Get-Canvas;resolution=@((Read-Int ($game+0xbf8)),(Read-Int ($game+0xbfc)));dpi=[LandscapeAcceptance]::GetDpiForWindow($window);matrix=$matrix;pieKind=[BitConverter]::ToInt32($pb,20);pieCenter=@([BitConverter]::ToSingle($pb,56),[BitConverter]::ToSingle($pb,60));cell=@([BitConverter]::ToInt32($pb,12),[BitConverter]::ToInt32($pb,16));options=$options}
    $samples.Add($record);return $record
}
# Drag through the real previous-location/tap-slop path; never write camera values.
function Drag-Camera([int]$Delta,[int]$DeltaY=0) { $c=Get-Canvas;$x=[int]($c[2]/2);$y=[int]($c[3]/2);Send-Point 0x201 $x $y 1;Send-Point 0x200 ($x+$Delta) ($y+$DeltaY) 1;Send-Point 0x202 ($x+$Delta) ($y+$DeltaY);Start-Sleep -Milliseconds 300 }
# Send only normal hotkeys, allowing the frame-boundary restore owner to replace GAME.
function Send-Key([int]$Key) { [LandscapeAcceptance]::Send($window,0x100,[IntPtr]$Key,[IntPtr]0)|Out-Null;Start-Sleep -Milliseconds 600 }
try {
    Start-Sleep -Seconds 3
    $window=[LandscapeAcceptance]::Find($process.Id)
    if($window-eq[IntPtr]::Zero){throw 'Own native window missing.'}
    $text=Get-Content -LiteralPath $log -Raw
    if($text-notmatch 'ORIGINAL_MENU_ALLOC (\d+)'){throw 'Original MENU address missing.'}
    $menu=[long]$Matches[1]
    $paused=Read-Snapshot 'startupPaused';[LandscapeAcceptance]::Wheel($window,120);Start-Sleep -Milliseconds 200;$unchanged=Read-Snapshot 'pausedWheel'
    if($paused.clock-ne$unchanged.clock-or($paused.matrix-join',')-ne($unchanged.matrix-join',')){throw 'Wheel changed paused game.'}
    Click-Logical 45 262;Start-Sleep -Seconds 2
    if((Read-Snapshot 'resumed').showing-ne 0){throw 'Original Resume hit region did not hide menu.'}
    $sizes=if($EdgeOnly){@()}else{@(@(1280,720),@(1920,1080),@(1920,1200),@(-1,-1),@(800,600),@(360,600),@(1280,720))}
    foreach($size in $sizes){
        if($size[0]-eq -1){[LandscapeAcceptance]::ShowWindow($window,3)|Out-Null}else{[LandscapeAcceptance]::Resize($window,$size[0],$size[1])}
        Start-Sleep -Milliseconds 700
        $before=Read-Snapshot ('resize-'+($size-join'x'))
        if($before.resolution[0]-ne$before.canvas[0]-or$before.resolution[1]-ne$before.canvas[1]){throw 'Original GAME resolution differs from responsive canvas.'}
        if($size[0]-gt 0-and($before.canvas[2]-ne$size[0]-or$before.canvas[3]-ne$size[1])){throw 'Physical client resize does not match requested dimensions.'}
        Drag-Camera 70;$dragged=Read-Snapshot 'dragRight'
        if(($before.matrix-join',')-eq($dragged.matrix-join',')){throw 'Original pointer drag did not translate the camera.'}
        Drag-Camera -70
        [LandscapeAcceptance]::Wheel($window,120);Start-Sleep -Milliseconds 200;$near=Read-Snapshot 'wheelIn'
        [LandscapeAcceptance]::Wheel($window,-120);Start-Sleep -Milliseconds 200;$far=Read-Snapshot 'wheelOut'
        if(($near.matrix-join',')-eq($far.matrix-join',')){throw 'Original wheel did not change camera.'}
        # Save before resize, then restore at a different aspect to test current-resolution installation.
        Send-Key 0x74
        [LandscapeAcceptance]::Resize($window,960,600);Start-Sleep -Milliseconds 400
        Send-Key 0x78;$restored=Read-Snapshot 'restoredAfterResize'
        if($restored.resolution[0]-ne$restored.canvas[0]-or$restored.resolution[1]-ne$restored.canvas[1]){throw 'F9 restored stale screen resolution.'}
        # GAME_resume preserves the original three-second countdown for started games.
        $clock=$restored.clock;Start-Sleep -Milliseconds 4200
        if((Read-Snapshot 'restoreClockAdvanced').clock-le$clock){throw 'Restored original clock did not advance.'}
    }
    if($EdgeOnly){
        Start-Sleep -Seconds 2
        # Zoom through the original pinch handler until terrain reaches the screen boundary.
        for($n=0;$n-lt 24;$n++){[LandscapeAcceptance]::Wheel($window,120)}
        Start-Sleep -Milliseconds 500
        if($EdgeSide-eq'top'){Drag-Camera 0 -240}
        if($EdgeSide-eq'bottom'){Drag-Camera 0 240}
        $c=Get-Canvas;$found=$false
        # Keep each case confined to its requested boundary so coverage cannot be mislabeled.
        $points=@()
        if($EdgeSide-in @('left','right')){
            foreach($y in @(150,250,350,450,550,650)){$edgeX=if($EdgeSide-eq'left'){70}else{$c[0]-70};$points+=,@($edgeX,$y)}
        }else{
            foreach($x in @(200,400,600,800,1000,1200)){$edgeY=if($EdgeSide-eq'top'){180}else{620};$points+=,@($x,$edgeY)}
        }
        foreach($p in $points){
            Click-Logical $p[0] $p[1];$selected=Read-Snapshot 'edgeCandidate'
            if($selected.pieKind-ne 1){continue}
            Start-Sleep -Milliseconds 900;$ring=Read-Snapshot 'edgeNudgedRing'
            if($ring.pieKind-ne 1){continue}
            foreach($option in $ring.options){$ox=$ring.pieCenter[0]+$option.x;$oy=$ring.pieCenter[1]+$option.y;if($ox-lt 24-or$ox-gt($c[0]-24)-or$oy-lt 24-or$oy-gt($c[1]-24)){throw 'An original edge wheel option remains clipped after camera nudge.'}}
            $category=@($ring.options|Where-Object action -eq 2)[0]
            if(!$category){continue}
            Click-Logical ($ring.pieCenter[0]+$category.x) ($ring.pieCenter[1]+$category.y)
            Start-Sleep -Milliseconds 300;$types=Read-Snapshot 'edgeBuildOptions'
            # Shipped Plasmatron I costs five; use ordinary affordable funds from this checkpoint.
            $choice=@($types.options|Where-Object {$_.action-eq 1-and$_.type-eq 57})[0]
            if(!$choice){throw 'Edge category did not expose original build options.'}
            Send-Key 0x74
            Click-Logical ($types.pieCenter[0]+$choice.x) ($types.pieCenter[1]+$choice.y)
            Start-Sleep -Milliseconds 1300;$built=Read-Snapshot 'edgeBuilt'
            Click-Logical $built.pieCenter[0] $built.pieCenter[1]
            $upgrade=Read-Snapshot 'edgeTowerSelected'
            if($upgrade.pieKind-ne 0){throw 'Original edge build did not expose an existing tower upgrade ring.'}
            Send-Key 0x74
            $found=$true;break
        }
        if(!$found){throw 'No legal boundary cell was reached; edge construction remains unverified.'}
    }
    Send-Key 27;$paused=Read-Snapshot 'pauseAfterResize';[LandscapeAcceptance]::Wheel($window,-120);Start-Sleep -Milliseconds 200;$unchanged=Read-Snapshot 'pausedAfterResizeWheel'
    if($paused.clock-ne$unchanged.clock-or($paused.matrix-join',')-ne($unchanged.matrix-join',')){throw 'Wheel changed original pause menu.'}
} finally {
    if(!$process.HasExited){[LandscapeAcceptance]::Send([LandscapeAcceptance]::Find($process.Id),0x10,[IntPtr]0,[IntPtr]0)|Out-Null}
    if(!$process.WaitForExit(10000)){throw 'Display test did not close normally.'}
    $samples|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $out 'samples.json') -Encoding utf8
}
$process.Refresh();$text=Get-Content -LiteralPath $log -Raw
$diagnostics=$text+(Get-Content -LiteralPath (Join-Path $out 'stdout.log') -Raw)+(Get-Content -LiteralPath (Join-Path $out 'stderr.log') -Raw)
if($process.ExitCode-ne 0-or$diagnostics-match 'HOST_ERROR|UNIMPLEMENTED|NATIVE_EXCEPTION|GL error after'){throw 'Native display runtime failed.'}
$saves=@([regex]::Matches($text,'ORIGINAL_SAVE_HOTKEY [^\r\n]* (\{[^\r\n]*\})'))
$loads=@([regex]::Matches($text,'ORIGINAL_LOAD_HOTKEY bytes=\d+ restore=\d+ game=\d+ (\{[^\r\n]*\})'))
if(!$EdgeOnly-and($saves.Count-ne 7-or$loads.Count-ne 7)){throw 'Missing actual F5/F9 events.'}
if($EdgeOnly){if($saves.Count-ne 2){throw 'Missing edge build snapshots.'};$before=$saves[0].Groups[1].Value|ConvertFrom-Json;$after=$saves[1].Groups[1].Value|ConvertFrom-Json;if($after.money-ge$before.money){throw 'Original edge construction did not spend normal funds.'}}
for($i=0;$i-lt$loads.Count;$i++){ $saved=$saves[$i].Groups[1].Value|ConvertFrom-Json;$loaded=$loads[$i].Groups[1].Value|ConvertFrom-Json;foreach($key in @('map','money','lives','score','gameStarted','simulationMs')){if($saved.$key-ne$loaded.$key){throw "F9 changed checkpoint field $key"}} }
if((Get-FileHash -LiteralPath $exe).Hash-ne$hash){throw 'Executable changed during verification.'}
$scope=if($EdgeOnly){"Original $EdgeSide boundary selection, camera nudge, unclipped option centers, original category/build, normal funds spent, existing tower upgrade ring."}else{'Physical resizing/maximize, real pointer drag and original two-touch wheel, paused input rejection, F5/F9 after aspect changes; edge construction is separate.'}
[pscustomobject]@{passed=$true;executableSha256=$hash;nativeExitCode=$process.ExitCode;sampleCount=$samples.Count;restores=$loads.Count;dpi=@($samples.dpi|Select-Object -Unique);scope=$scope}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $out 'acceptance.json')
Get-Content -LiteralPath (Join-Path $out 'acceptance.json')
