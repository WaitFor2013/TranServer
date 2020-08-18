# macOS Catalina(版本10.15.3) 构建
TranServer:main.o trans.o config.o
	cc -o build/TranServer build/main.o build/trans.o build/config.o -L /usr/local/lib  -framework OpenGL -framework AppKit -framework Foundation -framework CoreGraphics -framework CoreVideo -framework CoreImage -framework  VideoToolbox -framework CoreMedia -framework CoreFoundation -framework Security -framework AudioToolbox  -lavutil  -lavformat -lavcodec -lavfilter -lswresample -lswscale -llzma -lbz2 -lz  -liconv -lapr-1 -levent

main.o:main.c
	cc -c main.c -o build/main.o
trans.o:trans.c
	cc -c trans.c -o build/trans.o
config.o:config.c
	cc  -c config.c -o build/config.o