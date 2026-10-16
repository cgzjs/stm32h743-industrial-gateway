# 上传 GitHub

使用整理后的 `Industrial_Gateway_GitHub` 目录。它与本地调试目录分开，
不包含厂商手册、工具安装包、编译缓存或机器专属配置。

## GitHub Desktop（推荐）

1. 启动 GitHub Desktop，选择 **File → Add local repository**，选择该目录。
2. 如果提示尚不是 Git 仓库，点击 **create a repository here**；仓库名建议
   `stm32h743-industrial-gateway`，不要重复生成 README 或许可证。
3. 查看 Changes，填写提交说明 `Initial gateway source`，点击 **Commit to main**。
4. 点击 **Publish repository**，核对账号、仓库名和公开/私有选项，再发布。

## Git 命令

在发布目录打开 PowerShell。先在 GitHub 创建空仓库，不勾选自动生成 README、
license 或 gitignore，再运行以下命令。用 GitHub 页面给出的实际地址替换占位符。

```powershell
git init -b main
git add .
git commit -m "Initial gateway source"
git remote add origin <GitHub给出的仓库HTTPS地址>
git push -u origin main
```

如果 Git 提示没有作者身份，设置你希望在提交中公开的 name 和 email；
也可使用 GitHub 账号提供的 noreply 邮箱。不要把访问令牌写进代码或远程 URL。

ZIP 需要先解压再提交目录中的文件；把 ZIP 本身上传到仓库不会展示源代码。
仓库主页自动显示 README。上传后可在 Actions 页面查看自动化测试进度、报告和界面截图。

## 后续更新发布包

在原始工程根目录运行：

```powershell
python Tools/prepare_github.py --output <一个尚不存在的发布目录>
```

脚本只创建新目录，不覆盖已有目录，也不会上传或推送 GitHub。
之后将新改动合入自己的 Git 仓库，检查 diff 后提交。
固件可另放 GitHub Releases；源码包默认不带固件或构建缓存。
