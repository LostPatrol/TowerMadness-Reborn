<!-- The single retained working original-game reference environment and its silent launch commands. -->
# 原作动态对照

只保留已成功运行未修改ARM游戏的 `TM_Original_API17_ARM_clean`。其他失败/重复AVD、API23/24及Google镜像、旧下载和调试日志已清理。模拟器仅供开发对照，最终交付仍为Windows原生程序。

## 目录与依赖

根目录 `TowerMadness+Zero_+3D+TD_1.22_APKPure.apk` 是原ARM游戏。环境在 `agent/codex/android-reference/runtime`：

- `sdk/tools`：官方SDK25.2.5 classic emulator。
- `sdk/system-images/android-17/default/armeabi-v7a`：官方API17 ARM r6。
- `sdk/platform-tools-29/platform-tools/adb.exe`：官方adb29.0.6。
- `avd/TM_Original_API17_ARM_clean.avd`及同名ini：保留配置和已安装原游戏/依赖的数据。
- `downloads/gapps-jb-20130812/Phonesky.apk`：完整原Google签名4.3.11启动依赖，SHA256为 `04897ec9492ab119c137b18a021ec931f547c91faeb1d73d4a36b78592e2f03a`；仅普通安装，不改签名或系统设置。

保留的7张原作菜单、地图、建塔、选中和首波参考图在 `agent/codex/android-reference/reference`。它们不构成整局或逐帧等价验收。

## 静音启动

需要动态对照时才运行，环境变量只影响当前PowerShell及子进程：

```powershell
$referenceRoot = Join-Path $PWD 'agent/codex/android-reference/runtime'
$env:ANDROID_SDK_ROOT = Join-Path $referenceRoot 'sdk'
$env:ANDROID_AVD_HOME = Join-Path $referenceRoot 'avd'
& "$referenceRoot/sdk/tools/emulator.exe" -avd TM_Original_API17_ARM_clean -engine classic -port 5592 -accel off -no-audio -no-snapshot -gpu host
```

另开PowerShell，用上述adb显式选择 `-s emulator-5592`，确认设备与boot_completed后启动：

```powershell
& './agent/codex/android-reference/runtime/sdk/platform-tools-29/platform-tools/adb.exe' -s emulator-5592 shell am start -n com.limbicsoftware.towermadnesszero/com.apportable.activity.VerdeActivity
```

如果以后重建了AVD，可普通安装保留的Phonesky和原ARM APK恢复依赖。不要注入guest keys、修改APK或系统镜像，也不要为此更改本机硬件、虚拟化功能或重启系统。

此环境曾验证菜单、PlainPlane、10金币迫击炮建造/选中及首波；原UI显示V1.21，APK manifest为1.22。x86 APK在历史Android环境的启动失败不影响Windows直接复用路线，不再保留那些失败环境。
