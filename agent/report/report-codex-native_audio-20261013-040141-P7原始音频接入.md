<!-- P7 audio implementation and device-free verification results. -->
# P7 原始音频接入

完成 `src/audio.hpp`、`src/audio.cpp` 与 `docs/AUDIO.md`。使用项目固定版本的 stb_vorbis 解码原始 Ogg，SDL3 负责播放与格式转换；构造和默认静音状态不初始化音频。仅显式设置正音量后允许代码打开设备；完全静音关闭全部播放流。

原始 game/ui sound 配置的 `file` 事件直接映射资源；`comment_file` 保持原始禁用含义。支持 menu/build/game 音乐循环、音效32声部上限、分类音量与非致命错误提示。音乐保留压缩数据和解码状态，PCM队列限制在0.5秒加一个解码块，避免整轨PCM或无限排队。火焰喷射暂按射击事件播放有限音效片段，未恢复原音频管理器的塔生命周期循环控制。

使用独立离线验证程序，以 MSVC19.51、C++20、/W4 完成 Release 编译，无项目代码警告；原stb优化诊断C4701在本翻译单元显式关闭，原因已注释。36/36 Ogg全部成功解码，PCM非空且有限数；静音事件、零音量与离线验证前后 `SDL_WasInit(SDL_INIT_AUDIO)` 均为0。过程文件位于 `agent/codex/p7-audio/`。

遵守用户禁止打开电脑扬声器的指令，整个实施与验证未初始化音频、未打开设备、未播放声音。设备实际混音、听感、循环接缝与目标机器音频设备兼容性未验证，不宣称已达到原作听觉动态等价。主程序、构建和音量UI接入由主代理负责。
