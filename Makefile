.PHONY: main media audio video main-test

FLAGS= -o main -g \
	   -I./libs/ffmpeg5.1.2/include \
	   -I./libs/glad/include \
	   -lm \
	   -lavformat -lavcodec -lavutil -lswscale -lswresample \
	   -lglfw -lopenal \
	   -lpthread \
	   -Wall
FILES=main.c video.c audio.c utils.c codec.c list.c queue.c \
	  libs/glad/src/glad.c

main: $(FILES)
	gcc $(FILES) $(FLAGS)

media: main bad_apple.mp4
	./main bad_apple.mp4

audio: main audio.mp3
	./main audio.mp3

video: main video.mp4
	./main video.mp4

main-test: $(FILES)
	gcc $(FILES) $(FLAGS) -DTEST=1

test: main-test
	./main
