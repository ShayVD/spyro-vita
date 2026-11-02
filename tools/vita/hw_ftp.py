#!/usr/bin/env python3
"""The Vita side of tools/vita/hw_run.sh and hw_check.sh: vitacompanion's
FTP server (port 1337) and its command port (1338). Standard library only.

    hw_ftp.py cmd version|"quit SPYV00001"|"launch SPYV00001"|"promote DIR"|"screen on"
    hw_ftp.py ls ux0:data/spyro          one "d|f SIZE NAME" line each; exit 2: no such folder
    hw_ftp.py size ux0:data/spyro/x      the size; exit 2: not there
    hw_ftp.py get REMOTE LOCAL           a file
    hw_ftp.py getdir REMOTE LOCALDIR     a folder, recursively
    hw_ftp.py put LOCAL REMOTE           a file (its folder must exist)
    hw_ftp.py putdir LOCALDIR REMOTE     a folder's contents, recursively
    hw_ftp.py mkdir REMOTE               (no error if it is there)
    hw_ftp.py mv FROM TO                 rename
    hw_ftp.py rm REMOTE                  a file
    hw_ftp.py rmtree REMOTE              a folder and everything in it

Paths are the Vita's (ux0:data/spyro/config.txt); FTP spells them
/ux0:/data/spyro/config.txt. VITA_IP is the Vita's address, VITA_FTP_PORT
and VITA_CMD_PORT change the ports. Writes, renames and deletes are refused
outside ux0:data/spyro/ and ux0:app/SPYV00001/, and deletes and renames
also of ux0:data/spyro itself (the crash dumps in ux0:data/ are read, never
touched): the runner must not be able to touch another app's files whatever
it is told.
"""
import ftplib
import os
import socket
import sys

IP = os.environ.get("VITA_IP", "")
FTP_PORT = int(os.environ.get("VITA_FTP_PORT", "1337"))
CMD_PORT = int(os.environ.get("VITA_CMD_PORT", "1338"))
TIMEOUT = float(os.environ.get("VITA_NET_TIMEOUT", "15"))
WRITABLE = ("/ux0:/data/spyro/", "/ux0:/app/SPYV00001/")


class Missing(Exception):
    pass


def ftp_path(p):
    """ux0:data/spyro/x or /ux0:/data/spyro/x -> /ux0:/data/spyro/x"""
    p = p.strip()
    if not p.startswith("/"):
        dev, sep, rest = p.partition(":")
        if not sep:
            raise SystemExit("hw_ftp: not a Vita path: %s" % p)
        p = "/%s:/%s" % (dev, rest.lstrip("/"))
    while "//" in p:
        p = p.replace("//", "/")
    return p.rstrip("/") if len(p) > 1 and not p.endswith(":/") else p


def writable(p, delete=False):
    full = ftp_path(p)
    ok = any((full + "/").startswith(w) for w in WRITABLE)
    if ok and delete:
        ok = full.rstrip("/") + "/" not in WRITABLE  # never the folders themselves
    if not ok or "/../" in full + "/":
        raise SystemExit("hw_ftp: refusing to change %s (only inside %s)" % (full, ", ".join(WRITABLE)))
    return full


def split(full):
    d, _, name = full.rstrip("/").rpartition("/")
    return (d or "/"), name


class Vita:
    def __init__(self):
        if not IP:
            raise SystemExit("hw_ftp: VITA_IP is not set (tools/vita/hw.env)")
        self.ftp = ftplib.FTP()
        self.ftp.connect(IP, FTP_PORT, timeout=TIMEOUT)
        self.ftp.login()  # vitacompanion takes any user
        self.cwd = None

    def cd(self, d):
        # One folder at a time and plain names after it: what every FTP
        # server, ftpvita included, understands.
        if d != self.cwd:
            try:
                self.ftp.cwd(d)
            except ftplib.error_perm as e:
                raise Missing("%s: %s" % (d, e))
            self.cwd = d

    def ls(self, d):
        """[(is_dir, size, name)] of folder d"""
        self.cd(ftp_path(d))
        lines = []
        self.ftp.retrlines("LIST", lines.append)
        out = []
        for line in lines:
            f = line.split(None, 8)
            if len(f) < 9 or f[8] in (".", ".."):
                continue
            try:
                size = int(f[4])
            except ValueError:
                size = 0
            out.append((f[0].startswith("d"), size, f[8]))
        return out

    def stat(self, p):
        d, name = split(ftp_path(p))
        try:
            for is_dir, size, n in self.ls(d):
                if n == name:
                    return is_dir, size
        except Missing:
            pass
        return None

    def get(self, p, local):
        d, name = split(ftp_path(p))
        self.cd(d)
        tmp = local + ".part"
        with open(tmp, "wb") as f:
            try:
                self.ftp.retrbinary("RETR " + name, f.write)
            except ftplib.error_perm as e:
                f.close()
                os.unlink(tmp)
                raise Missing("%s: %s" % (p, e))
        os.replace(tmp, local)

    def getdir(self, p, local):
        os.makedirs(local, exist_ok=True)
        full = ftp_path(p)
        for is_dir, _, name in self.ls(full):
            if is_dir:
                self.getdir(full + "/" + name, os.path.join(local, name))
            else:
                self.get(full + "/" + name, os.path.join(local, name))

    def put(self, local, p):
        d, name = split(writable(p))
        self.cd(d)
        with open(local, "rb") as f:
            self.ftp.storbinary("STOR " + name, f)
        got = self.stat(p)
        if not got or got[1] != os.path.getsize(local):
            raise SystemExit("hw_ftp: %s: uploaded %s bytes, the Vita has %s"
                             % (p, os.path.getsize(local), got and got[1]))

    def mkdir(self, p):
        full = writable(p)
        if self.stat(full):
            return
        d, name = split(full)
        self.cd(d)
        self.ftp.mkd(name)

    def putdir(self, local, p):
        full = writable(p)
        self.mkdir(full)
        for name in sorted(os.listdir(local)):
            lp = os.path.join(local, name)
            if os.path.isdir(lp):
                self.putdir(lp, full + "/" + name)
            else:
                self.put(lp, full + "/" + name)

    def mv(self, a, b):
        fa, fb = writable(a, delete=True), writable(b, delete=True)
        da, na = split(fa)
        self.cd(da)
        self.ftp.rename(na, fb if split(fb)[0] != da else split(fb)[1])

    def rm(self, p):
        d, name = split(writable(p, delete=True))
        self.cd(d)
        self.ftp.voidcmd("DELE " + name)  # any 2xx: ftpvita answers 226

    def rmtree(self, p):
        full = writable(p, delete=True)
        for is_dir, _, name in self.ls(full):
            if is_dir:
                self.rmtree(full + "/" + name)
            else:
                self.rm(full + "/" + name)
        d, name = split(full)
        self.cd(d)
        self.ftp.rmd(name)


def command(text):
    """One command on vitacompanion's command port; its reply."""
    if not IP:
        raise SystemExit("hw_ftp: VITA_IP is not set (tools/vita/hw.env)")
    with socket.create_connection((IP, CMD_PORT), timeout=TIMEOUT) as s:
        s.sendall(text.encode() + b"\n")
        s.settimeout(float(os.environ.get("VITA_CMD_WAIT", "5")))
        reply = b""
        try:
            # until the connection closes, or a quiet moment after a whole
            # line (help's reply is several lines)
            while True:
                chunk = s.recv(4096)
                if not chunk:
                    break
                reply += chunk
                if reply.endswith(b"\n"):
                    s.settimeout(0.5)
        except socket.timeout:
            pass
    return reply.decode(errors="replace").strip()


def main(argv):
    if len(argv) < 2:
        raise SystemExit(__doc__)
    op, args = argv[1], argv[2:]
    try:
        if op == "cmd":
            print(command(" ".join(args)) or "(no reply)")
            return 0
        v = Vita()
        if op == "ls":
            for is_dir, size, name in v.ls(args[0]):
                print("%s %d %s" % ("d" if is_dir else "f", size, name))
        elif op == "size":
            st = v.stat(args[0])
            if not st:
                return 2
            print(st[1])
        elif op == "get":
            v.get(args[0], args[1])
        elif op == "getdir":
            v.getdir(args[0], args[1])
        elif op == "put":
            v.put(args[0], args[1])
        elif op == "putdir":
            v.putdir(args[0], args[1])
        elif op == "mkdir":
            v.mkdir(args[0])
        elif op == "mv":
            v.mv(args[0], args[1])
        elif op == "rm":
            v.rm(args[0])
        elif op == "rmtree":
            v.rmtree(args[0])
        else:
            raise SystemExit("hw_ftp: unknown operation %s" % op)
        v.ftp.quit()
    except Missing as e:
        print("hw_ftp: not there: %s" % e, file=sys.stderr)
        return 2
    except (OSError, EOFError, ftplib.Error) as e:
        print("hw_ftp: %s %s: %s" % (op, " ".join(args), e), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
