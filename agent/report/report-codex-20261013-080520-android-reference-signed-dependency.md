<!-- Genuine dependency acquisition, original ARM runtime evidence, and remaining fidelity limits. -->
# 原作ARM动态对照环境简报

## 结果

已实际运行未经修改的原ARM游戏：原菜单、PlainPlane、普通50金币开局、10金币Laser建造、选中范围/退款10/升级25、首波敌人/塔转向/HP/死亡爆炸/金币与得分变化均取得真实画面。正常Menu返回带Resume的原菜单。模拟器仅供开发，Windows原生产品路线不变。未修改原APK、签名、镜像或服务返回，没有ODEX拼接、安全密钥写入、WHPX/驱动/Windows功能/重启操作。

## 真正取得的完整依赖

从公开历史分发仓库 https://github.com/DrMacinyasha/proprietary_vendor_google/blob/gh-pages/gapps-jb-20130812-signed.zip 取得API17时代GApps ZIP。GitHub内容API返回实际blob `88e77c1c68810c5004d61e3b8467f93ce5c2ab09`；raw域名TLS失败，官方GitHub blob API配 `Accept: application/vnd.github.raw+json` 成功下载100751136字节。实际URL为 https://api.github.com/repos/DrMacinyasha/proprietary_vendor_google/git/blobs/88e77c1c68810c5004d61e3b8467f93ce5c2ab09 。ZIP MD5 `5f22f046e37038a3856eeb825e73d4ed`，SHA256 `480a1bab87c05b1f7ef6a3643d93b83d2c3f42dbd48604834e99c741ee2550e7`。

只读提取system/app/Phonesky.apk，未刷ZIP或安装其他组件。Play Store4.3.11，6038575字节，SHA256 `04897ec9492ab119c137b18a021ec931f547c91faeb1d73d4a36b78592e2f03a`。minSdk8/target14，classes.dex4217764字节，正常InAppBillingService.BIND声明。REA静态证据 `ev_77f0fa3f3ff0fe3a9fbd913cad8d804769580dc1a596b67f8b82031be3031c2d`；它本身不验证签名或运行。

另用Java JarFile实际读完1089项有效负载，触发每项JAR签名和manifest摘要验证，全部通过，每项签名证书SHA1都为原Google `38918a453d07199354f8b19af05ec6562ced5788`，无重复ZIP项且完整dex存在。验证器runtime/downloads/VerifyGoogleApk.java；验证进程单独允许历史SHA1，不改全局JDK或TLS设置。结果runtime/logs/TM_Original_API17_ARM/phonesky-4.3.11-signature-verification.txt。

OpenGApps最新100个及page5的100个GitHub release实际都无二进制assets，page10为空。官方.gitmodules指向gitlab.opengapps.org；其公开API目录可读，但当前vending分发只列API19/21，早期截至2016/2019查询为空；没有凭路径推断包可用于API17。对TLS失败端点只做有限不同客户端探测，没有重复APKMirror403/ApkFiles429/SourceForge旧失败请求。

## 实际安装与原作运行

旧5580重新核验为device，Play5586仍unauthorized。旧5580正常安装完整Phonesky返回INSTALL_FAILED_UID_CHANGED；包管理器无已安装vending，但/data/data/com.android.vending残留u0_a48目录。未删除目录或改变UID。

使用同一未经修改官方API17 default ARM镜像新建隔离TM_Original_API17_ARM_clean:5592。SDK25.2.5 classic、无加速、hostGPU、no-audio；boot_completed=1。Phonesky与原ARM APK均普通adb install返回Success；正常am start真实进入菜单。为避免多个软件ARM设备争用CPU，5592启动后对本项目旧5580执行普通emu kill，旧AVD数据保留；Play5586和其他服务未停止。

原ARM APK SHA256重新核验仍为 `0bc4ec90ebf0eab249cab4c39b9390c1f05dafffd83f92adcf09251890b3cb8b`。原UI显示V1.21，而manifest是1.22，各按来源记录，没有据此换APK。依赖普通安装后无需GSF额外安装即可进入本次游戏菜单/战斗；不推断联网商店功能正常。

Windows操作使用computer-use技能的sky/node_repl，逐次观察并普通点击；没有adb input绕过UI。正常点New Game、继续免费版、PlainPlane Tap to Start、完成原教程、草地建塔轮盘选Laser，发送首波。没有修改金币/HP/波/RNG或保存数据。

## 证据与限制

目录agent/codex/android-reference/runtime/logs/TM_Original_API17_ARM_clean：

- menu.png：真实原菜单。
- plainplane-select.png、plainplane-tutorial.png、plainplane-initial.png：首关选择/初始镜头/50金币10羊原HUD。
- plainplane-build-choice.png、plainplane-selected-range.png：原轮盘、10金币Laser、选中/退款10/升级25与范围。
- first-wave-frames/frame-000.png至frame-023.png：24张真实adb screencap PNG，包含首波和后续早期波、敌人/HP/死亡/塔转向/币分变化；first-wave-contact.png只是这些帧的带序号缩略联系图，未虚构帧。采集约一秒一帧，不等于高帧率视频。
- plainplane-speed-control.png：顶右箭头一次点击后白→绿；未测定具体倍率，不能单凭颜色声称2x已验证。
- paused-menu.png：Menu返回带Resume的原菜单；单Laser局第4波羊降至0，没有通关或胜利验收。
- android-properties.txt、phonesky-package.txt、original-package.txt、original-menu-logcat.txt、plainplane-gameplay-logcat.txt：属性/实际安装/运行日志。

白色Laser攻击线曾在sky实时截图明确看到，但保存PNG碰巧处于闪烁间隙，未将这些PNG冒称完整攻击线帧。尚需更高采样率、速度倍率、完整炮塔平滑/粒子/全玩法动态对照；本次是可运行原作参考环境和首关交互证据，不能声称全部保真验收。

最后核验：新ARM5592 emulator-arm PID44084=device，旧Play5586 qemu PID22884=unauthorized，adb29 server PID30800；PID仅为本次观察，续接须重新核验。新ARM停留原菜单带Resume，保持运行供后续对照。docs/ANDROID_REFERENCE.md已更新身份、实际结果和复现入口；未改SRC/MEMORY/README/TODO，无git commit/push。
