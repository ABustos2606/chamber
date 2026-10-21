# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Alejandro Bustos
"""Optional Linux host check, using a private dummy JACK server.

Needs jackd, jalv, Ingen, python3-jack-client and numpy. Never stops an
existing engine or server. Logs are kept in build/host-smoke/.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import sys
import time

import jack
import numpy as np

URI = "https://github.com/ABustos2606/chamber#chamber"
PREFIXES = """@prefix patch: <http://lv2plug.in/ns/ext/patch#> .
@prefix ingen: <http://drobilla.net/ns/ingen#> .
@prefix lv2: <http://lv2plug.in/ns/lv2core#> .
@prefix xsd: <http://www.w3.org/2001/XMLSchema#> .
"""


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def wait_for(check, message, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if check():
            return
        time.sleep(0.05)
    raise RuntimeError(message)


class Protocol:
    """Small request/response client for Ingen's patch protocol (from Discipline)."""
    def __init__(self, path, transcript):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.socket.connect(str(path))
        self.buffer = b""
        self.sequence = 0
        self.transcript = transcript.open("w")
        self.last_status = 0

    def request(self, verb, fields, allow_error=False):
        self.sequence += 1
        message = (PREFIXES + f'[] a patch:{verb} ; patch:sequenceNumber '
                   f'"{self.sequence}"^^xsd:int ; {fields} .\n')
        self.socket.sendall(message.encode())
        self.transcript.write("SEND " + message + "\n")
        deadline = time.monotonic() + 10
        while True:
            while b"\0" not in self.buffer:
                remaining = deadline - time.monotonic()
                require(remaining > 0, f"Ingen {verb} response timed out")
                self.socket.settimeout(remaining)
                data = self.socket.recv(65536)
                require(data, "Ingen closed its socket")
                self.buffer += data
            data, self.buffer = self.buffer.split(b"\0", 1)
            text = data.decode()
            self.transcript.write("RECV " + text + "\n")
            statuses = []
            for match in re.finditer(r"a patch:Response\s*;(.*?)\.\s*(?:\n|$)", text, re.S):
                sequence = re.search(r'patch:sequenceNumber "(\d+)"', match[1])
                status = re.search(r'patch:body "(-?\d+)"', match[1])
                if sequence and int(sequence[1]) == self.sequence and status:
                    statuses.append(int(status[1]))
            if statuses:
                self.last_status = max(statuses, key=abs)
                require(allow_error or self.last_status == 0, f"Ingen {verb}: {text}")
                return text

    def close(self):
        self.socket.close()
        self.transcript.close()

    def put(self, subject, body):
        return self.request("Put", f"patch:subject <{subject}> ; patch:body [ {body} ]")

    def connect(self, tail, head):
        return self.put("ingen:/main/", f"a ingen:Arc ; ingen:tail <{tail}> ; ingen:head <{head}>")

    def set(self, block, symbol, value):
        return self.request("Set", f'patch:subject <ingen:/main/{block}/{symbol}> ; '
                            f'patch:property ingen:value ; patch:value "{value}"^^xsd:float')

    def value(self, path):
        text = self.request("Get", f"patch:subject <{path}>")
        values = re.findall(r'ingen:value "?([0-9.eE+-]+)', text)
        require(values, f"no value for {path}")
        return float(values[-1])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    parser.add_argument("--ingen", default="ingen")
    parser.add_argument("--period", type=int, default=1024,
                        help="dummy JACK period (default: 1024, tolerant of WSL scheduling)")
    args = parser.parse_args()
    logdir = Path("build/host-smoke").resolve()
    logdir.mkdir(parents=True, exist_ok=True)
    server_name = f"chamber-smoke-{os.getpid()}"
    env = dict(os.environ, JACK_DEFAULT_SERVER=server_name, JACK_NO_AUDIO_RESERVATION="1")
    private_lv2 = logdir / f"lv2-{os.getpid()}"
    shutil.copytree(args.bundle.resolve(), private_lv2 / "chamber.lv2")
    env["LV2_PATH"] = str(private_lv2) + ":" + env.get("LV2_PATH", "/usr/local/lib/lv2:/usr/lib/lv2")
    processes, logs = [], []
    probe = protocol = None

    def launch(name, command):
        log = (logdir / f"{name}.log").open("w")
        logs.append(log)
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT, env=env)
        processes.append(process)
        return process

    def stop(process):
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)

    try:
        launch("jack", ["jackd", "--no-realtime", "--timeout", "5000", "--name", server_name,
                        "-d", "dummy", "-r", "48000", "-p", str(args.period)])
        wait_for(lambda: subprocess.run(["jack_lsp"], env=env, stdout=subprocess.DEVNULL,
                                        stderr=subprocess.DEVNULL).returncode == 0, "private JACK server did not start")
        probe = jack.Client("chamber_probe", servername=server_name, no_start_server=True)
        gate_source = probe.outports.register("gate")
        captures = [probe.inports.register(name) for name in ("cap_l", "cap_r")]
        state = {"gate": 0.0, "peak": [0.0, 0.0], "finite": True}

        @probe.set_process_callback
        def process(frames):
            gate_source.get_array()[:] = state["gate"]
            for i, port in enumerate(captures):
                data = port.get_array()
                state["finite"] &= bool(np.isfinite(data).all())
                state["peak"][i] = max(state["peak"][i], float(np.abs(data).max()))

        probe.activate()

        def listen(seconds):
            state["peak"] = [0.0, 0.0]
            time.sleep(seconds)
            require(state["finite"], "finite output")
            return max(state["peak"])

        # Every exciter is off by default (a guitar effect); raise Strike level
        # so a gate strikes a note.
        host = launch("jalv", ["jalv", "-i", "-n", "chamber_jalv", "-c", "strike_level=0.8", URI])
        wait_for(lambda: bool(probe.get_ports("chamber_jalv:out_r")), "jalv did not load Chamber")
        time.sleep(0.5)
        require(host.poll() is None, "jalv failed during activation")
        probe.connect(gate_source, "chamber_jalv:gate_cv")
        probe.connect("chamber_jalv:out_l", captures[0])
        probe.connect("chamber_jalv:out_r", captures[1])
        require(listen(0.5) == 0.0, "jalv: silent before a gate")
        state["gate"] = 1.0
        require(listen(1.0) > 0.01, "jalv: a gate strikes a note")
        state["gate"] = 0.0
        stop(host)
        print("PASS jalv: Chamber loads, a gate strikes a note", flush=True)

        for threads in (1, 4):
            path = Path(f"/tmp/{server_name}-{threads}.sock")
            client = f"chamber_ingen_{threads}"
            engine = launch(f"ingen-{threads}", [args.ingen, "-e", "-f", "-s", server_name, "-n", client,
                                                 "-S", str(path), "-E", "0", "-p", str(threads)])
            wait_for(path.exists, "Ingen socket did not appear")
            time.sleep(1)
            require(engine.poll() is None, "Ingen failed during JACK activation")
            protocol = Protocol(path, logdir / f"protocol-{threads}.log")
            protocol.put("ingen:/main/gate_in", "a lv2:InputPort, lv2:CVPort")
            for side in ("l", "r"):
                protocol.put(f"ingen:/main/out_{side}", "a lv2:OutputPort, lv2:AudioPort")
            # Two Chambers in one graph: with several threads they can run at once.
            for name in ("chamber", "chamber2"):
                protocol.put(f"ingen:/main/{name}", f"a ingen:Block ; lv2:prototype <{URI}>")
                protocol.connect("ingen:/main/gate_in", f"ingen:/main/{name}/gate_cv")
                protocol.set(name, "strike_level", 0.8)
            protocol.connect("ingen:/main/chamber/out_l", "ingen:/main/out_l")
            protocol.connect("ingen:/main/chamber2/out_r", "ingen:/main/out_r")
            protocol.set("chamber2", "model", 2)
            wait_for(lambda: bool(probe.get_ports(f"{client}:out_l")), "Ingen JACK outputs")
            probe.connect(gate_source, f"{client}:gate_in")
            probe.connect(f"{client}:out_l", captures[0])
            probe.connect(f"{client}:out_r", captures[1])
            state["gate"] = 0.0
            time.sleep(0.5)
            require(listen(0.5) == 0.0, "Ingen: silent before a gate")
            state["gate"] = 1.0
            require(listen(1.0) > 0.01, "Ingen: a gate from a CV source strikes notes")
            state["gate"] = 0.0
            print(f"PASS Ingen: two Chambers, gate from a CV source, {threads} threads", flush=True)

            settings = {"model": 1, "ominous": 1, "coarse": 42, "geometry": 0.7, "output": -6}
            for symbol, value in settings.items():
                protocol.set("chamber", symbol, value)
            save_path = logdir / f"{server_name}-{threads}.ingen"
            protocol.request("Copy", f"patch:subject <ingen:/main> ; patch:destination <{save_path.as_uri()}>")
            protocol.request("Delete", "patch:subject <ingen:/main/chamber>")
            protocol.request("Copy", f"patch:subject <{save_path.as_uri()}> ; patch:destination <ingen:/main/restored>")
            for symbol, value in settings.items():
                got = protocol.value(f"ingen:/main/restored/chamber/{symbol}")
                require(abs(got - value) < 1e-5, f"saved {symbol} restores: {got}")
            require("state:state" not in (save_path / "main.ttl").read_text(), "no LV2 state saved")
            require(engine.poll() is None, "Ingen exited")
            print(f"PASS Ingen: model, Ominous voice and controls save and restore, {threads} threads", flush=True)
            protocol.close()
            protocol = None
            stop(engine)
            path.unlink(missing_ok=True)
    finally:
        if protocol:
            protocol.close()
        if probe:
            probe.close()
        for process in reversed(processes):
            if process.poll() is None:
                stop(process)
        for log in logs:
            log.close()


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        sys.exit(f"FAIL host_smoke: {error} (see build/host-smoke/*.log)")
