# git-remote-xcrypt

一个 git-remote-helper 外部插件，用于加密远程 git 仓库

### 特性
- 加密远程仓库，包括
  - 文件内容
  - 文件名
  - 提交日志
- 加密解密操作运行于本地，密钥也保存于本地，远程仓库中不出现任何明文信息，密钥信息
- 远程仓库不需要特种仓库，依然是一个普通的 git 仓库，您可以使用 github、gitlab、本地仓库、自建服务器等做为远程仓库
- 没有密钥的情况下，依然可以进行克隆、拉取、推送等操作，但得到都是密文
- 支持增量式拉取、推送操作
- 不影响本地 git 各种操作
- 不影响 git 多人协作方式

### 缺陷
- 当前版本尚不支持 tag，后期会支持
- 克隆 git 仓库时必须完整克隆，不支持浅克隆
- 由于加密原因，导致 pack 文件体积比未加密版本大 10 倍左右

### 编译
#### 编译依赖
make, g++23, bzip3, libgit2, openssl, boost
- bzip3 必须使用 1.4.1 以上, 1.4.0 对某些 64 字节小文件可能无法解压缩
- libgit2 最好编译最新版本，当前 git 正在由 sha1 向 sha256 过渡，旧版本 libgit2 可能不认识一些新版本的仓库

#### 编译方法
在 git-remote-xcrypt 目录运行 make 命令
``` console
$ make
make -C src
make[1]: 进入目录“git-remote-xcrypt/src”
g++ -std=gnu++23 -O0 -g -c aes.cpp -o aes.o
g++ -std=gnu++23 -O0 -g -c crypto.cpp -o crypto.o
g++ -std=gnu++23 -O0 -g -c git.cpp -o git.o
g++ -std=gnu++23 -O0 -g -c main.cpp -o main.o
g++ -std=gnu++23 -O0 -g -c omp.cpp -o omp.o
g++ -std=gnu++23 -O0 -g -c progress.cpp -o progress.o
g++ -std=gnu++23 -O0 -g -c remote_helper.cpp -o remote_helper.o
g++ -std=gnu++23 -O0 -g -c user_command.cpp -o user_command.o
g++ -std=gnu++23 -O0 -g -c util.cpp -o util.o
g++ -std=gnu++23 -O0 -g aes.o crypto.o git.o main.o omp.o progress.o remote_helper.o user_command.o util.o -L/usr/local/lib -lgit2 -lbzip3 -lcrypto -lboost_system -lboost_filesystem -o git-remote-xcrypt
make[1]: 离开目录“git-remote-xcrypt/src”
```

安装运行 make install
清理运行 make clean

### 密钥
目前只支持明文密码，保存于本地仓库的 .git/config 内，远程仓库不保存任何密钥

在终端命令，需要输入密码参数时，需在密码前加 “psw:” 前缀，如密码为 abcde, 则终端输入的密码参数为
<br>&emsp;&emsp;psw:abcde

### 用户命令
在终端直接运行 git-remote-xcrypt 命令，实现远程加密仓库的配置
```
$ git-remote-xcrypt
usage: xcrypt <command> [<args>...]

command:
   add          add a remote
   clear        clear cache files
   clone        clone an encrypted repository
```

### 克隆已经存在的加密仓库
```
$ git-remote-xcrypt clone
usage: git-remote-xcrypt clone <name> <url> <password> [<git clone options>] [-- <dir>]
```

例：远程仓库为  https://www.abc.com/repo.git，密码为 abcde, 则可运行
```
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
```

### 为已经存在的 git 仓库添加远程加密仓库
```
$ git-remote-xcrypt add
git-remote-xcrypt add <name> <url> <password> [<git remote add options>]
```

例：添加远程仓库并推送
```
$ git-remote-xcrypt add origin https://www.abc.com/repo.git psw:abcde
$
$ git push -u origin master:master
Encrypting objects: 16, 151
Enumerating objects: 24
Compressing objects: 100% (24/24)
Writing objects: 100% (24/24)
To t-ren.com:my/mx/xui.git
   9e778b2..a39a0a4  master -> master
```

### 清除远程仓库相关缓存
当某些情况下，需要清除本地缓存

```
git-remote-xcrypt clean <name>
```
例：
```
$ git-remote-xcrypt clean origin
```

