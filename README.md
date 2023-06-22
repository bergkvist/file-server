# file-server
File server written in C using socket API for Linux/macOS.


## Usage
```sh
./file-server <HOST-IP> <PORT>
./file-server 127.0.0.1 5432
```

## Warnings
- Does not protect against [path traversal attacks](https://owasp.org/www-community/attacks/Path_Traversal)
- Does not set any HTTP cache headers


## Build
```
make file-server
```

## Install
```
make install
```

## Uninstall 
```
make uninstall
```