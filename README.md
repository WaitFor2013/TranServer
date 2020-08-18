TranServer是一个简单的转码服务器，主要将视频从rtsp格式转换为hls格式，并支持HTTP协议读取hls视频流。  
  
主要特性  
* 多线程支持: 不同rtsp转码，独立线程。
* 按需转码: 首次HTTP请求发生时，初始化转码线程，特定hls的HTTP请求截止后，在空闲一段时间后中止该hls对应的转码线程。
* HTTP服务集成: 无需单独部署静态资源服务器。
  
主要场景
* 浏览器无插件播放摄像头视频(替换ffmpeg+nginx的方案)


# （一）使用说明
## 程序下载
	# 仅提供centos7编译版本，如需其他环境，请自行编译
	链接:https://pan.baidu.com/s/1ES89wh17lZnSHKMaWGeVaw  密码:cre8

## 配置文件

	# 编辑文件
	vi config.ini
	# 录入,多个rtsp流，换行增加即可，最多支持1024个rtsp流
	# 等号之前，需满足目录名的规范，程序运行时会使用此名创建目录
	m_test=rtsp://wowzaec2demo.streamlock.net/vod/mp4:BigBuckBunny_115k.mov
	# 保存

## 程序运行

	# 赋权
	chmod +x TranServer
	# 运行
	./TranServer -p 8080
	# 后台运行
	./TranServer -p 8080 -D

## 客户端测试（http://chimee.org/）
选择LIVE-HLS。在SRC中填写服务地址。

	# 服务地址格式:  http://IP:PORT/TAG/hls.m3u8
	# IP: 机器IP
	# PORT: 转码程序占有端口(-p指定)
	# TAG: 在config.ini配置文件中，等号之前TAG，是RTSP流的标示

	例:http://127.0.0.1:8080/m_test/hls.m3u8

## 运行异常排查
* 查看端口占用
* 查看依赖的动态库是否缺失（ldd TranServer）
  
  
# （二）构建程序-安装三方库
TranServer依赖libav、apr、libevent等三方库。

## 1、libav库(https://ffmpeg.org/)
安装libav库，主要编译yasm和ffmpeg。
### yasm

	# 下载解压
	http://www.tortall.net/projects/yasm/releases/yasm-1.3.0.tar.gz
	# 编译安装
	./configure && make && make install

### ffmpeg

	# 下载解压，可以直接下载zip包
	git clone https://git.ffmpeg.org/ffmpeg.git
	# 编译安装
	./configure && make && make install

## 2、apr库(https://apr.apache.org/)
  
macOS下安装此库，执行下述命令即可。 

	brew install apr

如果是centos7则需要自己下载编译安装:  
* APR 1.7.0
* APR-util 1.6.1
* APR iconv 1.2.2

安装util和iconv时，configure时需要增加apr配置（--with-apr=/usr/local/apr/bin/apr-1-config）。
如果发生（expat.h: No such file or directory），则需要安装expat-devel。

	yum install expat-devel

## 3、libevent(https://libevent.org/)

	# 下载解压
	libevent-2.1.12-stable.tar.gz
	# 编译安装
	./configure && make && make install

# （三）构建程序-Makefile

	# 进入程序目录

	make


依赖C编译器，编译成功后，在build目录中会生成可执行文件TranServer。程序仅在macOS和Centos中编译测试，不同环境对应的Makefile会不同，其中centos编译时，可能需要安装一些依赖库。

## macOS Catalina(版本10.15.3)

	TranServer:main.o trans.o config.o
		cc -o build/TranServer build/main.o build/trans.o build/config.o -L /usr/local/lib  -framework OpenGL -framework AppKit -framework Foundation -framework CoreGraphics -framework CoreVideo -framework CoreImage -framework  VideoToolbox -framework CoreMedia -framework CoreFoundation -framework Security -framework AudioToolbox  -lavutil  -lavformat -lavcodec -lavfilter -lswresample -lswscale -llzma -lbz2 -lz -liconv -lapr-1 -levent

	main.o:main.c
		cc -c main.c -o build/main.o
	trans.o:trans.c
		cc -c trans.c -o build/trans.o
	config.o:config.c
		cc  -c config.c -o build/config.o

## CentOS Linux release 7.6.1810 (Core)
Centos编译时，可能需要安装X11、Va、lzma、bzip2、zlib等三方库。可自行检查系统中的三方库缺失情况，必要时，需要做一些软链接操作。

### X11库
	yum install libX11
### Va库
	yum install libva
### lzma库
	yum install -y xz-devel
### bzip2库
	yum install bzip2-devel
### zlib库
	yum install zlib*

### Makefile文件:

	TranServer:main.o trans.o config.o
		cc  -o build/TranServer build/main.o build/trans.o build/config.o -L /usr/local/lib -L /usr/local/apr/lib  -L /usr/local/apache2/lib -L /usr/lib64  -lavformat  -lavcodec  -lavutil -lswresample -lavfilter -lswscale -llzma -lbz2 -lz  -lpthread -lm  -lz -lX11 -lva -lva-drm -Wl,-Bstatic -lapriconv-1 -lapr-1  -levent -Wl,-Bdynamic
	
	main.o:main.c
		cc  -c main.c -o build/main.o
	trans.o:trans.c
		cc  -c trans.c -o build/trans.o
	config.o:config.c
		cc  -c config.c -o build/config.o

