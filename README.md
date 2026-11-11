# 👾 PacmanIST — a Pac-Man-style game server in C (threads, processes, pipes, signals)

> **TL;DR:** A terminal Pac-Man-like game written in **C** for Linux, used to practise the core ideas of **operating systems**. In part 1, every character (Pac-Man and each ghost) runs in its own **thread**, levels are read from files with low-level POSIX calls, and "save points" are made by cloning the whole game process with **`fork()`**. In part 2, the game becomes a **server** that hosts several games at once for different **client programs**, which talk to it through **named pipes**; a **signal** makes the server write a top-5 leaderboard. University project for *Operating Systems* at Instituto Superior Técnico.

---

## What is this, in plain words?

The game itself is simple: Pac-Man moves through a maze, eats dots and avoids ghosts. The interesting part is **how** it runs underneath, which is what an operating system course is about:

| OS concept | Where it shows up |
|---|---|
| **Threads** (several things running at the same time inside one program) | Each ghost and Pac-Man move independently, in parallel |
| **Synchronisation** (mutexes, semaphores) | Threads share the board without corrupting it; only one thread draws on the screen |
| **Processes and `fork()`** | Pressing `G` saves the game by creating a copy of the running process; if Pac-Man dies, the game resumes from that copy |
| **File system (POSIX)** | Levels (`.lvl`), ghost movements (`.m`) and Pac-Man moves (`.p`) are read from a folder with `open`/`read`, not `stdio` |
| **Inter-process communication (named pipes / FIFOs)** | Clients connect to the server and exchange moves and board updates through pipes |
| **Producer-consumer buffer** | The server's main thread queues new sessions; worker threads pick them up |
| **Signals** | Sending `SIGUSR1` to the server writes a file with the 5 connected players with the highest score |

## Part 1: multi-threaded game ([`part1-pacman-threads/`](part1-pacman-threads/))

```bash
cd part1-pacman-threads
make
./bin/Pacmanist testes_ex1      # folder with the level files
```

## Part 2: client-server ([`part2-client-server/`](part2-client-server/))

```bash
cd part2-client-server
make

# terminal 1: server with up to 3 parallel games, registration pipe /tmp/pacman_reg
./bin/server levels 3 /tmp/pacman_reg

# terminal 2, 3, ...: clients (optionally with a file of pre-recorded moves)
./bin/client 1 /tmp/pacman_reg
./bin/client 2 /tmp/pacman_reg src/client2_moves.txt

# leaderboard
kill -USR1 <server_pid>
```

Requirements: Linux (or WSL), `gcc`, `make` and the **ncurses** library (`sudo apt install libncurses-dev`).

## Project structure

```
part1-pacman-threads/
  src/game.c        # Game loop, threads, fork-based save points
  src/board.c       # Board, movement and level loading
  src/display.c     # ncurses drawing
  testes_ex1/       # Example levels
part2-client-server/
  src/server/       # Server: sessions, game logic, signal handling
  src/client/       # Client: API to talk to the server + display
  src/common/       # Shared protocol and I/O helpers
  levels/           # Example levels
```

## Authors

Vasco Caetano and group colleagues, Instituto Superior Técnico.

## Context

- **Course:** Operating Systems (Sistemas Operativos), Instituto Superior Técnico, University of Lisbon
- **Stack:** C17, POSIX threads, POSIX file API, named pipes, signals, ncurses, Make
- The base code (game rules, ncurses display) was provided by the course teaching staff; the original course README is kept in `part1-pacman-threads/README.md`.
