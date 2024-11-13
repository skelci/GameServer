# GameServer

A game server project built with CMake. Windows & Linux compatible.

## Project Structure

- src - Source code for the project.
- bin - Built executables and libraries.
- scripts - Utility scripts.
- client_data - Client-side data.
- server_data - Server-side data.

## Build Instructions

To build the project using CMake:

### Windows

```sh
cmake -G "MinGW Makefiles" . -DBUILD_SERVER=ON -DBUILD_CLIENT=ON -DBUILD_COMMON=ON -DRELEASE=ON
make
```

### Linux

```sh
cmake . -DBUILD_SERVER=ON -DBUILD_CLIENT=ON -DBUILD_COMMON=ON -DRELEASE=ON
make
```

## Dependencies

- boost
- pqxx
- px
- openssl
- jsoncpp
- ncurses (pdcurses on Windows)

## Usage

After building, you can run the server executables located in the `bin` directory.

```sh
./bin/servercontrol.exe
./bin/serverauth.exe
./bin/client.exe
```
