# Fix
这是一个专门供给[MC灵依资源站](https://mcres.cn)的Blaze服务端修复版本

在[原作者的仓库](https://github.com/traksag/blaze)中,

src/chunk_loader.c的第257行为:

`updateRequests.entries = reallocf(updateRequests.entries, updateRequests.arraySize * sizeof *updateRequests.entries);`

其中的"reallocf"可能会导致你在运行build.sh时报错，修改为:

`updateRequests.entries = realloc(updateRequests.entries, updateRequests.arraySize * sizeof *updateRequests.entries);`

这个仓库存在我们已经构建好的版本,在Releases中,你可以直接下载

服务端启动方法为:

cd至此服务端根目录

运行 `./blaze`，默认的开启端口为25565

Blaze 可以从 Anvil 区域文件加载区块。请在仓库根目录下创建一个名为 `world` 的文件夹，并将其他地方的 `region` 文件夹复制到该文件夹中。请注意，Blaze 仅加载最新 Minecraft 版本的区块，因此你可能需要在复制 `region` 文件夹之前优化你的世界。

# Blaze

这是针对 Minecraft Java Edition 1.19.4 的手工制作游戏服务器。

该项目的主要目标之一是使其更新到游戏的新版本变得简单。其他目标包括速度和稳定性：服务器不应出现 CPU 峰值、内存峰值，启动过程应尽可能快速且便宜，并且应尽量减少故障情况（即 Java 不能处理的情况）。用户应能够轻松地预先确定和控制最大内存使用量。

这些目标的一些副作用如下：我们不实现世界生成，不支持世界升级，也不实现实体 AI。如果这个项目有朝一日发展到那种程度，可能会有机会为这些功能编写插件。


## System Requirements

该软件可以在运行 macOS 或 Linux 的主流硬件上使用 Clang 或 GCC 编译和运行。你必须安装 zlib 才能构建和运行该软件。

如果你使用的是不同的编译器、不同的操作系统或非主流硬件，请确保满足以下条件：

* 整数使用二进制补码表示，位运算（如 'and'）在有符号整数上按预期工作。
* 有符号右移操作通过符号扩展来实现。
* 将整数转换为另一种无符号或有符号整数类型时，按照目标类型的范围进行模减。
* 必须提供几个预处理器宏和内建函数。这些可能会导致编译错误。
* 浮点数和双精度浮点数分别编码为 IEEE 754 binary32 和 binary64。

## Usage

如果你在 Unix 系统上，可以通过运行 `./build.sh` 来构建服务器。将构建脚本移植到其他系统应该也很简单。请注意，`build.sh` 顶部有几个配置选项，你可能希望进行修改。

要启动服务器，只需运行 `./blaze`。当前，`blaze` 二进制文件需要位于仓库的根目录，以便它可以从数据文件（如 `entitytags.txt` 和 `itemtags.txt`）中读取数据。如果需要，可以在代码中修改端口号，默认为本地主机的 25565 端口。

Blaze 可以从 Anvil 区域文件加载区块。请在仓库根目录下创建一个名为 `world` 的文件夹，并将其他地方的 `region` 文件夹复制到该文件夹中。请注意，Blaze 仅加载最新 Minecraft 版本的区块，因此你可能需要在复制 `region` 文件夹之前优化你的世界。

截至目前，Blaze 以离线模式运行，并具有以下功能：

1. 从区域文件异步加载区块，支持所有块状态。
2. 向客户端流式传输区块。
3. 玩家可以在世界中和标签列表中看到彼此。
4. 聊天消息。
5. 非常基础的库存管理。
6. 内存中放置和破坏方块，并将更改发送给客户端。
7. 从创造模式库存中生成物品。
8. 服务器列表 ping，显示在线玩家的样本。
9. 基本的块更新系统。
10. 非玩家实体移动和块碰撞。
11. 基本的光照引擎。
    
## Contributing

Contributions are welcome, provided you agree to put your contribution in the public domain.

A fair warning: I may deny a pull request if it doesn't fall in line with my goals (e.g. too complex). I may also deny a pull request and use parts of it to stitch together something myself.

If you have questions, remarks and otherwise, feel free to contact me by email via the email address I use to commit, or on Discord at traks#2633.
