# Capability Map: scene-full-support（全面对标 linux-wallpaperengine scene）

目标：xwallpaperd 新增 `scene` 后端，全面对标
https://github.com/Almamu/linux-wallpaperengine 的 scene 壁纸能力：
目录壁纸（`project.json` + `scene.pkg`/`scene.json`）在 X11 壁纸窗口内用
desktop GL 渲染，支持全对象类型 + 用户属性 + 音频 + 脚本 + 鼠标/视差输入。

> 历史说明：本分支从零重来，不复用悬空提交 9114ed8 的代码；
> 仅复用其模块划分思想。`tasks/` 下旧 we-scene 计划为本地残留，不作为本计划输入。

| Module id | Responsibility | Depends on |
|---|---|---|
| `scene-pkg` | PKGV 容器解析：文件表、按名读取、坏魔数/越界拒绝 | — |
| `scene-tex` | TEXV 纹理解码：内嵌 PNG/JPG 定位、尺寸、RGBA 像素 | `scene-pkg` |
| `scene-shader` | WE 方言 GLSL → desktop GL3.3 转译：common 展开、禁词消除、combo 注入 | — |
| `scene-parse` | JSON→对象模型：scene/camera/bloom/parallax、image/particle/text/sound 全对象、effect/pass/combo/bind、user properties、DynamicValue、script 入口 | `scene-pkg`, json-c |
| `scene-render-core` | GL 上下文、FBO provider（含别名）、Camera 正交+自动尺寸+视差、quad、blend 状态机、clear color、scene 合成、present、bloom | `scene-parse`, `scene-tex`, `scene-shader` |
| `scene-image` | image 对象完整复刻：base copy + effect pass 链 + 内部 FBO + 末 pass 合成（对标 CImage/CPass） | `scene-render-core` |
| `scene-particle` | 粒子系统：emitter、sprite/trail/rope、排序混合（对标 CParticle 2323 行） | `scene-render-core` |
| `scene-text` | 文本对象：FreeType 栅格化 + 上传 + 绘制（对标 CText） | `scene-render-core` |
| `scene-sound` | 音频对象：ogg 解码 + 循环 + 音量（对标 CSound/AudioStream） | `scene-parse` |
| `scene-script` | JS 脚本：timeline、对象属性驱动、user properties、mouse input、math/color（对标 ScriptEngine/SceneObject/EngineObject） | `scene-render-core`, `scene-image`, `scene-particle`, `scene-text`, `scene-sound` |
| `scene-backend` | `XWBackend` 壳：init/start/stop/tick、FBO 尺寸随窗口、DPMS 暂停、reload/restore/list、CLI 探测 | 全部上述 |

```
Dependency direction (no cycles):
  scene-pkg ──┐
              ├──→ scene-parse ──→ scene-render-core ──┬──→ scene-image ──┐
  scene-tex ──┘        ▲            ▲                   ├──→ scene-particle ┤
  scene-shader ────────┘            │                   ├──→ scene-text ───┼──→ scene-script ──→ scene-backend
  json-c ───────────────────────────┘                   └──→ scene-sound ──┘
```

Build order:
```
scene-pkg → scene-tex → scene-shader → scene-parse → scene-render-core
  → scene-image → scene-particle + scene-text + scene-sound（可并行）
  → scene-script → scene-backend
```

## Boundary contracts

- `scene-parse` 只读、arena 分配、`xw_scene_free` 释放；输出带类型判别的全对象模型。
- `scene-render-core` 拥有唯一 GL context/surface（绑定 `XWWindow*`）；提供 FBO/quad/blend/camera 原语。
- `scene-image/particle/text` 各自实现对象渲染，不互调；`scene-sound` 不进 GL。
- `scene-script` 经 DynamicValue 写对象属性，每帧 tick 驱动。
- `scene-backend` 是唯一对 `app`/`backend.h` 的实现；CLI 侧仅目录探测（stat 含 scene.pkg），不链重型库。
- CLI 薄约束不变：`make check-client-libs` 恒绿；新依赖（json-c、freetype2、SDL_mixer 或复用 mpv、quickjs vendored）仅 daemon。

## Scale estimate（对标源码量级）

- render-core ~4000 行，particle ~2500 行，scripting ~3500 行+quickjs，parse 扩展 ~1000 行，text ~500 行，sound ~800 行。总计约 1.2 万行 C，分模块递进交付。
