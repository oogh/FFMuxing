# FFMuxing

基于 [muxing.c](https://github.com/FFmpeg/FFmpeg/blob/master/doc/examples/muxing.c) 写的C++版本



## 运行

项目使用 `cmake` 来管理。以 `Xcode` 和 `CLion` 为例

### 1. Xcode

- **安装ffmpeg**

```shell
brew install ffmpeg

# CMakeLists.txt 文件中配置了FFmpeg的路径
# Mac上通过brew的方式安装ffmpeg
# 可以使用 brew info ffmpeg 来查看 ffmpeg 的安装路径
```

- **构建xcode工程**

```shell
mkdir build
cd build
cmake -G "Xcode" ..
```

- **运行**

  打开 `build` 中的 `GHExport.xcodeproj` 文件即可。

  ⚠️运行时，记得选择运行的 `target` 为 `exporter`，不是 `ALL BUILD` !!!

### 2. CLion 

- **Open**

<img src="https://tva1.sinaimg.cn/large/006tNbRwgy1gaod7ovo5aj31760qsn00.jpg" alt="image-20200107222628466" style="zoom:50%;" />

- **选择 CMakeLists.txt**

<img src="https://tva1.sinaimg.cn/large/006tNbRwgy1gaodak7fz0j318e0owncg.jpg" alt="image-20200107222911096" style="zoom:50%;" />

- **Open As Project**

<img src="https://tva1.sinaimg.cn/large/006tNbRwgy1gaodb9ru2lj30ps088jsx.jpg" alt="image-20200107222959951" style="zoom:50%;" />

- **Reload CMake Project**

<img src="https://tva1.sinaimg.cn/large/006tNbRwgy1gaodhus5ugj30p50cjtb3.jpg" alt="image-20200107223613753" style="zoom:50%;" />

- **运行即可**

