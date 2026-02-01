# git-remote-xcrypt

[简体中文](README.zh_CN.md)

A `git-remote-helper` plugin that provides transparent encryption for remote Git repositories.

## Features
- **End-to-end local encryption/decryption**: The key is stored only in the local repository’s `.git/config`. The remote repository contains no plaintext and no key material.
- **Encryption scope** (obfuscates everything visible to the remote):
  - File contents (blob)
  - File names (tree entries)
  - Commit information/logs (commit messages, etc.)
- **Zero changes required on the remote**: The remote is still a normal Git repository and can be hosted on GitHub / GitLab / self-hosted servers / local bare repos, etc.
- **Sync without a key**: You can `clone/pull/push`, but what you download/upload is ciphertext.
- **Incremental synchronization**: Fetch/push operates incrementally by Git objects.
- **No disruption to local workflows**: The local repository remains plaintext and standard Git commands and multi-user collaboration workflows work as usual.

## Limitations
- `tag` is not supported yet (planned).
- Cloning must be a full clone (no shallow clone via `--depth`).
- After encryption, pack files are typically much larger (often ~10x compared to unencrypted, depending on repository content).

## Ciphertext Examples
The following Git command outputs demonstrate that, **without the key**, everything visible in the remote repository exists **entirely as ciphertext**, with no plaintext information. Encrypted items include:
- Author
- Date
- Commit logs (commit messages, etc.)
- File names (tree entries)
- File contents (blobs)
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

## Build

### Dependencies
- Build tools: `cmake`, `make`, `g++`, `pkg-config`
- Third-party libraries: `bzip3`, `libgit2`, `openssl`, `boost`

Notes:
- `bzip3` must be **>= 1.4.1** (`1.4.0` may fail to decompress in some 64-byte small-file scenarios).
- A **newer** `libgit2` is recommended (Git is migrating from SHA-1 to SHA-256; older `libgit2` may not recognize repositories in the new format).

### Build Steps

In the project root directory:

**Release build (recommended):**
``` console
$ cmake -B build -DCMAKE_BUILD_TYPE=Release
$ cmake --build build -j
  ......
```

**Debug build:**
``` console
$ cmake -B build -DCMAKE_BUILD_TYPE=Debug
$ cmake --build build -j
  ......
```

**Install:**
``` console
$ cmake --install build
```

**Uninstall:**
``` console
$ cmake --build build --target uninstall
```
Note: Run `cmake --install build` before uninstall.

**Generated executable:**
- `build/git-remote-xcrypt`

## Usage

### Key Format
- Currently only **plaintext passphrases** are supported as keys.
- The key is stored only in the **local repository**'s `.git/config`; the remote repository stores no key information.
- When passing the passphrase via terminal, prefix it with `psw:`. For example, if the passphrase is `abcde`, enter: `psw:abcde`

### Commands
Run `git-remote-xcrypt` in a local repository directory to create and manage encrypted remotes. Configuration is written to `.git/config` without modifying the remote repository type.

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

### Clone an Existing Encrypted Repository

Clone an **already-encrypted remote repository** into a local plaintext repository. The key is written to the new **local repository**'s `.git/config` during cloning.

**Usage:**
``` console
$ git-remote-xcrypt clone
usage: git-remote-xcrypt clone <remote-name> <remote-url> <password> [<git clone options>] [-- <dir>]
$
```

**Example:** Clone remote repository `https://www.abc.com/repo.git` with passphrase `abcde`:
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

### Add an Encrypted Remote to an Existing Local Repository

**Usage:**
``` console
$ git-remote-xcrypt add
git-remote-xcrypt add <remote-name> <remote-url> <password> [<git remote add options>]
$
```

**Example:** Add an encrypted remote:
``` console
$ git-remote-xcrypt add origin https://www.abc.com/repo.git psw:abcde
$
```

### Clear Local Cache for an Encrypted Remote

When errors occur (e.g., fetch/push failures caused by local refs/cache being out of sync with the remote state), clear the local cache and refs for the specified encrypted remote, then re-run `pull/push`.

**Usage:**
``` console
$ git-remote-xcrypt clean
usage: xcrypt clear <remote-name>
$
```

**Example:** Clear the encrypted remote cache for `origin`:
``` console
$ git-remote-xcrypt clean origin
$
```

### Remove an Encrypted Remote

Remove an encrypted remote and clean up local remnants, including remote configuration and key entries.

**Usage:**
``` console
$ git-remote-xcrypt remove
usage: xcrypt remove <remote-name>
$
```

**Example:** Remove the encrypted remote `origin`:
``` console
$ git-remote-xcrypt remove origin
$
```
