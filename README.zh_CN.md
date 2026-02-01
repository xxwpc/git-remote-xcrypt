# git-remote-xcrypt

[English](README.md)

一个提供远程 Git 仓库透明加密功能的 `git-remote-helper` 插件。

## 特性
- **全链路本地加密/解密**：密钥仅保存在本地仓库的 `.git/config`，远程仓库不含任何明文或密钥信息
- **加密范围**（对远程可见内容做密文化处理）：
  - 文件内容（blob）
  - 文件名（tree 条目）
  - 提交信息/日志（commit message 等）
- **远端零改造**：远程仍是普通 Git 仓库，可使用 GitHub / GitLab / 自建服务器 / 本地裸仓库等
- **无密钥也可同步**：可 `clone/pull/push`，但获得/上传的均为密文
- **支持增量同步**：拉取/推送按对象增量进行
- **不影响本地工作流**：本地仓库保持明文，可正常使用常规 Git 命令与多人协作模式

## 限制
- 暂不支持 `tag`（后续计划支持）
- 克隆必须完整克隆（不支持浅克隆 `--depth`）
- 加密后 `pack` 体积通常显著增大（约为未加密的 ~10 倍，视仓库内容而定）

## 密文示例
以下几段 Git 命令输出用于展示：在**没有密钥**的情况下，远程仓库中可见的内容将**完全以密文形式存在**，不包含任何明文信息，被加密的内容包含：
- 作者（Author）
- 日期（Date）
- 提交日志（commit message 等）
- 文件名（tree 条目）
- 文件内容（blob）
``` console
$ git log --raw -1 2b38053884164
commit 2b380538841642825cf6e1056928c6d98054ba55
Author: git-remote-xcrypt <xxw_pc@163.com>
Date:   Sun Apr 14 14:24:33 2024 +0800

    D+8YoGYIlDEwFPEzuofIat89T4SIIjWEVsKoqn5SIPRL6/jHptJRDXcjxHAAGrCK
    Q1SYsrmjoTuldoJf9Z5Xtn4b7r8usBGfuKtZj4qrNphatCG7sBI6k0hYZj8/aV+w
    hSj+R77RQYVFNIGlkj45+hZKToUOG82oOVVaU2qOaD13pjHMJvCA0eBS8B5Uclyh
    HwOrcwKieoXeSVNjbrDc/Ysiljpu9oGQXSKf1n/lvkJy9/XxVlgiP35FxdXgg534
    8ev5bqLlw5Ygru2GZpFXTWpyaw6fTxbIo9H/Tjju7HrgdqxLBqG88LKJLzrpeG+l
    Vd16Qn+X+gyUgC3GxoVRupQsw+RTd6ilsccPnDdw2Yk=

:100644 100644 30ed132 7443dd2 M        0
:100644 100644 1d144f7 20d5a42 M        1
:100644 100644 a3d4067 39fd893 M        2
:100644 100644 982e100 b078803 M        3
:100644 100644 99261de e9f359a M        4/09
:100644 100644 ad074f6 be747f6 M        4/11
:100644 100644 1823f92 5293150 M        5

$ git ls-tree -r 2b38053884164
100644 blob 7443dd20b6078cc182d3311db5073f65061eb01d    0
100644 blob 20d5a427d837d6476659607c77878ae013ec4670    1
100644 blob 39fd893bd1bd922836c53a61104d9d31319d250e    2
100644 blob b078803718c0e510b585f8cb468799a242991cd8    3
100644 blob 0567237574c9ba8478c7569bcb2663861548fe6a    4/00
100644 blob 76125264df96fe6207bcf77a37ec5523d445f91f    4/01
100644 blob 80d95148ccd199ed7c06c16e481d7e24fddab63d    4/02
100644 blob a97423f5e0bfde5806161b8afac0045ff17aa22b    4/03
100644 blob ade670583fd7ea4d1e7369ebacd3f3a12a514275    4/04
100644 blob db3b804822977d4045200b911b4db8dd406f306a    4/05
100644 blob 39316c0093a057905d004a7e45a92cd4f8169dd0    4/06
100644 blob 3a5bfbd640de50ae02808547d38c2fabb53e71d6    4/07
100644 blob 9c687a69e501ad0952f759cb42160243c10c267d    4/08
100644 blob e9f359a78425a3c61fc1b21d875ab87a2de447bf    4/09
100644 blob a56957fa97003f44eb1bb5d01f487b786306ec2f    4/10
100644 blob be747f61fcd538c812c169d19ded9c43223abbfc    4/11
100644 blob 5293150c69e3d2bcb43dbe052e4b648b7c892234    5

$ git cat-file -p 7443dd20b6078 | hexdump -C
00000000  11 af c4 1a 21 db fa 90  72 85 f1 4a 62 a1 8e 22  |....!...r..Jb.."|
00000010  e7 44 34 88 4e 26 7c e2  5c 54 50 f3 9f 70 4d ee  |.D4.N&|.\TP..pM.|
00000020  ef aa 16 30 f5 d6 0f 86  7e 4d 8f e6 55 f6 00 52  |...0....~M..U..R|
00000030  49 4b 4b 99 96 90 db bf  70 34 8a e6 12 fe 54 2f  |IKK.....p4....T/|
00000040  60 56 11 3e 3c 41 e7 32  90 d3 0d 89 e9 9c ee d8  |`V.><A.2........|
00000050  08 6f c0 2a e4 9d cd db  96 66 c9 e9 dd 55 89 00  |.o.*.....f...U..|
00000060

$
```

## 编译

### 依赖
- 构建工具：`cmake`、`make`、`g++`、`pkg-config`
- 第三方库：`bzip3`、`libgit2`、`openssl`、`boost`

注意事项：
- `bzip3` 需 **>= 1.4.1**（`1.4.0` 在部分 64 字节小文件场景可能解压失败）
- 建议使用**较新版本** `libgit2`（Git 正在从 SHA-1 迁移到 SHA-256，旧版 `libgit2` 可能无法识别新格式仓库）

### 构建步骤

在项目根目录下运行：

**Release 构建（推荐）：**
``` console
$ cmake -B build -DCMAKE_BUILD_TYPE=Release
$ cmake --build build -j
  ......
```

**Debug 构建：**
``` console
$ cmake -B build -DCMAKE_BUILD_TYPE=Debug
$ cmake --build build -j
  ......
```

**安装：**
``` console
$ cmake --install build
```

**卸载：**
``` console
$ cmake --build build --target uninstall
```
注意：先要运行 `cmake --install build`

**生成的可执行文件：**
- `build/git-remote-xcrypt`

## 使用方法

### 密钥格式
- 目前仅支持**明文口令**形式的密钥。
- 密钥仅存储在**本地仓库**的 `.git/config` 中，远程仓库不保存任何密钥信息。
- 在终端传入口令参数时，必须添加 `psw:` 前缀。例如口令为 `abcde`，则应输入：`psw:abcde`

### 命令列表
在本地仓库目录中运行 `git-remote-xcrypt` 来创建和管理加密远程。配置写入 `.git/config`，不会修改远端仓库类型。

``` console
$ git-remote-xcrypt
usage: git-remote-xcrypt <command> [<args>...]

command:
   add      Add an encrypted remote
   clear    Clear cache files and local refs for an encrypted remote
   clone    Clone an encrypted remote
   remove   Remove an encrypted remote
$
```

### 克隆已加密的仓库

将**已加密的远程仓库**克隆为本地明文仓库。密钥在克隆过程中写入新**本地仓库**的 `.git/config`。

**用法：**
``` console
$ git-remote-xcrypt clone
usage: git-remote-xcrypt clone <remote-name> <remote-url> <password> [<git clone options>] [-- <dir>]
$
```

**示例：**克隆远程仓库 `https://www.abc.com/repo.git`，口令为 `abcde`：
``` console
$ git-remote-xcrypt clone origin https://www.abc.com/repo.git psw:abcde
正克隆到 'abc'...
remote: Enumerating objects: 410, done.
remote: Counting objects: 100% (410/410), done.
remote: Compressing objects: 100% (383/383), done.
Receiving objects: 100% (410/410)
remote: Total 410 (delta 29), reused 391 (delta 27)
Unpacking objects: 100% (29/29)
Decrypting objects: 340, 0
正在检查连通性: 340, 完成.
$
```

### 为已有的本地仓库添加加密远程

**用法：**
``` console
$ git-remote-xcrypt add
git-remote-xcrypt add <remote-name> <remote-url> <password> [<git remote add options>]
$
```

**示例：**添加加密远程：
``` console
$ git-remote-xcrypt add origin https://www.abc.com/repo.git psw:abcde
$
```

### 清除加密远程的本地缓存

当出现异常（如本地 refs/缓存与远端状态不一致导致的拉取/推送失败）时，清理指定加密远程对应的本地缓存与引用，然后重新执行 `pull/push`。

**用法：**
``` console
$ git-remote-xcrypt clean
usage: xcrypt clear <remote-name>
$
```

**示例：**清理 `origin` 的加密远程缓存：
``` console
$ git-remote-xcrypt clean origin
$
```

### 删除加密远程

删除加密远程并清理本地残留，包括远程配置与密钥项。

**用法：**
``` console
$ git-remote-xcrypt remove
usage: xcrypt remove <remote-name>
$
```

**示例：**删除加密远程 `origin`：
``` console
$ git-remote-xcrypt remove origin
$
```
