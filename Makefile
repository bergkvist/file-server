file-server: main.c
	cc $< -o $@ -Wall -Wextra -O2

clean:
	rm -rf file-server

install: file-server
	sudo cp $< /usr/local/bin/

uninstall:
	sudo rm /usr/local/bin/file-server
