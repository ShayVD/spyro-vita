#!/usr/bin/env python3
"""A pretend Vita for testing tools/vita/hw_run.sh and hw_check.sh without
one: vitacompanion's FTP server (paths as ftpvita spells them, /ux0:/data/...)
over a folder, its command port, and a fake Spyro Vita that reads config.txt
and writes a log, shots and (hang_test=) a crash dump. Standard library only.

    tools/vita/hw_mock.py ROOT [FTP_PORT CMD_PORT]
    VITA_IP=127.0.0.1 VITA_FTP_PORT=2337 VITA_CMD_PORT=2338 tools/vita/hw_check.sh

ROOT/ux0, ROOT/ur0 are the devices. The fake game runs 600 vblanks a second.
"""
import os
import socket
import socketserver
import sys
import threading
import time

ROOT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "hw-mock")
FTP_PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 2337
CMD_PORT = int(sys.argv[3]) if len(sys.argv) > 3 else 2338
game = None


def local(path):
    """/ux0:/data/x -> ROOT/ux0/data/x"""
    parts = [p for p in path.split("/") if p]
    if not parts:
        return ROOT
    if not parts[0].endswith(":") or ".." in parts:
        raise ValueError(path)
    return os.path.join(ROOT, parts[0][:-1], *parts[1:])


class Game(threading.Thread):
    def __init__(self):
        super().__init__(daemon=True)
        self.stop = threading.Event()

    def run(self):
        d = os.path.join(ROOT, "ux0/data/spyro")
        opts = {}
        try:
            for line in open(os.path.join(d, "config.txt")):
                k, _, val = line.strip().partition("=")
                opts[k] = val
        except OSError:
            pass
        frames = int(opts.get("frames", "0"))
        every = int(opts.get("shot_every", "0"))
        shots = {int(x) for x in opts.get("shot", "").split(",") if x}
        hang = int(opts.get("hang_test", "0").split(",")[0] or 0)
        log = open(os.path.join(d, "spyro.log"), "w", buffering=1)
        log.write("[     0] vita mock: config %s\n" % sorted(opts))
        v = 0
        while not self.stop.is_set():
            time.sleep(1 / 600)
            v += 1
            if v in shots or (every and v % every == 0):
                os.makedirs(os.path.join(d, "shots"), exist_ok=True)
                open(os.path.join(d, "shots/shot-%d.png" % v), "wb").write(b"\x89PNG mock")
                log.write("[%6d] vita shot ux0:data/spyro/shots/shot-%d.png\n" % (v, v))
            if v % 300 == 0:
                log.write("[%6d] vita vblank %d: 300 vblanks\n" % (v, v))
            if hang and v == hang:
                log.write("[%6d] vita hang: stopping the app for a crash dump\n" % v)
                time.sleep(3)
                p = os.path.join(ROOT, "ux0/data/psp2core-%d-0x0000-eboot.bin.psp2dmp" % time.time())
                open(p, "wb").write(b"\0" * 4096)
                return
            if frames and v >= frames:
                log.write("[%6d] vita frames=%d reached, exiting\n" % (v, frames))
                return


class Cmd(socketserver.BaseRequestHandler):
    def handle(self):
        global game
        words = self.request.recv(1024).decode().split()
        reply = "Unknown command"
        if words[:1] == ["destroy"]:
            if game:
                game.stop.set()
                game.join()
                game = None
            reply = "Apps destroyed."
        elif words[:1] == ["launch"] and len(words) == 2:
            if words[1] == "SPYV00001" and os.path.isfile(local("/ux0:/app/SPYV00001/eboot.bin")):
                game = Game()
                game.start()
                reply = "Launched."
            else:
                reply = "Error"
        elif words[:1] in (["screen"], ["reboot"]):
            reply = "OK"
        self.request.sendall(reply.encode() + b"\n")


class Ftp(socketserver.StreamRequestHandler):
    def send(self, line):
        self.wfile.write(line.encode() + b"\r\n")

    def full(self, arg):
        p = arg if arg.startswith("/") else self.cwd.rstrip("/") + "/" + arg
        return local(p)

    def data(self):
        conn, _ = self.pasv.accept()
        self.pasv.close()
        return conn

    def handle(self):
        self.cwd, self.pasv, self.rnfr = "/", None, None
        self.send("220 FTPVita mock")
        for raw in self.rfile:
            cmd, _, arg = raw.decode().rstrip("\r\n").partition(" ")
            cmd = cmd.upper()
            try:
                self.do(cmd, arg)
            except (OSError, ValueError) as e:
                self.send("550 %s" % e)
            if cmd == "QUIT":
                return

    def do(self, cmd, arg):
        if cmd in ("USER", "PASS", "TYPE", "NOOP"):
            self.send("230 OK" if cmd != "TYPE" else "200 OK")
        elif cmd == "SYST":
            self.send("215 UNIX Type: L8")
        elif cmd == "PWD":
            self.send('257 "%s"' % self.cwd)
        elif cmd == "CWD":
            p = arg if arg.startswith("/") else self.cwd.rstrip("/") + "/" + arg
            if not os.path.isdir(local(p)):
                raise OSError("no folder %s" % p)
            self.cwd = p
            self.send("250 OK")
        elif cmd == "PASV":
            self.pasv = socket.socket()
            self.pasv.bind(("127.0.0.1", 0))
            self.pasv.listen(1)
            port = self.pasv.getsockname()[1]
            self.send("227 Entering Passive Mode (127,0,0,1,%d,%d)" % (port >> 8, port & 255))
        elif cmd == "LIST":
            d = self.full(arg) if arg else local(self.cwd)
            if d == ROOT:
                rows = [("d", 0, n + ":") for n in sorted(os.listdir(ROOT))]
            else:
                rows = [("d" if os.path.isdir(os.path.join(d, n)) else "-",
                         os.path.getsize(os.path.join(d, n)), n) for n in sorted(os.listdir(d))]
            self.send("150 Here")
            c = self.data()
            for t, size, n in rows:
                c.sendall(("%srwxrwxrwx 1 vita vita %d Nov 02 12:00 %s\r\n" % (t, size, n)).encode())
            c.close()
            self.send("226 Done")
        elif cmd == "RETR":
            with open(self.full(arg), "rb") as f:
                self.send("150 Here")
                c = self.data()
                c.sendall(f.read())
                c.close()
            self.send("226 Done")
        elif cmd == "STOR":
            p = self.full(arg)
            if not os.path.isdir(os.path.dirname(p)):
                raise OSError("no folder for %s" % arg)
            self.send("150 Send")
            c = self.data()
            with open(p, "wb") as f:
                while True:
                    b = c.recv(65536)
                    if not b:
                        break
                    f.write(b)
            c.close()
            self.send("226 Done")
        elif cmd == "DELE":
            os.unlink(self.full(arg))
            self.send("226 OK")
        elif cmd == "MKD":
            os.mkdir(self.full(arg))
            self.send("257 OK")
        elif cmd == "RMD":
            os.rmdir(self.full(arg))
            self.send("226 OK")
        elif cmd == "RNFR":
            self.rnfr = self.full(arg)
            if not os.path.exists(self.rnfr):
                raise OSError("no %s" % arg)
            self.send("350 OK")
        elif cmd == "RNTO":
            os.rename(self.rnfr, self.full(arg))
            self.send("250 OK")
        elif cmd == "QUIT":
            self.send("221 Bye")
        else:
            self.send("502 Not here")


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


if __name__ == "__main__":
    for d in ("ux0/data/spyro", "ux0/app", "ur0/data"):
        os.makedirs(os.path.join(ROOT, d), exist_ok=True)
    threading.Thread(target=Server(("127.0.0.1", CMD_PORT), Cmd).serve_forever, daemon=True).start()
    print("hw_mock: %s, FTP on %d, commands on %d" % (ROOT, FTP_PORT, CMD_PORT), flush=True)
    Server(("127.0.0.1", FTP_PORT), Ftp).serve_forever()
