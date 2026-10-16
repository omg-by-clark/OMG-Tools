# PhotoShop

一个 Rust + Tauri 桌面照片编辑器，界面与编辑引擎位于 `src/`，原生窗口壳位于 `src-tauri/`。

## 运行

```powershell
npm install
npm run tauri dev
```

## 键盘操作

| 操作 | 快捷键 |
| --- | --- |
| 进入 File 菜单导航 | `Ctrl + T`，`F`，`Enter` |
| 打开文件 | File 菜单中按 `O` |
| 新建文件 | File 菜单中按 `N` |
| 聚焦编辑栏 | `Ctrl + E` |
| 在编辑栏切换项目 | `E`、`V`、`H`、`S`、`L`、`C`、`A`、`T`、`I` |
| 临时查看原图 | 按住 `Ctrl + Shift + O` |

## 打包

```powershell
npm run tauri build
```

生成的 Windows 安装包位于 `src-tauri/target/release/bundle/nsis/`。
